// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/network/network_icon.h"

#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/strings/grit/ash_strings.h"
#include "ash/system/network/network_icon_animation.h"
#include "ash/system/network/network_icon_animation_observer.h"
#include "ash/system/tray/tray_constants.h"
#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "chromeos/network/device_state.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_state.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/portal_detector/network_portal_detector.h"
#include "chromeos/network/tether_constants.h"
#include "third_party/cros_system_api/dbus/service_constants.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/gfx/color_palette.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size_conversions.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/image/image_skia_source.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/skia_util.h"
#include "ui/gfx/vector_icon_types.h"

using chromeos::DeviceState;
using chromeos::NetworkConnectionHandler;
using chromeos::NetworkHandler;
using chromeos::NetworkPortalDetector;
using chromeos::NetworkState;
using chromeos::NetworkStateHandler;
using chromeos::NetworkTypePattern;

namespace ash {
namespace network_icon {

namespace {

// Constants for offseting the badge displayed on top of the signal strength
// icon. The badge will extend outside of the base icon bounds by these amounts.
// All values are in dp.

// The badge offsets are different depending on whether the icon is in the tray
// or menu.
const int kTrayIconBadgeOffset = 3;
const int kMenuIconBadgeOffset = 2;

// Describes a single badge which is defined by a vector icon.
struct Badge {
  bool operator==(const Badge& other) const {
    return other.icon == icon && other.color == color;
  }
  bool operator!=(const Badge& other) const { return !(other == *this); }

  const gfx::VectorIcon* icon;
  SkColor color;
};

//------------------------------------------------------------------------------
// Struct to pass a collection of badges to NetworkIconImageSource.
struct Badges {
  Badge top_left = {};
  Badge center = {};
  Badge bottom_left = {};
  Badge bottom_right = {};
};

//------------------------------------------------------------------------------
// class used for maintaining a map of network state and images.
class NetworkIconImpl {
 public:
  NetworkIconImpl(const std::string& path,
                  IconType icon_type,
                  const std::string& network_type);

  // Determines whether or not the associated network might be dirty and if so
  // updates and generates the icon. Does nothing if network no longer exists.
  void Update(const chromeos::NetworkState* network);

  const gfx::ImageSkia& image() const { return image_; }

 private:
  // Updates |strength_index_| for wireless networks. Returns true if changed.
  bool UpdateWirelessStrengthIndex(const chromeos::NetworkState* network);

  // Updates the local state for cellular networks. Returns true if changed.
  bool UpdateCellularState(const chromeos::NetworkState* network);

  // Updates the portal state for wireless networks. Returns true if changed.
  bool UpdatePortalState(const chromeos::NetworkState* network);

  // Updates the VPN badge. Returns true if changed.
  bool UpdateVPNBadge();

  // Gets |badges| based on |network| and the current state.
  void GetBadges(const NetworkState* network, Badges* badges);

  // Gets the appropriate icon and badges and composites the image.
  void GenerateImage(const chromeos::NetworkState* network);

  // Network path, used for debugging.
  std::string network_path_;

  // Defines color theme and VPN badging
  const IconType icon_type_;

  // Cached state of the network when the icon was last generated.
  std::string state_;

  // Cached strength index of the network when the icon was last generated.
  int strength_index_;

  // Cached technology badge for the network when the icon was last generated.
  Badge technology_badge_ = {};

  // Cached vpn badge for the network when the icon was last generated.
  Badge vpn_badge_ = {};

  // Cached roaming state of the network when the icon was last generated.
  std::string roaming_state_;

  // Cached portal state of the network when the icon was last generated.
  bool behind_captive_portal_;

  // Generated icon image.
  gfx::ImageSkia image_;

