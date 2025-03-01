// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/subprocess_metrics_provider.h"

#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/histogram_base.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/metrics/persistent_memory_allocator.h"
#include "components/metrics/metrics_service.h"
#include "content/public/browser/browser_child_process_host.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"

namespace {

// This is used by tests that don't have an easy way to access the global
// instance of this class.
SubprocessMetricsProvider* g_subprocess_metrics_provider_for_testing;

}  // namespace

SubprocessMetricsProvider::SubprocessMetricsProvider()
    : scoped_observer_(this), weak_ptr_factory_(this) {
  base::StatisticsRecorder::RegisterHistogramProvider(
      weak_ptr_factory_.GetWeakPtr());
  content::BrowserChildProcessObserver::Add(this);
  registrar_.Add(this, content::NOTIFICATION_RENDERER_PROCESS_CREATED,
                 content::NotificationService::AllBrowserContextsAndSources());
  g_subprocess_metrics_provider_for_testing = this;
}

SubprocessMetricsProvider::~SubprocessMetricsProvider() {
  // Safe even if this object has never been added as an observer.
  content::BrowserChildProcessObserver::Remove(this);
  g_subprocess_metrics_provider_for_testing = nullptr;
}

// static
void SubprocessMetricsProvider::MergeHistogramDeltasForTesting() {
  DCHECK(g_subprocess_metrics_provider_for_testing);
  g_subprocess_metrics_provider_for_testing->MergeHistogramDeltas();
}

void SubprocessMetricsProvider::RegisterSubprocessAllocator(
    int id,
    std::unique_ptr<base::PersistentHistogramAllocator> allocator) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!allocators_by_id_.Lookup(id));

  // Stop now if this was called without an allocator, typically because
  // GetSubprocessHistogramAllocatorOnIOThread exited early and returned
  // null.
  if (!allocator)
    return;

  // Map is "MapOwnPointer" so transfer ownership to it.
  allocators_by_id_.AddWithID(std::move(allocator), id);
}

void SubprocessMetricsProvider::DeregisterSubprocessAllocator(int id) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (!allocators_by_id_.Lookup(id))
    return;

  // Extract the matching allocator from the list of active ones. It will
  // be automatically released when this method exits.
  std::unique_ptr<base::PersistentHistogramAllocator> allocator(
      allocators_by_id_.Replace(id, nullptr));
  allocators_by_id_.Remove(id);
  DCHECK(allocator);

  // Merge the last deltas from the allocator before it is released.
  MergeHistogramDeltasFromAllocator(id, allocator.get());
}

void SubprocessMetricsProvider::MergeHistogramDeltasFromAllocator(
    int id,
    base::PersistentHistogramAllocator* allocator) {
  DCHECK(allocator);

  // TODO(asvitkine): Remove this after crbug/736675.
  base::StatisticsRecorder::ValidateAllHistograms();

  int histogram_count = 0;
  base::PersistentHistogramAllocator::Iterator hist_iter(allocator);
  while (true) {
    std::unique_ptr<base::HistogramBase> histogram = hist_iter.GetNext();
    if (!histogram)
      break;
    allocator->MergeHistogramDeltaToStatisticsRecorder(histogram.get());
    ++histogram_count;
  }

  // TODO(asvitkine): Remove this after crbug/736675.
  base::StatisticsRecorder::ValidateAllHistograms();

  DVLOG(1) << "Reported " << histogram_count << " histograms from subprocess #"
           << id;
}

void SubprocessMetricsProvider::MergeHistogramDeltas() {
  DCHECK(thread_checker_.CalledOnValidThread());

  for (AllocatorByIdMap::iterator iter(&allocators_by_id_); !iter.IsAtEnd();
       iter.Advance()) {
    MergeHistogramDeltasFromAllocator(iter.GetCurrentKey(),
                                      iter.GetCurrentValue());
  }

  UMA_HISTOGRAM_COUNTS_100(
      "UMA.SubprocessMetricsProvider.SubprocessCount",
      allocators_by_id_.size());
}

void SubprocessMetricsProvider::BrowserChildProcessHostConnected(
    const content::ChildProcessData& data) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // It's necessary to access the BrowserChildProcessHost object that is
  // managing the child in order to extract the metrics memory from it.
  // Unfortunately, the required lookup can only be performed on the IO
  // thread so do the necessary dance.
  content::BrowserThread::PostTaskAndReplyWithResult(
      content::BrowserThread::IO, FROM_HERE,
      base::Bind(&SubprocessMetricsProvider::
                     GetSubprocessHistogramAllocatorOnIOThread,
                 data.id),
      base::Bind(&SubprocessMetricsProvider::
                     RegisterSubprocessAllocator,
                 weak_ptr_factory_.GetWeakPtr(), data.id));
}

void SubprocessMetricsProvider::BrowserChildProcessHostDisconnected(
    const content::ChildProcessData& data) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DeregisterSubprocessAllocator(data.id);
}

void SubprocessMetricsProvider::BrowserChildProcessCrashed(
    const content::ChildProcessData& data,
    int exit_code) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DeregisterSubprocessAllocator(data.id);
}

void SubprocessMetricsProvider::BrowserChildProcessKilled(
    const content::ChildProcessData& data,
    int exit_code) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DeregisterSubprocessAllocator(data.id);
}

void SubprocessMetricsProvider::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK_EQ(content::NOTIFICATION_RENDERER_PROCESS_CREATED, type);

  content::RenderProcessHost* host =
      content::Source<content::RenderProcessHost>(source).ptr();

  // Sometimes, the same host will cause multiple notifications in tests so
  // could possibly do the same in a release build.
  if (!scoped_observer_.IsObserving(host))
    scoped_observer_.Add(host);
}

void SubprocessMetricsProvider::RenderProcessReady(
    content::RenderProcessHost* host) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // If the render-process-host passed a persistent-memory-allocator to the
  // renderer process, extract it and register it here.
  std::unique_ptr<base::SharedPersistentMemoryAllocator> allocator =
      host->TakeMetricsAllocator();
  if (allocator) {
    RegisterSubprocessAllocator(
        host->GetID(), base::MakeUnique<base::PersistentHistogramAllocator>(
                           std::move(allocator)));
  }
}

void SubprocessMetricsProvider::RenderProcessExited(
    content::RenderProcessHost* host,
    base::TerminationStatus status,
    int exit_code) {
  DCHECK(thread_checker_.CalledOnValidThread());

  DeregisterSubprocessAllocator(host->GetID());
}

void SubprocessMetricsProvider::RenderProcessHostDestroyed(
    content::RenderProcessHost* host) {
  DCHECK(thread_checker_.CalledOnValidThread());

  // It's possible for a Renderer to terminate without RenderProcessExited
  // (above) being called so it's necessary to de-register also upon the
  // destruction of the host. If both get called, no harm is done.

  DeregisterSubprocessAllocator(host->GetID());
  scoped_observer_.Remove(host);
}

// static
std::unique_ptr<base::PersistentHistogramAllocator>
SubprocessMetricsProvider::GetSubprocessHistogramAllocatorOnIOThread(int id) {
  // See if the new process has a memory allocator and take control of it if so.
  // This call can only be made on the browser's IO thread.
  content::BrowserChildProcessHost* host =
      content::BrowserChildProcessHost::FromID(id);
  if (!host)
    return nullptr;

  std::unique_ptr<base::SharedPersistentMemoryAllocator> allocator =
      host->TakeMetricsAllocator();
  if (!allocator)
    return nullptr;

  return base::MakeUnique<base::PersistentHistogramAllocator>(
      std::move(allocator));
}
