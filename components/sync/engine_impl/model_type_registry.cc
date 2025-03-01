// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/engine_impl/model_type_registry.h"

#include <stddef.h>

#include <utility>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/observer_list.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/sync/base/cryptographer.h"
#include "components/sync/engine/activation_context.h"
#include "components/sync/engine/commit_queue.h"
#include "components/sync/engine/model_type_processor.h"
#include "components/sync/engine_impl/cycle/directory_type_debug_info_emitter.h"
#include "components/sync/engine_impl/cycle/non_blocking_type_debug_info_emitter.h"
#include "components/sync/engine_impl/directory_commit_contributor.h"
#include "components/sync/engine_impl/directory_update_handler.h"
#include "components/sync/engine_impl/model_type_worker.h"

namespace syncer {

namespace {

class CommitQueueProxy : public CommitQueue {
 public:
  CommitQueueProxy(const base::WeakPtr<ModelTypeWorker>& worker,
                   const scoped_refptr<base::SequencedTaskRunner>& sync_thread);
  ~CommitQueueProxy() override;

  void EnqueueForCommit(const CommitRequestDataList& list) override;
  void NudgeForCommit() override;

 private:
  base::WeakPtr<ModelTypeWorker> worker_;
  scoped_refptr<base::SequencedTaskRunner> sync_thread_;
};

CommitQueueProxy::CommitQueueProxy(
    const base::WeakPtr<ModelTypeWorker>& worker,
    const scoped_refptr<base::SequencedTaskRunner>& sync_thread)
    : worker_(worker), sync_thread_(sync_thread) {}

CommitQueueProxy::~CommitQueueProxy() {}

void CommitQueueProxy::EnqueueForCommit(const CommitRequestDataList& list) {
  sync_thread_->PostTask(
      FROM_HERE, base::Bind(&ModelTypeWorker::EnqueueForCommit, worker_, list));
}

void CommitQueueProxy::NudgeForCommit() {
  sync_thread_->PostTask(FROM_HERE,
                         base::Bind(&ModelTypeWorker::NudgeForCommit, worker_));
}

}  // namespace

ModelTypeRegistry::ModelTypeRegistry(
    const std::vector<scoped_refptr<ModelSafeWorker>>& workers,
    UserShare* user_share,
    NudgeHandler* nudge_handler,
    const UssMigrator& uss_migrator,
    CancelationSignal* cancelation_signal)
    : user_share_(user_share),
      nudge_handler_(nudge_handler),
      uss_migrator_(uss_migrator),
      cancelation_signal_(cancelation_signal),
      weak_ptr_factory_(this) {
  for (size_t i = 0u; i < workers.size(); ++i) {
    workers_map_.insert(
        std::make_pair(workers[i]->GetModelSafeGroup(), workers[i]));
  }
}

ModelTypeRegistry::~ModelTypeRegistry() {}

void ModelTypeRegistry::ConnectNonBlockingType(
    ModelType type,
    std::unique_ptr<ActivationContext> activation_context) {
  DCHECK(update_handler_map_.find(type) == update_handler_map_.end());
  DCHECK(commit_contributor_map_.find(type) == commit_contributor_map_.end());
  DVLOG(1) << "Enabling an off-thread sync type: " << ModelTypeToString(type);

  bool initial_sync_done =
      activation_context->model_type_state.initial_sync_done();
  // Attempt migration if the USS initial sync hasn't been done, there is a
  // migrator function, and directory has data for this type.
  bool do_migration = !initial_sync_done && !uss_migrator_.is_null() &&
                      directory()->InitialSyncEndedForType(type);
  bool trigger_initial_sync = !initial_sync_done && !do_migration;

  // Save a raw pointer to the processor for connecting later.
  ModelTypeProcessor* type_processor = activation_context->type_processor.get();

  std::unique_ptr<Cryptographer> cryptographer_copy;
  if (encrypted_types_.Has(type))
    cryptographer_copy = base::MakeUnique<Cryptographer>(*cryptographer_);

  DataTypeDebugInfoEmitter* emitter = GetEmitter(type);
  if (emitter == nullptr) {
    auto new_emitter = base::MakeUnique<NonBlockingTypeDebugInfoEmitter>(
        type, &type_debug_info_observers_);
    emitter = new_emitter.get();
    data_type_debug_info_emitter_map_.insert(
        std::make_pair(type, std::move(new_emitter)));
  }

  auto worker = base::MakeUnique<ModelTypeWorker>(
      type, activation_context->model_type_state, trigger_initial_sync,
      std::move(cryptographer_copy), nudge_handler_,
      std::move(activation_context->type_processor), emitter,
      cancelation_signal_);

  // Save a raw pointer and add the worker to our structures.
  ModelTypeWorker* worker_ptr = worker.get();
  model_type_workers_.push_back(std::move(worker));
  update_handler_map_.insert(std::make_pair(type, worker_ptr));
  commit_contributor_map_.insert(std::make_pair(type, worker_ptr));

  // Initialize Processor -> Worker communication channel.
  type_processor->ConnectSync(base::MakeUnique<CommitQueueProxy>(
      worker_ptr->AsWeakPtr(), base::ThreadTaskRunnerHandle::Get()));

  // Attempt migration if necessary.
  if (do_migration) {
    // TODO(crbug.com/658002): Store a pref before attempting migration
    // indicating that it was attempted so we can avoid failure loops.
    if (uss_migrator_.Run(type, user_share_, worker_ptr)) {
      // TODO(wychen): enum uma should be strongly typed. crbug.com/661401
      UMA_HISTOGRAM_ENUMERATION("Sync.USSMigrationSuccess",
                                ModelTypeToHistogramInt(type),
                                static_cast<int>(MODEL_TYPE_COUNT));
      // If we succesfully migrated, purge the directory of data for the type.
      // Purging removes the directory's local copy of the data only.
      directory()->PurgeEntriesWithTypeIn(ModelTypeSet(type), ModelTypeSet(),
                                          ModelTypeSet());
    } else {
      // TODO(wychen): enum uma should be strongly typed. crbug.com/661401
      UMA_HISTOGRAM_ENUMERATION("Sync.USSMigrationFailure",
                                ModelTypeToHistogramInt(type),
                                static_cast<int>(MODEL_TYPE_COUNT));
    }
  }

  // We want to check that we haven't accidentally enabled both the non-blocking
  // and directory implementations for a given model type. This is true even if
  // migration fails; our fallback in this case is to do an initial GetUpdates,
  // not to use the directory implementation.
  DCHECK(Intersection(GetEnabledDirectoryTypes(), GetEnabledNonBlockingTypes())
             .Empty());
}

void ModelTypeRegistry::DisconnectNonBlockingType(ModelType type) {
  DVLOG(1) << "Disabling an off-thread sync type: " << ModelTypeToString(type);
  DCHECK(update_handler_map_.find(type) != update_handler_map_.end());
  DCHECK(commit_contributor_map_.find(type) != commit_contributor_map_.end());

  size_t updaters_erased = update_handler_map_.erase(type);
  size_t committers_erased = commit_contributor_map_.erase(type);

  DCHECK_EQ(1U, updaters_erased);
  DCHECK_EQ(1U, committers_erased);

  auto iter = model_type_workers_.begin();
  while (iter != model_type_workers_.end()) {
    if ((*iter)->GetModelType() == type) {
      iter = model_type_workers_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void ModelTypeRegistry::RegisterDirectoryType(ModelType type,
                                              ModelSafeGroup group) {
  DCHECK(update_handler_map_.find(type) == update_handler_map_.end());
  DCHECK(commit_contributor_map_.find(type) == commit_contributor_map_.end());
  DCHECK(directory_update_handlers_.find(type) ==
         directory_update_handlers_.end());
  DCHECK(directory_commit_contributors_.find(type) ==
         directory_commit_contributors_.end());
  DCHECK(data_type_debug_info_emitter_map_.find(type) ==
         data_type_debug_info_emitter_map_.end());
  DCHECK_NE(GROUP_NON_BLOCKING, group);
  DCHECK(workers_map_.find(group) != workers_map_.end());

  auto worker = workers_map_.find(group)->second;
  DCHECK(GetEmitter(type) == nullptr);
  auto owned_emitter = base::MakeUnique<DirectoryTypeDebugInfoEmitter>(
      directory(), type, &type_debug_info_observers_);
  DataTypeDebugInfoEmitter* emitter_ptr = owned_emitter.get();
  data_type_debug_info_emitter_map_[type] = std::move(owned_emitter);

  auto updater = base::MakeUnique<DirectoryUpdateHandler>(directory(), type,
                                                          worker, emitter_ptr);
  auto committer = base::MakeUnique<DirectoryCommitContributor>(
      directory(), type, emitter_ptr);

  update_handler_map_[type] = updater.get();
  commit_contributor_map_[type] = committer.get();

  directory_update_handlers_[type] = std::move(updater);
  directory_commit_contributors_[type] = std::move(committer);

  DCHECK(Intersection(GetEnabledDirectoryTypes(), GetEnabledNonBlockingTypes())
             .Empty());
}

void ModelTypeRegistry::UnregisterDirectoryType(ModelType type) {
  DCHECK(update_handler_map_.find(type) != update_handler_map_.end());
  DCHECK(commit_contributor_map_.find(type) != commit_contributor_map_.end());
  DCHECK(directory_update_handlers_.find(type) !=
         directory_update_handlers_.end());
  DCHECK(directory_commit_contributors_.find(type) !=
         directory_commit_contributors_.end());
  DCHECK(data_type_debug_info_emitter_map_.find(type) !=
         data_type_debug_info_emitter_map_.end());

  update_handler_map_.erase(type);
  commit_contributor_map_.erase(type);
  directory_update_handlers_.erase(type);
  directory_commit_contributors_.erase(type);
  data_type_debug_info_emitter_map_.erase(type);
}

ModelTypeSet ModelTypeRegistry::GetEnabledTypes() const {
  return Union(GetEnabledDirectoryTypes(), GetEnabledNonBlockingTypes());
}

ModelTypeSet ModelTypeRegistry::GetInitialSyncEndedTypes() const {
  // TODO(pavely): GetInitialSyncEndedTypes is queried at the end of sync
  // manager initialization when update handlers aren't set up yet. Returning
  // correct set of types is important because otherwise data for al types will
  // be redownloaded during configuration. For now let's return union of types
  // reported by directory and types reported by update handlers. We need to
  // refactor initialization and configuratrion flow to be able to only query
  // this set from update handlers.
  ModelTypeSet result = directory()->InitialSyncEndedTypes();
  for (const auto& kv : update_handler_map_) {
    if (kv.second->IsInitialSyncEnded())
      result.Put(kv.first);
  }
  return result;
}

ModelTypeSet ModelTypeRegistry::GetInitialSyncDoneNonBlockingTypes() const {
  ModelTypeSet types;
  for (const auto& worker : model_type_workers_) {
    if (worker->IsInitialSyncEnded()) {
      types.Put(worker->GetModelType());
    }
  }
  return types;
}

UpdateHandlerMap* ModelTypeRegistry::update_handler_map() {
  return &update_handler_map_;
}

CommitContributorMap* ModelTypeRegistry::commit_contributor_map() {
  return &commit_contributor_map_;
}

void ModelTypeRegistry::RegisterDirectoryTypeDebugInfoObserver(
    TypeDebugInfoObserver* observer) {
  if (!type_debug_info_observers_.HasObserver(observer))
    type_debug_info_observers_.AddObserver(observer);
}

void ModelTypeRegistry::UnregisterDirectoryTypeDebugInfoObserver(
    TypeDebugInfoObserver* observer) {
  type_debug_info_observers_.RemoveObserver(observer);
}

bool ModelTypeRegistry::HasDirectoryTypeDebugInfoObserver(
    const TypeDebugInfoObserver* observer) const {
  return type_debug_info_observers_.HasObserver(observer);
}

void ModelTypeRegistry::RequestEmitDebugInfo() {
  for (const auto& kv : data_type_debug_info_emitter_map_) {
    kv.second->EmitCommitCountersUpdate();
    kv.second->EmitUpdateCountersUpdate();
    // Although this breaks encapsulation, don't emit status counters here.
    // They've already been asked for manually on the UI thread because USS
    // emitters don't have a working implementation yet.
  }
}

base::WeakPtr<ModelTypeConnector> ModelTypeRegistry::AsWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

void ModelTypeRegistry::OnPassphraseRequired(
    PassphraseRequiredReason reason,
    const sync_pb::EncryptedData& pending_keys) {}

void ModelTypeRegistry::OnPassphraseAccepted() {
  for (const auto& worker : model_type_workers_) {
    if (encrypted_types_.Has(worker->GetModelType())) {
      worker->EncryptionAcceptedApplyUpdates();
    }
  }
}

void ModelTypeRegistry::OnBootstrapTokenUpdated(
    const std::string& bootstrap_token,
    BootstrapTokenType type) {}

void ModelTypeRegistry::OnEncryptedTypesChanged(ModelTypeSet encrypted_types,
                                                bool encrypt_everything) {
  // TODO(skym): This does not handle reducing the number of encrypted types
  // correctly. They're removed from |encrypted_types_| but corresponding
  // workers never have their Cryptographers removed. This probably is not a use
  // case that currently needs to be supported, but it should be guarded against
  // here.
  encrypted_types_ = encrypted_types;
  OnEncryptionStateChanged();
}

void ModelTypeRegistry::OnEncryptionComplete() {}

void ModelTypeRegistry::OnCryptographerStateChanged(
    Cryptographer* cryptographer) {
  cryptographer_ = base::MakeUnique<Cryptographer>(*cryptographer);
  OnEncryptionStateChanged();
}

void ModelTypeRegistry::OnPassphraseTypeChanged(PassphraseType type,
                                                base::Time passphrase_time) {}

void ModelTypeRegistry::OnLocalSetPassphraseEncryption(
    const SyncEncryptionHandler::NigoriState& nigori_state) {}

void ModelTypeRegistry::OnEncryptionStateChanged() {
  for (const auto& worker : model_type_workers_) {
    if (encrypted_types_.Has(worker->GetModelType())) {
      worker->UpdateCryptographer(
          base::MakeUnique<Cryptographer>(*cryptographer_));
    }
  }
}

DataTypeDebugInfoEmitter* ModelTypeRegistry::GetEmitter(ModelType type) {
  DataTypeDebugInfoEmitter* raw_emitter = nullptr;
  auto it = data_type_debug_info_emitter_map_.find(type);
  if (it != data_type_debug_info_emitter_map_.end()) {
    raw_emitter = it->second.get();
  }
  return raw_emitter;
}

ModelTypeSet ModelTypeRegistry::GetEnabledDirectoryTypes() const {
  ModelTypeSet enabled_directory_types;
  for (const auto& kv : directory_update_handlers_)
    enabled_directory_types.Put(kv.first);
  return enabled_directory_types;
}

ModelTypeSet ModelTypeRegistry::GetEnabledNonBlockingTypes() const {
  ModelTypeSet enabled_non_blocking_types;
  for (const auto& worker : model_type_workers_) {
    enabled_non_blocking_types.Put(worker->GetModelType());
  }
  return enabled_non_blocking_types;
}

}  // namespace syncer