  DISALLOW_COPY_AND_ASSIGN(NetworkIconImpl);
};

//------------------------------------------------------------------------------
// Maintain a static (global) icon map. Note: Icons are never destroyed;
// it is assumed that a finite and reasonable number of network icons will be
// created during a session.

typedef std::map<std::string, NetworkIconImpl*> NetworkIconMap;

NetworkIconMap* GetIconMapInstance(IconType icon_type, bool create) {
  typedef std::map<IconType, NetworkIconMap*> IconTypeMap;
  static IconTypeMap* s_icon_map = nullptr;
  if (s_icon_map == nullptr) {
    if (!create)
      return nullptr;
    s_icon_map = new IconTypeMap;
  }
  if (s_icon_map->count(icon_type) == 0) {
    if (!create)
      return nullptr;
    (*s_icon_map)[icon_type] = new NetworkIconMap;
  }
  return (*s_icon_map)[icon_type];
}

NetworkIconMap* GetIconMap(IconType icon_type) {
  return GetIconMapInstance(icon_type, true);
}

void PurgeIconMap(IconType icon_type,
                  const std::set<std::string>& network_paths) {
  NetworkIconMap* icon_map = GetIconMapInstance(icon_type, false);
  if (!icon_map)
    return;
  for (NetworkIconMap::iterator loop_iter = icon_map->begin();
       loop_iter != icon_map->end();) {
    NetworkIconMap::iterator cur_iter = loop_iter++;
    if (network_paths.count(cur_iter->first) == 0) {
      delete cur_iter->second;
      icon_map->erase(cur_iter);
    }
  }
}

//------------------------------------------------------------------------------
// Utilities for generating icon images.

// Amount to fade icons while connecting.
const double kConnectingImageAlpha = 0.5;

// Images for strength arcs for wireless networks or strength bars for cellular
// networks.
const int kNumNetworkImages = 5;

// Number of discrete images to use for alpha fade animation
const int kNumFadeImages = 10;

// Padding between outside of icon and edge of the canvas, in dp. This value
// stays the same regardless of the canvas size.
static constexpr int kSignalStrengthImageInset = 2;

// TODO(estade): share this alpha with other things in ash (battery, etc.).
// See crbug.com/623987 and crbug.com/632827
static constexpr int kSignalStrengthImageBgAlpha = 0x4D;

SkColor GetDefaultColorForIconType(IconType icon_type) {
  return icon_type == ICON_TYPE_TRAY ? kTrayIconColor : kMenuIconColor;
}

bool IconTypeIsDark(IconType icon_type) {
  return (icon_type != ICON_TYPE_TRAY);
}

bool IconTypeHasVPNBadge(IconType icon_type) {
  return (icon_type != ICON_TYPE_LIST && icon_type != ICON_TYPE_MENU_LIST);
}

// This defines how we assemble a network icon.
class NetworkIconImageSource : public gfx::CanvasImageSource {
 public:
  static gfx::ImageSkia CreateImage(const gfx::ImageSkia& icon,
                                    const Badges& badges) {
    auto* source = new NetworkIconImageSource(icon, badges);
    return gfx::ImageSkia(source, source->size());
  }

  // gfx::CanvasImageSource:
  void Draw(gfx::Canvas* canvas) override {
    const int width = size().width();
    const int height = size().height();

    // The base icon is centered in both dimensions.
    const int icon_x = (width - icon_.width()) / 2;
    const int icon_y = (height - icon_.height()) / 2;
    canvas->DrawImageInt(icon_, icon_x, icon_y);

    auto paint_badge = [&canvas](const Badge& badge, int x, int y,
                                 int badge_size = 0) {
      gfx::ScopedCanvas scoped(canvas);
      canvas->Translate(gfx::Vector2d(x, y));
      if (badge_size)
        gfx::PaintVectorIcon(canvas, *badge.icon, badge_size, badge.color);
      else
        gfx::PaintVectorIcon(canvas, *badge.icon, badge.color);
    };

    // The center badge is scaled and centered over the icon.
    if (badges_.center.icon)
      paint_badge(badges_.center, icon_x, icon_y, icon_.width());

    // The other badges are flush against the edges of the canvas, except at the
    // top, where the badge is only 1dp higher than the base image.
    const int top_badge_y = icon_y - 1;
    if (badges_.top_left.icon)
      paint_badge(badges_.top_left, 0, top_badge_y);
    if (badges_.bottom_left.icon) {
      paint_badge(
          badges_.bottom_left, 0,
          height - gfx::GetDefaultSizeOfVectorIcon(*badges_.bottom_left.icon));
    }
    if (badges_.bottom_right.icon) {
      const int badge_size =
          gfx::GetDefaultSizeOfVectorIcon(*badges_.bottom_right.icon);
      paint_badge(badges_.bottom_right, width - badge_size,
                  height - badge_size);
    }
  }

  bool HasRepresentationAtAllScales() const override { return true; }

 private:
  NetworkIconImageSource(const gfx::ImageSkia& icon, const Badges& badges)
      : CanvasImageSource(GetSizeForBaseIconSize(icon.size()), false),
        icon_(icon),
        badges_(badges) {}
  ~NetworkIconImageSource() override {}

  static gfx::Size GetSizeForBaseIconSize(const gfx::Size& base_icon_size) {
    gfx::Size size = base_icon_size;
    const int badge_offset = base_icon_size.width() == kTrayIconSize
                                 ? kTrayIconBadgeOffset
                                 : kMenuIconBadgeOffset;
    size.Enlarge(badge_offset * 2, badge_offset * 2);
    return size;
  }

  const gfx::ImageSkia icon_;
  const Badges badges_;

