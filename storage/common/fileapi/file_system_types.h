// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef STORAGE_COMMON_FILEAPI_FILE_SYSTEM_TYPES_H_
#define STORAGE_COMMON_FILEAPI_FILE_SYSTEM_TYPES_H_

#include "third_party/WebKit/public/platform/WebFileSystemType.h"

namespace storage {

enum FileSystemType {
  // Indicates uninitialized or invalid filesystem type.
  kFileSystemTypeUnknown = -1,

  // ------------------------------------------------------------------------
  // Public FileSystem types, that are embedded in filesystem: URL and exposed
  // to WebKit/renderer. Both Chrome and WebKit know how to handle these types.

  // Following two types are for TEMPORARY or PERSISTENT filesystems that
  // can be used by webapps via standard app-facing API
  // as defined in File API: Directories and System.
  // http://www.w3.org/TR/file-system-api/#temporary-vs.-persistent-storage
  // They are sandboxed filesystems; all the files in the filesystems are
  // placed under the profile directory with path obfuscation and quota
  // enforcement.
  kFileSystemTypeTemporary = blink::kWebFileSystemTypeTemporary,
  kFileSystemTypePersistent = blink::kWebFileSystemTypePersistent,

  // Indicates non-sandboxed isolated filesystem.
  kFileSystemTypeIsolated = blink::kWebFileSystemTypeIsolated,

  // Indicates filesystems that are mounted externally via
  // ExternalMountPoints with a well-known mount name.  The mounted
  // filesystems can be sandboxed or non-sandboxed.  (E.g. Chrome OS mounts
  // non-sandboxed removable media folder with a name 'removable', while
  // chrome.syncFileSystem mounts a sandboxed filesystem with a name
  // 'syncfs'.)
  kFileSystemTypeExternal = blink::kWebFileSystemTypeExternal,

  // ------------------------------------------------------------------------
  // Marks the beginning of internal type enum. (This is not the actual fs type)
  kFileSystemInternalTypeEnumStart = 99,

  // Private FileSystem types, that should not appear in filesystem: URL as
  // WebKit has no idea how to handle those types.
  //
  // One can register (mount) a new file system with a private file system type
  // using IsolatedContext.  Files in such file systems can be accessed via
  // either Isolated or External public file system types (depending on
  // how the file system is registered).
  // See the comments for IsolatedContext and/or FileSystemURL for more details.

  // Should be used only for testing.
  kFileSystemTypeTest,

  // Indicates a local filesystem where we can access files using native
  // local path.
  kFileSystemTypeNativeLocal,

  // Indicates a local filesystem where we can access files using native
  // local path, but with restricted access.
  // Restricted native local file system is in read-only mode.
  kFileSystemTypeRestrictedNativeLocal,

  // Indicates a transient, isolated file system for dragged files (which could
  // contain multiple dragged paths in the virtual root).
  kFileSystemTypeDragged,

  // Indicates media filesystem which we can access with same manner to
  // regular filesystem.
  kFileSystemTypeNativeMedia,

  // Indicates media filesystem to which we need special protocol to access,
  // such as MTP or PTP.
  kFileSystemTypeDeviceMedia,

  // Indicates a Picasa virtual filesystem provided by Media Galleries API.
  kFileSystemTypePicasa,

  // Indicates a synthetic iTunes filesystem.
  kFileSystemTypeItunes,

  // Indicates a Drive filesystem which provides access to Google Drive.
  kFileSystemTypeDrive,

  // Indicates a Syncable sandboxed filesystem which can be backed by a
  // cloud storage service.
  kFileSystemTypeSyncable,

  // Indicates a special filesystem type for internal file sync operation
  // for Syncable sandboxed filesystems. The file system is overlayed, i.e.
  // points to the same sandboxed filesystem as that of kFileSystemTypeSyncable,
  // but the changes made with this filesystem type are not recorded for
  // further sync.
  kFileSystemTypeSyncableForInternalSync,

  // Indicates an external filesystem accessible by file paths from platform
  // Apps. As of writing, on non Chrome OS platform, this is merely a
  // kFileSystemTypeNativeLocal. On Chrome OS, the path is parsed by
  // the handlers of kFileSystemTypeExternal.
  kFileSystemTypeNativeForPlatformApp,

  // Indicates an isolated filesystem which is supposed to contain one
  // temporary which is supposed to go away when the last reference of
  // its snapshot is dropped.
  // This type is useful for creating a blob reference for a temporary
  // file which must go away when the blob's last reference is dropped.
  kFileSystemTypeForTransientFile,

  // Sandboxed private filesystem. This filesystem cannot be opened
  // via regular OpenFileSystem, and provides private filesystem space for
  // given identifier in each origin.
  kFileSystemTypePluginPrivate,

  // A filesystem that is mounted via the Privet storage protocol.
  kFileSystemTypeCloudDevice,

  // A filesystem that is mounted via the FileSystemProvider API.
  kFileSystemTypeProvided,

  // A media filesystem such as MTP or PTP, mounted as a file storage not
  // limited to media files.
  kFileSystemTypeDeviceMediaAsFileStorage,

  // A filesystem to provide access to contents managed by ARC.
  kFileSystemTypeArcContent,

  // A filesystem to provide access to documents providers in ARC.
  kFileSystemTypeArcDocumentsProvider,

  // A filesystem showing recently modified files across multiple sources.
  kFileSystemTypeRecent,

  // --------------------------------------------------------------------
  // Marks the end of internal type enum. (This is not the actual fs type)
  // New internal filesystem types must be added above this line.
  kFileSystemInternalTypeEnumEnd,
};

}  // namespace storage

#endif  // STORAGE_COMMON_FILEAPI_FILE_SYSTEM_TYPES_H_
