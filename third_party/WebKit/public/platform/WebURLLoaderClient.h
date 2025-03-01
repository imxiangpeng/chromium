/*
 * Copyright (C) 2009 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WebURLLoaderClient_h
#define WebURLLoaderClient_h

#include <memory>
#include "public/platform/WebCommon.h"
#include "public/platform/WebDataConsumerHandle.h"
#include "public/platform/WebReferrerPolicy.h"
#include "public/platform/WebURLRequest.h"

namespace blink {

class WebString;
class WebURL;
class WebURLResponse;
struct WebURLError;

class BLINK_PLATFORM_EXPORT WebURLLoaderClient {
 public:
  // Called when following a redirect. |new_.*| arguments contain the
  // information about the received redirect. When |report_raw_headers| is
  // updated it'll be used for filtering data of the next redirect or response.
  //
  // Implementations should return true to instruct the loader to follow the
  // redirect, or false otherwise.
  virtual bool WillFollowRedirect(
      const WebURL& new_url,
      const WebURL& new_first_party_for_cookies,
      const WebString& new_referrer,
      WebReferrerPolicy new_referrer_policy,
      const WebString& new_method,
      const WebURLResponse& passed_redirect_response,
      bool& report_raw_headers) {
    return true;
  }

  // Called to report upload progress. The bytes reported correspond to
  // the HTTP message body.
  virtual void DidSendData(unsigned long long bytes_sent,
                           unsigned long long total_bytes_to_be_sent) {}

  // Called when response headers are received.
  virtual void DidReceiveResponse(const WebURLResponse&) {}

  // Called when response headers are received.
  virtual void DidReceiveResponse(
      const WebURLResponse& response,
      std::unique_ptr<WebDataConsumerHandle> handle) {
    DidReceiveResponse(response);
  }

  // Called when a chunk of response data is downloaded. This is only called
  // if WebURLRequest's downloadToFile flag was set to true.
  virtual void DidDownloadData(int data_length, int encoded_data_length) {}

  // Called when a chunk of response data is received. |dataLength| is the
  // number of bytes pointed to by |data|. |encodedDataLength| is the number
  // of bytes actually received from network to serve this chunk, including
  // HTTP headers and framing if relevant. It is 0 if the response was served
  // from cache, and -1 if this information is unavailable.
  virtual void DidReceiveData(const char* data, int data_length) {}

  // Called when the number of bytes actually received from network including
  // HTTP headers is updated. |transferSizeDiff| is positive.
  virtual void DidReceiveTransferSizeUpdate(int transfer_size_diff) {}

  // Called when a chunk of renderer-generated metadata is received from the
  // cache.
  virtual void DidReceiveCachedMetadata(const char* data, int data_length) {}

  // Called when the load completes successfully.
  // |totalEncodedDataLength| may be equal to kUnknownEncodedDataLength.
  virtual void DidFinishLoading(double finish_time,
                                int64_t total_encoded_data_length,
                                int64_t total_encoded_body_length,
                                int64_t total_decoded_body_length) {}

  // Called when the load completes with an error.
  // |totalEncodedDataLength| may be equal to kUnknownEncodedDataLength.
  virtual void DidFail(const WebURLError&,
                       int64_t total_encoded_data_length,
                       int64_t total_encoded_body_length,
                       int64_t total_decoded_body_length) {}

  // Value passed to didFinishLoading when total encoded data length isn't
  // known.
  static const int64_t kUnknownEncodedDataLength = -1;

 protected:
  virtual ~WebURLLoaderClient() {}
};

}  // namespace blink

#endif
