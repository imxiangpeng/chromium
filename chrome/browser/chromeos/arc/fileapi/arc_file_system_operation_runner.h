// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_CHROMEOS_ARC_FILEAPI_ARC_FILE_SYSTEM_OPERATION_RUNNER_H_
#define CHROME_BROWSER_CHROMEOS_ARC_FILEAPI_ARC_FILE_SYSTEM_OPERATION_RUNNER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "chrome/browser/chromeos/arc/arc_session_manager.h"
#include "chrome/browser/chromeos/arc/fileapi/arc_file_system_bridge.h"
#include "components/arc/common/file_system.mojom.h"
#include "components/arc/instance_holder.h"
#include "components/keyed_service/core/keyed_service.h"
#include "storage/browser/fileapi/watcher_manager.h"

class BrowserContextKeyedServiceFactory;

namespace content {
class BrowserContext;
}  // namespace content

namespace arc {

class ArcBridgeService;

// Runs ARC file system operations.
//
// This is an abstraction layer on top of mojom::FileSystemInstance. ARC file
// system operations from chrome to the ARC container which can be initiated
// before the ARC container gets ready should go through this class, rather than
// invoking mojom::FileSystemInstance directly.
//
// When ARC is disabled or ARC has already booted, file system operations are
// performed immediately. While ARC boot is under progress, file operations are
// deferred until ARC boot finishes or the user disables ARC.
//
// This file system operation runner provides better UX when the user attempts
// to perform file operations while ARC is booting. For example:
//
// - Media views are mounted in Files app soon after the user logs into
//   the system. If the user attempts to open media views before ARC boots,
//   a spinner is shown until file system gets ready because ReadDirectory
//   operations are deferred.
// - When an Android content URL is opened soon after the user logs into
//   the system (because the user opened the tab before they logged out for
//   instance), the tab keeps loading until ARC boot finishes, instead of
//   failing immediately.
//
// All member functions must be called on the UI thread.
class ArcFileSystemOperationRunner
    : public KeyedService,
      public ArcFileSystemBridge::Observer,
      public ArcSessionManager::Observer,
      public InstanceHolder<mojom::FileSystemInstance>::Observer {
 public:
  using GetFileSizeCallback = mojom::FileSystemInstance::GetFileSizeCallback;
  using GetMimeTypeCallback = mojom::FileSystemInstance::GetMimeTypeCallback;
  using OpenFileToReadCallback =
      mojom::FileSystemInstance::OpenFileToReadCallback;
  using GetDocumentCallback = mojom::FileSystemInstance::GetDocumentCallback;
  using GetChildDocumentsCallback =
      mojom::FileSystemInstance::GetChildDocumentsCallback;
  using AddWatcherCallback = base::Callback<void(int64_t watcher_id)>;
  using RemoveWatcherCallback = base::Callback<void(bool success)>;
  using ChangeType = storage::WatcherManager::ChangeType;
  using WatcherCallback = base::Callback<void(ChangeType type)>;

  class Observer {
   public:
    // Called when the installed watchers are invalidated.
    // This can happen when Android system restarts, for example.
    // After this event is fired, watcher IDs issued before the event can be
    // reused.
    virtual void OnWatchersCleared() = 0;

   protected:
    virtual ~Observer() = default;
  };

  // Returns singleton instance for the given BrowserContext,
  // or nullptr if the browser |context| is not allowed to use ARC.
  static ArcFileSystemOperationRunner* GetForBrowserContext(
      content::BrowserContext* context);

  // Creates an instance suitable for unit tests.
  // This instance will run all operations immediately without deferring by
  // default. Also, deferring can be enabled/disabled by calling
  // SetShouldDefer() from friend classes.
  static std::unique_ptr<ArcFileSystemOperationRunner> CreateForTesting(
      content::BrowserContext* context,
      ArcBridgeService* bridge_service);

  // Returns Factory instance for ArcFileSystemOperationRunner.
  static BrowserContextKeyedServiceFactory* GetFactory();

  ArcFileSystemOperationRunner(content::BrowserContext* context,
                               ArcBridgeService* bridge_service);
  ~ArcFileSystemOperationRunner() override;

  // Adds or removes observers.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Runs file system operations. See file_system.mojom for documentation.
  void GetFileSize(const GURL& url, const GetFileSizeCallback& callback);
  void GetMimeType(const GURL& url, const GetMimeTypeCallback& callback);
  void OpenFileToRead(const GURL& url, const OpenFileToReadCallback& callback);
  void GetDocument(const std::string& authority,
                   const std::string& document_id,
                   const GetDocumentCallback& callback);
  void GetChildDocuments(const std::string& authority,
                         const std::string& parent_document_id,
                         const GetChildDocumentsCallback& callback);
  void AddWatcher(const std::string& authority,
                  const std::string& document_id,
                  const WatcherCallback& watcher_callback,
                  const AddWatcherCallback& callback);
  void RemoveWatcher(int64_t watcher_id, const RemoveWatcherCallback& callback);

  // KeyedService overrides:
  void Shutdown() override;

  // ArcFileSystemBridge::Observer overrides:
  void OnDocumentChanged(int64_t watcher_id, ChangeType type) override;

  // ArcSessionManager::Observer overrides:
  void OnArcPlayStoreEnabledChanged(bool enabled) override;

  // InstanceHolder<mojom::FileSystemInstance>::Observer overrides:
  void OnInstanceReady() override;
  void OnInstanceClosed() override;

 private:
  friend class ArcFileSystemOperationRunnerTest;

  ArcFileSystemOperationRunner(content::BrowserContext* context,
                               ArcBridgeService* bridge_service,
                               bool set_should_defer_by_events);

  void OnWatcherAdded(const WatcherCallback& watcher_callback,
                      const AddWatcherCallback& callback,
                      int64_t watcher_id);

  // Called whenever ARC states related to |should_defer_| are changed.
  void OnStateChanged();

  // Enables/disables deferring.
  // Friend unit tests can call this function to simulate enabling/disabling
  // deferring.
  void SetShouldDefer(bool should_defer);

  // Maybe nullptr in unittests.
  content::BrowserContext* const context_;
  ArcBridgeService* const arc_bridge_service_;  // Owned by ArcServiceManager

  // Indicates if this instance should enable/disable deferring by events.
  // Usually true, but set to false in unit tests.
  bool set_should_defer_by_events_;

  // Set to true if operations should be deferred at this moment.
  // The default is set to false so that operations are not deferred in
  // unit tests.
  bool should_defer_ = false;

  // List of deferred operations.
  std::vector<base::Closure> deferred_operations_;

  // Map from a watcher ID to a watcher callback.
  std::map<int64_t, WatcherCallback> watcher_callbacks_;

  base::ObserverList<Observer> observer_list_;

  base::WeakPtrFactory<ArcFileSystemOperationRunner> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(ArcFileSystemOperationRunner);
};

}  // namespace arc

#endif  // CHROME_BROWSER_CHROMEOS_ARC_FILEAPI_ARC_FILE_SYSTEM_OPERATION_RUNNER_H_
