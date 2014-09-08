// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_GCM_DRIVER_GCM_CHANNEL_STATUS_REQUEST_H_
#define COMPONENTS_GCM_DRIVER_GCM_CHANNEL_STATUS_REQUEST_H_

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "net/base/backoff_entry.h"
#include "net/url_request/url_fetcher_delegate.h"
#include "net/url_request/url_request_context_getter.h"

namespace net {
class URLRequestContextGetter;
}

namespace gcm {

// Defines the request to talk with the server to determine if the GCM support
// should be enabled.
class GCMChannelStatusRequest : public net::URLFetcherDelegate {
 public:
  // Callback completing the channel status request.
  typedef base::Callback<void(bool enabled, int poll_interval_seconds)>
      GCMChannelStatusRequestCallback;

  GCMChannelStatusRequest(
      const scoped_refptr<net::URLRequestContextGetter>& request_context_getter,
      const GCMChannelStatusRequestCallback& callback);
  virtual ~GCMChannelStatusRequest();

  void Start();

  // Exposed for testing purpose.
  static int default_poll_interval_seconds_for_testing();
  static int min_poll_interval_seconds_for_testing();

 private:
  // Overridden from URLFetcherDelegate:
  virtual void OnURLFetchComplete(const net::URLFetcher* source) OVERRIDE;

  bool ParseResponse(const net::URLFetcher* source);
  void RetryWithBackoff(bool update_backoff);

  scoped_refptr<net::URLRequestContextGetter> request_context_getter_;
  GCMChannelStatusRequestCallback callback_;
  scoped_ptr<net::URLFetcher> url_fetcher_;
  net::BackoffEntry backoff_entry_;
  base::WeakPtrFactory<GCMChannelStatusRequest> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(GCMChannelStatusRequest);
};

}  // namespace gcm

#endif  // COMPONENTS_GCM_DRIVER_GCM_CHANNEL_STATUS_REQUEST_H_
