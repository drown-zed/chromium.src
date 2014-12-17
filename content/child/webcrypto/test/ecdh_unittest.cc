// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/stl_util.h"
#include "content/child/webcrypto/algorithm_dispatch.h"
#include "content/child/webcrypto/crypto_data.h"
#include "content/child/webcrypto/jwk.h"
#include "content/child/webcrypto/status.h"
#include "content/child/webcrypto/test/test_helpers.h"
#include "content/child/webcrypto/webcrypto_util.h"
#include "third_party/WebKit/public/platform/WebCryptoAlgorithmParams.h"
#include "third_party/WebKit/public/platform/WebCryptoKeyAlgorithm.h"

namespace content {

namespace webcrypto {

namespace {

bool SupportsEcdh() {
#if defined(USE_OPENSSL)
  return true;
#else
  LOG(ERROR) << "Skipping ECDH test because unsupported";
  return false;
#endif
}

// TODO(eroman): Test passing an RSA public key instead of ECDH key.
// TODO(eroman): Test passing an ECDSA public key

blink::WebCryptoAlgorithm CreateEcdhImportAlgorithm(
    blink::WebCryptoNamedCurve named_curve) {
  return CreateEcImportAlgorithm(blink::WebCryptoAlgorithmIdEcdh, named_curve);
}

blink::WebCryptoAlgorithm CreateEcdhDeriveParams(
    const blink::WebCryptoKey& public_key) {
  return blink::WebCryptoAlgorithm::adoptParamsAndCreate(
      blink::WebCryptoAlgorithmIdEcdh,
      new blink::WebCryptoEcdhKeyDeriveParams(public_key));
}

blink::WebCryptoAlgorithm CreateAesGcmDerivedKeyParams(
    unsigned short length_bits) {
  return blink::WebCryptoAlgorithm::adoptParamsAndCreate(
      blink::WebCryptoAlgorithmIdAesGcm,
      new blink::WebCryptoAesDerivedKeyParams(length_bits));
}

// Helper that loads a "public_key" and "private_key" from the test data.
bool ImportKeysFromTest(const base::DictionaryValue* test,
                        blink::WebCryptoKey* public_key,
                        blink::WebCryptoKey* private_key) {
  // Import the public key.
  const base::DictionaryValue* public_key_json = NULL;
  EXPECT_TRUE(test->GetDictionary("public_key", &public_key_json));
  blink::WebCryptoNamedCurve curve =
      GetCurveNameFromDictionary(public_key_json, "crv");
  EXPECT_EQ(Status::Success(),
            ImportKey(blink::WebCryptoKeyFormatJwk,
                      CryptoData(MakeJsonVector(*public_key_json)),
                      CreateEcdhImportAlgorithm(curve), true, 0, public_key));

  // If the test didn't specify an error for private key import, that implies
  // it expects success.
  std::string expected_private_key_error = "Success";
  test->GetString("private_key_error", &expected_private_key_error);

  // Import the private key.
  const base::DictionaryValue* private_key_json = NULL;
  EXPECT_TRUE(test->GetDictionary("private_key", &private_key_json));
  curve = GetCurveNameFromDictionary(private_key_json, "crv");
  Status status = ImportKey(
      blink::WebCryptoKeyFormatJwk,
      CryptoData(MakeJsonVector(*private_key_json)),
      CreateEcdhImportAlgorithm(curve), true,
      blink::WebCryptoKeyUsageDeriveBits | blink::WebCryptoKeyUsageDeriveKey,
      private_key);
  EXPECT_EQ(expected_private_key_error, StatusToString(status));
  return status.IsSuccess();
}

TEST(WebCryptoEcdhTest, DeriveBitsKnownAnswer) {
  if (!SupportsEcdh())
    return;

  scoped_ptr<base::ListValue> tests;
  ASSERT_TRUE(ReadJsonTestFileToList("ecdh.json", &tests));

  for (size_t test_index = 0; test_index < tests->GetSize(); ++test_index) {
    SCOPED_TRACE(test_index);

    const base::DictionaryValue* test;
    ASSERT_TRUE(tests->GetDictionary(test_index, &test));

    // Import the keys.
    blink::WebCryptoKey public_key;
    blink::WebCryptoKey private_key;
    if (!ImportKeysFromTest(test, &public_key, &private_key))
      continue;

    // Now try to derive bytes.
    std::vector<uint8_t> derived_bytes;
    int length_bits = 0;
    ASSERT_TRUE(test->GetInteger("length_bits", &length_bits));

    // If the test didn't specify an error, that implies it expects success.
    std::string expected_error = "Success";
    test->GetString("error", &expected_error);

    Status status = DeriveBits(CreateEcdhDeriveParams(public_key), private_key,
                               length_bits, &derived_bytes);
    ASSERT_EQ(expected_error, StatusToString(status));
    if (status.IsError())
      continue;

    std::vector<uint8_t> expected_bytes =
        GetBytesFromHexString(test, "derived_bytes");

    EXPECT_EQ(CryptoData(expected_bytes), CryptoData(derived_bytes));
  }
}

// Loads up a test ECDH public and private key for P-521. The keys
// come from different key pairs, and can be used for key derivation of up to
// 528 bits.
void LoadTestKeys(blink::WebCryptoKey* public_key,
                  blink::WebCryptoKey* private_key) {
  // Assume that the 7th key in the test data is for P-521.
  scoped_ptr<base::ListValue> tests;
  ASSERT_TRUE(ReadJsonTestFileToList("ecdh.json", &tests));

  const base::DictionaryValue* test;
  ASSERT_TRUE(tests->GetDictionary(6, &test));

  ImportKeysFromTest(test, public_key, private_key);

  ASSERT_EQ(blink::WebCryptoNamedCurveP521,
            public_key->algorithm().ecParams()->namedCurve());
}

// Try deriving an AES key of length 129 bits.
TEST(WebCryptoEcdhTest, DeriveKeyBadAesLength) {
  if (!SupportsEcdh())
    return;

  blink::WebCryptoKey public_key;
  blink::WebCryptoKey base_key;
  LoadTestKeys(&public_key, &base_key);

  blink::WebCryptoKey derived_key;

  ASSERT_EQ(Status::ErrorGetAesKeyLength(),
            DeriveKey(CreateEcdhDeriveParams(public_key), base_key,
                      CreateAlgorithm(blink::WebCryptoAlgorithmIdAesGcm),
                      CreateAesGcmDerivedKeyParams(129), true,
                      blink::WebCryptoKeyUsageEncrypt, &derived_key));
}

// Try deriving an AES key of length 192 bits.
TEST(WebCryptoEcdhTest, DeriveKeyUnsupportedAesLength) {
  if (!SupportsEcdh())
    return;

  blink::WebCryptoKey public_key;
  blink::WebCryptoKey base_key;
  LoadTestKeys(&public_key, &base_key);

  blink::WebCryptoKey derived_key;

  ASSERT_EQ(Status::ErrorAes192BitUnsupported(),
            DeriveKey(CreateEcdhDeriveParams(public_key), base_key,
                      CreateAlgorithm(blink::WebCryptoAlgorithmIdAesGcm),
                      CreateAesGcmDerivedKeyParams(192), true,
                      blink::WebCryptoKeyUsageEncrypt, &derived_key));
}

// Try deriving an HMAC key of length 0 bits.
TEST(WebCryptoEcdhTest, DeriveKeyZeroLengthHmac) {
  if (!SupportsEcdh())
    return;

  blink::WebCryptoKey public_key;
  blink::WebCryptoKey base_key;
  LoadTestKeys(&public_key, &base_key);

  blink::WebCryptoKey derived_key;

  const blink::WebCryptoAlgorithm import_algorithm =
      CreateHmacImportAlgorithm(blink::WebCryptoAlgorithmIdSha1, 0);

  ASSERT_EQ(Status::ErrorGetHmacKeyLengthZero(),
            DeriveKey(CreateEcdhDeriveParams(public_key), base_key,
                      import_algorithm, import_algorithm, true,
                      blink::WebCryptoKeyUsageSign, &derived_key));
}

// Derive an HMAC key of length 19 bits.
TEST(WebCryptoEcdhTest, DeriveKeyHmac19Bits) {
  if (!SupportsEcdh())
    return;

  blink::WebCryptoKey public_key;
  blink::WebCryptoKey base_key;
  LoadTestKeys(&public_key, &base_key);

  blink::WebCryptoKey derived_key;

  const blink::WebCryptoAlgorithm import_algorithm =
      CreateHmacImportAlgorithm(blink::WebCryptoAlgorithmIdSha1, 19);

  ASSERT_EQ(Status::Success(),
            DeriveKey(CreateEcdhDeriveParams(public_key), base_key,
                      import_algorithm, import_algorithm, true,
                      blink::WebCryptoKeyUsageSign, &derived_key));

  ASSERT_EQ(blink::WebCryptoAlgorithmIdHmac, derived_key.algorithm().id());
  ASSERT_EQ(blink::WebCryptoAlgorithmIdSha1,
            derived_key.algorithm().hmacParams()->hash().id());
  ASSERT_EQ(19u, derived_key.algorithm().hmacParams()->lengthBits());

  // Export the key and verify its contents.
  std::vector<uint8_t> raw_key;
  EXPECT_EQ(Status::Success(),
            ExportKey(blink::WebCryptoKeyFormatRaw, derived_key, &raw_key));
  EXPECT_EQ(3u, raw_key.size());
  // The last 7 bits of the key should be zero.
  EXPECT_EQ(0, raw_key[raw_key.size() - 1] & 0x1f);
}

// Derive an HMAC key with no specified length (just the hash of SHA-256).
TEST(WebCryptoEcdhTest, DeriveKeyHmacSha256NoLength) {
  if (!SupportsEcdh())
    return;

  blink::WebCryptoKey public_key;
  blink::WebCryptoKey base_key;
  LoadTestKeys(&public_key, &base_key);

  blink::WebCryptoKey derived_key;

  const blink::WebCryptoAlgorithm import_algorithm =
      CreateHmacImportAlgorithmNoLength(blink::WebCryptoAlgorithmIdSha256);

  ASSERT_EQ(Status::Success(),
            DeriveKey(CreateEcdhDeriveParams(public_key), base_key,
                      import_algorithm, import_algorithm, true,
                      blink::WebCryptoKeyUsageSign, &derived_key));

  ASSERT_EQ(blink::WebCryptoAlgorithmIdHmac, derived_key.algorithm().id());
  ASSERT_EQ(blink::WebCryptoAlgorithmIdSha256,
            derived_key.algorithm().hmacParams()->hash().id());
  ASSERT_EQ(512u, derived_key.algorithm().hmacParams()->lengthBits());

  // Export the key and verify its contents.
  std::vector<uint8_t> raw_key;
  EXPECT_EQ(Status::Success(),
            ExportKey(blink::WebCryptoKeyFormatRaw, derived_key, &raw_key));
  EXPECT_EQ(64u, raw_key.size());
}

// Derive an HMAC key with no specified length (just the hash of SHA-512).
//
// This fails, because ECDH using P-521 can only generate 528 bits, however HMAC
// SHA-512 requires 1024 bits.
//
// In practice, authors won't be directly generating keys from key agreement
// schemes, as that is frequently insecure, and instead be using KDFs to expand
// and generate keys. For simplicity of testing, however, test using an HMAC
// key.
TEST(WebCryptoEcdhTest, DeriveKeyHmacSha512NoLength) {
  if (!SupportsEcdh())
    return;

  blink::WebCryptoKey public_key;
  blink::WebCryptoKey base_key;
  LoadTestKeys(&public_key, &base_key);

  blink::WebCryptoKey derived_key;

  const blink::WebCryptoAlgorithm import_algorithm =
      CreateHmacImportAlgorithmNoLength(blink::WebCryptoAlgorithmIdSha512);

  ASSERT_EQ(Status::ErrorEcdhLengthTooBig(528),
            DeriveKey(CreateEcdhDeriveParams(public_key), base_key,
                      import_algorithm, import_algorithm, true,
                      blink::WebCryptoKeyUsageSign, &derived_key));
}

// Try deriving an AES key of length 128 bits.
TEST(WebCryptoEcdhTest, DeriveKeyAes128) {
  if (!SupportsEcdh())
    return;

  blink::WebCryptoKey public_key;
  blink::WebCryptoKey base_key;
  LoadTestKeys(&public_key, &base_key);

  blink::WebCryptoKey derived_key;

  ASSERT_EQ(Status::Success(),
            DeriveKey(CreateEcdhDeriveParams(public_key), base_key,
                      CreateAlgorithm(blink::WebCryptoAlgorithmIdAesGcm),
                      CreateAesGcmDerivedKeyParams(128), true,
                      blink::WebCryptoKeyUsageEncrypt, &derived_key));

  ASSERT_EQ(blink::WebCryptoAlgorithmIdAesGcm, derived_key.algorithm().id());
  ASSERT_EQ(128, derived_key.algorithm().aesParams()->lengthBits());

  // Export the key and verify its contents.
  std::vector<uint8_t> raw_key;
  EXPECT_EQ(Status::Success(),
            ExportKey(blink::WebCryptoKeyFormatRaw, derived_key, &raw_key));
  EXPECT_EQ(16u, raw_key.size());
}

}  // namespace

}  // namespace webcrypto

}  // namespace content
