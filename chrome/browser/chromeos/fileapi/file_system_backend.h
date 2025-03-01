// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_FILEAPI_FILE_SYSTEM_BACKEND_H_
#define CHROME_BROWSER_CHROMEOS_FILEAPI_FILE_SYSTEM_BACKEND_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "storage/browser/fileapi/file_system_backend.h"
#include "storage/browser/fileapi/task_runner_bound_observer_list.h"
#include "storage/common/fileapi/file_system_types.h"

namespace storage {
class CopyOrMoveFileValidatorFactory;
class ExternalMountPoints;
class FileSystemURL;
class WatcherManager;
}  // namespace storage

namespace chromeos {

class FileSystemBackendDelegate;
class FileAccessPermissions;

// FileSystemBackend is a Chrome OS specific implementation of
// ExternalFileSystemBackend. This class is responsible for a
// number of things, including:
//
// - Add system mount points
// - Grant/revoke/check file access permissions
// - Create FileSystemOperation per file system type
// - Create FileStreamReader/Writer per file system type
//
// Chrome OS specific mount points:
//
// "Downloads" is a mount point for user's Downloads directory on the local
// disk, where downloaded files are stored by default.
//
// "archive" is a mount point for an archive file, such as a zip file. This
// mount point exposes contents of an archive file via cros_disks and AVFS
// <http://avf.sourceforge.net/>.
//
// "removable" is a mount point for removable media such as an SD card.
// Insertion and removal of removable media are handled by cros_disks.
//
// "oem" is a read-only mount point for a directory containing OEM data.
//
// "drive" is a mount point for Google Drive. Drive is integrated with the
// FileSystem API layer via drive::FileSystemProxy. This mount point is added
// by drive::DriveIntegrationService.
//
// These mount points are placed under the "external" namespace, and file
// system URLs for these mount points look like:
//
//   filesystem:<origin>/external/<mount_name>/...
//
class FileSystemBackend : public storage::ExternalFileSystemBackend {
 public:
  using storage::FileSystemBackend::OpenFileSystemCallback;

  // |system_mount_points| should outlive FileSystemBackend instance.
  FileSystemBackend(
      std::unique_ptr<FileSystemBackendDelegate> drive_delegate,
      std::unique_ptr<FileSystemBackendDelegate> file_system_provider_delegate,
      std::unique_ptr<FileSystemBackendDelegate> mtp_delegate,
      std::unique_ptr<FileSystemBackendDelegate> arc_content_delegate,
      std::unique_ptr<FileSystemBackendDelegate>
          arc_documents_provider_delegate,
      std::unique_ptr<FileSystemBackendDelegate> recent_delegate,
      scoped_refptr<storage::ExternalMountPoints> mount_points,
      storage::ExternalMountPoints* system_mount_points);
  ~FileSystemBackend() override;

  // Adds system mount points, such as "archive", and "removable". This
  // function is no-op if these mount points are already present.
  void AddSystemMountPoints();

  // Returns true if CrosMountpointProvider can handle |url|, i.e. its
  // file system type matches with what this provider supports.
  // This could be called on any threads.
  static bool CanHandleURL(const storage::FileSystemURL& url);

