// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_bypass_protocol.h"

#include <utility>

#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/field_trial.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_bypass_stats.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_config_test_utils.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_interceptor.h"
#include "components/data_reduction_proxy/core/browser/data_reduction_proxy_test_utils.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_headers.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_params_test_utils.h"
#include "net/base/completion_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/load_flags.h"
#include "net/base/network_change_notifier.h"
#include "net/base/network_delegate.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_transaction_test_util.h"
#include "net/proxy/proxy_server.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/socket_test_util.h"
#include "net/url_request/static_http_user_agent_settings.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_filter.h"
#include "net/url_request/url_request_http_job.h"
#include "net/url_request/url_request_intercepting_job_factory.h"
#include "net/url_request/url_request_interceptor.h"
#include "net/url_request/url_request_job_factory.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using net::HostPortPair;
using net::HttpResponseHeaders;
using net::MockRead;
using net::MockWrite;
using net::ProxyRetryInfoMap;
using net::ProxyService;
using net::StaticSocketDataProvider;
using net::TestDelegate;
using net::TestURLRequestContext;
using net::URLRequest;

namespace data_reduction_proxy {

class SimpleURLRequestInterceptor : public net::URLRequestInterceptor {
 public:
  net::URLRequestJob* MaybeInterceptRequest(
      net::URLRequest* request,
      net::NetworkDelegate* network_delegate) const override {
    return net::URLRequestHttpJob::Factory(request, network_delegate, "http");
  }
};

class BadEntropyProvider : public base::FieldTrial::EntropyProvider {
 public:
  ~BadEntropyProvider() override {}

  double GetEntropyForTrial(const std::string& trial_name,
                            uint32 randomization_seed) const override {
    return 0.5;
  }
};

// Constructs a |TestURLRequestContext| that uses a |MockSocketFactory| to
// simulate requests and responses.
class DataReductionProxyProtocolTest : public testing::Test {
 public:
  DataReductionProxyProtocolTest() : http_user_agent_settings_("", "") {
    simple_interceptor_.reset(new SimpleURLRequestInterceptor());
    net::URLRequestFilter::GetInstance()->AddHostnameInterceptor(
        "http", "www.google.com", simple_interceptor_.Pass());
  }

  ~DataReductionProxyProtocolTest() override {
    // URLRequestJobs may post clean-up tasks on destruction.
    net::URLRequestFilter::GetInstance()->RemoveHostnameHandler(
            "http", "www.google.com");
    test_context_->RunUntilIdle();
  }

  void SetUp() override {
    test_context_ =
        DataReductionProxyTestContext::Builder()
            .WithParamsFlags(DataReductionProxyParams::kAllowed |
                             DataReductionProxyParams::kFallbackAllowed |
                             DataReductionProxyParams::kPromoAllowed)
            .WithParamsDefinitions(
                TestDataReductionProxyParams::HAS_EVERYTHING &
                    ~TestDataReductionProxyParams::HAS_DEV_ORIGIN &
                    ~TestDataReductionProxyParams::HAS_DEV_FALLBACK_ORIGIN)
            .Build();
    network_change_notifier_.reset(net::NetworkChangeNotifier::CreateMock());
    net::NetworkChangeNotifier::SetTestNotificationsOnly(true);
    test_context_->RunUntilIdle();
  }

  // Sets up the |TestURLRequestContext| with the provided |ProxyService|.
  void ConfigureTestDependencies(ProxyService* proxy_service) {
    // Create a context with delayed initialization.
    context_.reset(new TestURLRequestContext(true));

    proxy_service_.reset(proxy_service);
    context_->set_client_socket_factory(&mock_socket_factory_);
    context_->set_proxy_service(proxy_service_.get());
    network_delegate_.reset(new net::TestNetworkDelegate());
    context_->set_network_delegate(network_delegate_.get());
    // This is needed to prevent the test context from adding language headers
    // to requests.
    context_->set_http_user_agent_settings(&http_user_agent_settings_);
    bypass_stats_.reset(new DataReductionProxyBypassStats(
        test_context_->config(),
        test_context_->unreachable_callback(),
        test_context_->task_runner()));

    DataReductionProxyInterceptor* interceptor =
        new DataReductionProxyInterceptor(test_context_->config(),
                                          bypass_stats_.get(),
                                          test_context_->event_store());
    scoped_ptr<net::URLRequestJobFactoryImpl> job_factory_impl(
        new net::URLRequestJobFactoryImpl());
    job_factory_.reset(new net::URLRequestInterceptingJobFactory(
        job_factory_impl.Pass(), make_scoped_ptr(interceptor)));

    context_->set_job_factory(job_factory_.get());
    context_->Init();
  }

