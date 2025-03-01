// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/loader/modulescript/ModuleScriptLoader.h"

#include "bindings/core/v8/V8BindingForCore.h"
#include "bindings/core/v8/WorkerOrWorkletScriptController.h"
#include "core/dom/Document.h"
#include "core/dom/Modulator.h"
#include "core/dom/ModuleScript.h"
#include "core/dom/TaskRunnerHelper.h"
#include "core/loader/modulescript/ModuleScriptFetchRequest.h"
#include "core/loader/modulescript/ModuleScriptLoaderClient.h"
#include "core/loader/modulescript/ModuleScriptLoaderRegistry.h"
#include "core/testing/DummyModulator.h"
#include "core/testing/DummyPageHolder.h"
#include "core/workers/MainThreadWorkletGlobalScope.h"
#include "platform/heap/Handle.h"
#include "platform/loader/fetch/ResourceFetcher.h"
#include "platform/loader/testing/FetchTestingPlatformSupport.h"
#include "platform/loader/testing/MockFetchContext.h"
#include "platform/testing/URLTestHelpers.h"
#include "platform/testing/UnitTestHelpers.h"
#include "public/platform/Platform.h"
#include "public/platform/WebURLLoaderMockFactory.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

namespace {

class TestModuleScriptLoaderClient final
    : public GarbageCollectedFinalized<TestModuleScriptLoaderClient>,
      public ModuleScriptLoaderClient {
  USING_GARBAGE_COLLECTED_MIXIN(TestModuleScriptLoaderClient);

 public:
  TestModuleScriptLoaderClient() = default;
  ~TestModuleScriptLoaderClient() override {}

  DEFINE_INLINE_TRACE() { visitor->Trace(module_script_); }

  void NotifyNewSingleModuleFinished(ModuleScript* module_script) override {
    was_notify_finished_ = true;
    module_script_ = module_script;
  }

  bool WasNotifyFinished() const { return was_notify_finished_; }
  ModuleScript* GetModuleScript() { return module_script_; }

 private:
  bool was_notify_finished_ = false;
  Member<ModuleScript> module_script_;
};

class ModuleScriptLoaderTestModulator final : public DummyModulator {
 public:
  ModuleScriptLoaderTestModulator(RefPtr<ScriptState> script_state,
                                  RefPtr<SecurityOrigin> security_origin)
      : script_state_(std::move(script_state)),
        security_origin_(std::move(security_origin)) {}

  ~ModuleScriptLoaderTestModulator() override {}

  SecurityOrigin* GetSecurityOrigin() override {
    return security_origin_.Get();
  }

  ScriptState* GetScriptState() override { return script_state_.Get(); }

  ScriptModule CompileModule(const String& script,
                             const String& url_str,
                             AccessControlStatus access_control_status,
                             const TextPosition& position,
                             ExceptionState& exception_state) override {
    ScriptState::Scope scope(script_state_.Get());
    return ScriptModule::Compile(
        script_state_->GetIsolate(), "export default 'foo';", "",
        access_control_status, TextPosition::MinimumPosition(),
        exception_state);
  }

  void SetModuleRequests(const Vector<String>& requests) {
    requests_.clear();
    for (const String& request : requests) {
      requests_.emplace_back(request, TextPosition::MinimumPosition());
    }
  }
  Vector<ModuleRequest> ModuleRequestsFromScriptModule(ScriptModule) override {
    return requests_;
  }
  ScriptModuleState GetRecordStatus(ScriptModule) override {
    return ScriptModuleState::kUninstantiated;
  }

  DECLARE_TRACE();

 private:
  RefPtr<ScriptState> script_state_;
  RefPtr<SecurityOrigin> security_origin_;
  Vector<ModuleRequest> requests_;
};

DEFINE_TRACE(ModuleScriptLoaderTestModulator) {
  DummyModulator::Trace(visitor);
}

}  // namespace

class ModuleScriptLoaderTest : public ::testing::Test {
  DISALLOW_COPY_AND_ASSIGN(ModuleScriptLoaderTest);

 public:
  ModuleScriptLoaderTest() = default;
  void SetUp() override;

  void InitializeForDocument();
  void InitializeForWorklet();

  void TestFetchDataURL(TestModuleScriptLoaderClient*);
  void TestInvalidSpecifier(TestModuleScriptLoaderClient*);
  void TestFetchInvalidURL(TestModuleScriptLoaderClient*);
  void TestFetchURL(TestModuleScriptLoaderClient*);

  LocalFrame& GetFrame() { return dummy_page_holder_->GetFrame(); }
  Document& GetDocument() { return dummy_page_holder_->GetDocument(); }
  ResourceFetcher* Fetcher() { return fetcher_.Get(); }
  ModuleScriptLoaderTestModulator* GetModulator() { return modulator_.Get(); }

