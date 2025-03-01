// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/engine_impl/uss_migrator.h"

#include <memory>
#include <string>
#include <utility>

#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/time/time.h"
#include "components/sync/base/cancelation_signal.h"
#include "components/sync/base/hash_util.h"
#include "components/sync/engine_impl/cycle/non_blocking_type_debug_info_emitter.h"
#include "components/sync/engine_impl/model_type_worker.h"
#include "components/sync/engine_impl/test_entry_factory.h"
#include "components/sync/protocol/model_type_state.pb.h"
#include "components/sync/syncable/directory.h"
#include "components/sync/syncable/read_node.h"
#include "components/sync/syncable/read_transaction.h"
#include "components/sync/syncable/test_user_share.h"
#include "components/sync/test/engine/mock_model_type_processor.h"
#include "components/sync/test/engine/mock_nudge_handler.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

const ModelType kModelType = PREFERENCES;

std::string GenerateTagHash(const std::string& tag) {
  return GenerateSyncableHash(kModelType, tag);
}

const char kToken1[] = "token1";
const char kTag1[] = "tag1";
const char kTag2[] = "tag2";
const char kTag3[] = "tag3";
const char kValue1[] = "value1";
const char kValue2[] = "value2";
const char kValue3[] = "value3";
const std::string kHash1(GenerateTagHash(kTag1));

sync_pb::EntitySpecifics GenerateSpecifics(const std::string& tag,
                                           const std::string& value) {
  sync_pb::EntitySpecifics specifics;
  specifics.mutable_preference()->set_name(tag);
  specifics.mutable_preference()->set_value(value);
  return specifics;
}

}  // namespace

class UssMigratorTest : public ::testing::Test {
 public:
  UssMigratorTest() : debug_emitter_(kModelType, &debug_observers_) {
    test_user_share_.SetUp();
    entry_factory_ = base::MakeUnique<TestEntryFactory>(directory());

    auto processor = base::MakeUnique<MockModelTypeProcessor>();
    processor_ = processor.get();
    worker_ = base::MakeUnique<ModelTypeWorker>(
        kModelType, sync_pb::ModelTypeState(), false, nullptr, &nudge_handler_,
        std::move(processor), &debug_emitter_, &cancelation_signal_);
  }

  ~UssMigratorTest() override { test_user_share_.TearDown(); }

  void SetProgressMarkerToken(const std::string& token) {
    sync_pb::DataTypeProgressMarker progress_marker;
    progress_marker.set_token(token);
    directory()->SetDownloadProgress(kModelType, progress_marker);
  }

  void CreateTypeRoot() { entry_factory_->CreateTypeRootNode(kModelType); }

  int64_t InsertEntity(const std::string& key, const std::string& value) {
    const sync_pb::EntitySpecifics specifics = GenerateSpecifics(key, value);
    return entry_factory_->CreateSyncedItem(key, kModelType, false, specifics);
  }

  int64_t DeleteEntity(const std::string& key) {
    return entry_factory_->CreateTombstone(key, kModelType);
  }

  base::Time GetCtimeForEntity(int64_t metahandle) {
    ReadTransaction trans(FROM_HERE, user_share());
    ReadNode read_node(&trans);
    auto result = read_node.InitByIdLookup(metahandle);
    DCHECK_EQ(BaseNode::INIT_OK, result);
    return read_node.GetEntry()->GetServerCtime();
  }

  UserShare* user_share() { return test_user_share_.user_share(); }
  MockNudgeHandler* nudge_handler() { return &nudge_handler_; }
  ModelTypeWorker* worker() { return worker_.get(); }
  MockModelTypeProcessor* processor() { return processor_; }

 private:
  syncable::Directory* directory() { return user_share()->directory.get(); }

  base::MessageLoop message_loop_;
  TestUserShare test_user_share_;
  CancelationSignal cancelation_signal_;
  std::unique_ptr<TestEntryFactory> entry_factory_;
  MockNudgeHandler nudge_handler_;
  base::ObserverList<TypeDebugInfoObserver> debug_observers_;
  NonBlockingTypeDebugInfoEmitter debug_emitter_;
  MockModelTypeProcessor* processor_ = nullptr;
  std::unique_ptr<ModelTypeWorker> worker_;
};

