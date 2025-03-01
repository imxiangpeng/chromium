// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <Foundation/Foundation.h>
#import <ImageCaptureCore/ImageCaptureCore.h>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/mac/foundation_util.h"
#include "base/mac/scoped_nsobject.h"
#include "base/mac/sdk_forward_declarations.h"
#include "base/macros.h"
#include "base/strings/sys_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/scoped_task_environment.h"
#include "base/test/sequenced_worker_pool_owner.h"
#include "base/threading/sequenced_worker_pool.h"
#include "chrome/browser/media_galleries/mac/mtp_device_delegate_impl_mac.h"
#include "components/storage_monitor/image_capture_device_manager.h"
#include "components/storage_monitor/test_storage_monitor.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char kDeviceId[] = "id";
const char kDevicePath[] = "/ic:id";
const char kTestFileContents[] = "test";

}  // namespace

@interface MockMTPICCameraDevice : ICCameraDevice {
 @private
  base::scoped_nsobject<NSMutableArray> allMediaFiles_;
}

- (void)addMediaFile:(ICCameraFile*)file;

@end

@implementation MockMTPICCameraDevice

- (NSString*)mountPoint {
  return @"mountPoint";
}

- (NSString*)name {
  return @"name";
}

- (NSString*)UUIDString {
  return base::SysUTF8ToNSString(kDeviceId);
}

- (ICDeviceType)type {
  return ICDeviceTypeCamera;
}

- (void)requestOpenSession {
}

- (void)requestCloseSession {
}

- (NSArray*)mediaFiles {
  return allMediaFiles_;
}

- (void)addMediaFile:(ICCameraFile*)file {
  if (!allMediaFiles_.get())
    allMediaFiles_.reset([[NSMutableArray alloc] init]);
  [allMediaFiles_ addObject:file];
}

- (void)requestDownloadFile:(ICCameraFile*)file
                    options:(NSDictionary*)options
           downloadDelegate:(id<ICCameraDeviceDownloadDelegate>)downloadDelegate
        didDownloadSelector:(SEL)selector
                contextInfo:(void*)contextInfo {
  base::FilePath saveDir(base::SysNSStringToUTF8(
      [[options objectForKey:ICDownloadsDirectoryURL] path]));
  std::string saveAsFilename =
      base::SysNSStringToUTF8([options objectForKey:ICSaveAsFilename]);
  // It appears that the ImageCapture library adds an extension to the requested
  // filename. Do that here to require a rename.
  saveAsFilename += ".jpg";
  base::FilePath toBeSaved = saveDir.Append(saveAsFilename);
  ASSERT_EQ(static_cast<int>(strlen(kTestFileContents)),
            base::WriteFile(toBeSaved, kTestFileContents,
                            strlen(kTestFileContents)));

  NSMutableDictionary* returnOptions =
      [NSMutableDictionary dictionaryWithDictionary:options];
  [returnOptions setObject:base::SysUTF8ToNSString(saveAsFilename)
                    forKey:ICSavedFilename];

  [static_cast<NSObject<ICCameraDeviceDownloadDelegate>*>(downloadDelegate)
   didDownloadFile:file
             error:nil
           options:returnOptions
       contextInfo:contextInfo];
}

@end

@interface MockMTPICCameraFile : ICCameraFile {
 @private
  base::scoped_nsobject<NSString> name_;
  base::scoped_nsobject<NSDate> date_;
}

- (id)init:(NSString*)name;

@end

@implementation MockMTPICCameraFile

- (id)init:(NSString*)name {
  if ((self = [super init])) {
    name_.reset([name retain]);
    date_.reset([[NSDate dateWithNaturalLanguageString:@"12/12/12"] retain]);
  }
  return self;
}

- (NSString*)name {
  return name_.get();
}

- (NSString*)UTI {
  return base::mac::CFToNSCast(kUTTypeImage);
}

- (NSDate*)modificationDate {
  return date_.get();
}

- (NSDate*)creationDate {
  return date_.get();
}

- (off_t)fileSize {
  return 1000;
}

@end

