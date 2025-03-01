// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_WEB_PUBLIC_WEB_STATE_WEB_STATE_H_
#define IOS_WEB_PUBLIC_WEB_STATE_WEB_STATE_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/callback_forward.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "ios/web/public/referrer.h"
#include "ios/web/public/web_state/url_verification_constants.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/geometry/size.h"
#include "url/gurl.h"

class GURL;

@class CRWJSInjectionReceiver;
@class CRWSessionStorage;
@protocol CRWScrollableContent;
@protocol CRWWebViewProxy;
typedef id<CRWWebViewProxy> CRWWebViewProxyType;
@class UIView;
typedef UIView<CRWScrollableContent> CRWContentView;

namespace base {
class DictionaryValue;
class Value;
}

namespace gfx {
class Image;
}

namespace web {

class BrowserState;
class NavigationManager;
class SessionCertificatePolicyCache;
class WebInterstitial;
class WebStateDelegate;
class WebStateInterfaceProvider;
class WebStateObserver;
class WebStatePolicyDecider;
class WebStateWeakPtrFactory;

// Core interface for interaction with the web.
class WebState : public base::SupportsUserData {
 public:
  // Parameters for the Create() method.
  struct CreateParams {
    explicit CreateParams(web::BrowserState* browser_state);
    ~CreateParams();

    // The corresponding BrowserState for the new WebState.
    web::BrowserState* browser_state;

    // Whether the WebState is created as the result of a window.open or by
    // clicking a link with a blank target.  Used to determine whether the
    // WebState is allowed to be closed via window.close().
    bool created_with_opener;
  };

  // Parameters for the OpenURL() method.
  struct OpenURLParams {
    OpenURLParams(const GURL& url,
                  const Referrer& referrer,
                  WindowOpenDisposition disposition,
                  ui::PageTransition transition,
                  bool is_renderer_initiated);
    ~OpenURLParams();

    // The URL/referrer to be opened.
    GURL url;
    Referrer referrer;

    // The disposition requested by the navigation source.
    WindowOpenDisposition disposition;

    // The transition type of navigation.
    ui::PageTransition transition;

    // Whether this navigation is initiated by the renderer process.
    bool is_renderer_initiated;
  };

  // Creates a new WebState.
  static std::unique_ptr<WebState> Create(const CreateParams& params);

  // Creates a new WebState from a serialized representation of the session.
  // |session_storage| must not be nil.
  static std::unique_ptr<WebState> CreateWithStorageSession(
      const CreateParams& params,
      CRWSessionStorage* session_storage);

  ~WebState() override {}

  // Gets/Sets the delegate.
  virtual WebStateDelegate* GetDelegate() = 0;
  virtual void SetDelegate(WebStateDelegate* delegate) = 0;

  // Whether or not a web view is allowed to exist in this WebState. Defaults
  // to false; this should be enabled before attempting to access the view.
  virtual bool IsWebUsageEnabled() const = 0;
  virtual void SetWebUsageEnabled(bool enabled) = 0;

  // Whether or not JavaScript dialogs and window open requests
  // should be suppressed. Default is false. When dialog is suppressed
  // |WebStateObserver::DidSuppressDialog| will be called.
  virtual bool ShouldSuppressDialogs() const = 0;
  virtual void SetShouldSuppressDialogs(bool should_suppress) = 0;

  // The view containing the contents of the current web page. If the view has
  // been purged due to low memory, this will recreate it. It is up to the
  // caller to size the view.
  virtual UIView* GetView() = 0;

  // Gets the BrowserState associated with this WebState. Can never return null.
  virtual BrowserState* GetBrowserState() const = 0;

  // Opens a URL with the given disposition.  The transition specifies how this
  // navigation should be recorded in the history system (for example, typed).
  virtual void OpenURL(const OpenURLParams& params) = 0;

  // Stops any pending navigation.
  virtual void Stop() = 0;

