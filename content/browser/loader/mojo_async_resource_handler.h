// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_LOADER_MOJO_ASYNC_RESOURCE_HANDLER_H_
#define CONTENT_BROWSER_LOADER_MOJO_ASYNC_RESOURCE_HANDLER_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/loader/resource_handler.h"
#include "content/browser/loader/upload_progress_tracker.h"
#include "content/common/content_export.h"
#include "content/public/common/resource_type.h"
#include "content/public/common/url_loader.mojom.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/system/data_pipe.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "net/base/io_buffer.h"
#include "net/base/request_priority.h"

class GURL;

namespace tracked_objects {
class Location;
}

namespace net {
class IOBufferWithSize;
class URLRequest;
}

namespace content {
class ResourceController;
class ResourceDispatcherHostImpl;
struct ResourceResponse;

// Used to complete an asynchronous resource request in response to resource
// load events from the resource dispatcher host. This class is used only
// when LoadingWithMojo runtime flag is enabled.
//
// TODO(yhirano): Add histograms.
// TODO(yhirano): Send cached metadata.
//
// This class can be inherited only for tests.
class CONTENT_EXPORT MojoAsyncResourceHandler
    : public ResourceHandler,
      public NON_EXPORTED_BASE(mojom::URLLoader) {
 public:
  MojoAsyncResourceHandler(net::URLRequest* request,
                           ResourceDispatcherHostImpl* rdh,
                           mojom::URLLoaderRequest mojo_request,
                           mojom::URLLoaderClientPtr url_loader_client,
                           ResourceType resource_type);
  ~MojoAsyncResourceHandler() override;

  // ResourceHandler implementation:
  void OnRequestRedirected(
      const net::RedirectInfo& redirect_info,
      ResourceResponse* response,
      std::unique_ptr<ResourceController> controller) override;
  void OnResponseStarted(
      ResourceResponse* response,
      std::unique_ptr<ResourceController> controller) override;
  void OnWillStart(const GURL& url,
                   std::unique_ptr<ResourceController> controller) override;
  void OnWillRead(scoped_refptr<net::IOBuffer>* buf,
                  int* buf_size,
                  std::unique_ptr<ResourceController> controller) override;
  void OnReadCompleted(int bytes_read,
                       std::unique_ptr<ResourceController> controller) override;
  void OnResponseCompleted(
      const net::URLRequestStatus& status,
      std::unique_ptr<ResourceController> controller) override;
  void OnDataDownloaded(int bytes_downloaded) override;

  // mojom::URLLoader implementation:
  void FollowRedirect() override;
  void SetPriority(net::RequestPriority priority,
                   int32_t intra_priority_value) override;

  void OnWritableForTesting();
  static void SetAllocationSizeForTesting(size_t size);
  static constexpr size_t kDefaultAllocationSize = 512 * 1024;

 protected:
  // These functions can be overriden only for tests.
  virtual MojoResult BeginWrite(void** data, uint32_t* available);
  virtual MojoResult EndWrite(uint32_t written);
  virtual net::IOBufferWithSize* GetResponseMetadata(net::URLRequest* request);

 private:
  class SharedWriter;
  class WriterIOBuffer;

  // This funcion copies data stored in |buffer_| to |shared_writer_| and
  // resets |buffer_| to a WriterIOBuffer when all bytes are copied. Returns
  // true when done successfully.
  bool CopyReadDataToDataPipe(bool* defer);
  // Allocates a WriterIOBuffer and set it to |*buf|. Returns true when done
  // successfully.
  bool AllocateWriterIOBuffer(scoped_refptr<net::IOBufferWithSize>* buf,
                              bool* defer);

  bool CheckForSufficientResource();
  void OnWritable(MojoResult result);
  void Cancel();
  // Calculates the diff between URLRequest::GetTotalReceivedBytes() and
  // |reported_total_received_bytes_|, returns it, and updates
  // |reported_total_received_bytes_|.
  int64_t CalculateRecentlyReceivedBytes();

  // These functions can be overriden only for tests.
  virtual void ReportBadMessage(const std::string& error);
  virtual std::unique_ptr<UploadProgressTracker> CreateUploadProgressTracker(
      const tracked_objects::Location& from_here,
      UploadProgressTracker::UploadProgressReportCallback callback);

  void OnTransfer(mojom::URLLoaderRequest mojo_request,
                  mojom::URLLoaderClientPtr url_loader_client);
  void SendUploadProgress(const net::UploadProgress& progress);
  void OnUploadProgressACK();

  ResourceDispatcherHostImpl* rdh_;
  mojo::Binding<mojom::URLLoader> binding_;

  bool has_checked_for_sufficient_resources_ = false;
  bool sent_received_response_message_ = false;
  bool is_using_io_buffer_not_from_writer_ = false;
  // True if OnWillRead was deferred, in order to wait to be able to allocate a
  // buffer.
  bool did_defer_on_will_read_ = false;
  bool did_defer_on_writing_ = false;
  bool did_defer_on_redirect_ = false;
  base::TimeTicks response_started_ticks_;
  int64_t reported_total_received_bytes_ = 0;
  int64_t total_written_bytes_ = 0;

  // Pointer to parent's information about the read buffer. Only non-null while
  // OnWillRead is deferred.
  scoped_refptr<net::IOBuffer>* parent_buffer_ = nullptr;
  int* parent_buffer_size_ = nullptr;

  mojo::SimpleWatcher handle_watcher_;
  std::unique_ptr<mojom::URLLoader> url_loader_;
  mojom::URLLoaderClientPtr url_loader_client_;
  scoped_refptr<net::IOBufferWithSize> buffer_;
  size_t buffer_offset_ = 0;
  size_t buffer_bytes_read_ = 0;
  scoped_refptr<SharedWriter> shared_writer_;
  mojo::ScopedDataPipeConsumerHandle response_body_consumer_handle_;

  std::unique_ptr<UploadProgressTracker> upload_progress_tracker_;

  base::WeakPtrFactory<MojoAsyncResourceHandler> weak_factory_;
  DISALLOW_COPY_AND_ASSIGN(MojoAsyncResourceHandler);
};

}  // namespace content

#endif  // CONTENT_BROWSER_LOADER_MOJO_ASYNC_RESOURCE_HANDLER_H_