  // Simulates a request to a data reduction proxy that may result in bypassing
  // the proxy and retrying the the request.
  // Runs a test with the given request |method| that expects the first response
  // from the server to be |first_response|. If |expected_retry|, the test
  // will expect a retry of the request. A response body will be expected
  // if |expect_response_body|.
  void TestProxyFallback(const char* method,
                         const char* first_response,
                         bool expected_retry,
                         bool generate_response_error,
                         size_t expected_bad_proxy_count,
                         bool expect_response_body) {
    std::string m(method);
    std::string trailer =
        (m == "HEAD" || m == "PUT" || m == "POST") ?
            "Content-Length: 0\r\n" : "";

    std::string request1 =
        base::StringPrintf("%s http://www.google.com/ HTTP/1.1\r\n"
                           "Host: www.google.com\r\n"
                           "Proxy-Connection: keep-alive\r\n%s"
                           "User-Agent:\r\n"
                           "Accept-Encoding: gzip, deflate\r\n\r\n",
                           method, trailer.c_str());

    std::string payload1 =
        (expected_retry ? "Bypass message" : "content");

    MockWrite data_writes[] = {
      MockWrite(request1.c_str()),
    };

    MockRead data_reads[] = {
      MockRead(first_response),
      MockRead(payload1.c_str()),
      MockRead(net::SYNCHRONOUS, net::OK),
    };
    MockRead data_reads_error[] = {
      MockRead(net::SYNCHRONOUS, net::ERR_INVALID_RESPONSE),
    };

    StaticSocketDataProvider data1(data_reads, arraysize(data_reads),
                                  data_writes, arraysize(data_writes));
    StaticSocketDataProvider data1_error(data_reads_error,
                                         arraysize(data_reads_error),
                                         data_writes, arraysize(data_writes));
    if (!generate_response_error)
      mock_socket_factory_.AddSocketDataProvider(&data1);
    else
      mock_socket_factory_.AddSocketDataProvider(&data1_error);

    std::string response2;
    std::string request2;
    std::string response2_via_header = "";
    std::string request2_connection_type = "";
    std::string request2_path = "/";
    bool idempotent_block_once =
        m != "POST" && expected_retry && expected_bad_proxy_count == 0u;

    if (expected_bad_proxy_count < 2u && !idempotent_block_once) {
      request2_path = "http://www.google.com/";
      request2_connection_type = "Proxy-";
      response2_via_header = "Via: 1.1 Chrome-Compression-Proxy\r\n";
    }

    request2 = base::StringPrintf(
        "%s %s HTTP/1.1\r\n"
        "Host: www.google.com\r\n"
        "%sConnection: keep-alive\r\n%s"
        "User-Agent:\r\n"
        "Accept-Encoding: gzip, deflate\r\n\r\n",
        method, request2_path.c_str(), request2_connection_type.c_str(),
        trailer.c_str());

    response2 = base::StringPrintf(
        "HTTP/1.0 200 OK\r\n"
        "Server: foo\r\n%s\r\n", response2_via_header.c_str());

    MockWrite data_writes2[] = {
      MockWrite(request2.c_str()),
    };

    MockRead data_reads2[] = {
      MockRead(response2.c_str()),
      MockRead("content"),
      MockRead(net::SYNCHRONOUS, net::OK),
    };

    StaticSocketDataProvider data2(data_reads2, arraysize(data_reads2),
                                   data_writes2, arraysize(data_writes2));
    if (expected_retry) {
      mock_socket_factory_.AddSocketDataProvider(&data2);
    }

    // Expect that we get "content" and not "Bypass message", and that there's
    // a "not-proxy" "Server:" header in the final response.
    ExecuteRequestExpectingContentAndHeader(
        method,
        (expect_response_body ? "content" : ""),
        expected_retry,
        generate_response_error);
  }