TEST_F(UssMigratorTest, Migrate) {
  CreateTypeRoot();
  SetProgressMarkerToken(kToken1);
  int64_t metahandle = InsertEntity(kTag1, kValue1);
  base::Time ctime = GetCtimeForEntity(metahandle);

  ASSERT_TRUE(MigrateDirectoryData(kModelType, user_share(), worker()));

  // No nudge should happen in the happy case.
  EXPECT_EQ(0, nudge_handler()->GetNumInitialDownloadNudges());
  // One update with one entity in it.
  EXPECT_EQ(1U, processor()->GetNumUpdateResponses());
  EXPECT_EQ(1U, processor()->GetNthUpdateResponse(0).size());

  const sync_pb::ModelTypeState& state = processor()->GetNthUpdateState(0);
  EXPECT_EQ(kToken1, state.progress_marker().token());

  UpdateResponseData update = processor()->GetNthUpdateResponse(0).at(0);
  const EntityData& entity = update.entity.value();

  EXPECT_FALSE(entity.id.empty());
  EXPECT_EQ(kHash1, entity.client_tag_hash);
  EXPECT_EQ(1, update.response_version);
  EXPECT_EQ(ctime, entity.creation_time);
  EXPECT_EQ(ctime, entity.modification_time);
  EXPECT_EQ(kTag1, entity.non_unique_name);
  EXPECT_FALSE(entity.is_deleted());
  EXPECT_EQ(kTag1, entity.specifics.preference().name());
  EXPECT_EQ(kValue1, entity.specifics.preference().value());
}

TEST_F(UssMigratorTest, MigrateMultiple) {
  CreateTypeRoot();
  SetProgressMarkerToken(kToken1);
  InsertEntity(kTag1, kValue1);
  InsertEntity(kTag2, kValue2);
  InsertEntity(kTag3, kValue3);

  ASSERT_TRUE(MigrateDirectoryData(kModelType, user_share(), worker()));

  EXPECT_EQ(1U, processor()->GetNumUpdateResponses());
  EXPECT_EQ(3U, processor()->GetNthUpdateResponse(0).size());

  UpdateResponseDataList updates = processor()->GetNthUpdateResponse(0);
  EXPECT_EQ(kTag1, updates.at(0).entity.value().specifics.preference().name());
  EXPECT_EQ(kTag2, updates.at(1).entity.value().specifics.preference().name());
  EXPECT_EQ(kTag3, updates.at(2).entity.value().specifics.preference().name());
}

TEST_F(UssMigratorTest, MigrateMultipleBatches) {
  CreateTypeRoot();
  SetProgressMarkerToken(kToken1);
  InsertEntity(kTag1, kValue1);
  InsertEntity(kTag2, kValue2);
  InsertEntity(kTag3, kValue3);

  ASSERT_TRUE(
      MigrateDirectoryDataWithBatchSize(kModelType, user_share(), worker(), 2));

  EXPECT_EQ(1U, processor()->GetNumUpdateResponses());
  EXPECT_EQ(3U, processor()->GetNthUpdateResponse(0).size());

  UpdateResponseDataList updates = processor()->GetNthUpdateResponse(0);
  EXPECT_EQ(kTag1, updates.at(0).entity.value().specifics.preference().name());
  EXPECT_EQ(kTag2, updates.at(1).entity.value().specifics.preference().name());
  EXPECT_EQ(kTag3, updates.at(2).entity.value().specifics.preference().name());
}

TEST_F(UssMigratorTest, MigrateIgnoresTombstone) {
  CreateTypeRoot();
  SetProgressMarkerToken(kToken1);
  DeleteEntity(kTag1);

  ASSERT_TRUE(MigrateDirectoryData(kModelType, user_share(), worker()));

  EXPECT_EQ(0, nudge_handler()->GetNumInitialDownloadNudges());
  EXPECT_EQ(1U, processor()->GetNumUpdateResponses());
  EXPECT_EQ(0U, processor()->GetNthUpdateResponse(0).size());
}

TEST_F(UssMigratorTest, MigrateZero) {
  CreateTypeRoot();
  SetProgressMarkerToken(kToken1);

  ASSERT_TRUE(MigrateDirectoryData(kModelType, user_share(), worker()));

  EXPECT_EQ(0, nudge_handler()->GetNumInitialDownloadNudges());
  EXPECT_EQ(1U, processor()->GetNumUpdateResponses());
  EXPECT_EQ(0U, processor()->GetNthUpdateResponse(0).size());
}

TEST_F(UssMigratorTest, MissingTypeRoot) {
  EXPECT_EQ(0, nudge_handler()->GetNumInitialDownloadNudges());
  ASSERT_FALSE(MigrateDirectoryData(kModelType, user_share(), worker()));
  EXPECT_EQ(1, nudge_handler()->GetNumInitialDownloadNudges());
  EXPECT_EQ(0U, processor()->GetNumUpdateResponses());
}

}  // namespace syncer
