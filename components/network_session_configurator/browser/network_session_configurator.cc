// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/network_session_configurator/browser/network_session_configurator.h"

#include <map>
#include <unordered_set>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/network_session_configurator/common/network_features.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "components/variations/variations_associated_data.h"
#include "net/base/host_mapping_rules.h"
#include "net/http/http_stream_factory.h"
#include "net/quic/chromium/quic_utils_chromium.h"
#include "net/quic/core/quic_packets.h"
#include "net/spdy/core/spdy_protocol.h"

namespace {

// Map from name to value for all parameters associate with a field trial.
using VariationParameters = std::map<std::string, std::string>;

const char kTCPFastOpenFieldTrialName[] = "TCPFastOpen";
const char kTCPFastOpenHttpsEnabledGroupName[] = "HttpsEnabled";

const char kQuicFieldTrialName[] = "QUIC";
const char kQuicFieldTrialEnabledGroupName[] = "Enabled";
const char kQuicFieldTrialHttpsEnabledGroupName[] = "HttpsEnabled";

// Field trial for HTTP/2.
const char kHttp2FieldTrialName[] = "HTTP2";
const char kHttp2FieldTrialDisablePrefix[] = "Disable";

// Gets the value of the specified command line switch, as an integer. If unable
// to convert it to an int (It's not an int, or the switch is not present)
// returns 0.
int GetSwitchValueAsInt(const base::CommandLine& command_line,
                        const std::string& switch_name) {
  int value;
  if (!base::StringToInt(command_line.GetSwitchValueASCII(switch_name),
                         &value)) {
    return 0;
  }
  return value;
}

// Returns the value associated with |key| in |params| or "" if the
// key is not present in the map.
const std::string& GetVariationParam(
    const std::map<std::string, std::string>& params,
    const std::string& key) {
  std::map<std::string, std::string>::const_iterator it = params.find(key);
  if (it == params.end())
    return base::EmptyString();

  return it->second;
}

void ConfigureTCPFastOpenParams(base::StringPiece tfo_trial_group,
                                net::HttpNetworkSession::Params* params) {
  if (tfo_trial_group == kTCPFastOpenHttpsEnabledGroupName)
    params->enable_tcp_fast_open_for_ssl = true;
}

net::SettingsMap GetHttp2Settings(
    const VariationParameters& http2_trial_params) {
  net::SettingsMap http2_settings;

  const std::string settings_string =
      GetVariationParam(http2_trial_params, "http2_settings");

  base::StringPairs key_value_pairs;
  if (!base::SplitStringIntoKeyValuePairs(settings_string, ':', ',',
                                          &key_value_pairs)) {
    return http2_settings;
  }

  for (auto key_value : key_value_pairs) {
    uint32_t key;
    if (!base::StringToUint(key_value.first, &key))
      continue;
    uint32_t value;
    if (!base::StringToUint(key_value.second, &value))
      continue;
    http2_settings[static_cast<net::SpdySettingsIds>(key)] = value;
  }

  return http2_settings;
}

void ConfigureHttp2Params(base::StringPiece http2_trial_group,
                          const VariationParameters& http2_trial_params,
                          net::HttpNetworkSession::Params* params) {
  if (http2_trial_group.starts_with(kHttp2FieldTrialDisablePrefix)) {
    params->enable_http2 = false;
    return;
  }
  params->http2_settings = GetHttp2Settings(http2_trial_params);
}

bool ShouldEnableQuic(base::StringPiece quic_trial_group,
                      const VariationParameters& quic_trial_params,
                      bool is_quic_force_disabled,
                      bool is_quic_force_enabled) {
  if (is_quic_force_disabled)
    return false;
  if (is_quic_force_enabled)
    return true;

  return quic_trial_group.starts_with(kQuicFieldTrialEnabledGroupName) ||
         quic_trial_group.starts_with(kQuicFieldTrialHttpsEnabledGroupName) ||
         base::LowerCaseEqualsASCII(
             GetVariationParam(quic_trial_params, "enable_quic"), "true");
}

bool ShouldMarkQuicBrokenWhenNetworkBlackholes(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params,
                        "mark_quic_broken_when_network_blackholes"),
      "true");
}

bool ShouldRetryWithoutAltSvcOnQuicErrors(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params,
                        "retry_without_alt_svc_on_quic_errors"),
      "true");
}

net::QuicTagVector GetQuicConnectionOptions(
    const VariationParameters& quic_trial_params) {
  VariationParameters::const_iterator it =
      quic_trial_params.find("connection_options");
  if (it == quic_trial_params.end()) {
    return net::QuicTagVector();
  }

  return net::ParseQuicConnectionOptions(it->second);
}

bool ShouldForceHolBlocking(const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "force_hol_blocking"), "true");
}

bool ShouldQuicCloseSessionsOnIpChange(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "close_sessions_on_ip_change"),
      "true");
}