  // Starts a request with the given |method| and checks that the response
  // contains |content|.
  void ExecuteRequestExpectingContentAndHeader(const std::string& method,
                                               const std::string& content,
                                               bool expected_retry,
                                               bool expected_error) {
    int initial_headers_received_count =
        network_delegate_->headers_received_count();
    TestDelegate d;
    scoped_ptr<URLRequest> r(context_->CreateRequest(
        GURL("http://www.google.com/"),
        net::DEFAULT_PRIORITY,
        &d));
    r->set_method(method);
    r->SetLoadFlags(net::LOAD_NORMAL);

    r->Start();
    base::RunLoop().Run();

    if (!expected_error) {
      EXPECT_EQ(net::URLRequestStatus::SUCCESS, r->status().status());
      EXPECT_EQ(net::OK, r->status().error());
      if (expected_retry)
        EXPECT_EQ(initial_headers_received_count + 2,
                  network_delegate_->headers_received_count());
      else
        EXPECT_EQ(initial_headers_received_count + 1,
                  network_delegate_->headers_received_count());
      EXPECT_EQ(content, d.data_received());
      return;
    }
    EXPECT_EQ(net::URLRequestStatus::FAILED, r->status().status());
    EXPECT_EQ(net::ERR_INVALID_RESPONSE, r->status().error());
    EXPECT_EQ(initial_headers_received_count,
              network_delegate_->headers_received_count());
  }

  // Returns the key to the |ProxyRetryInfoMap|.
  std::string GetProxyKey(std::string proxy) {
    net::ProxyServer proxy_server = net::ProxyServer::FromURI(
        proxy, net::ProxyServer::SCHEME_HTTP);
    if (!proxy_server.is_valid())
      return HostPortPair::FromURL(GURL(std::string())).ToString();
    return proxy_server.host_port_pair().ToString();
  }

  // Checks that |expected_num_bad_proxies| proxies are on the proxy retry list.
  // If the list has one proxy, it should match |bad_proxy|. If it has two
  // proxies, it should match |bad_proxy| and |bad_proxy2|. Checks also that
  // the current delay associated with each bad proxy is |duration_seconds|.
  void TestBadProxies(unsigned int expected_num_bad_proxies,
                      int duration_seconds,
                      const std::string& bad_proxy,
                      const std::string& bad_proxy2) {
    const ProxyRetryInfoMap& retry_info = proxy_service_->proxy_retry_info();
    ASSERT_EQ(expected_num_bad_proxies, retry_info.size());

    base::TimeDelta expected_min_duration;
    base::TimeDelta expected_max_duration;
    if (duration_seconds == 0) {
      expected_min_duration = base::TimeDelta::FromMinutes(1);
      expected_max_duration = base::TimeDelta::FromMinutes(5);
    } else {
      expected_min_duration = base::TimeDelta::FromSeconds(duration_seconds);
      expected_max_duration = base::TimeDelta::FromSeconds(duration_seconds);
    }

    if (expected_num_bad_proxies >= 1u) {
      ProxyRetryInfoMap::const_iterator i =
          retry_info.find(GetProxyKey(bad_proxy));
      ASSERT_TRUE(i != retry_info.end());
      EXPECT_TRUE(expected_min_duration <= (*i).second.current_delay);
      EXPECT_TRUE((*i).second.current_delay <= expected_max_duration);
    }
    if (expected_num_bad_proxies == 2u) {
      ProxyRetryInfoMap::const_iterator i =
          retry_info.find(GetProxyKey(bad_proxy2));
      ASSERT_TRUE(i != retry_info.end());
      EXPECT_TRUE(expected_min_duration <= (*i).second.current_delay);
      EXPECT_TRUE((*i).second.current_delay <= expected_max_duration);
    }
  }

 protected:
  scoped_ptr<net::NetworkChangeNotifier> network_change_notifier_;

  scoped_ptr<net::URLRequestInterceptor> simple_interceptor_;
  net::MockClientSocketFactory mock_socket_factory_;
  scoped_ptr<net::TestNetworkDelegate> network_delegate_;
  scoped_ptr<ProxyService> proxy_service_;
  scoped_ptr<DataReductionProxyTestContext> test_context_;
  scoped_ptr<DataReductionProxyBypassStats> bypass_stats_;
  net::StaticHttpUserAgentSettings http_user_agent_settings_;

