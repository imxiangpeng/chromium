// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXTENSIONS_SHELL_BROWSER_SHELL_APP_DELEGATE_H_
#define EXTENSIONS_SHELL_BROWSER_SHELL_APP_DELEGATE_H_

#include "base/macros.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/app_window/app_delegate.h"

namespace extensions {

// AppDelegate implementation for app_shell. Sets focus after the WebContents is
// created. Ignores most operations that would create a new dialog or window.
class ShellAppDelegate : public AppDelegate {
 public:
  ShellAppDelegate();
  ~ShellAppDelegate() override;

  // AppDelegate overrides:
  void InitWebContents(content::WebContents* web_contents) override;
  void RenderViewCreated(content::RenderViewHost* render_view_host) override;
  void ResizeWebContents(content::WebContents* web_contents,
                         const gfx::Size& size) override;
  content::WebContents* OpenURLFromTab(
      content::BrowserContext* context,
      content::WebContents* source,
      const content::OpenURLParams& params) override;
  void AddNewContents(content::BrowserContext* context,
                      content::WebContents* new_contents,
                      WindowOpenDisposition disposition,
                      const gfx::Rect& initial_rect,
                      bool user_gesture,
                      bool* was_blocked) override;
  content::ColorChooser* ShowColorChooser(content::WebContents* web_contents,
                                          SkColor initial_color) override;
  void RunFileChooser(content::RenderFrameHost* render_frame_host,
                      const content::FileChooserParams& params) override;
  void RequestMediaAccessPermission(
      content::WebContents* web_contents,
      const content::MediaStreamRequest& request,
      const content::MediaResponseCallback& callback,
      const Extension* extension) override;
  bool CheckMediaAccessPermission(content::WebContents* web_contents,
                                  const GURL& security_origin,
                                  content::MediaStreamType type,
                                  const Extension* extension) override;
  int PreferredIconSize() const override;
  void SetWebContentsBlocked(content::WebContents* web_contents,
                             bool blocked) override;
  bool IsWebContentsVisible(content::WebContents* web_contents) override;
  void SetTerminatingCallback(const base::Closure& callback) override;
  void OnHide() override {}
  void OnShow() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(ShellAppDelegate);
};

}  // namespace extensions

#endif  // EXTENSIONS_SHELL_BROWSER_SHELL_APP_DELEGATE_H_
