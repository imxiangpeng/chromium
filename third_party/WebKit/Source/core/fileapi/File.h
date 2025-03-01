/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef File_h
#define File_h

#include "bindings/core/v8/ArrayBufferOrArrayBufferViewOrBlobOrUSVString.h"
#include "core/CoreExport.h"
#include "core/fileapi/Blob.h"
#include "platform/heap/Handle.h"
#include "platform/wtf/RefPtr.h"
#include "platform/wtf/text/WTFString.h"

namespace blink {

class ExceptionState;
class ExecutionContext;
class FilePropertyBag;
class FileMetadata;
class KURL;
class ScriptState;

class CORE_EXPORT File final : public Blob {
  DEFINE_WRAPPERTYPEINFO();

 public:
  // AllContentTypes should only be used when the full path/name are trusted;
  // otherwise, it could allow arbitrary pages to determine what applications an
  // user has installed.
  enum ContentTypeLookupPolicy {
    kWellKnownContentTypes,
    kAllContentTypes,
  };

  // The user should not be able to browse to some files, such as the ones
  // generated by the Filesystem API.
  enum UserVisibility { kIsUserVisible, kIsNotUserVisible };

  // Constructor in File.idl
  static File* Create(
      ExecutionContext*,
      const HeapVector<ArrayBufferOrArrayBufferViewOrBlobOrUSVString>&,
      const String& file_name,
      const FilePropertyBag&,
      ExceptionState&);

  static File* Create(const String& path,
                      ContentTypeLookupPolicy policy = kWellKnownContentTypes) {
    return new File(path, policy, File::kIsUserVisible);
  }

  static File* Create(const String& name,
                      double modification_time,
                      RefPtr<BlobDataHandle> blob_data_handle) {
    return new File(name, modification_time, std::move(blob_data_handle));
  }

  // For deserialization.
  static File* CreateFromSerialization(
      const String& path,
      const String& name,
      const String& relative_path,
      UserVisibility user_visibility,
      bool has_snapshot_data,
      uint64_t size,
      double last_modified,
      RefPtr<BlobDataHandle> blob_data_handle) {
    return new File(path, name, relative_path, user_visibility,
                    has_snapshot_data, size, last_modified,
                    std::move(blob_data_handle));
  }
  static File* CreateFromIndexedSerialization(
      const String& path,
      const String& name,
      uint64_t size,
      double last_modified,
      RefPtr<BlobDataHandle> blob_data_handle) {
    return new File(path, name, String(), kIsNotUserVisible, true, size,
                    last_modified, std::move(blob_data_handle));
  }

  static File* CreateWithRelativePath(const String& path,
                                      const String& relative_path);

  // If filesystem files live in the remote filesystem, the port might pass the
  // valid metadata (whose length field is non-negative) and cache in the File
  // object.
  //
  // Otherwise calling size(), lastModifiedTime() and slice() will synchronously
  // query the file metadata.
  static File* CreateForFileSystemFile(const String& name,
                                       const FileMetadata& metadata,
                                       UserVisibility user_visibility) {
    return new File(name, metadata, user_visibility);
  }

  static File* CreateForFileSystemFile(const KURL& url,
                                       const FileMetadata& metadata,
                                       UserVisibility user_visibility) {
    return new File(url, metadata, user_visibility);
  }

  KURL FileSystemURL() const {
#if DCHECK_IS_ON()
    DCHECK(HasValidFileSystemURL());
#endif
    return file_system_url_;
  }

  // Create a file with a name exposed to the author (via File.name and
  // associated DOM properties) that differs from the one provided in the path.
  static File* CreateForUserProvidedFile(const String& path,
                                         const String& display_name) {
    if (display_name.IsEmpty())
      return new File(path, File::kAllContentTypes, File::kIsUserVisible);
    return new File(path, display_name, File::kAllContentTypes,
                    File::kIsUserVisible);
  }

  static File* CreateForFileSystemFile(
      const String& path,
      const String& name,
      ContentTypeLookupPolicy policy = kWellKnownContentTypes) {
    if (name.IsEmpty())
      return new File(path, policy, File::kIsNotUserVisible);
    return new File(path, name, policy, File::kIsNotUserVisible);
  }

  File* Clone(const String& name = String()) const;

  unsigned long long size() const override;
  Blob* slice(long long start,
              long long end,
              const String& content_type,
              ExceptionState&) const override;
  void close(ScriptState*, ExceptionState&) override;

  bool IsFile() const override { return true; }
  bool HasBackingFile() const override { return has_backing_file_; }

  void AppendTo(BlobData&) const override;

  const String& GetPath() const {
#if DCHECK_IS_ON()
    DCHECK(HasValidFilePath());
#endif
    return path_;
  }
  const String& name() const { return name_; }

  // Getter for the lastModified IDL attribute,
  // http://dev.w3.org/2006/webapi/FileAPI/#file-attrs
  long long lastModified() const;

  // Getter for the lastModifiedDate IDL attribute,
  // http://www.w3.org/TR/FileAPI/#dfn-lastModifiedDate
  double lastModifiedDate() const;

  UserVisibility GetUserVisibility() const { return user_visibility_; }

  // Returns the relative path of this file in the context of a directory
  // selection.
  const String& webkitRelativePath() const { return relative_path_; }

  // Note that this involves synchronous file operation. Think twice before
  // calling this function.
  void CaptureSnapshot(long long& snapshot_size,
                       double& snapshot_modification_time_ms) const;

  // Returns true if this has a valid snapshot metadata
  // (i.e. m_snapshotSize >= 0).
  bool HasValidSnapshotMetadata() const { return snapshot_size_ >= 0; }

  // Returns true if the sources (file path, file system URL, or blob handler)
  // of the file objects are same or not.
  bool HasSameSource(const File& other) const;

 private:
  File(const String& path, ContentTypeLookupPolicy, UserVisibility);
  File(const String& path,
       const String& name,
       ContentTypeLookupPolicy,
       UserVisibility);
  File(const String& path,
       const String& name,
       const String& relative_path,
       UserVisibility,
       bool has_snapshot_data,
       uint64_t size,
       double last_modified,
       RefPtr<BlobDataHandle>);
  File(const String& name, double modification_time, RefPtr<BlobDataHandle>);
  File(const String& name, const FileMetadata&, UserVisibility);
  File(const KURL& file_system_url, const FileMetadata&, UserVisibility);
  File(const File&);

  void InvalidateSnapshotMetadata() { snapshot_size_ = -1; }

  // Returns File's last modified time (in MS since Epoch.)
  // If the modification time isn't known, the current time is returned.
  double LastModifiedMS() const;

#if DCHECK_IS_ON()
  // Instances backed by a file must have an empty file system URL.
  bool HasValidFileSystemURL() const {
    return !HasBackingFile() || file_system_url_.IsEmpty();
  }
  // Instances not backed by a file must have an empty path set.
  bool HasValidFilePath() const { return HasBackingFile() || path_.IsEmpty(); }
#endif

  bool has_backing_file_;
  UserVisibility user_visibility_;
  String path_;
  String name_;

  KURL file_system_url_;

  // If m_snapshotSize is negative (initialized to -1 by default), the snapshot
  // metadata is invalid and we retrieve the latest metadata synchronously in
  // size(), lastModifiedTime() and slice().
  // Otherwise, the snapshot metadata are used directly in those methods.
  long long snapshot_size_;
  const double snapshot_modification_time_ms_;

  String relative_path_;
};

DEFINE_TYPE_CASTS(File, Blob, blob, blob->IsFile(), blob.IsFile());

}  // namespace blink

#endif  // File_h