  scoped_ptr<net::URLRequestInterceptingJobFactory> job_factory_;
  scoped_ptr<TestURLRequestContext> context_;
};

// Tests that request are deemed idempotent or not according to the method used.
TEST_F(DataReductionProxyProtocolTest, TestIdempotency) {
  net::TestURLRequestContext context;
  const struct {
    const char* method;
    bool expected_result;
  } tests[] = {
      { "GET", true },
      { "OPTIONS", true },
      { "HEAD", true },
      { "PUT", true },
      { "DELETE", true },
      { "TRACE", true },
      { "POST", false },
      { "CONNECT", false },
  };
  for (size_t i = 0; i < arraysize(tests); ++i) {
    scoped_ptr<net::URLRequest> request(
        context.CreateRequest(GURL("http://www.google.com/"),
                              net::DEFAULT_PRIORITY,
                              NULL));
    request->set_method(tests[i].method);
    EXPECT_EQ(
        tests[i].expected_result,
        DataReductionProxyBypassProtocol::IsRequestIdempotent(request.get()));
  }
}

// After each test, the proxy retry info will contain zero, one, or two of the
// data reduction proxies depending on whether no bypass was indicated by the
// initial response, a single proxy bypass was indicated, or a double bypass
// was indicated. In both the single and double bypass cases, if the request
// was idempotent, it will be retried over a direct connection.
TEST_F(DataReductionProxyProtocolTest, BypassLogic) {
  const struct {
    const char* method;
    const char* first_response;
    bool expected_retry;
    bool generate_response_error;
    size_t expected_bad_proxy_count;
    bool expect_response_body;
    int expected_duration;
    DataReductionProxyBypassType expected_bypass_type;
  } tests[] = {
    // Valid data reduction proxy response with no bypass message.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      false,
      false,
      0u,
      true,
      -1,
      BYPASS_EVENT_TYPE_MAX,
    },
    // Response error does not result in bypass.
    { "GET",
      "Not an HTTP response",
      false,
      true,
      0u,
      true,
      -1,
      BYPASS_EVENT_TYPE_MAX,
    },
    // Valid data reduction proxy response with chained via header,
    // no bypass message.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Via: 1.1 Chrome-Compression-Proxy, 1.0 some-other-proxy\r\n\r\n",
      false,
      false,
      0u,
      true,
      -1,
      BYPASS_EVENT_TYPE_MAX
    },
    // Valid data reduction proxy response with a bypass message.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: bypass=0\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_MEDIUM
    },
    // Valid data reduction proxy response with a bypass message.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: bypass=1\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      1,
      BYPASS_EVENT_TYPE_SHORT
    },
    // Same as above with the OPTIONS method, which is idempotent.
    { "OPTIONS",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: bypass=0\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_MEDIUM
    },
    // Same as above with the HEAD method, which is idempotent.
    { "HEAD",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: bypass=0\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      false,
      0,
      BYPASS_EVENT_TYPE_MEDIUM
    },
    // Same as above with the PUT method, which is idempotent.
    { "PUT",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: bypass=0\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_MEDIUM
    },
    // Same as above with the DELETE method, which is idempotent.
    { "DELETE",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: bypass=0\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_MEDIUM
    },
    // Same as above with the TRACE method, which is idempotent.
    { "TRACE",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: bypass=0\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_MEDIUM
    },
    // 500 responses should be bypassed.
    { "GET",
      "HTTP/1.1 500 Internal Server Error\r\n"
      "Server: proxy\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_STATUS_500_HTTP_INTERNAL_SERVER_ERROR
    },
    // 502 responses should be bypassed.
    { "GET",
      "HTTP/1.1 502 Internal Server Error\r\n"
      "Server: proxy\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_STATUS_502_HTTP_BAD_GATEWAY
    },
    // 503 responses should be bypassed.
    { "GET",
      "HTTP/1.1 503 Internal Server Error\r\n"
      "Server: proxy\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_STATUS_503_HTTP_SERVICE_UNAVAILABLE
    },
    // Invalid data reduction proxy 4xx response. Missing Via header.
    { "GET",
      "HTTP/1.1 404 Not Found\r\n"
      "Server: proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      1,
      BYPASS_EVENT_TYPE_MISSING_VIA_HEADER_4XX
    },
    // Invalid data reduction proxy response. Missing Via header.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_MISSING_VIA_HEADER_OTHER
    },
    // Invalid data reduction proxy response. Wrong Via header.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Via: 1.0 some-other-proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_MISSING_VIA_HEADER_OTHER
    },
    // Valid data reduction proxy response. 304 missing Via header.
    { "GET",
      "HTTP/1.1 304 Not Modified\r\n"
      "Server: proxy\r\n\r\n",
      false,
      false,
      0u,
      false,
      0,
      BYPASS_EVENT_TYPE_MAX
    },
    // Valid data reduction proxy response with a bypass message. It will
    // not be retried because the request is non-idempotent.
    { "POST",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: bypass=0\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      false,
      false,
      1u,
      true,
      0,
      BYPASS_EVENT_TYPE_MEDIUM
    },
    // Valid data reduction proxy response with block message. Both proxies
    // should be on the retry list when it completes.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: block=1\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      2u,
      true,
      1,
      BYPASS_EVENT_TYPE_SHORT
    },
    // Valid data reduction proxy response with a block-once message. It will be
    // retried, and there will be no proxies on the retry list since block-once
    // only affects the current request.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: block-once\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      0u,
      true,
      0,
      BYPASS_EVENT_TYPE_CURRENT
    },
    // Same as above with the OPTIONS method, which is idempotent.
    { "OPTIONS",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: block-once\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      0u,
      true,
      0,
      BYPASS_EVENT_TYPE_CURRENT
    },
    // Same as above with the HEAD method, which is idempotent.
    { "HEAD",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: block-once\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      0u,
      false,
      0,
      BYPASS_EVENT_TYPE_CURRENT
    },
    // Same as above with the PUT method, which is idempotent.
    { "PUT",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: block-once\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      0u,
      true,
      0,
      BYPASS_EVENT_TYPE_CURRENT
    },
    // Same as above with the DELETE method, which is idempotent.
    { "DELETE",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: block-once\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      0u,
      true,
      0,
      BYPASS_EVENT_TYPE_CURRENT
    },
    // Same as above with the TRACE method, which is idempotent.
    { "TRACE",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: block-once\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      0u,
      true,
      0,
      BYPASS_EVENT_TYPE_CURRENT
    },
    // Valid data reduction proxy response with a block-once message. It will
    // not be retried because the request is non-idempotent, and there will be
    // no proxies on the retry list since block-once only affects the current
    // request.
    { "POST",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: block-once\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      false,
      false,
      0u,
      true,
      0,
      BYPASS_EVENT_TYPE_CURRENT
    },
    // Valid data reduction proxy response with block and block-once messages.
    // The block message will override the block-once message, so both proxies
    // should be on the retry list when it completes.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: block=1, block-once\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      2u,
      true,
      1,
      BYPASS_EVENT_TYPE_SHORT
    },
    // Valid data reduction proxy response with bypass and block-once messages.
    // The bypass message will override the block-once message, so one proxy
    // should be on the retry list when it completes.
    { "GET",
      "HTTP/1.1 200 OK\r\n"
      "Server: proxy\r\n"
      "Chrome-Proxy: bypass=1, block-once\r\n"
      "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
      true,
      false,
      1u,
      true,
      1,
      BYPASS_EVENT_TYPE_SHORT
    },
  };
  std::string primary = test_context_->config()->test_params()->DefaultOrigin();
  std::string fallback =
      test_context_->config()->test_params()->DefaultFallbackOrigin();
  for (size_t i = 0; i < arraysize(tests); ++i) {
    ConfigureTestDependencies(ProxyService::CreateFixedFromPacResult(
        net::ProxyServer::FromURI(
            primary, net::ProxyServer::SCHEME_HTTP).ToPacString() + "; " +
            net::ProxyServer::FromURI(
                fallback,
                net::ProxyServer::SCHEME_HTTP).ToPacString() + "; DIRECT"));
    TestProxyFallback(tests[i].method,
                      tests[i].first_response,
                      tests[i].expected_retry,
                      tests[i].generate_response_error,
                      tests[i].expected_bad_proxy_count,
                      tests[i].expect_response_body);
    EXPECT_EQ(tests[i].expected_bypass_type, bypass_stats_->GetBypassType());
    // We should also observe the bad proxy in the retry list.
    TestBadProxies(tests[i].expected_bad_proxy_count,
                   tests[i].expected_duration,
                   primary, fallback);
  }
}