class MTPDeviceDelegateImplMacTest : public testing::Test {
 public:
  MTPDeviceDelegateImplMacTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::UI),
        camera_(NULL),
        delegate_(NULL) {}

  void SetUp() override {
    storage_monitor::TestStorageMonitor* monitor =
        storage_monitor::TestStorageMonitor::CreateAndInstall();
    manager_.SetNotifications(monitor->receiver());

    camera_ = [MockMTPICCameraDevice alloc];
    id<ICDeviceBrowserDelegate> delegate = manager_.device_browser_delegate();
    [delegate deviceBrowser:manager_.device_browser_for_test()
               didAddDevice:camera_
                 moreComing:NO];

    delegate_ = new MTPDeviceDelegateImplMac(kDeviceId, kDevicePath);
  }

  void TearDown() override {
    id<ICDeviceBrowserDelegate> delegate = manager_.device_browser_delegate();
    [delegate deviceBrowser:manager_.device_browser_for_test()
            didRemoveDevice:camera_
                  moreGoing:NO];

    delegate_->CancelPendingTasksAndDeleteDelegate();

    storage_monitor::TestStorageMonitor::Destroy();
  }

  void OnError(base::WaitableEvent* event, base::File::Error error) {
    error_ = error;
    event->Signal();
  }

  void OverlappedOnError(base::WaitableEvent* event,
                         base::File::Error error) {
    overlapped_error_ = error;
    event->Signal();
  }

  void OnFileInfo(base::WaitableEvent* event,
                  const base::File::Info& info) {
    error_ = base::File::FILE_OK;
    info_ = info;
    event->Signal();
  }

  void OnReadDir(base::WaitableEvent* event,
                 const storage::AsyncFileUtil::EntryList& files,
                 bool has_more) {
    error_ = base::File::FILE_OK;
    ASSERT_FALSE(has_more);
    file_list_ = files;
    event->Signal();
  }

  void OverlappedOnReadDir(base::WaitableEvent* event,
                           const storage::AsyncFileUtil::EntryList& files,
                           bool has_more) {
    overlapped_error_ = base::File::FILE_OK;
    ASSERT_FALSE(has_more);
    overlapped_file_list_ = files;
    event->Signal();
  }

  void OnDownload(base::WaitableEvent* event,
                  const base::File::Info& file_info,
                  const base::FilePath& local_path) {
    error_ = base::File::FILE_OK;
    event->Signal();
  }

  base::File::Error GetFileInfo(const base::FilePath& path,
                                base::File::Info* info) {
    base::WaitableEvent wait(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);
    delegate_->GetFileInfo(
      path,
      base::Bind(&MTPDeviceDelegateImplMacTest::OnFileInfo,
                 base::Unretained(this),
                 &wait),
      base::Bind(&MTPDeviceDelegateImplMacTest::OnError,
                 base::Unretained(this),
                 &wait));
    scoped_task_environment_.RunUntilIdle();
    EXPECT_TRUE(wait.IsSignaled());
    *info = info_;
    return error_;
  }

  base::File::Error ReadDir(const base::FilePath& path) {
    base::WaitableEvent wait(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);
    delegate_->ReadDirectory(
        path,
        base::Bind(&MTPDeviceDelegateImplMacTest::OnReadDir,
                   base::Unretained(this),
                   &wait),
        base::Bind(&MTPDeviceDelegateImplMacTest::OnError,
                   base::Unretained(this),
                   &wait));
    scoped_task_environment_.RunUntilIdle();
    wait.Wait();
    return error_;
  }

  base::File::Error DownloadFile(
      const base::FilePath& path,
      const base::FilePath& local_path) {
    base::WaitableEvent wait(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);
    delegate_->CreateSnapshotFile(
        path, local_path,
        base::Bind(&MTPDeviceDelegateImplMacTest::OnDownload,
                   base::Unretained(this),
                   &wait),
        base::Bind(&MTPDeviceDelegateImplMacTest::OnError,
                   base::Unretained(this),
                   &wait));
    scoped_task_environment_.RunUntilIdle();
    wait.Wait();
    return error_;
  }

 protected:
  base::test::ScopedTaskEnvironment scoped_task_environment_;
  content::TestBrowserThreadBundle test_browser_thread_bundle_;

  base::ScopedTempDir temp_dir_;
  storage_monitor::ImageCaptureDeviceManager manager_;
  MockMTPICCameraDevice* camera_;

  // This object needs special deletion inside the above |task_runner_|.
  MTPDeviceDelegateImplMac* delegate_;

  base::File::Error error_;
  base::File::Info info_;
  storage::AsyncFileUtil::EntryList file_list_;

  base::File::Error overlapped_error_;
  storage::AsyncFileUtil::EntryList overlapped_file_list_;

 private:
  DISALLOW_COPY_AND_ASSIGN(MTPDeviceDelegateImplMacTest);
};

TEST_F(MTPDeviceDelegateImplMacTest, TestGetRootFileInfo) {
  base::File::Info info;
  // Making a fresh delegate should have a single file entry for the synthetic
  // root directory, with the name equal to the device id string.
  EXPECT_EQ(base::File::FILE_OK,
            GetFileInfo(base::FilePath(kDevicePath), &info));
  EXPECT_TRUE(info.is_directory);
  EXPECT_EQ(base::File::FILE_ERROR_NOT_FOUND,
            GetFileInfo(base::FilePath("/nonexistent"), &info));

  // Signal the delegate that no files are coming.
  delegate_->NoMoreItems();

  EXPECT_EQ(base::File::FILE_OK, ReadDir(base::FilePath(kDevicePath)));
  EXPECT_EQ(0U, file_list_.size());
}

