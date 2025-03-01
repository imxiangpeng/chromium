// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

#include "android_webview/browser/aw_browser_context.h"
#include "android_webview/browser/aw_cookie_access_policy.h"
#include "android_webview/browser/net/init_native_callback.h"
#include "base/android/jni_string.h"
#include "base/android/path_utils.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/lazy_instance.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/path_service.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/threading/thread.h"
#include "base/threading/thread_restrictions.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/cookie_store_factory.h"
#include "jni/AwCookieManager_jni.h"
#include "net/cookies/cookie_monster.h"
#include "net/cookies/cookie_options.h"
#include "net/cookies/cookie_store.h"
#include "net/cookies/parsed_cookie.h"
#include "net/extras/sqlite/cookie_crypto_delegate.h"
#include "net/url_request/url_request_context.h"
#include "url/url_constants.h"

using base::FilePath;
using base::WaitableEvent;
using base::android::ConvertJavaStringToUTF8;
using base::android::ConvertJavaStringToUTF16;
using base::android::JavaParamRef;
using base::android::ScopedJavaGlobalRef;
using base::android::ScopedJavaLocalRef;
using content::BrowserThread;
using net::CookieList;

// In the future, we may instead want to inject an explicit CookieStore
// dependency into this object during process initialization to avoid
// depending on the URLRequestContext.
// See issue http://crbug.com/157683

// On the CookieManager methods without a callback and methods with a callback
// when that callback is null can be called from any thread, including threads
// without a message loop. Methods with a non-null callback must be called on
// a thread with a running message loop.

namespace android_webview {

namespace {

typedef base::Callback<void(bool)> BoolCallback;
typedef base::Callback<void(int)> IntCallback;

// Holds a Java BooleanCookieCallback, knows how to invoke it and turn it
// into a base callback.
class BoolCookieCallbackHolder {
 public:
  BoolCookieCallbackHolder(JNIEnv* env, jobject callback) {
    callback_.Reset(env, callback);
  }

  void Invoke(bool result) {
    if (!callback_.is_null()) {
      JNIEnv* env = base::android::AttachCurrentThread();
      Java_AwCookieManager_invokeBooleanCookieCallback(env, callback_, result);
    }
  }

  static BoolCallback ConvertToCallback(
      std::unique_ptr<BoolCookieCallbackHolder> me) {
    return base::Bind(&BoolCookieCallbackHolder::Invoke,
                      base::Owned(me.release()));
  }

 private:
  ScopedJavaGlobalRef<jobject> callback_;
  DISALLOW_COPY_AND_ASSIGN(BoolCookieCallbackHolder);
};

// Construct a closure which signals a waitable event if and when the closure
// is called the waitable event must still exist.
static base::Closure SignalEventClosure(WaitableEvent* completion) {
  return base::Bind(&WaitableEvent::Signal, base::Unretained(completion));
}

static void DiscardBool(const base::Closure& f, bool b) {
  f.Run();
}

static BoolCallback BoolCallbackAdapter(const base::Closure& f) {
  return base::Bind(&DiscardBool, f);
}

static void DiscardInt(const base::Closure& f, int i) {
  f.Run();
}

static IntCallback IntCallbackAdapter(const base::Closure& f) {
  return base::Bind(&DiscardInt, f);
}

// Are cookies allowed for file:// URLs by default?
const bool kDefaultFileSchemeAllowed = false;

void ImportLegacyCookieStore(const FilePath& cookie_store_path) {
  // We use the old cookie store to create the new cookie store only if the
  // new cookie store does not exist.
  if (base::PathExists(cookie_store_path))
    return;

  // WebViewClassic gets the database path from Context and appends a
  // hardcoded name. See:
  // https://android.googlesource.com/platform/frameworks/base/+/bf6f6f9d/core/java/android/webkit/JniUtil.java
  // https://android.googlesource.com/platform/external/webkit/+/7151e/
  //     Source/WebKit/android/WebCoreSupport/WebCookieJar.cpp
  FilePath old_cookie_store_path;
  base::android::GetDatabaseDirectory(&old_cookie_store_path);
  old_cookie_store_path = old_cookie_store_path.Append(
      FILE_PATH_LITERAL("webviewCookiesChromium.db"));
  if (base::PathExists(old_cookie_store_path) &&
      !base::Move(old_cookie_store_path, cookie_store_path)) {
    LOG(WARNING) << "Failed to move old cookie store path from "
                 << old_cookie_store_path.AsUTF8Unsafe() << " to "
                 << cookie_store_path.AsUTF8Unsafe();
  }
}

void GetUserDataDir(FilePath* user_data_dir) {
  if (!PathService::Get(base::DIR_ANDROID_APP_DATA, user_data_dir)) {
    NOTREACHED() << "Failed to get app data directory for Android WebView";
  }
}

}  // namespace

// CookieManager creates and owns Webview's CookieStore, in addition to handling
// calls into the CookieStore from Java.
//
// Since Java calls can be made on the IO Thread, and must synchronously return
// a result, and the CookieStore API allows it to asynchronously return results,
// the CookieStore must be run on its own thread, to prevent deadlock.
class CookieManager {
 public:
  static CookieManager* GetInstance();