TEST_F(DataReductionProxyProtocolTest,
       RelaxedMissingViaHeaderOtherBypassLogic) {
  std::string primary = test_context_->config()->test_params()->DefaultOrigin();
  std::string fallback =
      test_context_->config()->test_params()->DefaultFallbackOrigin();
  base::FieldTrialList field_trial_list(new BadEntropyProvider());
  base::FieldTrialList::CreateFieldTrial(
      "DataReductionProxyRemoveMissingViaHeaderOtherBypass", "Relaxed");

  ConfigureTestDependencies(ProxyService::CreateFixedFromPacResult(
      net::ProxyServer::FromURI(
          primary, net::ProxyServer::SCHEME_HTTP).ToPacString() + "; " +
          net::ProxyServer::FromURI(
              fallback,
              net::ProxyServer::SCHEME_HTTP).ToPacString() +
              "; DIRECT"));

  // This response with the DRP via header should be accepted without causing a
  // bypass.
  TestProxyFallback("GET",
                    "HTTP/1.1 200 OK\r\n"
                    "Server: proxy\r\n"
                    "Via: 1.1 Chrome-Compression-Proxy\r\n\r\n",
                    false /* expected_retry */,
                    false /* generate_response_error */,
                    0u /* expected_bad_proxy_count */,
                    true /* expect_response_body */);
  EXPECT_EQ(BYPASS_EVENT_TYPE_MAX, bypass_stats_->GetBypassType());
  TestBadProxies(0u, -1, primary, fallback);

  // This non-4xx response without the DRP via header should not cause a bypass
  // because a DRP via header has been seen since the last network change.
  TestProxyFallback("GET",
                    "HTTP/1.1 200 OK\r\n\r\n",
                    false /* expected_retry */,
                    false /* generate_response_error */,
                    0u /* expected_bad_proxy_count */,
                    true /* expect_response_body */);
  EXPECT_EQ(BYPASS_EVENT_TYPE_MAX, bypass_stats_->GetBypassType());
  TestBadProxies(0u, -1, primary, fallback);

  // The first response after a network change is missing the DRP via header, so
  // this should cause a bypass.
  net::NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  test_context_->RunUntilIdle();
  TestProxyFallback("GET",
                    "HTTP/1.1 200 OK\r\n\r\n",
                    true /* expected_retry */,
                    false /* generate_response_error */,
                    1u /* expected_bad_proxy_count */,
                    true /* expect_response_body */);
  EXPECT_EQ(BYPASS_EVENT_TYPE_MISSING_VIA_HEADER_OTHER,
            bypass_stats_->GetBypassType());
  TestBadProxies(1u, 0, primary, fallback);
}