TEST_F(MTPDeviceDelegateImplMacTest, TestOverlappedReadDir) {
  base::Time time1 = base::Time::Now();
  base::File::Info info1;
  info1.size = 1;
  info1.is_directory = false;
  info1.is_symbolic_link = false;
  info1.last_modified = time1;
  info1.last_accessed = time1;
  info1.creation_time = time1;
  delegate_->ItemAdded("name1", info1);

  base::WaitableEvent wait(base::WaitableEvent::ResetPolicy::MANUAL,
                           base::WaitableEvent::InitialState::NOT_SIGNALED);

  delegate_->ReadDirectory(
      base::FilePath(kDevicePath),
      base::Bind(&MTPDeviceDelegateImplMacTest::OnReadDir,
                 base::Unretained(this),
                 &wait),
      base::Bind(&MTPDeviceDelegateImplMacTest::OnError,
                 base::Unretained(this),
                 &wait));

  delegate_->ReadDirectory(
      base::FilePath(kDevicePath),
      base::Bind(&MTPDeviceDelegateImplMacTest::OverlappedOnReadDir,
                 base::Unretained(this),
                 &wait),
      base::Bind(&MTPDeviceDelegateImplMacTest::OverlappedOnError,
                 base::Unretained(this),
                 &wait));


  // Signal the delegate that no files are coming.
  delegate_->NoMoreItems();

  scoped_task_environment_.RunUntilIdle();
  wait.Wait();

  EXPECT_EQ(base::File::FILE_OK, error_);
  EXPECT_EQ(1U, file_list_.size());
  EXPECT_EQ(base::File::FILE_OK, overlapped_error_);
  EXPECT_EQ(1U, overlapped_file_list_.size());
}

TEST_F(MTPDeviceDelegateImplMacTest, TestGetFileInfo) {
  base::Time time1 = base::Time::Now();
  base::File::Info info1;
  info1.size = 1;
  info1.is_directory = false;
  info1.is_symbolic_link = false;
  info1.last_modified = time1;
  info1.last_accessed = time1;
  info1.creation_time = time1;
  delegate_->ItemAdded("name1", info1);

  base::File::Info info;
  EXPECT_EQ(base::File::FILE_OK,
            GetFileInfo(base::FilePath("/ic:id/name1"), &info));
  EXPECT_EQ(info1.size, info.size);
  EXPECT_EQ(info1.is_directory, info.is_directory);
  EXPECT_EQ(info1.last_modified, info.last_modified);
  EXPECT_EQ(info1.last_accessed, info.last_accessed);
  EXPECT_EQ(info1.creation_time, info.creation_time);

  info1.size = 2;
  delegate_->ItemAdded("name2", info1);
  delegate_->NoMoreItems();

  EXPECT_EQ(base::File::FILE_OK,
            GetFileInfo(base::FilePath("/ic:id/name2"), &info));
  EXPECT_EQ(info1.size, info.size);

  EXPECT_EQ(base::File::FILE_OK, ReadDir(base::FilePath(kDevicePath)));

  ASSERT_EQ(2U, file_list_.size());
  EXPECT_FALSE(file_list_[0].is_directory);
  EXPECT_EQ("name1", file_list_[0].name);

  EXPECT_FALSE(file_list_[1].is_directory);
  EXPECT_EQ("name2", file_list_[1].name);
}

TEST_F(MTPDeviceDelegateImplMacTest, TestDirectoriesAndSorting) {
  base::Time time1 = base::Time::Now();
  base::File::Info info1;
  info1.size = 1;
  info1.is_directory = false;
  info1.is_symbolic_link = false;
  info1.last_modified = time1;
  info1.last_accessed = time1;
  info1.creation_time = time1;
  delegate_->ItemAdded("name2", info1);

  info1.is_directory = true;
  delegate_->ItemAdded("dir2", info1);
  delegate_->ItemAdded("dir1", info1);

  info1.is_directory = false;
  delegate_->ItemAdded("name1", info1);
  delegate_->NoMoreItems();

  EXPECT_EQ(base::File::FILE_OK, ReadDir(base::FilePath(kDevicePath)));

  ASSERT_EQ(4U, file_list_.size());
  EXPECT_EQ("dir1", file_list_[0].name);
  EXPECT_EQ("dir2", file_list_[1].name);
  EXPECT_FALSE(file_list_[2].is_directory);
  EXPECT_EQ("name1", file_list_[2].name);

  EXPECT_FALSE(file_list_[3].is_directory);
  EXPECT_EQ("name2", file_list_[3].name);
}