 protected:
  ScopedTestingPlatformSupport<FetchTestingPlatformSupport> platform_;
  std::unique_ptr<DummyPageHolder> dummy_page_holder_;
  Persistent<ResourceFetcher> fetcher_;
  Persistent<ModuleScriptLoaderTestModulator> modulator_;
  Persistent<MainThreadWorkletGlobalScope> global_scope_;
};

void ModuleScriptLoaderTest::SetUp() {
  platform_->AdvanceClockSeconds(1.);  // For non-zero DocumentParserTimings
  dummy_page_holder_ = DummyPageHolder::Create(IntSize(500, 500));
  GetDocument().SetURL(KURL(NullURL(), "https://example.test"));
  auto* context =
      MockFetchContext::Create(MockFetchContext::kShouldLoadNewResource);
  fetcher_ = ResourceFetcher::Create(context);
}

void ModuleScriptLoaderTest::InitializeForDocument() {
  modulator_ = new ModuleScriptLoaderTestModulator(
      ToScriptStateForMainWorld(&GetFrame()),
      GetDocument().GetSecurityOrigin());
}

void ModuleScriptLoaderTest::InitializeForWorklet() {
  global_scope_ = new MainThreadWorkletGlobalScope(
      &GetFrame(), KURL(NullURL(), "https://example.test/worklet.js"),
      "fake user agent", GetDocument().GetSecurityOrigin(),
      ToIsolate(&GetDocument()));
  global_scope_->ScriptController()->InitializeContextIfNeeded("Dummy Context");
  global_scope_->SetModuleResponsesMapProxyForTesting(
      WorkletModuleResponsesMapProxy::Create(
          new WorkletModuleResponsesMap,
          TaskRunnerHelper::Get(TaskType::kUnspecedLoading, &GetDocument()),
          TaskRunnerHelper::Get(TaskType::kUnspecedLoading, global_scope_)));
  modulator_ = new ModuleScriptLoaderTestModulator(
      global_scope_->ScriptController()->GetScriptState(),
      GetDocument().GetSecurityOrigin());
}

void ModuleScriptLoaderTest::TestFetchDataURL(
    TestModuleScriptLoaderClient* client) {
  ModuleScriptLoaderRegistry* registry = ModuleScriptLoaderRegistry::Create();
  KURL url(NullURL(), "data:text/javascript,export default 'grapes';");
  ModuleScriptFetchRequest module_request(
      url, String(), kParserInserted, WebURLRequest::kFetchCredentialsModeOmit);
  registry->Fetch(module_request, ModuleGraphLevel::kTopLevelModuleFetch,
                  GetModulator(), Fetcher(), client);
}

TEST_F(ModuleScriptLoaderTest, FetchDataURL) {
  InitializeForDocument();
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  TestFetchDataURL(client);

  EXPECT_TRUE(client->WasNotifyFinished())
      << "ModuleScriptLoader should finish synchronously.";
  ASSERT_TRUE(client->GetModuleScript());
  EXPECT_FALSE(client->GetModuleScript()->IsErrored());
}

TEST_F(ModuleScriptLoaderTest, FetchDataURL_OnWorklet) {
  InitializeForWorklet();
  TestModuleScriptLoaderClient* client1 = new TestModuleScriptLoaderClient;
  TestFetchDataURL(client1);

  EXPECT_FALSE(client1->WasNotifyFinished())
      << "ModuleScriptLoader should finish asynchronously.";
  platform_->RunUntilIdle();

  EXPECT_TRUE(client1->WasNotifyFinished());
  ASSERT_TRUE(client1->GetModuleScript());
  EXPECT_FALSE(client1->GetModuleScript()->IsErrored());

  // Try to fetch the same URL again in order to verify the case where
  // WorkletModuleResponsesMap serves a cache.
  TestModuleScriptLoaderClient* client2 = new TestModuleScriptLoaderClient;
  TestFetchDataURL(client2);

  EXPECT_FALSE(client2->WasNotifyFinished())
      << "ModuleScriptLoader should finish asynchronously.";
  platform_->RunUntilIdle();

  EXPECT_TRUE(client2->WasNotifyFinished());
  ASSERT_TRUE(client2->GetModuleScript());
  EXPECT_FALSE(client2->GetModuleScript()->IsErrored());
}