TEST_F(DataReductionProxyProtocolTest,
       ProxyBypassIgnoredOnDirectConnection) {
  // Verify that a Chrome-Proxy header is ignored when returned from a directly
  // connected origin server.
  ConfigureTestDependencies(ProxyService::CreateFixedFromPacResult("DIRECT"));

  MockRead data_reads[] = {
    MockRead("HTTP/1.1 200 OK\r\n"
             "Chrome-Proxy: bypass=0\r\n\r\n"),
    MockRead("Bypass message"),
    MockRead(net::SYNCHRONOUS, net::OK),
  };
  MockWrite data_writes[] = {
    MockWrite("GET / HTTP/1.1\r\n"
              "Host: www.google.com\r\n"
              "Connection: keep-alive\r\n"
              "User-Agent:\r\n"
              "Accept-Encoding: gzip, deflate\r\n\r\n"),
  };
  StaticSocketDataProvider data1(data_reads, arraysize(data_reads),
                                 data_writes, arraysize(data_writes));
  mock_socket_factory_.AddSocketDataProvider(&data1);

  TestDelegate d;
  scoped_ptr<URLRequest> r(context_->CreateRequest(
      GURL("http://www.google.com/"),
      net::DEFAULT_PRIORITY,
      &d));
  r->set_method("GET");
  r->SetLoadFlags(net::LOAD_NORMAL);

  r->Start();
  base::RunLoop().Run();

  EXPECT_EQ(net::URLRequestStatus::SUCCESS, r->status().status());
  EXPECT_EQ(net::OK, r->status().error());

  EXPECT_EQ("Bypass message", d.data_received());

  // We should have no entries in our bad proxy list.
  TestBadProxies(0, -1, "", "");
}

}  // namespace data_reduction_proxy
