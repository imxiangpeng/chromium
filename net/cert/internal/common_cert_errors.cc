// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/internal/common_cert_errors.h"

namespace net {

namespace cert_errors {

DEFINE_CERT_ERROR_ID(kValidityFailedNotAfter, "Time is after notAfter");
DEFINE_CERT_ERROR_ID(kValidityFailedNotBefore, "Time is before notBefore");
DEFINE_CERT_ERROR_ID(kDistrustedByTrustStore, "Distrusted by trust store");

DEFINE_CERT_ERROR_ID(
    kSignatureAlgorithmMismatch,
    "Certificate.signatureAlgorithm != TBSCertificate.signature");

DEFINE_CERT_ERROR_ID(kChainIsEmpty, "Chain is empty");
DEFINE_CERT_ERROR_ID(kChainIsLength1,
                     "TODO: Cannot verify a chain of length 1");
DEFINE_CERT_ERROR_ID(kUnconsumedCriticalExtension,
                     "Unconsumed critical extension");
DEFINE_CERT_ERROR_ID(
    kTargetCertInconsistentCaBits,
    "Target certificate looks like a CA but does not set all CA properties");
DEFINE_CERT_ERROR_ID(kKeyCertSignBitNotSet, "keyCertSign bit is not set");
DEFINE_CERT_ERROR_ID(kMaxPathLengthViolated, "max_path_length reached");
DEFINE_CERT_ERROR_ID(kBasicConstraintsIndicatesNotCa,
                     "Basic Constraints indicates not a CA");
DEFINE_CERT_ERROR_ID(kMissingBasicConstraints,
                     "Does not have Basic Constraints");
DEFINE_CERT_ERROR_ID(kNotPermittedByNameConstraints,
                     "Not permitted by name constraints");
DEFINE_CERT_ERROR_ID(kSubjectDoesNotMatchIssuer,
                     "subject does not match issuer");
DEFINE_CERT_ERROR_ID(kVerifySignedDataFailed, "VerifySignedData failed");
DEFINE_CERT_ERROR_ID(kSignatureAlgorithmsDifferentEncoding,
                     "Certificate.signatureAlgorithm is encoded differently "
                     "than TBSCertificate.signature");
DEFINE_CERT_ERROR_ID(kEkuLacksServerAuth,
                     "The extended key usage does not include server auth");
DEFINE_CERT_ERROR_ID(kEkuLacksClientAuth,
                     "The extended key usage does not include client auth");
DEFINE_CERT_ERROR_ID(kCertIsNotTrustAnchor,
                     "Certificate is not a trust anchor");
DEFINE_CERT_ERROR_ID(kNoValidPolicy, "No valid policy");
DEFINE_CERT_ERROR_ID(kPolicyMappingAnyPolicy,
                     "PolicyMappings must not map anyPolicy");
DEFINE_CERT_ERROR_ID(kFailedParsingSpki, "Couldn't parse SubjectPublicKeyInfo");
DEFINE_CERT_ERROR_ID(kUnacceptableSignatureAlgorithm,
                     "Unacceptable signature algorithm");
DEFINE_CERT_ERROR_ID(kUnacceptablePublicKey, "Unacceptable public key");

}  // namespace cert_errors

}  // namespace net
