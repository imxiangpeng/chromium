// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_FILE_IMPL_H_
#define CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_FILE_IMPL_H_

#include "content/browser/download/download_file.h"

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/files/file.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/browser/byte_stream.h"
#include "content/browser/download/base_file.h"
#include "content/browser/download/rate_estimator.h"
#include "content/public/browser/download_item.h"
#include "content/public/browser/download_save_info.h"
#include "mojo/public/cpp/system/simple_watcher.h"
#include "net/log/net_log_with_source.h"

namespace content {
class ByteStreamReader;
class DownloadDestinationObserver;

class CONTENT_EXPORT DownloadFileImpl : public DownloadFile {
 public:
  // Takes ownership of the object pointed to by |save_info|.
  // |net_log| will be used for logging the download file's events.
  // May be constructed on any thread.  All methods besides the constructor
  // (including destruction) must occur in the same sequence.
  //
  // Note that the DownloadFileImpl automatically reads from the passed in
  // |stream_reader| or |consumer_handle|, and sends updates and status of
  // those reads to the DownloadDestinationObserver.
  DownloadFileImpl(std::unique_ptr<DownloadSaveInfo> save_info,
                   const base::FilePath& default_downloads_directory,
                   std::unique_ptr<ByteStreamReader> stream_reader,
                   const net::NetLogWithSource& net_log,
                   base::WeakPtr<DownloadDestinationObserver> observer);
  DownloadFileImpl(std::unique_ptr<DownloadSaveInfo> save_info,
                   const base::FilePath& default_downloads_directory,
                   mojo::ScopedDataPipeConsumerHandle consumer_handle,
                   const net::NetLogWithSource& net_log,
                   base::WeakPtr<DownloadDestinationObserver> observer);

  ~DownloadFileImpl() override;

  // DownloadFile functions.
  void Initialize(const InitializeCallback& initialize_callback,
                  const CancelRequestCallback& cancel_request_callback,
                  const DownloadItem::ReceivedSlices& received_slices,
                  bool is_parallelizable) override;
  void AddByteStream(std::unique_ptr<ByteStreamReader> stream_reader,
                     int64_t offset,
                     int64_t length) override;
  void AddDataPipeConsumerHandle(mojo::ScopedDataPipeConsumerHandle handle,
                                 int64_t offset,
                                 int64_t length) override;
  void RenameAndUniquify(const base::FilePath& full_path,
                         const RenameCompletionCallback& callback) override;
  void RenameAndAnnotate(const base::FilePath& full_path,
                         const std::string& client_guid,
                         const GURL& source_url,
                         const GURL& referrer_url,
                         const RenameCompletionCallback& callback) override;
  void Detach() override;
  void Cancel() override;
  void SetPotentialFileLength(int64_t length) override;
  const base::FilePath& FullPath() const override;
  bool InProgress() const override;
  void WasPaused() override;

 protected:
  // For test class overrides.
  // Write data from the offset to the file.
  // On OS level, it will seek to the |offset| and write from there.
  virtual DownloadInterruptReason WriteDataToFile(int64_t offset,
                                                  const char* data,
                                                  size_t data_len);

  virtual base::TimeDelta GetRetryDelayForFailedRename(int attempt_number);

  virtual bool ShouldRetryFailedRename(DownloadInterruptReason reason);

 private:
  friend class DownloadFileTest;

  DownloadFileImpl(std::unique_ptr<DownloadSaveInfo> save_info,
                   const base::FilePath& default_downloads_directory,
                   const net::NetLogWithSource& net_log,
                   base::WeakPtr<DownloadDestinationObserver> observer);

  // Wrapper of a ByteStreamReader or ScopedDataPipeConsumerHandle, and the meta
  // data needed to write to a slice of the target file.
  //
  // Does not require the stream reader or the consumer handle to be ready when
  // constructor is called. |stream_reader_| can be set later when the network
  // response is handled.
  //
  // Multiple SourceStreams can concurrently write to the same file sink.
  class CONTENT_EXPORT SourceStream {
   public:
    SourceStream(int64_t offset,
                 int64_t length,
                 std::unique_ptr<ByteStreamReader> stream_reader);
    SourceStream(int64_t offset,
                 int64_t length,
                 mojo::ScopedDataPipeConsumerHandle consumer_handle);
    ~SourceStream();

