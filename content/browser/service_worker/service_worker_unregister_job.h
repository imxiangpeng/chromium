// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_UNREGISTER_JOB_H_
#define CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_UNREGISTER_JOB_H_

#include <stdint.h>

#include <vector>

#include "base/callback_forward.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/service_worker/service_worker_register_job_base.h"
#include "content/common/service_worker/service_worker_status_code.h"
#include "url/gurl.h"

namespace content {

class ServiceWorkerContextCore;
class ServiceWorkerRegistration;

// Handles the unregistration of a Service Worker.
//
// The unregistration process is primarily cleanup, removing everything that was
// created during the registration process, including the
// ServiceWorkerRegistration itself.
class ServiceWorkerUnregisterJob : public ServiceWorkerRegisterJobBase {
 public:
  typedef base::Callback<void(int64_t registration_id,
                              ServiceWorkerStatusCode status)>
      UnregistrationCallback;

  ServiceWorkerUnregisterJob(base::WeakPtr<ServiceWorkerContextCore> context,
                             const GURL& pattern);
  ~ServiceWorkerUnregisterJob() override;

  // Registers a callback to be called when the job completes (whether
  // successfully or not). Multiple callbacks may be registered.
  void AddCallback(const UnregistrationCallback& callback);

  // ServiceWorkerRegisterJobBase implementation:
  void Start() override;
  void Abort() override;
  bool Equals(ServiceWorkerRegisterJobBase* job) const override;
  base::TimeTicks StartTime() const override;
  RegistrationJobType GetType() const override;

 private:
  void OnRegistrationFound(
      ServiceWorkerStatusCode status,
      scoped_refptr<ServiceWorkerRegistration> registration);
  void Complete(int64_t registration_id, ServiceWorkerStatusCode status);
  void CompleteInternal(int64_t registration_id,
                        ServiceWorkerStatusCode status);
  void ResolvePromise(int64_t registration_id, ServiceWorkerStatusCode status);

  base::WeakPtr<ServiceWorkerContextCore> context_;
  base::TimeTicks start_time_;
  const GURL pattern_;
  std::vector<UnregistrationCallback> callbacks_;
  bool is_promise_resolved_;
  base::WeakPtrFactory<ServiceWorkerUnregisterJob> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ServiceWorkerUnregisterJob);
};
}  // namespace content

#endif  // CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_UNREGISTER_JOB_H_