int GetQuicIdleConnectionTimeoutSeconds(
    const VariationParameters& quic_trial_params) {
  int value;
  if (base::StringToInt(GetVariationParam(quic_trial_params,
                                          "idle_connection_timeout_seconds"),
                        &value)) {
    return value;
  }
  return 0;
}

int GetQuicReducedPingTimeoutSeconds(
    const VariationParameters& quic_trial_params) {
  int value;
  if (base::StringToInt(
          GetVariationParam(quic_trial_params, "reduced_ping_timeout_seconds"),
          &value)) {
    return value;
  }
  return 0;
}

bool ShouldQuicRaceCertVerification(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "race_cert_verification"), "true");
}

bool ShouldQuicEstimateInitialRtt(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "estimate_initial_rtt"), "true");
}

bool ShouldQuicMigrateSessionsOnNetworkChange(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params,
                        "migrate_sessions_on_network_change"),
      "true");
}

bool ShouldQuicMigrateSessionsEarly(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "migrate_sessions_early"), "true");
}

bool ShouldQuicAllowServerMigration(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "allow_server_migration"), "true");
}

size_t GetQuicMaxPacketLength(const VariationParameters& quic_trial_params) {
  unsigned value;
  if (base::StringToUint(
          GetVariationParam(quic_trial_params, "max_packet_length"), &value)) {
    return value;
  }
  return 0;
}

net::QuicVersionVector GetQuicVersions(
    const VariationParameters& quic_trial_params) {
  return network_session_configurator::ParseQuicVersions(
      GetVariationParam(quic_trial_params, "quic_version"));
}

bool ShouldEnableServerPushCancelation(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "enable_server_push_cancellation"),
      "true");
}

void ConfigureQuicParams(base::StringPiece quic_trial_group,
                         const VariationParameters& quic_trial_params,
                         bool is_quic_force_disabled,
                         bool is_quic_force_enabled,
                         const std::string& quic_user_agent_id,
                         net::HttpNetworkSession::Params* params) {
  params->enable_quic =
      ShouldEnableQuic(quic_trial_group, quic_trial_params,
                       is_quic_force_disabled, is_quic_force_enabled);
  params->mark_quic_broken_when_network_blackholes =
      ShouldMarkQuicBrokenWhenNetworkBlackholes(quic_trial_params);

  params->enable_server_push_cancellation =
      ShouldEnableServerPushCancelation(quic_trial_params);

  params->retry_without_alt_svc_on_quic_errors =
      ShouldRetryWithoutAltSvcOnQuicErrors(quic_trial_params);

  if (params->enable_quic) {
    params->quic_force_hol_blocking = ShouldForceHolBlocking(quic_trial_params);
    params->quic_connection_options =
        GetQuicConnectionOptions(quic_trial_params);
    params->quic_close_sessions_on_ip_change =
        ShouldQuicCloseSessionsOnIpChange(quic_trial_params);
    int idle_connection_timeout_seconds =
        GetQuicIdleConnectionTimeoutSeconds(quic_trial_params);
    if (idle_connection_timeout_seconds != 0) {
      params->quic_idle_connection_timeout_seconds =
          idle_connection_timeout_seconds;
    }
    int reduced_ping_timeout_seconds =
        GetQuicReducedPingTimeoutSeconds(quic_trial_params);
    if (reduced_ping_timeout_seconds > 0 &&
        reduced_ping_timeout_seconds < net::kPingTimeoutSecs) {
      params->quic_reduced_ping_timeout_seconds = reduced_ping_timeout_seconds;
    }
    params->quic_race_cert_verification =
        ShouldQuicRaceCertVerification(quic_trial_params);
    params->quic_estimate_initial_rtt =
        ShouldQuicEstimateInitialRtt(quic_trial_params);
    params->quic_migrate_sessions_on_network_change =
        ShouldQuicMigrateSessionsOnNetworkChange(quic_trial_params);
    params->quic_migrate_sessions_early =
        ShouldQuicMigrateSessionsEarly(quic_trial_params);
    params->quic_allow_server_migration =
        ShouldQuicAllowServerMigration(quic_trial_params);
  }

  size_t max_packet_length = GetQuicMaxPacketLength(quic_trial_params);
  if (max_packet_length != 0) {
    params->quic_max_packet_length = max_packet_length;
  }

  params->quic_user_agent_id = quic_user_agent_id;

  net::QuicVersionVector supported_versions =
      GetQuicVersions(quic_trial_params);
  if (!supported_versions.empty())
    params->quic_supported_versions = supported_versions;
}

}  // anonymous namespace