    // Called after successfully writing a buffer to disk.
    void OnWriteBytesToDisk(int64_t bytes_write);

    // Given a data block that is already written, truncate the length of this
    // object to avoid overwriting that block.
    void TruncateLengthWithWrittenDataBlock(int64_t offset,
                                            int64_t bytes_written);

    // Registers the callback that will be called when data is ready.
    void RegisterDataReadyCallback(
        const mojo::SimpleWatcher::ReadyCallback& callback);
    // Clears the callback that is registed when data is ready.
    void ClearDataReadyCallback();

    // Gets the status of the input stream when the stream completes.
    // TODO(qinmin): for data pipe, it currently doesn't support sending an
    // abort status at the end. The best way to do this is to add a separate
    // mojo interface for control messages when creating this object. See
    // http://crbug.com/748240. An alternative strategy is to let the
    // DownloadManager pass the status code to DownloadItem or DownloadFile.
    // However, a DownloadFile can have multiple SourceStreams, so we have to
    // maintain a map between data pipe and DownloadItem/DownloadFile somewhere.
    DownloadInterruptReason GetStatus();

    // Results for reading the SourceStream.
    enum ReadResult {
      EMPTY = 0,
      HAS_DATA,
      COMPLETE,
    };
    ReadResult Read(scoped_refptr<net::IOBuffer>* data, size_t* length);

    int64_t offset() const { return offset_; }
    int64_t length() const { return length_; }
    int64_t bytes_written() const { return bytes_written_; }
    bool is_finished() const { return finished_; }
    void set_finished(bool finish) { finished_ = finish; }
    size_t index() { return index_; }
    void set_index(size_t index) { index_ = index; }

   private:
    // Starting position for the stream to write to disk.
    int64_t offset_;

    // The maximum length to write to the disk. If set to 0, keep writing until
    // the stream depletes.
    int64_t length_;

    // Number of bytes written to disk from the stream.
    // Next write position is (|offset_| + |bytes_written_|).
    int64_t bytes_written_;

    // If all the data read from the stream has been successfully written to
    // disk.
    bool finished_;

    // The slice index in the |received_slices_| vector. A slice was created
    // once the stream started writing data to the target file.
    size_t index_;

    // The stream through which data comes.
    std::unique_ptr<ByteStreamReader> stream_reader_;

    // Objects for reading from a mojo data pipe.
    mojo::ScopedDataPipeConsumerHandle consumer_handle_;
    std::unique_ptr<mojo::SimpleWatcher> handle_watcher_;

    DISALLOW_COPY_AND_ASSIGN(SourceStream);
  };

  typedef std::unordered_map<int64_t, std::unique_ptr<SourceStream>>
      SourceStreams;

  // Options for RenameWithRetryInternal.
  enum RenameOption {
    UNIQUIFY = 1 << 0,  // If there's already a file on disk that conflicts with
                        // |new_path|, try to create a unique file by appending
                        // a uniquifier.
    ANNOTATE_WITH_SOURCE_INFORMATION = 1 << 1
  };

  struct RenameParameters {
    RenameParameters(RenameOption option,
                     const base::FilePath& new_path,
                     const RenameCompletionCallback& completion_callback);
    ~RenameParameters();

    RenameOption option;
    base::FilePath new_path;
    std::string client_guid;  // See BaseFile::AnnotateWithSourceInformation()
    GURL source_url;          // See BaseFile::AnnotateWithSourceInformation()
    GURL referrer_url;        // See BaseFile::AnnotateWithSourceInformation()
    int retries_left;         // RenameWithRetryInternal() will
                              // automatically retry until this
                              // count reaches 0. Each attempt
                              // decrements this counter.
    base::TimeTicks time_of_first_failure;  // Set to empty at first, but is set
                                            // when a failure is first
                                            // encountered. Used for UMA.
    RenameCompletionCallback completion_callback;
  };

