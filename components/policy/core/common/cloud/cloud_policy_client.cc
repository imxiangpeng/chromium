// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/cloud/cloud_policy_client.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/guid.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "components/policy/core/common/cloud/device_management_service.h"
#include "components/policy/core/common/cloud/signing_service.h"
#include "google_apis/gaia/gaia_constants.h"
#include "google_apis/gaia/gaia_urls.h"
#include "net/url_request/url_request_context_getter.h"

namespace em = enterprise_management;

namespace policy {

namespace {

// Translates the DeviceRegisterResponse::DeviceMode |mode| to the enum used
// internally to represent different device modes.
DeviceMode TranslateProtobufDeviceMode(
    em::DeviceRegisterResponse::DeviceMode mode) {
  switch (mode) {
    case em::DeviceRegisterResponse::ENTERPRISE:
      return DEVICE_MODE_ENTERPRISE;
    case em::DeviceRegisterResponse::RETAIL:
      return DEVICE_MODE_LEGACY_RETAIL_MODE;
    case em::DeviceRegisterResponse::CHROME_AD:
      return DEVICE_MODE_ENTERPRISE_AD;
  }
  LOG(ERROR) << "Unknown enrollment mode in registration response: " << mode;
  return DEVICE_MODE_NOT_SET;
}

bool IsChromePolicy(const std::string& type) {
  return type == dm_protocol::kChromeDevicePolicyType ||
         type == dm_protocol::kChromeUserPolicyType;
}

LicenseType TranslateLicenseType(em::LicenseType type) {
  switch (type.license_type()) {
    case em::LicenseType::UNDEFINED:
      LOG(ERROR) << "Unknown License type: " << type.license_type();
      return LicenseType::UNKNOWN;
    case em::LicenseType::CDM_PERPETUAL:
      return LicenseType::PERPETUAL;
    case em::LicenseType::CDM_ANNUAL:
      return LicenseType::ANNUAL;
    case em::LicenseType::KIOSK:
      return LicenseType::KIOSK;
  }
  NOTREACHED();
  return LicenseType::UNKNOWN;
}

void ExtractLicenseMap(const em::CheckDeviceLicenseResponse& license_response,
                       CloudPolicyClient::LicenseMap& licenses) {
  for (int i = 0; i < license_response.license_availability_size(); i++) {
    const em::LicenseAvailability& license =
        license_response.license_availability(i);
    if (!license.has_license_type() || !license.has_available_licenses())
      continue;
    auto license_type = TranslateLicenseType(license.license_type());
    if (license_type == LicenseType::UNKNOWN)
      continue;
    bool duplicate =
        licenses
            .insert(std::make_pair(license_type, license.available_licenses()))
            .second;
    if (duplicate) {
      LOG(WARNING) << "Duplicate license type in response :"
                   << static_cast<int>(license_type);
    }
  }
}

}  // namespace

CloudPolicyClient::Observer::~Observer() {}

void CloudPolicyClient::Observer::OnRobotAuthCodesFetched(
    CloudPolicyClient* client) {}

CloudPolicyClient::CloudPolicyClient(
    const std::string& machine_id,
    const std::string& machine_model,
    DeviceManagementService* service,
    scoped_refptr<net::URLRequestContextGetter> request_context,
    SigningService* signing_service)
    : machine_id_(machine_id),
      machine_model_(machine_model),
      service_(service),                  // Can be null for unit tests.
      signing_service_(signing_service),
      request_context_(request_context),
      weak_ptr_factory_(this) {
}

CloudPolicyClient::~CloudPolicyClient() {
}

void CloudPolicyClient::SetupRegistration(const std::string& dm_token,
                                          const std::string& client_id) {
  DCHECK(!dm_token.empty());
  DCHECK(!client_id.empty());
  DCHECK(!is_registered());

  dm_token_ = dm_token;
  client_id_ = client_id;
  request_jobs_.clear();
  policy_fetch_request_job_.reset();
  responses_.clear();

  NotifyRegistrationStateChanged();
}

// Sets the client ID or generate a new one. A new one is intentionally
// generated on each new registration request in order to preserve privacy.
// Reusing IDs would mean the server could track clients by their registration
// attempts.
void CloudPolicyClient::SetClientId(const std::string& client_id) {
  client_id_ = client_id.empty() ?  base::GenerateGUID() : client_id;
}

void CloudPolicyClient::Register(em::DeviceRegisterRequest::Type type,
                                 em::DeviceRegisterRequest::Flavor flavor,
                                 const std::string& auth_token,
                                 const std::string& client_id,
                                 const std::string& requisition,
                                 const std::string& current_state_key) {
  DCHECK(service_);
  DCHECK(!auth_token.empty());
  DCHECK(!is_registered());

  SetClientId(client_id);

  policy_fetch_request_job_.reset(
      service_->CreateJob(DeviceManagementRequestJob::TYPE_REGISTRATION,
                          GetRequestContext()));
  policy_fetch_request_job_->SetOAuthToken(auth_token);
  policy_fetch_request_job_->SetClientID(client_id_);

  em::DeviceRegisterRequest* request =
      policy_fetch_request_job_->GetRequest()->mutable_register_request();
  if (!client_id.empty())
    request->set_reregister(true);
  request->set_type(type);
  if (!machine_id_.empty())
    request->set_machine_id(machine_id_);
  if (!machine_model_.empty())
    request->set_machine_model(machine_model_);
  if (!requisition.empty())
    request->set_requisition(requisition);
  if (!current_state_key.empty())
    request->set_server_backed_state_key(current_state_key);
  request->set_flavor(flavor);

  policy_fetch_request_job_->SetRetryCallback(
      base::Bind(&CloudPolicyClient::OnRetryRegister,
                 weak_ptr_factory_.GetWeakPtr()));

  policy_fetch_request_job_->Start(
      base::Bind(&CloudPolicyClient::OnRegisterCompleted,
                 weak_ptr_factory_.GetWeakPtr()));
}

void CloudPolicyClient::RegisterWithCertificate(
    em::DeviceRegisterRequest::Type type,
    em::DeviceRegisterRequest::Flavor flavor,
    const std::string& pem_certificate_chain,
    const std::string& client_id,
    const std::string& requisition,
    const std::string& current_state_key) {
  DCHECK(signing_service_);
  DCHECK(service_);
  DCHECK(!is_registered());

  SetClientId(client_id);

  em::CertificateBasedDeviceRegistrationData data;
  data.set_certificate_type(em::CertificateBasedDeviceRegistrationData::
      ENTERPRISE_ENROLLMENT_CERTIFICATE);
  data.set_device_certificate(pem_certificate_chain);

  em::DeviceRegisterRequest* request = data.mutable_device_register_request();
  if (!client_id.empty())
    request->set_reregister(true);
  request->set_type(type);
  if (!machine_id_.empty())
    request->set_machine_id(machine_id_);
  if (!machine_model_.empty())
    request->set_machine_model(machine_model_);
  if (!requisition.empty())
    request->set_requisition(requisition);
  if (!current_state_key.empty())
    request->set_server_backed_state_key(current_state_key);
  request->set_flavor(flavor);

  signing_service_->SignData(data.SerializeAsString(),
      base::Bind(&CloudPolicyClient::OnRegisterWithCertificateRequestSigned,
                 weak_ptr_factory_.GetWeakPtr()));
}

void CloudPolicyClient::OnRegisterWithCertificateRequestSigned(bool success,
    em::SignedData signed_data) {
  if (!success) {
    const em::DeviceManagementResponse response;
    OnRegisterCompleted(DM_STATUS_CANNOT_SIGN_REQUEST, 0, response);
    return;
  }

  policy_fetch_request_job_.reset(
      service_->CreateJob(
          DeviceManagementRequestJob::TYPE_CERT_BASED_REGISTRATION,
          GetRequestContext()));
  policy_fetch_request_job_->SetClientID(client_id_);
  em::SignedData* signed_request = policy_fetch_request_job_->GetRequest()->
      mutable_certificate_based_register_request()->mutable_signed_request();
  signed_request->set_data(signed_data.data());
  signed_request->set_signature(signed_data.signature());
  signed_request->set_extra_data_bytes(signed_data.extra_data_bytes());
  policy_fetch_request_job_->SetRetryCallback(
      base::Bind(&CloudPolicyClient::OnRetryRegister,
                 weak_ptr_factory_.GetWeakPtr()));
  policy_fetch_request_job_->Start(
      base::Bind(&CloudPolicyClient::OnRegisterCompleted,
                 weak_ptr_factory_.GetWeakPtr()));
}

void CloudPolicyClient::SetInvalidationInfo(int64_t version,
                                            const std::string& payload) {
  invalidation_version_ = version;
  invalidation_payload_ = payload;
}

void CloudPolicyClient::FetchPolicy() {
  CHECK(is_registered());
  CHECK(!types_to_fetch_.empty());

  policy_fetch_request_job_.reset(
      service_->CreateJob(DeviceManagementRequestJob::TYPE_POLICY_FETCH,
                          GetRequestContext()));
  policy_fetch_request_job_->SetDMToken(dm_token_);
  policy_fetch_request_job_->SetClientID(client_id_);
  if (!public_key_version_valid_)
    policy_fetch_request_job_->SetCritical(true);

  em::DeviceManagementRequest* request =
      policy_fetch_request_job_->GetRequest();

  // Build policy fetch requests.
  em::DevicePolicyRequest* policy_request = request->mutable_policy_request();
  for (const auto& type_to_fetch : types_to_fetch_) {
    em::PolicyFetchRequest* fetch_request = policy_request->add_request();
    fetch_request->set_policy_type(type_to_fetch.first);
    if (!type_to_fetch.second.empty())
      fetch_request->set_settings_entity_id(type_to_fetch.second);

    // Request signed policy blobs to help prevent tampering on the client.
    fetch_request->set_signature_type(em::PolicyFetchRequest::SHA1_RSA);
    if (public_key_version_valid_)
      fetch_request->set_public_key_version(public_key_version_);

    fetch_request->set_verification_key_hash(kPolicyVerificationKeyHash);

    // These fields are included only in requests for chrome policy.
    if (IsChromePolicy(type_to_fetch.first)) {
      if (!last_policy_timestamp_.is_null())
        fetch_request->set_timestamp(last_policy_timestamp_.ToJavaTime());
      if (!invalidation_payload_.empty()) {
        fetch_request->set_invalidation_version(invalidation_version_);
        fetch_request->set_invalidation_payload(invalidation_payload_);
      }
    }
  }

  // Add device state keys.
  if (!state_keys_to_upload_.empty()) {
    em::DeviceStateKeyUpdateRequest* key_update_request =
        request->mutable_device_state_key_update_request();
    for (std::vector<std::string>::const_iterator key(
             state_keys_to_upload_.begin());
         key != state_keys_to_upload_.end();
         ++key) {
      key_update_request->add_server_backed_state_key(*key);
    }
  }

  // Set the fetched invalidation version to the latest invalidation version
  // since it is now the invalidation version used for the latest fetch.
  fetched_invalidation_version_ = invalidation_version_;

  // Fire the job.
  policy_fetch_request_job_->Start(
      base::Bind(&CloudPolicyClient::OnPolicyFetchCompleted,
                 weak_ptr_factory_.GetWeakPtr()));
}

void CloudPolicyClient::FetchRobotAuthCodes(const std::string& auth_token) {
  CHECK(is_registered());

  policy_fetch_request_job_.reset(service_->CreateJob(
      DeviceManagementRequestJob::TYPE_API_AUTH_CODE_FETCH,
      GetRequestContext()));
  policy_fetch_request_job_->SetOAuthToken(auth_token);
  policy_fetch_request_job_->SetDMToken(dm_token_);
  policy_fetch_request_job_->SetClientID(client_id_);

  em::DeviceServiceApiAccessRequest* request =
      policy_fetch_request_job_->GetRequest()->
      mutable_service_api_access_request();
  request->set_oauth2_client_id(
      GaiaUrls::GetInstance()->oauth2_chrome_client_id());
  request->add_auth_scope(GaiaConstants::kAnyApiOAuth2Scope);
  request->set_device_type(em::DeviceServiceApiAccessRequest::CHROME_OS);

  policy_fetch_request_job_->Start(
      base::Bind(&CloudPolicyClient::OnFetchRobotAuthCodesCompleted,
                 weak_ptr_factory_.GetWeakPtr()));
}

void CloudPolicyClient::Unregister() {
  DCHECK(service_);
  policy_fetch_request_job_.reset(
      service_->CreateJob(DeviceManagementRequestJob::TYPE_UNREGISTRATION,
                          GetRequestContext()));
  policy_fetch_request_job_->SetDMToken(dm_token_);
  policy_fetch_request_job_->SetClientID(client_id_);
  policy_fetch_request_job_->GetRequest()->mutable_unregister_request();
  policy_fetch_request_job_->Start(
      base::Bind(&CloudPolicyClient::OnUnregisterCompleted,
                 weak_ptr_factory_.GetWeakPtr()));
}

void CloudPolicyClient::UploadCertificate(
    const std::string& certificate_data,
    const CloudPolicyClient::StatusCallback& callback) {
  CHECK(is_registered());
  std::unique_ptr<DeviceManagementRequestJob> request_job(
      service_->CreateJob(DeviceManagementRequestJob::TYPE_UPLOAD_CERTIFICATE,
                          GetRequestContext()));
  request_job->SetDMToken(dm_token_);
  request_job->SetClientID(client_id_);

  em::DeviceManagementRequest* request = request_job->GetRequest();
  request->mutable_cert_upload_request()->set_device_certificate(
      certificate_data);

  const DeviceManagementRequestJob::Callback job_callback =
      base::Bind(&CloudPolicyClient::OnCertificateUploadCompleted,
                 weak_ptr_factory_.GetWeakPtr(), request_job.get(), callback);

  request_jobs_.push_back(std::move(request_job));
  request_jobs_.back()->Start(job_callback);
}

void CloudPolicyClient::UploadDeviceStatus(
    const em::DeviceStatusReportRequest* device_status,
    const em::SessionStatusReportRequest* session_status,
    const CloudPolicyClient::StatusCallback& callback) {
  CHECK(is_registered());
  // Should pass in at least one type of status.
  DCHECK(device_status || session_status);
  std::unique_ptr<DeviceManagementRequestJob> request_job(service_->CreateJob(
      DeviceManagementRequestJob::TYPE_UPLOAD_STATUS, GetRequestContext()));
  request_job->SetDMToken(dm_token_);
  request_job->SetClientID(client_id_);

  em::DeviceManagementRequest* request = request_job->GetRequest();
  if (device_status)
    *request->mutable_device_status_report_request() = *device_status;
  if (session_status)
    *request->mutable_session_status_report_request() = *session_status;

  const DeviceManagementRequestJob::Callback job_callback =
      base::Bind(&CloudPolicyClient::OnStatusUploadCompleted,
                 weak_ptr_factory_.GetWeakPtr(), request_job.get(), callback);

  request_jobs_.push_back(std::move(request_job));
  request_jobs_.back()->Start(job_callback);
}

void CloudPolicyClient::FetchRemoteCommands(
    std::unique_ptr<RemoteCommandJob::UniqueIDType> last_command_id,
    const std::vector<em::RemoteCommandResult>& command_results,
    const RemoteCommandCallback& callback) {
  CHECK(is_registered());
  std::unique_ptr<DeviceManagementRequestJob> request_job(service_->CreateJob(
      DeviceManagementRequestJob::TYPE_REMOTE_COMMANDS, GetRequestContext()));

  request_job->SetDMToken(dm_token_);
  request_job->SetClientID(client_id_);

  em::DeviceRemoteCommandRequest* const request =
      request_job->GetRequest()->mutable_remote_command_request();

  if (last_command_id)
    request->set_last_command_unique_id(*last_command_id);

  for (const auto& command_result : command_results)
    *request->add_command_results() = command_result;

  const DeviceManagementRequestJob::Callback job_callback =
      base::Bind(&CloudPolicyClient::OnRemoteCommandsFetched,
                 weak_ptr_factory_.GetWeakPtr(), request_job.get(), callback);

  request_jobs_.push_back(std::move(request_job));
  request_jobs_.back()->Start(job_callback);
}

void CloudPolicyClient::GetDeviceAttributeUpdatePermission(
    const std::string &auth_token,
    const CloudPolicyClient::StatusCallback& callback) {
  // TODO(pbond): remove this LOG once http://crbug.com/586961 is fixed.
  LOG(WARNING) << "Send DeviceAttributeUpdatePermissionRequest";
  CHECK(is_registered());
  DCHECK(!auth_token.empty());

  std::unique_ptr<DeviceManagementRequestJob> request_job(service_->CreateJob(
      DeviceManagementRequestJob::TYPE_ATTRIBUTE_UPDATE_PERMISSION,
      GetRequestContext()));

  request_job->SetOAuthToken(auth_token);
  request_job->SetClientID(client_id_);

  request_job->GetRequest()->
      mutable_device_attribute_update_permission_request();

  const DeviceManagementRequestJob::Callback job_callback =
      base::Bind(&CloudPolicyClient::OnDeviceAttributeUpdatePermissionCompleted,
      weak_ptr_factory_.GetWeakPtr(), request_job.get(), callback);

  request_jobs_.push_back(std::move(request_job));
  request_jobs_.back()->Start(job_callback);
}

void CloudPolicyClient::UpdateDeviceAttributes(
    const std::string& auth_token,
    const std::string& asset_id,
    const std::string& location,
    const CloudPolicyClient::StatusCallback& callback) {
  CHECK(is_registered());
  DCHECK(!auth_token.empty());

  std::unique_ptr<DeviceManagementRequestJob> request_job(service_->CreateJob(
      DeviceManagementRequestJob::TYPE_ATTRIBUTE_UPDATE, GetRequestContext()));

  request_job->SetOAuthToken(auth_token);
  request_job->SetClientID(client_id_);

  em::DeviceAttributeUpdateRequest* request =
      request_job->GetRequest()->mutable_device_attribute_update_request();

  request->set_asset_id(asset_id);
  request->set_location(location);

  const DeviceManagementRequestJob::Callback job_callback =
      base::Bind(&CloudPolicyClient::OnDeviceAttributeUpdated,
      weak_ptr_factory_.GetWeakPtr(), request_job.get(), callback);

  request_jobs_.push_back(std::move(request_job));
  request_jobs_.back()->Start(job_callback);
}

void CloudPolicyClient::RequestAvailableLicenses(
    const std::string& auth_token,
    const LicenseRequestCallback& callback) {
  DCHECK(!auth_token.empty());

  std::unique_ptr<DeviceManagementRequestJob> request_job(service_->CreateJob(
      DeviceManagementRequestJob::TYPE_REQUEST_LICENSE_TYPES,
      GetRequestContext()));

  request_job->SetOAuthToken(auth_token);

  request_job->GetRequest()->mutable_check_device_license_request();

  const DeviceManagementRequestJob::Callback job_callback =
      base::Bind(&CloudPolicyClient::OnAvailableLicensesRequested,
                 weak_ptr_factory_.GetWeakPtr(), request_job.get(), callback);

  request_jobs_.push_back(std::move(request_job));
  request_jobs_.back()->Start(job_callback);
}

void CloudPolicyClient::UpdateGcmId(
    const std::string& gcm_id,
    const CloudPolicyClient::StatusCallback& callback) {
  CHECK(is_registered());

  std::unique_ptr<DeviceManagementRequestJob> request_job(service_->CreateJob(
      DeviceManagementRequestJob::TYPE_GCM_ID_UPDATE, GetRequestContext()));

  request_job->SetDMToken(dm_token_);
  request_job->SetClientID(client_id_);

  em::GcmIdUpdateRequest* const request =
      request_job->GetRequest()->mutable_gcm_id_update_request();

  request->set_gcm_id(gcm_id);

  const DeviceManagementRequestJob::Callback job_callback =
      base::Bind(&CloudPolicyClient::OnGcmIdUpdated,
                 weak_ptr_factory_.GetWeakPtr(), request_job.get(), callback);

  request_jobs_.push_back(std::move(request_job));
  request_jobs_.back()->Start(job_callback);
}

void CloudPolicyClient::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void CloudPolicyClient::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

void CloudPolicyClient::AddPolicyTypeToFetch(
    const std::string& policy_type,
    const std::string& settings_entity_id) {
  types_to_fetch_.insert(std::make_pair(policy_type, settings_entity_id));
}

void CloudPolicyClient::RemovePolicyTypeToFetch(
    const std::string& policy_type,
    const std::string& settings_entity_id) {
  types_to_fetch_.erase(std::make_pair(policy_type, settings_entity_id));
}

void CloudPolicyClient::SetStateKeysToUpload(
    const std::vector<std::string>& keys) {
  state_keys_to_upload_ = keys;
}

const em::PolicyFetchResponse* CloudPolicyClient::GetPolicyFor(
    const std::string& policy_type,
    const std::string& settings_entity_id) const {
  auto it = responses_.find(std::make_pair(policy_type, settings_entity_id));
  return it == responses_.end() ? nullptr : it->second.get();
}

scoped_refptr<net::URLRequestContextGetter>
CloudPolicyClient::GetRequestContext() {
  return request_context_;
}

int CloudPolicyClient::GetActiveRequestCountForTest() const {
  return request_jobs_.size();
}

void CloudPolicyClient::OnRetryRegister(DeviceManagementRequestJob* job) {
  DCHECK_EQ(policy_fetch_request_job_.get(), job);
  // If the initial request managed to get to the server but the response didn't
  // arrive at the client then retrying with the same client ID will fail.
  // Set the re-registration flag so that the server accepts it.
  // If the server hasn't seen the client ID before then it will also accept
  // the re-registration.
  job->GetRequest()->mutable_register_request()->set_reregister(true);
}

void CloudPolicyClient::OnRegisterCompleted(
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  if (status == DM_STATUS_SUCCESS &&
      (!response.has_register_response() ||
       !response.register_response().has_device_management_token())) {
    LOG(WARNING) << "Invalid registration response.";
    status = DM_STATUS_RESPONSE_DECODING_ERROR;
  }

  status_ = status;
  if (status == DM_STATUS_SUCCESS) {
    dm_token_ = response.register_response().device_management_token();
    DVLOG(1) << "Client registration complete - DMToken = " << dm_token_;

    // Device mode is only relevant for device policy really, it's the
    // responsibility of the consumer of the field to check validity.
    device_mode_ = DEVICE_MODE_NOT_SET;
    if (response.register_response().has_enrollment_type()) {
      device_mode_ = TranslateProtobufDeviceMode(
          response.register_response().enrollment_type());
    }

    NotifyRegistrationStateChanged();
  } else {
    NotifyClientError();
  }
}

void CloudPolicyClient::OnFetchRobotAuthCodesCompleted(
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  if (status == DM_STATUS_SUCCESS &&
      (!response.has_service_api_access_response())) {
    LOG(WARNING) << "Invalid service api access response.";
    status = DM_STATUS_RESPONSE_DECODING_ERROR;
  }

  status_ = status;
  if (status == DM_STATUS_SUCCESS) {
    robot_api_auth_code_ = response.service_api_access_response().auth_code();
    DVLOG(1) << "Device robot account auth code fetch complete - code = "
             << robot_api_auth_code_;

    NotifyRobotAuthCodesFetched();
  } else {
    NotifyClientError();
  }
}

void CloudPolicyClient::OnPolicyFetchCompleted(
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  if (status == DM_STATUS_SUCCESS) {
    if (!response.has_policy_response() ||
        response.policy_response().response_size() == 0) {
      LOG(WARNING) << "Empty policy response.";
      status = DM_STATUS_RESPONSE_DECODING_ERROR;
    }
  }

  status_ = status;
  if (status == DM_STATUS_SUCCESS) {
    const em::DevicePolicyResponse& policy_response =
        response.policy_response();
    responses_.clear();
    for (int i = 0; i < policy_response.response_size(); ++i) {
      const em::PolicyFetchResponse& response = policy_response.response(i);
      em::PolicyData policy_data;
      if (!policy_data.ParseFromString(response.policy_data()) ||
          !policy_data.IsInitialized() ||
          !policy_data.has_policy_type()) {
        LOG(WARNING) << "Invalid PolicyData received, ignoring";
        continue;
      }
      const std::string& type = policy_data.policy_type();
      std::string entity_id;
      if (policy_data.has_settings_entity_id())
        entity_id = policy_data.settings_entity_id();
      std::pair<std::string, std::string> key(type, entity_id);
      if (base::ContainsKey(responses_, key)) {
        LOG(WARNING) << "Duplicate PolicyFetchResponse for type: "
            << type << ", entity: " << entity_id << ", ignoring";
        continue;
      }
      responses_[key] = base::MakeUnique<em::PolicyFetchResponse>(response);
    }
    state_keys_to_upload_.clear();
    NotifyPolicyFetched();
  } else {
    NotifyClientError();
  }
}

void CloudPolicyClient::OnUnregisterCompleted(
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  if (status == DM_STATUS_SUCCESS && !response.has_unregister_response()) {
    // Assume unregistration has succeeded either way.
    LOG(WARNING) << "Empty unregistration response.";
  }

  status_ = status;
  if (status == DM_STATUS_SUCCESS) {
    dm_token_.clear();
    // Cancel all outstanding jobs.
    request_jobs_.clear();
    NotifyRegistrationStateChanged();
  } else {
    NotifyClientError();
  }
}

void CloudPolicyClient::OnCertificateUploadCompleted(
    const DeviceManagementRequestJob* job,
    const CloudPolicyClient::StatusCallback& callback,
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  bool success = true;
  status_ = status;
  if (status != DM_STATUS_SUCCESS) {
    success = false;
    NotifyClientError();
  } else if (!response.has_cert_upload_response()) {
    LOG(WARNING) << "Empty upload certificate response.";
    success = false;
  }
  callback.Run(success);
  // Must call RemoveJob() last, because it frees |callback|.
  RemoveJob(job);
}

void CloudPolicyClient::OnDeviceAttributeUpdatePermissionCompleted(
    const DeviceManagementRequestJob* job,
    const CloudPolicyClient::StatusCallback& callback,
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  // TODO(pbond): remove this LOG once http://crbug.com/586961 is fixed.
  LOG(WARNING) << "Receive DeviceAttributeUpdatePermissionResponse status="
               << status << " net_error=" << net_error;
  bool success = false;

  if (status == DM_STATUS_SUCCESS &&
      !response.has_device_attribute_update_permission_response()) {
    LOG(WARNING) << "Invalid device attribute update permission response.";
    status = DM_STATUS_RESPONSE_DECODING_ERROR;
  }

  status_ = status;
  if (status == DM_STATUS_SUCCESS &&
      response.device_attribute_update_permission_response().has_result() &&
      response.device_attribute_update_permission_response().result() ==
      em::DeviceAttributeUpdatePermissionResponse::ATTRIBUTE_UPDATE_ALLOWED) {
    // TODO(pbond): remove this LOG once http://crbug.com/586961 is fixed.
    LOG(WARNING) << "The device attribute update is permitted.";
    success = true;
  }

  callback.Run(success);
  RemoveJob(job);
}

void CloudPolicyClient::OnDeviceAttributeUpdated(
    const DeviceManagementRequestJob* job,
    const CloudPolicyClient::StatusCallback& callback,
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  bool success = false;

  if (status == DM_STATUS_SUCCESS &&
      !response.has_device_attribute_update_response()) {
    LOG(WARNING) << "Invalid device attribute update response.";
    status = DM_STATUS_RESPONSE_DECODING_ERROR;
  }

  status_ = status;
  if (status == DM_STATUS_SUCCESS &&
      response.device_attribute_update_response().has_result() &&
      response.device_attribute_update_response().result() ==
      em::DeviceAttributeUpdateResponse::ATTRIBUTE_UPDATE_SUCCESS) {
    success = true;
  }

  callback.Run(success);
  RemoveJob(job);
}

void CloudPolicyClient::OnAvailableLicensesRequested(
    const DeviceManagementRequestJob* job,
    const CloudPolicyClient::LicenseRequestCallback& callback,
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  CloudPolicyClient::LicenseMap licenses;

  if (status != DM_STATUS_SUCCESS) {
    LOG(WARNING) << "Could not get available license types";
    status_ = status;
    callback.Run(false /* success */, licenses);
    RemoveJob(job);
    return;
  }

  if (!response.has_check_device_license_response()) {
    LOG(WARNING) << "Invalid license request response.";
    status_ = DM_STATUS_RESPONSE_DECODING_ERROR;
    callback.Run(false /* success */, licenses);
    RemoveJob(job);
    return;
  }

  status_ = status;
  const em::CheckDeviceLicenseResponse& license_response =
      response.check_device_license_response();

  if (license_response.has_license_selection_mode() &&
      (license_response.license_selection_mode() ==
       em::CheckDeviceLicenseResponse::USER_SELECTION)) {
    ExtractLicenseMap(license_response, licenses);
  }

  callback.Run(true /* success */, licenses);
  RemoveJob(job);
}

void CloudPolicyClient::RemoveJob(const DeviceManagementRequestJob* job) {
  for (auto it = request_jobs_.begin(); it != request_jobs_.end(); ++it) {
    if (it->get() == job) {
      request_jobs_.erase(it);
      return;
    }
  }
  // This job was already deleted from our list, somehow. This shouldn't
  // happen since deleting the job should cancel the callback.
  NOTREACHED();
}

void CloudPolicyClient::OnStatusUploadCompleted(
    const DeviceManagementRequestJob* job,
    const CloudPolicyClient::StatusCallback& callback,
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  status_ = status;
  if (status != DM_STATUS_SUCCESS)
    NotifyClientError();

  callback.Run(status == DM_STATUS_SUCCESS);
  // Must call RemoveJob() last, because it frees |callback|.
  RemoveJob(job);
}

void CloudPolicyClient::OnRemoteCommandsFetched(
    const DeviceManagementRequestJob* job,
    const RemoteCommandCallback& callback,
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  std::vector<em::RemoteCommand> commands;
  if (status == DM_STATUS_SUCCESS) {
    if (response.has_remote_command_response()) {
      for (const auto& command : response.remote_command_response().commands())
        commands.push_back(command);
    } else {
      status = DM_STATUS_RESPONSE_DECODING_ERROR;
    }
  }
  callback.Run(status, commands);
  // Must call RemoveJob() last, because it frees |callback|.
  RemoveJob(job);
}

void CloudPolicyClient::OnGcmIdUpdated(
    const DeviceManagementRequestJob* job,
    const StatusCallback& callback,
    DeviceManagementStatus status,
    int net_error,
    const em::DeviceManagementResponse& response) {
  status_ = status;
  if (status != DM_STATUS_SUCCESS)
    NotifyClientError();

  callback.Run(status == DM_STATUS_SUCCESS);
  RemoveJob(job);
}

void CloudPolicyClient::NotifyPolicyFetched() {
  for (auto& observer : observers_)
    observer.OnPolicyFetched(this);
}

void CloudPolicyClient::NotifyRegistrationStateChanged() {
  for (auto& observer : observers_)
    observer.OnRegistrationStateChanged(this);
}

void CloudPolicyClient::NotifyRobotAuthCodesFetched() {
  for (auto& observer : observers_)
    observer.OnRobotAuthCodesFetched(this);
}

void CloudPolicyClient::NotifyClientError() {
  for (auto& observer : observers_)
    observer.OnClientError(this);
}

}  // namespace policy
