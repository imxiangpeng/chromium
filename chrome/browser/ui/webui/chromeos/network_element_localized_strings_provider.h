// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROMEOS_NETWORK_ELEMENT_LOCALIZED_STRINGS_PROVIDER_H_
#define CHROME_BROWSER_UI_WEBUI_CHROMEOS_NETWORK_ELEMENT_LOCALIZED_STRINGS_PROVIDER_H_

namespace content {
class WebUIDataSource;
}

namespace chromeos {
namespace network_element {

// Adds the strings needed for network elements to |html_source|. String ids
// correspond to ids in ui/webui/resources/cr_elements/chromeos/network/.
void AddLocalizedStrings(content::WebUIDataSource* html_source);

}  // namespace network_element
}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_CHROMEOS_NETWORK_ELEMENT_LOCALIZED_STRINGS_PROVIDER_H_
