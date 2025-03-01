// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ukm/ukm_service.h"

#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/rand_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "components/metrics/metrics_log.h"
#include "components/metrics/metrics_service_client.h"
#include "components/metrics/proto/ukm/report.pb.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "components/ukm/persisted_logs_metrics_impl.h"
#include "components/ukm/ukm_pref_names.h"
#include "components/ukm/ukm_rotation_scheduler.h"

namespace ukm {

namespace {

// The delay, in seconds, after starting recording before doing expensive
// initialization work.
constexpr int kInitializationDelaySeconds = 5;

// True if we should record session ids in the UKM Report proto.
bool ShouldRecordSessionId() {
  return base::GetFieldTrialParamByFeatureAsBool(kUkmFeature, "RecordSessionId",
                                                 false);
}

// Generates a new client id and stores it in prefs.
uint64_t GenerateClientId(PrefService* pref_service) {
  uint64_t client_id = 0;
  while (!client_id)
    client_id = base::RandUint64();
  pref_service->SetInt64(prefs::kUkmClientId, client_id);

  // Also reset the session id counter.
  pref_service->SetInteger(prefs::kUkmSessionId, 0);
  return client_id;
}

uint64_t LoadOrGenerateClientId(PrefService* pref_service) {
  uint64_t client_id = pref_service->GetInt64(prefs::kUkmClientId);
  if (!client_id)
    client_id = GenerateClientId(pref_service);
  return client_id;
}

int32_t LoadSessionId(PrefService* pref_service) {
  int32_t session_id = pref_service->GetInteger(prefs::kUkmSessionId);
  ++session_id;  // increment session id, once per session
  pref_service->SetInteger(prefs::kUkmSessionId, session_id);
  return session_id;
}

}  // namespace

UkmService::UkmService(PrefService* pref_service,
                       metrics::MetricsServiceClient* client)
    : pref_service_(pref_service),
      client_id_(0),
      session_id_(0),
      client_(client),
      reporting_service_(client, pref_service),
      initialize_started_(false),
      initialize_complete_(false),
      self_ptr_factory_(this) {
  DCHECK(pref_service_);
  DCHECK(client_);
  DVLOG(1) << "UkmService::Constructor";

  reporting_service_.Initialize();

  base::Closure rotate_callback =
      base::Bind(&UkmService::RotateLog, self_ptr_factory_.GetWeakPtr());
  // MetricsServiceClient outlives UkmService, and
  // MetricsReportingScheduler is tied to the lifetime of |this|.
  const base::Callback<base::TimeDelta(void)>& get_upload_interval_callback =
      base::Bind(&metrics::MetricsServiceClient::GetStandardUploadInterval,
                 base::Unretained(client_));
  scheduler_.reset(new ukm::UkmRotationScheduler(rotate_callback,
                                                 get_upload_interval_callback));

  metrics_providers_.Init();

  StoreWhitelistedEntries();

  UkmRecorder::Set(this);
}

UkmService::~UkmService() {
  DisableReporting();
  UkmRecorder::Set(nullptr);
}

void UkmService::Initialize() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!initialize_started_);
  DVLOG(1) << "UkmService::Initialize";
  initialize_started_ = true;

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&UkmService::StartInitTask, self_ptr_factory_.GetWeakPtr()),
      base::TimeDelta::FromSeconds(kInitializationDelaySeconds));
}

void UkmService::EnableReporting() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "UkmService::EnableReporting";
  if (reporting_service_.reporting_active())
    return;

  metrics_providers_.OnRecordingEnabled();

  if (!initialize_started_)
    Initialize();
  scheduler_->Start();
  reporting_service_.EnableReporting();
}

void UkmService::DisableReporting() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "UkmService::DisableReporting";

  reporting_service_.DisableReporting();

  metrics_providers_.OnRecordingDisabled();

  scheduler_->Stop();
  Flush();
}

#if defined(OS_ANDROID) || defined(OS_IOS)
void UkmService::OnAppEnterForeground() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "UkmService::OnAppEnterForeground";

  // If initialize_started_ is false, UKM has not yet been started, so bail. The
  // scheduler will instead be started via EnableReporting().
  if (!initialize_started_)
    return;

  scheduler_->Start();
}

void UkmService::OnAppEnterBackground() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "UkmService::OnAppEnterBackground";

  if (!initialize_started_)
    return;

  scheduler_->Stop();

  // Give providers a chance to persist ukm data as part of being backgrounded.
  metrics_providers_.OnAppEnterBackground();

  Flush();
}
#endif

void UkmService::Flush() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (initialize_complete_)
    BuildAndStoreLog();
  reporting_service_.ukm_log_store()->PersistUnsentLogs();
}

void UkmService::Purge() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "UkmService::Purge";
  reporting_service_.ukm_log_store()->Purge();
  UkmRecorderImpl::Purge();
}

// TODO(bmcquade): rename this to something more generic, like
// ResetClientState. Consider resetting all prefs here.
void UkmService::ResetClientId() {
  client_id_ = GenerateClientId(pref_service_);
  session_id_ = LoadSessionId(pref_service_);
}

void UkmService::RegisterMetricsProvider(
    std::unique_ptr<metrics::MetricsProvider> provider) {
  metrics_providers_.RegisterMetricsProvider(std::move(provider));
}

// static
void UkmService::RegisterPrefs(PrefRegistrySimple* registry) {
  registry->RegisterInt64Pref(prefs::kUkmClientId, 0);
  registry->RegisterIntegerPref(prefs::kUkmSessionId, 0);
  UkmReportingService::RegisterPrefs(registry);
}

void UkmService::StartInitTask() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "UkmService::StartInitTask";
  client_id_ = LoadOrGenerateClientId(pref_service_);
  session_id_ = LoadSessionId(pref_service_);

  metrics_providers_.AsyncInit(base::Bind(&UkmService::FinishedInitTask,
                                          self_ptr_factory_.GetWeakPtr()));
}

void UkmService::FinishedInitTask() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "UkmService::FinishedInitTask";
  initialize_complete_ = true;
  scheduler_->InitTaskComplete();
}

void UkmService::RotateLog() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "UkmService::RotateLog";
  if (!reporting_service_.ukm_log_store()->has_unsent_logs())
    BuildAndStoreLog();
  reporting_service_.Start();
}

void UkmService::BuildAndStoreLog() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DVLOG(1) << "UkmService::BuildAndStoreLog";

  // Suppress generating a log if we have no new data to include.
  // TODO(zhenw): add a histogram here to debug if this case is hitting a lot.
  if (sources().empty() && entries().empty())
    return;

  Report report;
  report.set_client_id(client_id_);
  if (ShouldRecordSessionId())
    report.set_session_id(session_id_);

  StoreRecordingsInReport(&report);

  metrics::MetricsLog::RecordCoreSystemProfile(client_,
                                               report.mutable_system_profile());

  metrics_providers_.ProvideSystemProfileMetrics(
      report.mutable_system_profile());

  std::string serialized_log;
  report.SerializeToString(&serialized_log);
  reporting_service_.ukm_log_store()->StoreLog(serialized_log);
}

}  // namespace ukm