TEST_F(MTPDeviceDelegateImplMacTest, SubDirectories) {
  base::Time time1 = base::Time::Now();
  base::File::Info info1;
  info1.size = 0;
  info1.is_directory = true;
  info1.is_symbolic_link = false;
  info1.last_modified = time1;
  info1.last_accessed = time1;
  info1.creation_time = time1;
  delegate_->ItemAdded("dir1", info1);

  info1.size = 1;
  info1.is_directory = false;
  info1.is_symbolic_link = false;
  info1.last_modified = time1;
  info1.last_accessed = time1;
  info1.creation_time = time1;
  delegate_->ItemAdded("dir1/name1", info1);

  info1.is_directory = true;
  info1.size = 0;
  delegate_->ItemAdded("dir2", info1);

  info1.is_directory = false;
  info1.size = 1;
  delegate_->ItemAdded("dir2/name2", info1);

  info1.is_directory = true;
  info1.size = 0;
  delegate_->ItemAdded("dir2/subdir", info1);

  info1.is_directory = false;
  info1.size = 1;
  delegate_->ItemAdded("dir2/subdir/name3", info1);
  delegate_->ItemAdded("name4", info1);

  delegate_->NoMoreItems();

  EXPECT_EQ(base::File::FILE_OK, ReadDir(base::FilePath(kDevicePath)));
  ASSERT_EQ(3U, file_list_.size());
  EXPECT_TRUE(file_list_[0].is_directory);
  EXPECT_EQ("dir1", file_list_[0].name);
  EXPECT_TRUE(file_list_[1].is_directory);
  EXPECT_EQ("dir2", file_list_[1].name);
  EXPECT_FALSE(file_list_[2].is_directory);
  EXPECT_EQ("name4", file_list_[2].name);

  EXPECT_EQ(base::File::FILE_OK,
            ReadDir(base::FilePath(kDevicePath).Append("dir1")));
  ASSERT_EQ(1U, file_list_.size());
  EXPECT_FALSE(file_list_[0].is_directory);
  EXPECT_EQ("name1", file_list_[0].name);

  EXPECT_EQ(base::File::FILE_OK,
            ReadDir(base::FilePath(kDevicePath).Append("dir2")));
  ASSERT_EQ(2U, file_list_.size());
  EXPECT_FALSE(file_list_[0].is_directory);
  EXPECT_EQ("name2", file_list_[0].name);
  EXPECT_TRUE(file_list_[1].is_directory);
  EXPECT_EQ("subdir", file_list_[1].name);

  EXPECT_EQ(base::File::FILE_OK,
            ReadDir(base::FilePath(kDevicePath)
                    .Append("dir2").Append("subdir")));
  ASSERT_EQ(1U, file_list_.size());
  EXPECT_FALSE(file_list_[0].is_directory);
  EXPECT_EQ("name3", file_list_[0].name);

  EXPECT_EQ(base::File::FILE_ERROR_NOT_FOUND,
            ReadDir(base::FilePath(kDevicePath)
                    .Append("dir2").Append("subdir").Append("subdir")));
  EXPECT_EQ(base::File::FILE_ERROR_NOT_FOUND,
            ReadDir(base::FilePath(kDevicePath)
                    .Append("dir3").Append("subdir")));
  EXPECT_EQ(base::File::FILE_ERROR_NOT_FOUND,
            ReadDir(base::FilePath(kDevicePath).Append("dir3")));
}

TEST_F(MTPDeviceDelegateImplMacTest, TestDownload) {
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::Time t1 = base::Time::Now();
  base::File::Info info;
  info.size = 4;
  info.is_directory = false;
  info.is_symbolic_link = false;
  info.last_modified = t1;
  info.last_accessed = t1;
  info.creation_time = t1;
  std::string kTestFileName("filename");
  base::scoped_nsobject<MockMTPICCameraFile> picture1(
      [[MockMTPICCameraFile alloc]
          init:base::SysUTF8ToNSString(kTestFileName)]);
  [camera_ addMediaFile:picture1];
  delegate_->ItemAdded(kTestFileName, info);
  delegate_->NoMoreItems();

  EXPECT_EQ(base::File::FILE_OK, ReadDir(base::FilePath(kDevicePath)));
  ASSERT_EQ(1U, file_list_.size());
  ASSERT_EQ("filename", file_list_[0].name);

  EXPECT_EQ(base::File::FILE_ERROR_NOT_FOUND,
            DownloadFile(base::FilePath("/ic:id/nonexist"),
                         temp_dir_.GetPath().Append("target")));

  EXPECT_EQ(base::File::FILE_OK,
            DownloadFile(base::FilePath("/ic:id/filename"),
                         temp_dir_.GetPath().Append("target")));
  std::string contents;
  EXPECT_TRUE(
      base::ReadFileToString(temp_dir_.GetPath().Append("target"), &contents));
  EXPECT_EQ(kTestFileContents, contents);
}