  // Returns the TaskRunner on which the CookieStore lives.
  base::SingleThreadTaskRunner* GetCookieStoreTaskRunner();
  // Returns the CookieStore, creating it if necessary. This must only be called
  // on the CookieStore TaskRunner.
  net::CookieStore* GetCookieStore();

  void SetShouldAcceptCookies(bool accept);
  bool GetShouldAcceptCookies();
  void SetCookie(const GURL& host,
                 const std::string& cookie_value,
                 std::unique_ptr<BoolCookieCallbackHolder> callback);
  void SetCookieSync(const GURL& host, const std::string& cookie_value);
  std::string GetCookie(const GURL& host);
  void RemoveSessionCookies(std::unique_ptr<BoolCookieCallbackHolder> callback);
  void RemoveAllCookies(std::unique_ptr<BoolCookieCallbackHolder> callback);
  void RemoveAllCookiesSync();
  void RemoveSessionCookiesSync();
  void RemoveExpiredCookies();
  void FlushCookieStore();
  bool HasCookies();
  bool AllowFileSchemeCookies();
  void SetAcceptFileSchemeCookies(bool accept);

 private:
  friend struct base::LazyInstanceTraitsBase<CookieManager>;

  CookieManager();
  ~CookieManager();

  void ExecCookieTaskSync(const base::Callback<void(BoolCallback)>& task);
  void ExecCookieTaskSync(const base::Callback<void(IntCallback)>& task);
  void ExecCookieTaskSync(const base::Callback<void(base::Closure)>& task);
  void ExecCookieTask(const base::Closure& task);

  void SetCookieHelper(const GURL& host,
                       const std::string& value,
                       BoolCallback callback);

  void GetCookieValueAsyncHelper(const GURL& host,
                                 std::string* result,
                                 base::Closure complete);
  void GetCookieValueCompleted(base::Closure complete,
                               std::string* result,
                               const std::string& value);

  void RemoveSessionCookiesHelper(BoolCallback callback);
  void RemoveAllCookiesHelper(BoolCallback callback);
  void RemoveCookiesCompleted(BoolCallback callback, uint32_t num_deleted);

  void FlushCookieStoreAsyncHelper(base::Closure complete);

  void HasCookiesAsyncHelper(bool* result, base::Closure complete);
  void HasCookiesCompleted(base::Closure complete,
                           bool* result,
                           const net::CookieList& cookies);

  // This protects the following two bools, as they're used on multiple threads.
  base::Lock accept_file_scheme_cookies_lock_;
  // True if cookies should be allowed for file URLs. Can only be changed prior
  // to creating the CookieStore.
  bool accept_file_scheme_cookies_;
  // True once the cookie store has been created. Just used to track when
  // |accept_file_scheme_cookies_| can no longer be modified.
  bool cookie_store_created_;

