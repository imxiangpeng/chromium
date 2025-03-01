// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/media_router/mojo/media_router_struct_traits.h"

#include "chrome/common/media_router/media_source.h"
#include "net/interfaces/ip_address_struct_traits.h"
#include "url/mojo/url_gurl_struct_traits.h"

namespace mojo {

// static
bool StructTraits<media_router::mojom::IssueDataView, media_router::IssueInfo>::
    Read(media_router::mojom::IssueDataView data,
         media_router::IssueInfo* out) {
  if (!data.ReadTitle(&out->title))
    return false;

  if (!data.ReadDefaultAction(&out->default_action))
    return false;

  if (!data.ReadSeverity(&out->severity))
    return false;

  base::Optional<std::string> message;
  if (!data.ReadMessage(&message))
    return false;

  out->message = message.value_or(std::string());

  if (!data.ReadSecondaryActions(&out->secondary_actions))
    return false;

  base::Optional<std::string> route_id;
  if (!data.ReadRouteId(&route_id))
    return false;

  out->route_id = route_id.value_or(std::string());

  out->is_blocking = data.is_blocking();
  out->help_page_id = data.help_page_id();

  return true;
}

// static
media_router::mojom::MediaSinkExtraDataDataView::Tag
UnionTraits<media_router::mojom::MediaSinkExtraDataDataView,
            media_router::MediaSinkInternal>::
    GetTag(const media_router::MediaSinkInternal& sink) {
  if (sink.is_dial_sink()) {
    return media_router::mojom::MediaSinkExtraDataDataView::Tag::
        DIAL_MEDIA_SINK;
  } else if (sink.is_cast_sink()) {
    return media_router::mojom::MediaSinkExtraDataDataView::Tag::
        CAST_MEDIA_SINK;
  }
  NOTREACHED();
  return media_router::mojom::MediaSinkExtraDataDataView::Tag::CAST_MEDIA_SINK;
}

// static
bool StructTraits<media_router::mojom::MediaSinkDataView,
                  media_router::MediaSinkInternal>::
    Read(media_router::mojom::MediaSinkDataView data,
         media_router::MediaSinkInternal* out) {
  media_router::MediaSink::Id id;
  if (!data.ReadSinkId(&id) ||
      !media_router::MediaSinkInternal::IsValidSinkId(id)) {
    return false;
  }

  out->set_sink_id(id);

  std::string name;
  if (!data.ReadName(&name))
    return false;

  out->set_name(name);

  base::Optional<std::string> description;
  if (!data.ReadDescription(&description))
    return false;

  if (description)
    out->set_description(*description);

  base::Optional<std::string> domain;
  if (!data.ReadDomain(&domain))
    return false;

  if (domain)
    out->set_domain(*domain);

  media_router::SinkIconType icon_type;
  if (!data.ReadIconType(&icon_type))
    return false;

  out->set_icon_type(icon_type);

  if (!data.ReadExtraData(out))
    return false;

  return true;
}

// static
bool UnionTraits<media_router::mojom::MediaSinkExtraDataDataView,
                 media_router::MediaSinkInternal>::
    Read(media_router::mojom::MediaSinkExtraDataDataView data,
         media_router::MediaSinkInternal* out) {
  switch (data.tag()) {
    case media_router::mojom::MediaSinkExtraDataDataView::Tag::
        DIAL_MEDIA_SINK: {
      media_router::DialSinkExtraData extra_data;
      if (!data.ReadDialMediaSink(&extra_data))
        return false;
      out->set_dial_data(extra_data);
      return true;
    }
    case media_router::mojom::MediaSinkExtraDataDataView::Tag::
        CAST_MEDIA_SINK: {
      media_router::CastSinkExtraData extra_data;
      if (!data.ReadCastMediaSink(&extra_data))
        return false;
      out->set_cast_data(extra_data);
      return true;
    }
  }
  NOTREACHED();
  return false;
}

// static
bool StructTraits<media_router::mojom::DialMediaSinkDataView,
                  media_router::DialSinkExtraData>::
    Read(media_router::mojom::DialMediaSinkDataView data,
         media_router::DialSinkExtraData* out) {
  if (!data.ReadIpAddress(&out->ip_address))
    return false;

  if (!data.ReadModelName(&out->model_name))
    return false;

  if (!data.ReadAppUrl(&out->app_url))
    return false;

  return true;
}

// static
bool StructTraits<media_router::mojom::CastMediaSinkDataView,
                  media_router::CastSinkExtraData>::
    Read(media_router::mojom::CastMediaSinkDataView data,
         media_router::CastSinkExtraData* out) {
  if (!data.ReadIpAddress(&out->ip_address))
    return false;

  if (!data.ReadModelName(&out->model_name))
    return false;

  out->capabilities = data.capabilities();
  out->cast_channel_id = data.cast_channel_id();

  return true;
}

bool StructTraits<media_router::mojom::MediaRouteDataView,
                  media_router::MediaRoute>::
    Read(media_router::mojom::MediaRouteDataView data,
         media_router::MediaRoute* out) {
  media_router::MediaRoute::Id media_route_id;
  if (!data.ReadMediaRouteId(&media_route_id))
    return false;

  out->set_media_route_id(media_route_id);

  base::Optional<media_router::MediaSource::Id> media_source_id;
  if (!data.ReadMediaSource(&media_source_id))
    return false;

  if (media_source_id)
    out->set_media_source(media_router::MediaSource(*media_source_id));

  media_router::MediaSink::Id media_sink_id;
  if (!data.ReadMediaSinkId(&media_sink_id))
    return false;

  out->set_media_sink_id(media_sink_id);

  std::string description;
  if (!data.ReadDescription(&description))
    return false;

  out->set_description(description);

  base::Optional<std::string> custom_controller_path;
  if (!data.ReadCustomControllerPath(&custom_controller_path))
    return false;

  if (custom_controller_path)
    out->set_custom_controller_path(*custom_controller_path);

  out->set_local(data.is_local());
  out->set_supports_media_route_controller(
      data.supports_media_route_controller());
  out->set_for_display(data.for_display());
  out->set_incognito(data.is_incognito());
  out->set_offscreen_presentation(data.is_offscreen_presentation());

  return true;
}

}  // namespace mojo