  // Gets the NavigationManager associated with this WebState. Can never return
  // null.
  virtual const NavigationManager* GetNavigationManager() const = 0;
  virtual NavigationManager* GetNavigationManager() = 0;

  // Gets the SessionCertificatePolicyCache for this WebState.  Can never return
  // null.
  virtual const SessionCertificatePolicyCache*
  GetSessionCertificatePolicyCache() const = 0;
  virtual SessionCertificatePolicyCache* GetSessionCertificatePolicyCache() = 0;

  // Creates a serializable representation of the session. The returned value
  // is autoreleased.
  virtual CRWSessionStorage* BuildSessionStorage() = 0;

  // Gets the CRWJSInjectionReceiver associated with this WebState.
  virtual CRWJSInjectionReceiver* GetJSInjectionReceiver() const = 0;

  // Runs JavaScript in the main frame's context. If a callback is provided, it
  // will be used to return the result, when the result is available or script
  // execution has failed due to an error.
  // NOTE: Integer values will be returned as Type::DOUBLE because of underlying
  // library limitation.
  typedef base::Callback<void(const base::Value*)> JavaScriptResultCallback;
  virtual void ExecuteJavaScript(const base::string16& javascript) = 0;
  virtual void ExecuteJavaScript(const base::string16& javascript,
                                 const JavaScriptResultCallback& callback) = 0;

  // Asynchronously executes |javaScript| in the main frame's context,
  // registering user interaction.
  virtual void ExecuteUserJavaScript(NSString* javaScript) = 0;

  // Gets the contents MIME type.
  virtual const std::string& GetContentsMimeType() const = 0;

  // Gets the value of the "Content-Language" HTTP header.
  virtual const std::string& GetContentLanguageHeader() const = 0;

  // Returns true if the current page is a web view with HTML.
  virtual bool ContentIsHTML() const = 0;

  // Returns the current navigation title. This could be the title of the page
  // if it is available or the URL.
  virtual const base::string16& GetTitle() const = 0;

  // Returns true if the current page is loading.
  virtual bool IsLoading() const = 0;

  // The fraction of the page load that has completed as a number between 0.0
  // (nothing loaded) and 1.0 (fully loaded).
  virtual double GetLoadingProgress() const = 0;

  // Whether this instance is in the process of being destroyed.
  virtual bool IsBeingDestroyed() const = 0;

  // Gets the URL currently being displayed in the URL bar, if there is one.
  // This URL might be a pending navigation that hasn't committed yet, so it is
  // not guaranteed to match the current page in this WebState. A typical
  // example of this is interstitials, which show the URL of the new/loading
  // page (active) but the security context is of the old page (last committed).
  virtual const GURL& GetVisibleURL() const = 0;

  // Gets the last committed URL. It represents the current page that is
  // displayed in this WebState. It represents the current security context.
  virtual const GURL& GetLastCommittedURL() const = 0;

  // Returns the WebState view of the current URL. Moreover, this method
  // will set the trustLevel enum to the appropriate level from a security point
  // of view. The caller has to handle the case where |trust_level| is not
  // appropriate.
  // TODO(stuartmorgan): Figure out a clean API for this.
  // See http://crbug.com/457679
  virtual GURL GetCurrentURL(URLVerificationTrustLevel* trust_level) const = 0;

  // Resizes |content_view| to the content area's size and adds it to the
  // hierarchy.  A navigation will remove the view from the hierarchy.
  virtual void ShowTransientContentView(CRWContentView* content_view) = 0;

  // Returns true if a WebInterstitial is currently displayed.
  virtual bool IsShowingWebInterstitial() const = 0;

  // Returns the currently visible WebInterstitial if one is shown.
  virtual WebInterstitial* GetWebInterstitial() const = 0;

  // Called when the WebState has displayed a password field on an HTTP page.
  // This method modifies the appropriate NavigationEntry's SSLStatus to record
  // the sensitive input field, so that embedders can adjust the UI if desired.
  virtual void OnPasswordInputShownOnHttp() = 0;