  base::Thread cookie_store_client_thread_;
  base::Thread cookie_store_backend_thread_;

  scoped_refptr<base::SingleThreadTaskRunner> cookie_store_task_runner_;
  std::unique_ptr<net::CookieStore> cookie_store_;

  DISALLOW_COPY_AND_ASSIGN(CookieManager);
};

namespace {
base::LazyInstance<CookieManager>::Leaky g_lazy_instance;
}

// static
CookieManager* CookieManager::GetInstance() {
  return g_lazy_instance.Pointer();
}

CookieManager::CookieManager()
    : accept_file_scheme_cookies_(kDefaultFileSchemeAllowed),
      cookie_store_created_(false),
      cookie_store_client_thread_("CookieMonsterClient"),
      cookie_store_backend_thread_("CookieMonsterBackend") {
  cookie_store_client_thread_.Start();
  cookie_store_backend_thread_.Start();
  cookie_store_task_runner_ = cookie_store_client_thread_.task_runner();
}

CookieManager::~CookieManager() {}

// Executes the |task| on |cookie_store_task_runner_| and waits for it to
// complete before returning.
//
// To execute a CookieTask synchronously you must arrange for Signal to be
// called on the waitable event at some point. You can call the bool or int
// versions of ExecCookieTaskSync, these will supply the caller with a dummy
// callback which takes an int/bool, throws it away and calls Signal.
// Alternatively you can call the version which supplies a Closure in which
// case you must call Run on it when you want the unblock the calling code.
//
// Ignore a bool callback.
void CookieManager::ExecCookieTaskSync(
    const base::Callback<void(BoolCallback)>& task) {
  WaitableEvent completion(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                           base::WaitableEvent::InitialState::NOT_SIGNALED);
  ExecCookieTask(
      base::Bind(task, BoolCallbackAdapter(SignalEventClosure(&completion))));
  base::ThreadRestrictions::ScopedAllowWait wait;
  completion.Wait();
}

// Ignore an int callback.
void CookieManager::ExecCookieTaskSync(
    const base::Callback<void(IntCallback)>& task) {
  WaitableEvent completion(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                           base::WaitableEvent::InitialState::NOT_SIGNALED);
  ExecCookieTask(
      base::Bind(task, IntCallbackAdapter(SignalEventClosure(&completion))));
  base::ThreadRestrictions::ScopedAllowWait wait;
  completion.Wait();
}

// Call the supplied closure when you want to signal that the blocked code can
// continue.
void CookieManager::ExecCookieTaskSync(
    const base::Callback<void(base::Closure)>& task) {
  WaitableEvent completion(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                           base::WaitableEvent::InitialState::NOT_SIGNALED);
  ExecCookieTask(base::Bind(task, SignalEventClosure(&completion)));
  base::ThreadRestrictions::ScopedAllowWait wait;
  completion.Wait();
}

// Executes the |task| using |cookie_store_task_runner_|.
void CookieManager::ExecCookieTask(const base::Closure& task) {
  cookie_store_task_runner_->PostTask(FROM_HERE, task);
}

base::SingleThreadTaskRunner* CookieManager::GetCookieStoreTaskRunner() {
  return cookie_store_task_runner_.get();
}

net::CookieStore* CookieManager::GetCookieStore() {
  DCHECK(cookie_store_task_runner_->RunsTasksInCurrentSequence());

  if (!cookie_store_) {
    FilePath user_data_dir;
    GetUserDataDir(&user_data_dir);
    FilePath cookie_store_path =
        user_data_dir.Append(FILE_PATH_LITERAL("Cookies"));

    cookie_store_backend_thread_.task_runner()->PostTask(
        FROM_HERE, base::Bind(ImportLegacyCookieStore, cookie_store_path));

    content::CookieStoreConfig cookie_config(
        cookie_store_path, content::CookieStoreConfig::RESTORED_SESSION_COOKIES,
        nullptr, nullptr);
    cookie_config.client_task_runner = cookie_store_task_runner_;
    cookie_config.background_task_runner =
        cookie_store_backend_thread_.task_runner();

    {
      base::AutoLock lock(accept_file_scheme_cookies_lock_);

      // There are some unknowns about how to correctly handle file:// cookies,
      // and our implementation for this is not robust.  http://crbug.com/582985
      //
      // TODO(mmenke): This call should be removed once we can deprecate and
      // remove the Android WebView 'CookieManager::setAcceptFileSchemeCookies'
      // method. Until then, note that this is just not a great idea.
      cookie_config.cookieable_schemes.insert(
          cookie_config.cookieable_schemes.begin(),
          net::CookieMonster::kDefaultCookieableSchemes,
          net::CookieMonster::kDefaultCookieableSchemes +
              net::CookieMonster::kDefaultCookieableSchemesCount);
      if (accept_file_scheme_cookies_)
        cookie_config.cookieable_schemes.push_back(url::kFileScheme);
      cookie_store_created_ = true;
    }

    cookie_store_ = content::CreateCookieStore(cookie_config);
  }

  return cookie_store_.get();
}

void CookieManager::SetShouldAcceptCookies(bool accept) {
  AwCookieAccessPolicy::GetInstance()->SetShouldAcceptCookies(accept);
}

bool CookieManager::GetShouldAcceptCookies() {
  return AwCookieAccessPolicy::GetInstance()->GetShouldAcceptCookies();
}

void CookieManager::SetCookie(
    const GURL& host,
    const std::string& cookie_value,
    std::unique_ptr<BoolCookieCallbackHolder> callback_holder) {
  BoolCallback callback =
      BoolCookieCallbackHolder::ConvertToCallback(std::move(callback_holder));
  ExecCookieTask(base::Bind(&CookieManager::SetCookieHelper,
                            base::Unretained(this), host, cookie_value,
                            callback));
}

void CookieManager::SetCookieSync(const GURL& host,
                                  const std::string& cookie_value) {
  ExecCookieTaskSync(base::Bind(&CookieManager::SetCookieHelper,
                                base::Unretained(this), host, cookie_value));
}

void CookieManager::SetCookieHelper(const GURL& host,
                                    const std::string& value,
                                    const BoolCallback callback) {
  net::CookieOptions options;
  options.set_include_httponly();

  // Log message for catching strict secure cookies related bugs.
  // TODO(sgurun) temporary. Add UMA stats to monitor, and remove afterwards.
  if (host.is_valid() &&
      (!host.has_scheme() || host.SchemeIs(url::kHttpScheme))) {
    net::ParsedCookie parsed_cookie(value);
    if (parsed_cookie.IsValid() && parsed_cookie.IsSecure()) {
      LOG(WARNING) << "Strict Secure Cookie policy does not allow setting a "
                      "secure cookie for "
                   << host.spec();
      GURL::Replacements replace_host;
      replace_host.SetSchemeStr("https");
      GURL new_host = host.ReplaceComponents(replace_host);
      GetCookieStore()->SetCookieWithOptionsAsync(new_host, value, options,
                                                  callback);
      return;
    }
  }

  GetCookieStore()->SetCookieWithOptionsAsync(host, value, options, callback);
}

std::string CookieManager::GetCookie(const GURL& host) {
  std::string cookie_value;
  ExecCookieTaskSync(base::Bind(&CookieManager::GetCookieValueAsyncHelper,
                                base::Unretained(this), host, &cookie_value));
  return cookie_value;
}

void CookieManager::GetCookieValueAsyncHelper(const GURL& host,
                                              std::string* result,
                                              base::Closure complete) {
  net::CookieOptions options;
  options.set_include_httponly();

  GetCookieStore()->GetCookiesWithOptionsAsync(
      host, options,
      base::Bind(&CookieManager::GetCookieValueCompleted,
                 base::Unretained(this), complete, result));
}

void CookieManager::GetCookieValueCompleted(base::Closure complete,
                                            std::string* result,
                                            const std::string& value) {
  *result = value;
  complete.Run();
}

void CookieManager::RemoveSessionCookies(
    std::unique_ptr<BoolCookieCallbackHolder> callback_holder) {
  BoolCallback callback =
      BoolCookieCallbackHolder::ConvertToCallback(std::move(callback_holder));
  ExecCookieTask(base::Bind(&CookieManager::RemoveSessionCookiesHelper,
                            base::Unretained(this), callback));
}

void CookieManager::RemoveSessionCookiesSync() {
  ExecCookieTaskSync(base::Bind(&CookieManager::RemoveSessionCookiesHelper,
                                base::Unretained(this)));
}

void CookieManager::RemoveSessionCookiesHelper(BoolCallback callback) {
  GetCookieStore()->DeleteSessionCookiesAsync(
      base::Bind(&CookieManager::RemoveCookiesCompleted, base::Unretained(this),
                 callback));
}

void CookieManager::RemoveCookiesCompleted(BoolCallback callback,
                                           uint32_t num_deleted) {
  callback.Run(num_deleted > 0u);
}

void CookieManager::RemoveAllCookies(
    std::unique_ptr<BoolCookieCallbackHolder> callback_holder) {
  BoolCallback callback =
      BoolCookieCallbackHolder::ConvertToCallback(std::move(callback_holder));
  ExecCookieTask(base::Bind(&CookieManager::RemoveAllCookiesHelper,
                            base::Unretained(this), callback));
}

void CookieManager::RemoveAllCookiesSync() {
  ExecCookieTaskSync(base::Bind(&CookieManager::RemoveAllCookiesHelper,
                                base::Unretained(this)));
}

void CookieManager::RemoveAllCookiesHelper(const BoolCallback callback) {
  GetCookieStore()->DeleteAllAsync(
      base::Bind(&CookieManager::RemoveCookiesCompleted, base::Unretained(this),
                 callback));
}

void CookieManager::RemoveExpiredCookies() {
  // HasCookies will call GetAllCookiesAsync, which in turn will force a GC.
  HasCookies();
}

void CookieManager::FlushCookieStore() {
  ExecCookieTaskSync(base::Bind(&CookieManager::FlushCookieStoreAsyncHelper,
                                base::Unretained(this)));
}

void CookieManager::FlushCookieStoreAsyncHelper(base::Closure complete) {
  GetCookieStore()->FlushStore(complete);
}

bool CookieManager::HasCookies() {
  bool has_cookies;
  ExecCookieTaskSync(base::Bind(&CookieManager::HasCookiesAsyncHelper,
                                base::Unretained(this), &has_cookies));
  return has_cookies;
}

// TODO(kristianm): Simplify this, copying the entire list around
// should not be needed.
void CookieManager::HasCookiesAsyncHelper(bool* result,
                                          base::Closure complete) {
  GetCookieStore()->GetAllCookiesAsync(
      base::Bind(&CookieManager::HasCookiesCompleted, base::Unretained(this),
                 complete, result));
}

void CookieManager::HasCookiesCompleted(base::Closure complete,
                                        bool* result,
                                        const CookieList& cookies) {
  *result = cookies.size() != 0;
  complete.Run();
}

bool CookieManager::AllowFileSchemeCookies() {
  base::AutoLock lock(accept_file_scheme_cookies_lock_);
  return accept_file_scheme_cookies_;
}

void CookieManager::SetAcceptFileSchemeCookies(bool accept) {
  base::AutoLock lock(accept_file_scheme_cookies_lock_);
  // Can only modify this before the cookie store is created.
  if (!cookie_store_created_)
    accept_file_scheme_cookies_ = accept;
}

static void SetShouldAcceptCookies(JNIEnv* env,
                                   const JavaParamRef<jobject>& obj,
                                   jboolean accept) {
  CookieManager::GetInstance()->SetShouldAcceptCookies(accept);
}

static jboolean GetShouldAcceptCookies(JNIEnv* env,
                                       const JavaParamRef<jobject>& obj) {
  return CookieManager::GetInstance()->GetShouldAcceptCookies();
}

static void SetCookie(JNIEnv* env,
                      const JavaParamRef<jobject>& obj,
                      const JavaParamRef<jstring>& url,
                      const JavaParamRef<jstring>& value,
                      const JavaParamRef<jobject>& java_callback) {
  GURL host(ConvertJavaStringToUTF16(env, url));
  std::string cookie_value(ConvertJavaStringToUTF8(env, value));
  std::unique_ptr<BoolCookieCallbackHolder> callback(
      new BoolCookieCallbackHolder(env, java_callback));
  CookieManager::GetInstance()->SetCookie(host, cookie_value,
                                          std::move(callback));
}

static void SetCookieSync(JNIEnv* env,
                          const JavaParamRef<jobject>& obj,
                          const JavaParamRef<jstring>& url,
                          const JavaParamRef<jstring>& value) {
  GURL host(ConvertJavaStringToUTF16(env, url));
  std::string cookie_value(ConvertJavaStringToUTF8(env, value));

  CookieManager::GetInstance()->SetCookieSync(host, cookie_value);
}

static ScopedJavaLocalRef<jstring> GetCookie(JNIEnv* env,
                                             const JavaParamRef<jobject>& obj,
                                             const JavaParamRef<jstring>& url) {
  GURL host(ConvertJavaStringToUTF16(env, url));

  return base::android::ConvertUTF8ToJavaString(
      env, CookieManager::GetInstance()->GetCookie(host));
}

static void RemoveSessionCookies(JNIEnv* env,
                                 const JavaParamRef<jobject>& obj,
                                 const JavaParamRef<jobject>& java_callback) {
  std::unique_ptr<BoolCookieCallbackHolder> callback(
      new BoolCookieCallbackHolder(env, java_callback));
  CookieManager::GetInstance()->RemoveSessionCookies(std::move(callback));
}

static void RemoveSessionCookiesSync(JNIEnv* env,
                                     const JavaParamRef<jobject>& obj) {
  CookieManager::GetInstance()->RemoveSessionCookiesSync();
}

static void RemoveAllCookies(JNIEnv* env,
                             const JavaParamRef<jobject>& obj,
                             const JavaParamRef<jobject>& java_callback) {
  std::unique_ptr<BoolCookieCallbackHolder> callback(
      new BoolCookieCallbackHolder(env, java_callback));
  CookieManager::GetInstance()->RemoveAllCookies(std::move(callback));
}

static void RemoveAllCookiesSync(JNIEnv* env,
                                 const JavaParamRef<jobject>& obj) {
  CookieManager::GetInstance()->RemoveAllCookiesSync();
}

static void RemoveExpiredCookies(JNIEnv* env,
                                 const JavaParamRef<jobject>& obj) {
  CookieManager::GetInstance()->RemoveExpiredCookies();
}

static void FlushCookieStore(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  CookieManager::GetInstance()->FlushCookieStore();
}

static jboolean HasCookies(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  return CookieManager::GetInstance()->HasCookies();
}

static jboolean AllowFileSchemeCookies(JNIEnv* env,
                                       const JavaParamRef<jobject>& obj) {
  return CookieManager::GetInstance()->AllowFileSchemeCookies();
}

static void SetAcceptFileSchemeCookies(JNIEnv* env,
                                       const JavaParamRef<jobject>& obj,
                                       jboolean accept) {
  return CookieManager::GetInstance()->SetAcceptFileSchemeCookies(accept);
}

// The following two methods are used to avoid a circular project dependency.
// TODO(mmenke):  This is weird. Maybe there should be a leaky Singleton in
// browser/net that creates and owns there?

scoped_refptr<base::SingleThreadTaskRunner> GetCookieStoreTaskRunner() {
  return CookieManager::GetInstance()->GetCookieStoreTaskRunner();
}

net::CookieStore* GetCookieStore() {
  return CookieManager::GetInstance()->GetCookieStore();
}

}  // namespace android_webview