  // storage::FileSystemBackend overrides.
  bool CanHandleType(storage::FileSystemType type) const override;
  void Initialize(storage::FileSystemContext* context) override;
  void ResolveURL(const storage::FileSystemURL& url,
                  storage::OpenFileSystemMode mode,
                  const OpenFileSystemCallback& callback) override;
  storage::AsyncFileUtil* GetAsyncFileUtil(
      storage::FileSystemType type) override;
  storage::WatcherManager* GetWatcherManager(
      storage::FileSystemType type) override;
  storage::CopyOrMoveFileValidatorFactory* GetCopyOrMoveFileValidatorFactory(
      storage::FileSystemType type,
      base::File::Error* error_code) override;
  storage::FileSystemOperation* CreateFileSystemOperation(
      const storage::FileSystemURL& url,
      storage::FileSystemContext* context,
      base::File::Error* error_code) const override;
  bool SupportsStreaming(const storage::FileSystemURL& url) const override;
  bool HasInplaceCopyImplementation(
      storage::FileSystemType type) const override;
  std::unique_ptr<storage::FileStreamReader> CreateFileStreamReader(
      const storage::FileSystemURL& path,
      int64_t offset,
      int64_t max_bytes_to_read,
      const base::Time& expected_modification_time,
      storage::FileSystemContext* context) const override;
  std::unique_ptr<storage::FileStreamWriter> CreateFileStreamWriter(
      const storage::FileSystemURL& url,
      int64_t offset,
      storage::FileSystemContext* context) const override;
  storage::FileSystemQuotaUtil* GetQuotaUtil() override;
  const storage::UpdateObserverList* GetUpdateObservers(
      storage::FileSystemType type) const override;
  const storage::ChangeObserverList* GetChangeObservers(
      storage::FileSystemType type) const override;
  const storage::AccessObserverList* GetAccessObservers(
      storage::FileSystemType type) const override;

  // storage::ExternalFileSystemBackend overrides.
  bool IsAccessAllowed(const storage::FileSystemURL& url) const override;
  std::vector<base::FilePath> GetRootDirectories() const override;
  void GrantFileAccessToExtension(const std::string& extension_id,
                                  const base::FilePath& virtual_path) override;
  void RevokeAccessForExtension(const std::string& extension_id) override;
  bool GetVirtualPath(const base::FilePath& filesystem_path,
                      base::FilePath* virtual_path) const override;
  void GetRedirectURLForContents(
      const storage::FileSystemURL& url,
      const storage::URLCallback& callback) const override;
  storage::FileSystemURL CreateInternalURL(
      storage::FileSystemContext* context,
      const base::FilePath& entry_path) const override;

 private:
  std::unique_ptr<FileAccessPermissions> file_access_permissions_;
  std::unique_ptr<storage::AsyncFileUtil> local_file_util_;

  // The delegate instance for the drive file system related operations.
  std::unique_ptr<FileSystemBackendDelegate> drive_delegate_;

  // The delegate instance for the provided file system related operations.
  std::unique_ptr<FileSystemBackendDelegate> file_system_provider_delegate_;

  // The delegate instance for the MTP file system related operations.
  std::unique_ptr<FileSystemBackendDelegate> mtp_delegate_;

  // The delegate instance for the ARC content file system related operations.
  std::unique_ptr<FileSystemBackendDelegate> arc_content_delegate_;

  // The delegate instance for the ARC documents provider related operations.
  std::unique_ptr<FileSystemBackendDelegate> arc_documents_provider_delegate_;

  // The delegate instance for the Recent file system related operations.
  std::unique_ptr<FileSystemBackendDelegate> recent_delegate_;

  // Mount points specific to the owning context (i.e. per-profile mount
  // points).
  //
  // It is legal to have mount points with the same name as in
  // system_mount_points_. Also, mount point paths may overlap with mount point
  // paths in system_mount_points_. In both cases mount points in
  // |mount_points_| will have a priority.
  // E.g. if |mount_points_| map 'foo1' to '/foo/foo1' and
  // |file_system_mount_points_| map 'xxx' to '/foo/foo1/xxx', |GetVirtualPaths|
  // will resolve '/foo/foo1/xxx/yyy' as 'foo1/xxx/yyy' (i.e. the mapping from
  // |mount_points_| will be used).
  scoped_refptr<storage::ExternalMountPoints> mount_points_;

  // Globally visible mount points. System MountPonts instance should outlive
  // all FileSystemBackend instances, so raw pointer is safe.
  storage::ExternalMountPoints* system_mount_points_;

  DISALLOW_COPY_AND_ASSIGN(FileSystemBackend);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_CHROMEOS_FILEAPI_FILE_SYSTEM_BACKEND_H_