  // Called when the WebState has displayed a credit card field on an HTTP page.
  // This method modifies the appropriate NavigationEntry's SSLStatus to record
  // the sensitive input field, so that embedders can adjust the UI if desired.
  virtual void OnCreditCardInputShownOnHttp() = 0;

  // Callback used to handle script commands.
  // The callback must return true if the command was handled, and false
  // otherwise.
  // In particular the callback must return false if the command is unexpected
  // or ill-formatted.
  // The first parameter is the content of the command, the second parameter is
  // the URL of the page, and the third parameter is a bool indicating if the
  // user is currently interacting with the page.
  typedef base::Callback<bool(const base::DictionaryValue&, const GURL&, bool)>
      ScriptCommandCallback;

  // Registers a callback that will be called when a command matching
  // |command_prefix| is received.
  virtual void AddScriptCommandCallback(const ScriptCommandCallback& callback,
                                        const std::string& command_prefix) = 0;

  // Removes the callback associated with |command_prefix|.
  virtual void RemoveScriptCommandCallback(
      const std::string& command_prefix) = 0;

  // Returns the current CRWWebViewProxy object.
  virtual CRWWebViewProxyType GetWebViewProxy() const = 0;

  // Returns Mojo interface registry for this WebState.
  virtual WebStateInterfaceProvider* GetWebStateInterfaceProvider() = 0;

 protected:
  // Binds |interface_pipe| to an implementation of |interface_name| that is
  // scoped to this WebState instance (if that such an implementation is
  // present). Embedders of //ios/web can inject interface implementations by
  // overriding WebClient::BindInterfaceRequestFromMainFrame().
  // NOTE: Callers should use the more-friendly wrapper below.
  virtual void BindInterfaceRequestFromMainFrame(
      const std::string& interface_name,
      mojo::ScopedMessagePipeHandle interface_pipe) {}

 public:
  template <class Interface>
  void BindInterfaceRequestFromMainFrame(
      mojo::InterfaceRequest<Interface> request) {
    BindInterfaceRequestFromMainFrame(Interface::Name_,
                                      std::move(request.PassMessagePipe()));
  }

  // Returns whether this WebState was created with an opener.  See
  // CreateParams::created_with_opener for more details.
  virtual bool HasOpener() const = 0;

  // Callback used to handle snapshots. The parameter is the snapshot image.
  typedef base::Callback<void(const gfx::Image&)> SnapshotCallback;

  // Takes a snapshot of this WebState with |target_size|. |callback| is
  // asynchronously invoked after performing the snapshot.
  virtual void TakeSnapshot(const SnapshotCallback& callback,
                            CGSize target_size) const = 0;

 protected:
  friend class WebStateObserver;
  friend class WebStatePolicyDecider;

  // Adds and removes observers for page navigation notifications. The order in
  // which notifications are sent to observers is undefined. Clients must be
  // sure to remove the observer before they go away.
  // TODO(droger): Move these methods to WebStateImpl once it is in ios/.
  virtual void AddObserver(WebStateObserver* observer) = 0;
  virtual void RemoveObserver(WebStateObserver* observer) = 0;

  // Adds and removes policy deciders for navigation actions. The order in which
  // deciders are called is undefined, and will stop on the first decider that
  // refuses a navigation. Clients must be sure to remove the deciders before
  // they go away.
  virtual void AddPolicyDecider(WebStatePolicyDecider* decider) = 0;
  virtual void RemovePolicyDecider(WebStatePolicyDecider* decider) = 0;

  WebState() {}

 private:
  friend class WebStateWeakPtrFactory;  // For AsWeakPtr.

  // Returns a WeakPtr<WebState> to the current WebState. Must remain private
  // and only call must be in WebStateWeakPtrFactory. Please consult that class
  // for more details. Remove as part of http://crbug.com/556736.
  virtual base::WeakPtr<WebState> AsWeakPtr() = 0;
};

}  // namespace web

#endif  // IOS_WEB_PUBLIC_WEB_STATE_WEB_STATE_H_