void ModuleScriptLoaderTest::TestInvalidSpecifier(
    TestModuleScriptLoaderClient* client) {
  ModuleScriptLoaderRegistry* registry = ModuleScriptLoaderRegistry::Create();
  KURL url(NullURL(),
           "data:text/javascript,import 'invalid';export default 'grapes';");
  ModuleScriptFetchRequest module_request(
      url, String(), kParserInserted, WebURLRequest::kFetchCredentialsModeOmit);
  GetModulator()->SetModuleRequests({"invalid"});
  registry->Fetch(module_request, ModuleGraphLevel::kTopLevelModuleFetch,
                  GetModulator(), Fetcher(), client);
}

TEST_F(ModuleScriptLoaderTest, InvalidSpecifier) {
  InitializeForDocument();
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  TestInvalidSpecifier(client);

  EXPECT_TRUE(client->WasNotifyFinished())
      << "ModuleScriptLoader should finish synchronously.";
  ASSERT_TRUE(client->GetModuleScript());
  EXPECT_TRUE(client->GetModuleScript()->IsErrored());
}

TEST_F(ModuleScriptLoaderTest, InvalidSpecifier_OnWorklet) {
  InitializeForWorklet();
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  TestInvalidSpecifier(client);

  EXPECT_FALSE(client->WasNotifyFinished())
      << "ModuleScriptLoader should finish asynchronously.";
  platform_->RunUntilIdle();

  EXPECT_TRUE(client->WasNotifyFinished());
  ASSERT_TRUE(client->GetModuleScript());
  EXPECT_TRUE(client->GetModuleScript()->IsErrored());
}

void ModuleScriptLoaderTest::TestFetchInvalidURL(
    TestModuleScriptLoaderClient* client) {
  ModuleScriptLoaderRegistry* registry = ModuleScriptLoaderRegistry::Create();
  KURL url;
  EXPECT_FALSE(url.IsValid());
  ModuleScriptFetchRequest module_request(
      url, String(), kParserInserted, WebURLRequest::kFetchCredentialsModeOmit);
  registry->Fetch(module_request, ModuleGraphLevel::kTopLevelModuleFetch,
                  GetModulator(), Fetcher(), client);
}

TEST_F(ModuleScriptLoaderTest, FetchInvalidURL) {
  InitializeForDocument();
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  TestFetchInvalidURL(client);

  EXPECT_TRUE(client->WasNotifyFinished())
      << "ModuleScriptLoader should finish synchronously.";
  EXPECT_FALSE(client->GetModuleScript());
}

TEST_F(ModuleScriptLoaderTest, FetchInvalidURL_OnWorklet) {
  InitializeForWorklet();
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  TestFetchInvalidURL(client);

  EXPECT_FALSE(client->WasNotifyFinished())
      << "ModuleScriptLoader should finish asynchronously.";
  platform_->RunUntilIdle();

  EXPECT_TRUE(client->WasNotifyFinished());
  EXPECT_FALSE(client->GetModuleScript());
}

void ModuleScriptLoaderTest::TestFetchURL(
    TestModuleScriptLoaderClient* client) {
  KURL url(kParsedURLString, "http://127.0.0.1:8000/module.js");
  URLTestHelpers::RegisterMockedURLLoad(
      url, testing::CoreTestDataPath("module.js"), "text/javascript");

  ModuleScriptLoaderRegistry* registry = ModuleScriptLoaderRegistry::Create();
  ModuleScriptFetchRequest module_request(
      url, String(), kParserInserted, WebURLRequest::kFetchCredentialsModeOmit);
  registry->Fetch(module_request, ModuleGraphLevel::kTopLevelModuleFetch,
                  GetModulator(), Fetcher(), client);
}

TEST_F(ModuleScriptLoaderTest, FetchURL) {
  InitializeForDocument();
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  TestFetchURL(client);

  EXPECT_FALSE(client->WasNotifyFinished())
      << "ModuleScriptLoader unexpectedly finished synchronously.";
  platform_->GetURLLoaderMockFactory()->ServeAsynchronousRequests();

  EXPECT_TRUE(client->WasNotifyFinished());
  EXPECT_FALSE(client->GetModuleScript());
}

TEST_F(ModuleScriptLoaderTest, FetchURL_OnWorklet) {
  InitializeForWorklet();
  TestModuleScriptLoaderClient* client = new TestModuleScriptLoaderClient;
  TestFetchURL(client);

  EXPECT_FALSE(client->WasNotifyFinished())
      << "ModuleScriptLoader unexpectedly finished synchronously.";

  // Advance until WorkletModuleScriptFetcher finishes looking up a cache in
  // WorkletModuleResponsesMap and issues a fetch request so that
  // ServeAsynchronousRequests() can serve for the pending request.
  platform_->RunUntilIdle();
  platform_->GetURLLoaderMockFactory()->ServeAsynchronousRequests();

  EXPECT_TRUE(client->WasNotifyFinished());
  EXPECT_FALSE(client->GetModuleScript());
}

}  // namespace blink