  DISALLOW_COPY_AND_ASSIGN(NetworkIconImageSource);
};

//------------------------------------------------------------------------------
// Utilities for extracting icon images.

ImageType ImageTypeForNetworkType(const std::string& type) {
  if (NetworkTypePattern::WiFi().MatchesType(type))
    return ARCS;

  if (NetworkTypePattern::Mobile().MatchesType(type))
    return BARS;

  return NONE;
}

// Returns the network type, performing a check to see if Wi-Fi networks
// have an associated Tether network. Used to display the correct icon.
std::string GetEffectiveNetworkType(const NetworkState* network,
                                    IconType icon_type) {
  if (icon_type == ICON_TYPE_TRAY && network->type() == shill::kTypeWifi &&
      !network->tether_guid().empty()) {
    return chromeos::kTypeTether;
  }

  return network->type();
}

ImageType ImageTypeForNetwork(const NetworkState* network, IconType icon_type) {
  return ImageTypeForNetworkType(GetEffectiveNetworkType(network, icon_type));
}

gfx::ImageSkia GetImageForIndex(ImageType image_type,
                                IconType icon_type,
                                int index) {
  gfx::CanvasImageSource* source =
      new SignalStrengthImageSource(image_type, icon_type, index);
  return gfx::ImageSkia(source, source->size());
}

// Returns an image to represent either a fully connected network or a
// disconnected network.
const gfx::ImageSkia GetBasicImage(bool connected,
                                   IconType icon_type,
                                   const std::string& network_type) {
  DCHECK_NE(shill::kTypeVPN, network_type);
  return GetImageForIndex(ImageTypeForNetworkType(network_type), icon_type,
                          connected ? kNumNetworkImages - 1 : 0);
}

gfx::ImageSkia* ConnectingWirelessImage(ImageType image_type,
                                        IconType icon_type,
                                        double animation) {
  static const int kImageCount = kNumNetworkImages - 1;
  static gfx::ImageSkia* s_bars_images_dark[kImageCount];
  static gfx::ImageSkia* s_bars_images_light[kImageCount];
  static gfx::ImageSkia* s_arcs_images_dark[kImageCount];
  static gfx::ImageSkia* s_arcs_images_light[kImageCount];
  int index = animation * nextafter(static_cast<float>(kImageCount), 0);
  index = std::max(std::min(index, kImageCount - 1), 0);
  gfx::ImageSkia** images;
  bool dark = IconTypeIsDark(icon_type);
  if (image_type == BARS)
    images = dark ? s_bars_images_dark : s_bars_images_light;
  else
    images = dark ? s_arcs_images_dark : s_arcs_images_light;
  if (!images[index]) {
    // Lazily cache images.
    // TODO(estade): should the alpha be applied in SignalStrengthImageSource?
    gfx::ImageSkia source = GetImageForIndex(image_type, icon_type, index + 1);
    images[index] =
        new gfx::ImageSkia(gfx::ImageSkiaOperations::CreateTransparentImage(
            source, kConnectingImageAlpha));
  }
  return images[index];
}

gfx::ImageSkia ConnectingVpnImage(double animation) {
  float floored_animation_value =
      std::floor(animation * kNumFadeImages) / kNumFadeImages;
  return gfx::CreateVectorIcon(
      kNetworkVpnIcon,
      gfx::Tween::ColorValueBetween(
          floored_animation_value,
          SkColorSetA(kMenuIconColor, kConnectingImageAlpha), kMenuIconColor));
}

Badge ConnectingVpnBadge(double animation, IconType icon_type) {
  return {&kNetworkBadgeVpnIcon,
          SkColorSetA(GetDefaultColorForIconType(icon_type), 0xFF * animation)};
}

int StrengthIndex(int strength) {
  // Return an index in the range [1, kNumNetworkImages - 1].
  const float findex = (static_cast<float>(strength) / 100.0f) *
                       nextafter(static_cast<float>(kNumNetworkImages - 1), 0);
  int index = 1 + static_cast<int>(findex);
  index = std::max(std::min(index, kNumNetworkImages - 1), 1);
  return index;
}

Badge BadgeForNetworkTechnology(const NetworkState* network,
                                IconType icon_type) {
  Badge badge = {nullptr, GetDefaultColorForIconType(icon_type)};
  const std::string& technology = network->network_technology();
  if (technology == shill::kNetworkTechnologyEvdo) {
    badge.icon = &kNetworkBadgeTechnologyEvdoIcon;
  } else if (technology == shill::kNetworkTechnology1Xrtt) {
    badge.icon = &kNetworkBadgeTechnology1xIcon;
  } else if (technology == shill::kNetworkTechnologyGprs ||
             technology == shill::kNetworkTechnologyGsm) {
    badge.icon = &kNetworkBadgeTechnologyGprsIcon;
  } else if (technology == shill::kNetworkTechnologyEdge) {
    badge.icon = &kNetworkBadgeTechnologyEdgeIcon;
  } else if (technology == shill::kNetworkTechnologyUmts) {
    badge.icon = &kNetworkBadgeTechnology3gIcon;
  } else if (technology == shill::kNetworkTechnologyHspa) {
    badge.icon = &kNetworkBadgeTechnologyHspaIcon;
  } else if (technology == shill::kNetworkTechnologyHspaPlus) {
    badge.icon = &kNetworkBadgeTechnologyHspaPlusIcon;
  } else if (technology == shill::kNetworkTechnologyLte) {
    badge.icon = &kNetworkBadgeTechnologyLteIcon;
  } else if (technology == shill::kNetworkTechnologyLteAdvanced) {
    badge.icon = &kNetworkBadgeTechnologyLteAdvancedIcon;
  } else {
    return {};
  }
  return badge;
}

gfx::ImageSkia GetIcon(const NetworkState* network,
                       IconType icon_type,
                       int strength_index) {
  if (network->Matches(NetworkTypePattern::Ethernet())) {
    DCHECK_NE(ICON_TYPE_TRAY, icon_type);
    return gfx::CreateVectorIcon(kNetworkEthernetIcon,
                                 GetDefaultColorForIconType(ICON_TYPE_LIST));
  } else if (network->Matches(NetworkTypePattern::Wireless())) {
    DCHECK(strength_index > 0);
    return GetImageForIndex(ImageTypeForNetwork(network, icon_type), icon_type,
                            strength_index);
  } else if (network->Matches(NetworkTypePattern::VPN())) {
    DCHECK_NE(ICON_TYPE_TRAY, icon_type);
    return gfx::CreateVectorIcon(kNetworkVpnIcon,
                                 GetDefaultColorForIconType(ICON_TYPE_LIST));
  }

  NOTREACHED() << "Request for icon for unsupported type: " << network->type();
  return gfx::ImageSkia();
}

//------------------------------------------------------------------------------
// Get connecting images

gfx::ImageSkia GetConnectingVpnImage(IconType icon_type) {
  NetworkStateHandler* handler = NetworkHandler::Get()->network_state_handler();
  const NetworkState* connected_network = nullptr;
  if (icon_type == ICON_TYPE_TRAY) {
    connected_network =
        handler->ConnectedNetworkByType(NetworkTypePattern::NonVirtual());
  }
  double animation = NetworkIconAnimation::GetInstance()->GetAnimation();

  gfx::ImageSkia icon;
  Badges badges;
  if (connected_network) {
    icon = GetImageForNetwork(connected_network, icon_type);
    badges.bottom_left = ConnectingVpnBadge(animation, icon_type);
  } else {
    icon = ConnectingVpnImage(animation);
  }
  return NetworkIconImageSource::CreateImage(icon, badges);
}

gfx::ImageSkia GetConnectingImage(IconType icon_type,
                                  const std::string& network_type) {
  if (network_type == shill::kTypeVPN)
    return GetConnectingVpnImage(icon_type);

  ImageType image_type = ImageTypeForNetworkType(network_type);
  double animation = NetworkIconAnimation::GetInstance()->GetAnimation();

  return NetworkIconImageSource::CreateImage(
      *ConnectingWirelessImage(image_type, icon_type, animation), Badges());
}

}  // namespace

//------------------------------------------------------------------------------
// NetworkIconImpl

NetworkIconImpl::NetworkIconImpl(const std::string& path,
                                 IconType icon_type,
                                 const std::string& network_type)
    : network_path_(path),
      icon_type_(icon_type),
      strength_index_(-1),
      behind_captive_portal_(false) {
  // Default image is null.
}

void NetworkIconImpl::Update(const NetworkState* network) {
  DCHECK(network);
  // Determine whether or not we need to update the icon.
  bool dirty = image_.isNull();

  // If the network state has changed, the icon needs updating.
  if (state_ != network->connection_state()) {
    state_ = network->connection_state();
    dirty = true;
  }

  dirty |= UpdatePortalState(network);

  if (network->Matches(NetworkTypePattern::Wireless())) {
    dirty |= UpdateWirelessStrengthIndex(network);
  }

  if (network->Matches(NetworkTypePattern::Cellular()))
    dirty |= UpdateCellularState(network);

  if (IconTypeHasVPNBadge(icon_type_) &&
      network->Matches(NetworkTypePattern::NonVirtual())) {
    dirty |= UpdateVPNBadge();
  }

  if (dirty) {
    // Set the icon and badges based on the network and generate the image.
    GenerateImage(network);
  }
}

bool NetworkIconImpl::UpdateWirelessStrengthIndex(const NetworkState* network) {
  int index = StrengthIndex(network->signal_strength());
  if (index != strength_index_) {
    strength_index_ = index;
    return true;
  }
  return false;
}

bool NetworkIconImpl::UpdateCellularState(const NetworkState* network) {
  bool dirty = false;
  const Badge technology_badge = BadgeForNetworkTechnology(network, icon_type_);
  if (technology_badge == technology_badge_) {
    technology_badge_ = technology_badge;
    dirty = true;
  }
  std::string roaming_state = network->roaming();
  if (roaming_state != roaming_state_) {
    roaming_state_ = roaming_state;
    dirty = true;
  }
  return dirty;
}

bool NetworkIconImpl::UpdatePortalState(const NetworkState* network) {
  bool behind_captive_portal = false;
  if (network && chromeos::network_portal_detector::IsInitialized()) {
    NetworkPortalDetector::CaptivePortalState state =
        chromeos::network_portal_detector::GetInstance()->GetCaptivePortalState(
            network->guid());
    behind_captive_portal =
        state.status == NetworkPortalDetector::CAPTIVE_PORTAL_STATUS_PORTAL;
  }

  if (behind_captive_portal == behind_captive_portal_)
    return false;
  behind_captive_portal_ = behind_captive_portal;
  return true;
}

bool NetworkIconImpl::UpdateVPNBadge() {
  const NetworkState* vpn =
      NetworkHandler::Get()->network_state_handler()->ConnectedNetworkByType(
          NetworkTypePattern::VPN());
  Badge vpn_badge = {};
  if (vpn)
    vpn_badge = {&kNetworkBadgeVpnIcon, GetDefaultColorForIconType(icon_type_)};
  if (vpn_badge != vpn_badge_) {
    vpn_badge_ = vpn_badge;
    return true;
  }
  return false;
}

void NetworkIconImpl::GetBadges(const NetworkState* network, Badges* badges) {
  DCHECK(network);

  const std::string& type = network->type();
  const SkColor icon_color = GetDefaultColorForIconType(icon_type_);
  if (type == shill::kTypeWifi) {
    if (network->security_class() != shill::kSecurityNone &&
        IconTypeIsDark(icon_type_)) {
      badges->bottom_right = {&kNetworkBadgeSecureIcon, icon_color};
    }
  } else if (type == shill::kTypeWimax) {
    technology_badge_ = {&kNetworkBadgeTechnology4gIcon, icon_color};
  } else if (type == shill::kTypeCellular) {
    if (network->roaming() == shill::kRoamingStateRoaming) {
      // For networks that are always in roaming don't show roaming badge.
      const DeviceState* device =
          NetworkHandler::Get()->network_state_handler()->GetDeviceState(
              network->device_path());
      LOG_IF(WARNING, !device) << "Could not find device state for "
                               << network->device_path();
      if (!device || !device->provider_requires_roaming()) {
        badges->bottom_right = {&kNetworkBadgeRoamingIcon, icon_color};
      }
    }
  }
  if (!network->IsConnectingState()) {
    badges->top_left = technology_badge_;
    badges->bottom_left = vpn_badge_;
  }

  if (behind_captive_portal_) {
    badges->bottom_right = {&kNetworkBadgeCaptivePortalIcon, icon_color};
  }
}

void NetworkIconImpl::GenerateImage(const NetworkState* network) {
  DCHECK(network);
  gfx::ImageSkia icon = GetIcon(network, icon_type_, strength_index_);
  Badges badges;
  GetBadges(network, &badges);
  image_ = NetworkIconImageSource::CreateImage(icon, badges);
}

namespace {

NetworkIconImpl* FindAndUpdateImageImpl(const NetworkState* network,
                                        IconType icon_type) {
  // Find or add the icon.
  NetworkIconMap* icon_map = GetIconMap(icon_type);
  NetworkIconImpl* icon;
  NetworkIconMap::iterator iter = icon_map->find(network->path());
  if (iter == icon_map->end()) {
    icon = new NetworkIconImpl(network->path(), icon_type,
                               GetEffectiveNetworkType(network, icon_type));
    icon_map->insert(std::make_pair(network->path(), icon));
  } else {
    icon = iter->second;
  }

  // Update and return the icon's image.
  icon->Update(network);
  return icon;
}

}  // namespace

//------------------------------------------------------------------------------
// SignalStrengthImageSource

SignalStrengthImageSource::SignalStrengthImageSource(ImageType image_type,
                                                     SkColor color,
                                                     const gfx::Size& size,
                                                     int signal_strength)
    : CanvasImageSource(size, false),
      image_type_(image_type /* is_opaque */),
      color_(color),
      signal_strength_(signal_strength) {
  if (image_type_ == NONE)
    image_type_ = ARCS;

  DCHECK_GE(signal_strength, 0);
  DCHECK_LT(signal_strength, kNumNetworkImages);
}

SignalStrengthImageSource::SignalStrengthImageSource(ImageType image_type,
                                                     IconType icon_type,
                                                     int signal_strength)
    : SignalStrengthImageSource(image_type,
                                GetDefaultColorForIconType(icon_type),
                                GetSizeForIconType(icon_type),
                                signal_strength) {}

SignalStrengthImageSource::~SignalStrengthImageSource() {}

void SignalStrengthImageSource::set_color(SkColor color) {
  color_ = color;
}

// gfx::CanvasImageSource:
void SignalStrengthImageSource::Draw(gfx::Canvas* canvas) {
  if (image_type_ == ARCS)
    DrawArcs(canvas);
  else
    DrawBars(canvas);
}

bool SignalStrengthImageSource::HasRepresentationAtAllScales() const {
  return true;
}

gfx::Size SignalStrengthImageSource::GetSizeForIconType(IconType icon_type) {
  int side = icon_type == ICON_TYPE_TRAY ? kTrayIconSize : kMenuIconSize;
  return gfx::Size(side, side);
}

void SignalStrengthImageSource::DrawArcs(gfx::Canvas* canvas) {
  gfx::RectF oval_bounds((gfx::Rect(size())));
  oval_bounds.Inset(gfx::Insets(kSignalStrengthImageInset));
  // Double the width and height. The new midpoint should be the former
  // bottom center.
  oval_bounds.Inset(-oval_bounds.width() / 2, 0, -oval_bounds.width() / 2,
                    -oval_bounds.height());

  const SkScalar kAngleAboveHorizontal = 51.f;
  const SkScalar kStartAngle = 180.f + kAngleAboveHorizontal;
  const SkScalar kSweepAngle = 180.f - 2 * kAngleAboveHorizontal;

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  // Background. Skip drawing for full signal.
  if (signal_strength_ != kNumNetworkImages - 1) {
    flags.setColor(SkColorSetA(color_, kSignalStrengthImageBgAlpha));
    canvas->sk_canvas()->drawArc(gfx::RectFToSkRect(oval_bounds), kStartAngle,
                                 kSweepAngle, true, flags);
  }
  // Foreground (signal strength).
  if (signal_strength_ != 0) {
    flags.setColor(color_);
    // Percent of the height of the background wedge that we draw the
    // foreground wedge, indexed by signal strength.
    static constexpr float kWedgeHeightPercentages[] = {0.f, 0.375f, 0.5833f,
                                                        0.75f, 1.f};
    const float wedge_percent = kWedgeHeightPercentages[signal_strength_];
    oval_bounds.Inset(
        gfx::InsetsF((oval_bounds.height() / 2) * (1.f - wedge_percent)));
    canvas->sk_canvas()->drawArc(gfx::RectFToSkRect(oval_bounds), kStartAngle,
                                 kSweepAngle, true, flags);
  }
}

void SignalStrengthImageSource::DrawBars(gfx::Canvas* canvas) {
  // Undo the canvas's device scaling and round values to the nearest whole
  // number so we can draw on exact pixel boundaries.
  const float dsf = canvas->UndoDeviceScaleFactor();
  auto scale = [dsf](SkScalar dimension) {
    return std::round(dimension * dsf);
  };

  // Length of short side of an isosceles right triangle, in dip.
  const SkScalar kFullTriangleSide =
      SkIntToScalar(size().width()) - kSignalStrengthImageInset * 2;

  auto make_triangle = [scale, kFullTriangleSide](SkScalar side) {
    SkPath triangle;
    triangle.moveTo(scale(kSignalStrengthImageInset),
                    scale(kSignalStrengthImageInset + kFullTriangleSide));
    triangle.rLineTo(scale(side), 0);
    triangle.rLineTo(0, -scale(side));
    triangle.close();
    return triangle;
  };

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  // Background. Skip drawing for full signal.
  if (signal_strength_ != kNumNetworkImages - 1) {
    flags.setColor(SkColorSetA(color_, kSignalStrengthImageBgAlpha));
    canvas->DrawPath(make_triangle(kFullTriangleSide), flags);
  }
  // Foreground (signal strength).
  if (signal_strength_ != 0) {
    flags.setColor(color_);
    // As a percentage of the bg triangle, the length of one of the short
    // sides of the fg triangle, indexed by signal strength.
    static constexpr float kTriangleSidePercents[] = {0.f, 0.5f, 0.625f, 0.75f,
                                                      1.f};
    canvas->DrawPath(make_triangle(kTriangleSidePercents[signal_strength_] *
                                   kFullTriangleSide),
                     flags);
  }
}

//------------------------------------------------------------------------------
// Public interface

gfx::ImageSkia GetImageForNetwork(const NetworkState* network,
                                  IconType icon_type) {
  DCHECK(network);
  const std::string network_type = GetEffectiveNetworkType(network, icon_type);

  if (!network->visible())
    return GetBasicImage(false /* is_connected */, icon_type, network_type);

  if (network->IsConnectingState())
    return GetConnectingImage(icon_type, network_type);

  NetworkIconImpl* icon = FindAndUpdateImageImpl(network, icon_type);
  return icon->image();
}

gfx::ImageSkia GetImageForWiFiEnabledState(bool enabled, IconType icon_type) {
  gfx::ImageSkia image =
      GetBasicImage(true /* connected */, icon_type, shill::kTypeWifi);
  Badges badges;
  if (!enabled) {
    badges.center = {&kNetworkBadgeOffIcon,
                     GetDefaultColorForIconType(icon_type)};
  }
  return NetworkIconImageSource::CreateImage(image, badges);
}

gfx::ImageSkia GetImageForDisconnectedCellNetwork() {
  return GetBasicImage(false /* not connected */, ICON_TYPE_LIST,
                       shill::kTypeCellular);
}

gfx::ImageSkia GetImageForNewWifiNetwork(SkColor icon_color,
                                         SkColor badge_color) {
  SignalStrengthImageSource* source =
      new SignalStrengthImageSource(ImageTypeForNetworkType(shill::kTypeWifi),
                                    ICON_TYPE_LIST, kNumNetworkImages - 1);
  source->set_color(icon_color);
  gfx::ImageSkia icon = gfx::ImageSkia(source, source->size());
  Badges badges;
  badges.bottom_right = {&kNetworkBadgeAddOtherIcon, badge_color};
  return NetworkIconImageSource::CreateImage(icon, badges);
}

base::string16 GetLabelForNetwork(const chromeos::NetworkState* network,
                                  IconType icon_type) {
  DCHECK(network);
  std::string activation_state = network->activation_state();
  if (icon_type == ICON_TYPE_LIST || icon_type == ICON_TYPE_MENU_LIST) {
    // Show "<network>: [Connecting|Activating|Reconnecting]..."
    // TODO(varkha): Remaining states should migrate to secondary status in the
    // network item and no longer be part of the label.
    // See http://crbug.com/676181 .
    if (network->IsReconnecting()) {
      return l10n_util::GetStringFUTF16(
          IDS_ASH_STATUS_TRAY_NETWORK_LIST_RECONNECTING,
          base::UTF8ToUTF16(network->name()));
    }
    if (icon_type != ICON_TYPE_MENU_LIST && network->IsConnectingState()) {
      return l10n_util::GetStringFUTF16(
          IDS_ASH_STATUS_TRAY_NETWORK_LIST_CONNECTING,
          base::UTF8ToUTF16(network->name()));
    }
    if (activation_state == shill::kActivationStateActivating) {
      return l10n_util::GetStringFUTF16(
          IDS_ASH_STATUS_TRAY_NETWORK_LIST_ACTIVATING,
          base::UTF8ToUTF16(network->name()));
    }
    // Show "Activate <network>" in list view only.
    if (activation_state == shill::kActivationStateNotActivated ||
        activation_state == shill::kActivationStatePartiallyActivated) {
      return l10n_util::GetStringFUTF16(
          IDS_ASH_STATUS_TRAY_NETWORK_LIST_ACTIVATE,
          base::UTF8ToUTF16(network->name()));
    }
  } else {
    // Show "[Connected to|Connecting to|Activating|Reconnecting to] <network>"
    // (non-list view).
    if (network->IsReconnecting()) {
      return l10n_util::GetStringFUTF16(
          IDS_ASH_STATUS_TRAY_NETWORK_RECONNECTING,
          base::UTF8ToUTF16(network->name()));
    }
    if (network->IsConnectedState()) {
      return l10n_util::GetStringFUTF16(IDS_ASH_STATUS_TRAY_NETWORK_CONNECTED,
                                        base::UTF8ToUTF16(network->name()));
    }
    if (network->IsConnectingState()) {
      return l10n_util::GetStringFUTF16(IDS_ASH_STATUS_TRAY_NETWORK_CONNECTING,
                                        base::UTF8ToUTF16(network->name()));
    }
    if (activation_state == shill::kActivationStateActivating) {
      return l10n_util::GetStringFUTF16(IDS_ASH_STATUS_TRAY_NETWORK_ACTIVATING,
                                        base::UTF8ToUTF16(network->name()));
    }
  }

  // Otherwise just show the network name or 'Ethernet'.
  if (network->Matches(NetworkTypePattern::Ethernet())) {
    return l10n_util::GetStringUTF16(IDS_ASH_STATUS_TRAY_ETHERNET);
  } else {
    return base::UTF8ToUTF16(network->name());
  }
}

int GetMobileUninitializedMsg() {
  static base::Time s_uninitialized_state_time;
  static int s_uninitialized_msg(0);

  NetworkStateHandler* handler = NetworkHandler::Get()->network_state_handler();

  // Never show messages if the list of Mobile networks is non-empty.
  NetworkStateHandler::NetworkStateList mobile_networks;
  handler->GetVisibleNetworkListByType(chromeos::NetworkTypePattern::Mobile(),
                                       &mobile_networks);
  if (!mobile_networks.empty())
    return 0;

  // TODO(lesliewatkins): Only return this message when Tether is uninitialized
  // due to no Bluetooth (dependent on codereview.chromium.org/2969493002/).
  if (handler->GetTechnologyState(NetworkTypePattern::Tether()) ==
      NetworkStateHandler::TECHNOLOGY_UNINITIALIZED) {
    s_uninitialized_msg = IDS_ASH_STATUS_TRAY_ENABLE_BLUETOOTH;
    s_uninitialized_state_time = base::Time::Now();
    return s_uninitialized_msg;
  }

  if (handler->GetTechnologyState(NetworkTypePattern::Cellular()) ==
      NetworkStateHandler::TECHNOLOGY_UNINITIALIZED) {
    s_uninitialized_msg = IDS_ASH_STATUS_TRAY_INITIALIZING_CELLULAR;
    s_uninitialized_state_time = base::Time::Now();
    return s_uninitialized_msg;
  }

  if (handler->GetScanningByType(NetworkTypePattern::Cellular())) {
    s_uninitialized_msg = IDS_ASH_STATUS_TRAY_MOBILE_SCANNING;
    s_uninitialized_state_time = base::Time::Now();
    return s_uninitialized_msg;
  }

  // There can be a delay between leaving the Initializing state and when
  // a Mobile device shows up, so keep showing the initializing
  // animation for a bit to avoid flashing the disconnect icon.
  const int kInitializingDelaySeconds = 1;
  base::TimeDelta dtime = base::Time::Now() - s_uninitialized_state_time;
  if (dtime.InSeconds() < kInitializingDelaySeconds)
    return s_uninitialized_msg;
  return 0;
}

void GetDefaultNetworkImageAndLabel(IconType icon_type,
                                    gfx::ImageSkia* image,
                                    base::string16* label,
                                    bool* animating) {
  NetworkStateHandler* state_handler =
      NetworkHandler::Get()->network_state_handler();
  NetworkConnectionHandler* connect_handler =
      NetworkHandler::Get()->network_connection_handler();
  const NetworkState* connected_network =
      state_handler->ConnectedNetworkByType(NetworkTypePattern::NonVirtual());
  const NetworkState* connecting_network =
      state_handler->ConnectingNetworkByType(NetworkTypePattern::Wireless());
  if (!connecting_network && icon_type == ICON_TYPE_TRAY) {
    connecting_network =
        state_handler->ConnectingNetworkByType(NetworkTypePattern::VPN());
  }

  const NetworkState* network;
  // If we are connecting to a network, and there is either no connected
  // network, or the connection was user requested, or shill triggered a
  // reconnection, use the connecting network.
  if (connecting_network &&
      (!connected_network || connecting_network->IsReconnecting() ||
       connect_handler->HasConnectingNetwork(connecting_network->path()))) {
    network = connecting_network;
  } else {
    network = connected_network;
  }

  // Don't show ethernet in the tray
  if (icon_type == ICON_TYPE_TRAY && network &&
      network->Matches(NetworkTypePattern::Ethernet())) {
    *image = gfx::ImageSkia();
    *animating = false;
    return;
  }

  if (!network) {
    // If no connecting network, check if we are activating a network.
    const NetworkState* mobile_network =
        state_handler->FirstNetworkByType(NetworkTypePattern::Mobile());
    if (mobile_network && (mobile_network->activation_state() ==
                           shill::kActivationStateActivating)) {
      network = mobile_network;
    }
  }
  if (!network) {
    // If no connecting network, check for mobile initializing. Do not display
    // the message about enabling Bluetooth for Tether.
    int uninitialized_msg = GetMobileUninitializedMsg();
    if (uninitialized_msg != 0 &&
        uninitialized_msg != IDS_ASH_STATUS_TRAY_ENABLE_BLUETOOTH) {
      *image = GetConnectingImage(icon_type, shill::kTypeCellular);
      if (label)
        *label = l10n_util::GetStringUTF16(uninitialized_msg);
      *animating = true;
    } else {
      // Otherwise show a Wi-Fi icon. If Wi-Fi is disabled, show a full icon
      // with a strikethrough. If it's enabled then it's disconnected, so show
      // an empty wedge.
      bool wifi_enabled =
          NetworkHandler::Get()->network_state_handler()->IsTechnologyEnabled(
              NetworkTypePattern::WiFi());
      *image = wifi_enabled ? GetBasicImage(false /* not connected */,
                                            icon_type, shill::kTypeWifi)
                            : GetImageForWiFiEnabledState(
                                  false /* not enabled*/, icon_type);
      if (label) {
        *label = l10n_util::GetStringUTF16(
            IDS_ASH_STATUS_TRAY_NETWORK_NOT_CONNECTED);
      }
      *animating = false;
    }
    return;
  }
  *animating = network->IsConnectingState();
  // Get icon and label for connected or connecting network.
  *image = GetImageForNetwork(network, icon_type);
  if (label)
    *label = GetLabelForNetwork(network, icon_type);
}

void PurgeNetworkIconCache() {
  NetworkStateHandler::NetworkStateList networks;
  NetworkHandler::Get()->network_state_handler()->GetVisibleNetworkList(
      &networks);
  std::set<std::string> network_paths;
  for (NetworkStateHandler::NetworkStateList::iterator iter = networks.begin();
       iter != networks.end(); ++iter) {
    network_paths.insert((*iter)->path());
  }
  PurgeIconMap(ICON_TYPE_TRAY, network_paths);
  PurgeIconMap(ICON_TYPE_DEFAULT_VIEW, network_paths);
  PurgeIconMap(ICON_TYPE_LIST, network_paths);
  PurgeIconMap(ICON_TYPE_MENU_LIST, network_paths);
}

}  // namespace network_icon
}  // namespace ash