  // Rename file_ based on |parameters|.
  void RenameWithRetryInternal(std::unique_ptr<RenameParameters> parameters);

  // Send an update on our progress.
  void SendUpdate();

  // Called before the data is written to disk.
  void WillWriteToDisk(size_t data_len);

  // For a given SourceStream object and the bytes available to write, determine
  // the actual number of bytes it can write to the disk. For parallel
  // downloading, if the first disk IO writes to a location that is already
  // written by another stream, the current stream should stop writing. Returns
  // true if the stream can write no more data and should be finished, returns
  // false otherwise.
  bool CalculateBytesToWrite(SourceStream* source_stream,
                             size_t bytes_available_to_write,
                             size_t* bytes_to_write);

  // Called when a new SourceStream object is added.
  void OnSourceStreamAdded(SourceStream* source_stream);

  // Called when there's some activity on the input data that needs to be
  // handled.
  void StreamActive(SourceStream* source_stream, MojoResult result);

  // Register callback and start to read data from the stream.
  void RegisterAndActivateStream(SourceStream* source_stream);

  // Adds a new slice to |received_slices_| and update the existing entries in
  // |source_streams_| as their lengths will change.
  // TODO(qinmin): add a test for this function.
  void AddNewSlice(int64_t offset, int64_t length);

  // Check if download is completed.
  bool IsDownloadCompleted();

  // Return the total valid bytes received in the target file.
  // If the file is a sparse file, return the total number of valid bytes.
  // Otherwise, return the current file size.
  int64_t TotalBytesReceived() const;

  // Helper method to handle stream error
  void HandleStreamError(SourceStream* source_stream,
                         DownloadInterruptReason reason);

  // Check whether this file is potentially sparse.
  bool IsSparseFile() const;

  // Given a SourceStream object, returns its neighbor that preceds it if
  // SourceStreams are ordered by their offsets
  SourceStream* FindPrecedingNeighbor(SourceStream* source_stream);

  // See |cancel_request_callback_|.
  void CancelRequest(int64_t offset);

  // Print the internal states for debugging.
  void DebugStates() const;

  net::NetLogWithSource net_log_;

  // The base file instance.
  BaseFile file_;

  // DownloadSaveInfo provided during construction. Since the DownloadFileImpl
  // can be created on any thread, this holds the save_info_ until it can be
  // used to initialize file_ on the download sequence.
  std::unique_ptr<DownloadSaveInfo> save_info_;

  // The default directory for creating the download file.
  base::FilePath default_download_directory_;

  // Map of the offset and the source stream that represents the slice
  // starting from offset.
  SourceStreams source_streams_;

  // Used to cancel the request on UI thread, since the ByteStreamReader can't
  // close the underlying resource writing to the pipe.
  CancelRequestCallback cancel_request_callback_;

  // Used to trigger progress updates.
  std::unique_ptr<base::RepeatingTimer> update_timer_;

  // Potential file length. A range request with an offset larger than this
  // value will fail. So the actual file length cannot be larger than this.
  int64_t potential_file_length_;

  // Statistics
  size_t bytes_seen_;
  base::TimeDelta disk_writes_time_;
  base::TimeTicks download_start_;
  RateEstimator rate_estimator_;
  int num_active_streams_;
  bool record_stream_bandwidth_;
  base::TimeTicks last_update_time_;
  size_t bytes_seen_with_parallel_streams_;
  size_t bytes_seen_without_parallel_streams_;
  base::TimeDelta download_time_with_parallel_streams_;
  base::TimeDelta download_time_without_parallel_streams_;

  std::vector<DownloadItem::ReceivedSlice> received_slices_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtr<DownloadDestinationObserver> observer_;
  base::WeakPtrFactory<DownloadFileImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(DownloadFileImpl);
};

}  // namespace content

#endif  // CONTENT_BROWSER_DOWNLOAD_DOWNLOAD_FILE_IMPL_H_