namespace network_session_configurator {

net::QuicVersionVector ParseQuicVersions(const std::string& quic_versions) {
  net::QuicVersionVector supported_versions;
  net::QuicVersionVector all_supported_versions = net::AllSupportedVersions();

  for (const base::StringPiece& version : base::SplitStringPiece(
           quic_versions, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    auto it = all_supported_versions.begin();
    while (it != all_supported_versions.end()) {
      if (net::QuicVersionToString(*it) == version) {
        supported_versions.push_back(*it);
        // Remove the supported version to deduplicate versions extracted from
        // |quic_versions|.
        all_supported_versions.erase(it);
        break;
      }
      it++;
    }
  }
  return supported_versions;
}

void ParseCommandLineAndFieldTrials(const base::CommandLine& command_line,
                                    bool is_quic_force_disabled,
                                    const std::string& quic_user_agent_id,
                                    net::HttpNetworkSession::Params* params) {
  is_quic_force_disabled |= command_line.HasSwitch(switches::kDisableQuic);
  bool is_quic_force_enabled = command_line.HasSwitch(switches::kEnableQuic);

  std::string quic_trial_group =
      base::FieldTrialList::FindFullName(kQuicFieldTrialName);
  VariationParameters quic_trial_params;
  if (!variations::GetVariationParams(kQuicFieldTrialName, &quic_trial_params))
    quic_trial_params.clear();
  ConfigureQuicParams(quic_trial_group, quic_trial_params,
                      is_quic_force_disabled, is_quic_force_enabled,
                      quic_user_agent_id, params);

  std::string http2_trial_group =
      base::FieldTrialList::FindFullName(kHttp2FieldTrialName);
  VariationParameters http2_trial_params;
  if (!variations::GetVariationParams(kHttp2FieldTrialName,
                                      &http2_trial_params))
    http2_trial_params.clear();
  ConfigureHttp2Params(http2_trial_group, http2_trial_params, params);

  const std::string tfo_trial_group =
      base::FieldTrialList::FindFullName(kTCPFastOpenFieldTrialName);
  ConfigureTCPFastOpenParams(tfo_trial_group, params);

  // Command line flags override field trials.
  if (command_line.HasSwitch(switches::kDisableHttp2))
    params->enable_http2 = false;

  if (params->enable_quic) {
    if (command_line.HasSwitch(switches::kQuicConnectionOptions)) {
      params->quic_connection_options = net::ParseQuicConnectionOptions(
          command_line.GetSwitchValueASCII(switches::kQuicConnectionOptions));
    }

    if (command_line.HasSwitch(switches::kQuicMaxPacketLength)) {
      unsigned value;
      if (base::StringToUint(
              command_line.GetSwitchValueASCII(switches::kQuicMaxPacketLength),
              &value)) {
        params->quic_max_packet_length = value;
      }
    }

    if (command_line.HasSwitch(switches::kQuicVersion)) {
      net::QuicVersionVector supported_versions =
          network_session_configurator::ParseQuicVersions(
              command_line.GetSwitchValueASCII(switches::kQuicVersion));
      if (!supported_versions.empty())
        params->quic_supported_versions = supported_versions;
    }

    if (command_line.HasSwitch(switches::kOriginToForceQuicOn)) {
      std::string origins =
          command_line.GetSwitchValueASCII(switches::kOriginToForceQuicOn);
      for (const std::string& host_port : base::SplitString(
               origins, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
        if (host_port == "*")
          params->origins_to_force_quic_on.insert(net::HostPortPair());
        net::HostPortPair quic_origin =
            net::HostPortPair::FromString(host_port);
        if (!quic_origin.IsEmpty())
          params->origins_to_force_quic_on.insert(quic_origin);
      }
    }
  }

  // Parameters only controlled by command line.
  if (command_line.HasSwitch(switches::kEnableUserAlternateProtocolPorts)) {
    params->enable_user_alternate_protocol_ports = true;
  }
  if (command_line.HasSwitch(switches::kIgnoreCertificateErrors)) {
    params->ignore_certificate_errors = true;
  }
  UMA_HISTOGRAM_BOOLEAN(
      "Net.Certificate.IgnoreErrors",
      command_line.HasSwitch(switches::kIgnoreCertificateErrors));
  if (command_line.HasSwitch(switches::kTestingFixedHttpPort)) {
    params->testing_fixed_http_port =
        GetSwitchValueAsInt(command_line, switches::kTestingFixedHttpPort);
  }
  if (command_line.HasSwitch(switches::kTestingFixedHttpsPort)) {
    params->testing_fixed_https_port =
        GetSwitchValueAsInt(command_line, switches::kTestingFixedHttpsPort);
  }

  if (command_line.HasSwitch(switches::kHostRules)) {
    params->host_mapping_rules.SetRulesFromString(
        command_line.GetSwitchValueASCII(switches::kHostRules));
  }

  params->enable_token_binding =
      base::FeatureList::IsEnabled(features::kTokenBinding);
}

}  // namespace network_session_configurator
