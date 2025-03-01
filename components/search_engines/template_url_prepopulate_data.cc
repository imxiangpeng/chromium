// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/search_engines/template_url_prepopulate_data.h"

#if defined(OS_POSIX) && !defined(OS_MACOSX)
#include <locale.h>
#endif

#include "base/logging.h"
#include "base/macros.h"
#include "base/strings/string16.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "components/google/core/browser/google_util.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/prepopulated_engines.h"
#include "components/search_engines/search_engines_pref_names.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_data_util.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

#if defined(OS_WIN)
#undef IN  // On Windows, windef.h defines this, which screws up "India" cases.
#elif defined(OS_MACOSX)
#include "base/mac/scoped_cftyperef.h"
#endif

#if defined(OS_ANDROID)
#include "base/android/locale_utils.h"
#endif

namespace TemplateURLPrepopulateData {

// Helpers --------------------------------------------------------------------

namespace {

// NOTE: You should probably not change the data in this file without changing
// |kCurrentDataVersion| in prepopulated_engines.json. See comments in
// GetDataVersion() below!

// Put the engines within each country in order with most interesting/important
// first.  The default will be the first engine.

// Default (for countries with no better engine set)
const PrepopulatedEngine* const engines_default[] = {
    &google, &bing, &yahoo,
};

// United Arab Emirates
const PrepopulatedEngine* const engines_AE[] = {
    &google, &yahoo_maktoob, &bing,
};

// Albania
const PrepopulatedEngine* const engines_AL[] = {
    &google, &yahoo, &bing,
};

// Argentina
const PrepopulatedEngine* const engines_AR[] = {
    &google, &bing, &yahoo_ar,
};

// Austria
const PrepopulatedEngine* const engines_AT[] = {
    &google, &bing, &yahoo_at,
};

// Australia
const PrepopulatedEngine* const engines_AU[] = {
    &google, &bing, &yahoo_au,
};

// Bosnia and Herzegovina
const PrepopulatedEngine* const engines_BA[] = {
    &google, &yahoo, &bing,
};

// Belgium
const PrepopulatedEngine* const engines_BE[] = {
    &google, &bing, &yahoo, &yahoo_fr,
};

// Bulgaria
const PrepopulatedEngine* const engines_BG[] = {
    &google, &bing, &ask,
};

// Bahrain
const PrepopulatedEngine* const engines_BH[] = {
    &google, &yahoo_maktoob, &bing,
};

// Burundi
const PrepopulatedEngine* const engines_BI[] = {
    &google, &yahoo, &bing,
};

// Brunei
const PrepopulatedEngine* const engines_BN[] = {
    &google, &yahoo_my, &bing,
};

// Bolivia
const PrepopulatedEngine* const engines_BO[] = {
    &google, &bing, &yahoo,
};

// Brazil
const PrepopulatedEngine* const engines_BR[] = {
    &google, &ask_br, &bing, &yahoo_br,
};

// Belarus
const PrepopulatedEngine* const engines_BY[] = {
    &google, &yandex_by, &mail_ru,
};

// Belize
const PrepopulatedEngine* const engines_BZ[] = {
    &google, &yahoo, &bing,
};

// Canada
const PrepopulatedEngine* const engines_CA[] = {
    &google, &bing, &ask, &yahoo_ca, &yahoo_qc,
};

// Switzerland
const PrepopulatedEngine* const engines_CH[] = {
    &google, &bing, &yahoo_ch,
};

// Chile
const PrepopulatedEngine* const engines_CL[] = {
    &google, &bing, &yahoo_cl,
};

// China
const PrepopulatedEngine* const engines_CN[] = {
    &google, &baidu, &sogou, &so_360,
};

// Colombia
const PrepopulatedEngine* const engines_CO[] = {
    &google, &bing, &yahoo_co,
};

// Costa Rica
const PrepopulatedEngine* const engines_CR[] = {
    &google, &yahoo, &bing,
};

// Czech Republic
const PrepopulatedEngine* const engines_CZ[] = {
    &google, &seznam, &bing,
};

// Germany
const PrepopulatedEngine* const engines_DE[] = {
    &google, &bing, &yahoo_de,
};

// Denmark
const PrepopulatedEngine* const engines_DK[] = {
    &google, &bing, &yahoo_dk,
};

// Dominican Republic
const PrepopulatedEngine* const engines_DO[] = {
    &google, &yahoo, &bing,
};

// Algeria
const PrepopulatedEngine* const engines_DZ[] = {
    &google, &bing, &yahoo_maktoob,
};

// Ecuador
const PrepopulatedEngine* const engines_EC[] = {
    &google, &bing, &yahoo,
};

// Estonia
const PrepopulatedEngine* const engines_EE[] = {
    &google, &bing, &yahoo,
};

// Egypt
const PrepopulatedEngine* const engines_EG[] = {
    &google, &yahoo_maktoob, &bing,
};

// Spain
const PrepopulatedEngine* const engines_ES[] = {
    &google, &bing, &yahoo_es,
};

// Faroe Islands
const PrepopulatedEngine* const engines_FO[] = {
    &google, &bing, &ask,
};

// Finland
const PrepopulatedEngine* const engines_FI[] = {
    &google, &bing, &yahoo_fi,
};

// France
const PrepopulatedEngine* const engines_FR[] = {
    &google, &bing, &yahoo_fr,
};

// United Kingdom
const PrepopulatedEngine* const engines_GB[] = {
    &google, &bing, &yahoo_uk, &ask_uk,
};

// Greece
const PrepopulatedEngine* const engines_GR[] = {
    &google, &bing, &yahoo_gr,
};

// Guatemala
const PrepopulatedEngine* const engines_GT[] = {
    &google, &yahoo, &bing,
};

// Hong Kong
const PrepopulatedEngine* const engines_HK[] = {
    &google, &yahoo_hk, &baidu, &bing,
};

// Honduras
const PrepopulatedEngine* const engines_HN[] = {
    &google, &yahoo, &bing,
};

// Croatia
const PrepopulatedEngine* const engines_HR[] = {
    &google, &bing, &yahoo,
};

// Hungary
const PrepopulatedEngine* const engines_HU[] = {
    &google, &bing, &yahoo,
};

// Indonesia
const PrepopulatedEngine* const engines_ID[] = {
    &google, &yahoo_id, &bing,
};

// Ireland
const PrepopulatedEngine* const engines_IE[] = {
    &google, &bing, &yahoo_uk,
};

// Israel
const PrepopulatedEngine* const engines_IL[] = {
    &google, &yahoo, &bing,
};

// India
const PrepopulatedEngine* const engines_IN[] = {
    &google, &bing, &yahoo_in,
};

// Iraq
const PrepopulatedEngine* const engines_IQ[] = {
    &google, &yahoo_maktoob, &bing,
};

// Iran
const PrepopulatedEngine* const engines_IR[] = {
    &google, &yahoo, &bing,
};

// Iceland
const PrepopulatedEngine* const engines_IS[] = {
    &google, &bing, &yahoo,
};

// Italy
const PrepopulatedEngine* const engines_IT[] = {
    &google, &virgilio, &bing,
};

// Jamaica
const PrepopulatedEngine* const engines_JM[] = {
    &google, &yahoo, &bing,
};

// Jordan
const PrepopulatedEngine* const engines_JO[] = {
    &google, &yahoo_maktoob, &bing,
};

// Japan
const PrepopulatedEngine* const engines_JP[] = {
    &google, &yahoo_jp, &bing,
};

// Kenya
const PrepopulatedEngine* const engines_KE[] = {
    &google, &yahoo, &bing,
};

// Kuwait
const PrepopulatedEngine* const engines_KW[] = {
    &google, &yahoo_maktoob, &bing,
};

// South Korea
const PrepopulatedEngine* const engines_KR[] = {
    &google, &naver, &daum,
};

// Kazakhstan
const PrepopulatedEngine* const engines_KZ[] = {
    &google, &mail_ru, &yandex_kz,
};

// Lebanon
const PrepopulatedEngine* const engines_LB[] = {
    &google, &yahoo_maktoob, &bing,
};

// Liechtenstein
const PrepopulatedEngine* const engines_LI[] = {
    &google, &bing, &yahoo_de,
};

// Lithuania
const PrepopulatedEngine* const engines_LT[] = {
    &google, &bing, &yandex_ru,
};

// Luxembourg
const PrepopulatedEngine* const engines_LU[] = {
    &google, &bing, &yahoo_fr,
};

// Latvia
const PrepopulatedEngine* const engines_LV[] = {
    &google, &yandex_ru, &bing,
};

// Libya
const PrepopulatedEngine* const engines_LY[] = {
    &google, &yahoo_maktoob, &bing,
};

// Morocco
const PrepopulatedEngine* const engines_MA[] = {
    &google, &bing, &yahoo_maktoob,
};

// Monaco
const PrepopulatedEngine* const engines_MC[] = {
    &google, &yahoo_fr, &bing,
};

// Moldova
const PrepopulatedEngine* const engines_MD[] = {
    &google, &bing, &yahoo,
};

// Montenegro
const PrepopulatedEngine* const engines_ME[] = {
    &google, &bing, &yahoo,
};

// Macedonia
const PrepopulatedEngine* const engines_MK[] = {
    &google, &yahoo, &bing,
};

// Mexico
const PrepopulatedEngine* const engines_MX[] = {
    &google, &bing, &yahoo_mx,
};

// Malaysia
const PrepopulatedEngine* const engines_MY[] = {
    &google, &yahoo_my, &bing,
};

// Nicaragua
const PrepopulatedEngine* const engines_NI[] = {
    &google, &yahoo, &bing,
};

// Netherlands
const PrepopulatedEngine* const engines_NL[] = {
    &google, &yahoo_nl, &vinden,
};

// Norway
const PrepopulatedEngine* const engines_NO[] = {
    &google, &bing, &kvasir,
};

// New Zealand
const PrepopulatedEngine* const engines_NZ[] = {
    &google, &bing, &yahoo_nz,
};

// Oman
const PrepopulatedEngine* const engines_OM[] = {
    &google, &bing, &yahoo_maktoob,
};

// Panama
const PrepopulatedEngine* const engines_PA[] = {
    &google, &yahoo, &bing,
};

// Peru
const PrepopulatedEngine* const engines_PE[] = {
    &google, &bing, &yahoo_pe,
};

// Philippines
const PrepopulatedEngine* const engines_PH[] = {
    &google, &yahoo_ph, &bing,
};

// Pakistan
const PrepopulatedEngine* const engines_PK[] = {
    &google, &yahoo, &bing,
};

// Puerto Rico
const PrepopulatedEngine* const engines_PR[] = {
    &google, &yahoo, &bing,
};

// Poland
const PrepopulatedEngine* const engines_PL[] = {
    &google, &onet, &bing,
};

// Portugal
const PrepopulatedEngine* const engines_PT[] = {
    &google, &bing, &yahoo,
};

// Paraguay
const PrepopulatedEngine* const engines_PY[] = {
    &google, &bing, &yahoo,
};

// Qatar
const PrepopulatedEngine* const engines_QA[] = {
    &google, &yahoo_maktoob, &bing,
};

// Romania
const PrepopulatedEngine* const engines_RO[] = {
    &google, &yahoo_ro, &bing,
};

// Serbia
const PrepopulatedEngine* const engines_RS[] = {
    &google, &bing, &yahoo,
};

// Russia
const PrepopulatedEngine* const engines_RU[] = {
    &google, &yandex_ru, &mail_ru,
};

// Rwanda
const PrepopulatedEngine* const engines_RW[] = {
    &google, &bing, &yahoo,
};

// Saudi Arabia
const PrepopulatedEngine* const engines_SA[] = {
    &google, &yahoo_maktoob, &bing,
};

// Sweden
const PrepopulatedEngine* const engines_SE[] = {
    &google, &bing, &yahoo_se,
};

// Singapore
const PrepopulatedEngine* const engines_SG[] = {
    &google, &yahoo_sg, &bing,
};

// Slovenia
const PrepopulatedEngine* const engines_SI[] = {
    &google, &najdi, &ask,
};

// Slovakia
const PrepopulatedEngine* const engines_SK[] = {
    &google, &bing, &yahoo,
};

// El Salvador
const PrepopulatedEngine* const engines_SV[] = {
    &google, &yahoo, &bing,
};

// Syria
const PrepopulatedEngine* const engines_SY[] = {
    &google, &bing, &yahoo_maktoob,
};

// Thailand
const PrepopulatedEngine* const engines_TH[] = {
    &google, &yahoo_th, &bing,
};

// Tunisia
const PrepopulatedEngine* const engines_TN[] = {
    &google, &bing, &yahoo_maktoob,
};

// Turkey
const PrepopulatedEngine* const engines_TR[] = {
    &google, &bing, &yahoo_tr, &yandex_tr,
};

// Trinidad and Tobago
const PrepopulatedEngine* const engines_TT[] = {
    &google, &bing, &yahoo,
};

// Taiwan
const PrepopulatedEngine* const engines_TW[] = {
    &google, &yahoo_tw, &bing,
};

// Tanzania
const PrepopulatedEngine* const engines_TZ[] = {
    &google, &yahoo, &bing,
};

// Ukraine
const PrepopulatedEngine* const engines_UA[] = {
    &google, &yandex_ua, &bing,
};

// United States
const PrepopulatedEngine* const engines_US[] = {
    &google, &bing, &yahoo, &aol, &ask,
};

// Uruguay
const PrepopulatedEngine* const engines_UY[] = {
    &google, &bing, &yahoo,
};

// Venezuela
const PrepopulatedEngine* const engines_VE[] = {
    &google, &bing, &yahoo_ve,
};

// Vietnam
const PrepopulatedEngine* const engines_VN[] = {
    &google, &yahoo_vn, &bing,
};

// Yemen
const PrepopulatedEngine* const engines_YE[] = {
    &google, &bing, &yahoo_maktoob,
};

// South Africa
const PrepopulatedEngine* const engines_ZA[] = {
    &google, &bing, &yahoo,
};

// Zimbabwe
const PrepopulatedEngine* const engines_ZW[] = {
    &google, &bing, &yahoo, &ask,
};

// A list of all the engines that we know about.
const PrepopulatedEngine* const kAllEngines[] = {
    // Prepopulated engines:
    &aol, &ask, &ask_br, &ask_uk, &baidu, &bing, &daum, &google, &kvasir,
    &mail_ru, &najdi, &naver, &onet, &seznam, &sogou, &vinden, &virgilio,
    &yahoo, &yahoo_ar, &yahoo_at, &yahoo_au, &yahoo_br, &yahoo_ca, &yahoo_ch,
    &yahoo_cl, &yahoo_co, &yahoo_de, &yahoo_dk, &yahoo_es, &yahoo_fi, &yahoo_fr,
    &yahoo_gr, &yahoo_hk, &yahoo_id, &yahoo_in, &yahoo_jp, &yahoo_maktoob,
    &yahoo_mx, &yahoo_my, &yahoo_nl, &yahoo_nz, &yahoo_pe, &yahoo_ph, &yahoo_qc,
    &yahoo_ro, &yahoo_se, &yahoo_sg, &yahoo_th, &yahoo_tr, &yahoo_tw, &yahoo_uk,
    &yahoo_ve, &yahoo_vn, &yandex_by, &yandex_kz, &yandex_ru, &yandex_tr,
    &yandex_ua,

    // UMA-only engines:
    &atlas_cz, &atlas_sk, &avg, &babylon, &conduit, &delfi_lt, &delfi_lv,
    &delta, &funmoods, &goo, &imesh, &iminent, &in, &incredibar, &libero, &neti,
    &nigma, &ok, &rambler, &sapo, &search_results, &searchnu, &snapdo,
    &softonic, &sweetim, &terra_ar, &terra_es, &tut, &walla, &wp, &zoznam,
};

// Please refer to ISO 3166-1 for information about the two-character country
// codes; http://en.wikipedia.org/wiki/ISO_3166-1_alpha-2 is useful. In the
// following (C++) code, we pack the two letters of the country code into an int
// value we call the CountryID.

const int kCountryIDUnknown = -1;

inline int CountryCharsToCountryID(char c1, char c2) {
  return c1 << 8 | c2;
}

int CountryCharsToCountryIDWithUpdate(char c1, char c2) {
  // SPECIAL CASE: In 2003, Yugoslavia renamed itself to Serbia and Montenegro.
  // Serbia and Montenegro dissolved their union in June 2006. Yugoslavia was
  // ISO 'YU' and Serbia and Montenegro were ISO 'CS'. Serbia was subsequently
  // issued 'RS' and Montenegro 'ME'. Windows XP and Mac OS X Leopard still use
  // the value 'YU'. If we get a value of 'YU' or 'CS' we will map it to 'RS'.
  if ((c1 == 'Y' && c2 == 'U') ||
      (c1 == 'C' && c2 == 'S')) {
    c1 = 'R';
    c2 = 'S';
  }

  // SPECIAL CASE: Timor-Leste changed from 'TP' to 'TL' in 2002. Windows XP
  // predates this; we therefore map this value.
  if (c1 == 'T' && c2 == 'P')
    c2 = 'L';

  return CountryCharsToCountryID(c1, c2);
}

#if !defined(OS_WIN) && !defined(OS_MACOSX)

int CountryStringToCountryID(const std::string& country) {
  return (country.length() == 2)
             ? CountryCharsToCountryIDWithUpdate(country[0], country[1])
             : kCountryIDUnknown;
}

#endif

#if defined(OS_WIN)

// For reference, a list of GeoIDs can be found at
// http://msdn.microsoft.com/en-us/library/dd374073.aspx .
int GeoIDToCountryID(GEOID geo_id) {
  const int kISOBufferSize = 3;  // Two plus one for the terminator.
  wchar_t isobuf[kISOBufferSize] = { 0 };
  int retval = GetGeoInfo(geo_id, GEO_ISO2, isobuf, kISOBufferSize, 0);

  if (retval == kISOBufferSize &&
      !(isobuf[0] == L'X' && isobuf[1] == L'X'))
    return CountryCharsToCountryIDWithUpdate(static_cast<char>(isobuf[0]),
                                             static_cast<char>(isobuf[1]));

  // Various locations have ISO codes that Windows does not return.
  switch (geo_id) {
    case 0x144:   // Guernsey
      return CountryCharsToCountryID('G', 'G');
    case 0x148:   // Jersey
      return CountryCharsToCountryID('J', 'E');
    case 0x3B16:  // Isle of Man
      return CountryCharsToCountryID('I', 'M');

    // 'UM' (U.S. Minor Outlying Islands)
    case 0x7F:    // Johnston Atoll
    case 0x102:   // Wake Island
    case 0x131:   // Baker Island
    case 0x146:   // Howland Island
    case 0x147:   // Jarvis Island
    case 0x149:   // Kingman Reef
    case 0x152:   // Palmyra Atoll
    case 0x52FA:  // Midway Islands
      return CountryCharsToCountryID('U', 'M');

    // 'SH' (Saint Helena)
    case 0x12F:  // Ascension Island
    case 0x15C:  // Tristan da Cunha
      return CountryCharsToCountryID('S', 'H');

    // 'IO' (British Indian Ocean Territory)
    case 0x13A:  // Diego Garcia
      return CountryCharsToCountryID('I', 'O');

    // Other cases where there is no ISO country code; we assign countries that
    // can serve as reasonable defaults.
    case 0x154:  // Rota Island
    case 0x155:  // Saipan
    case 0x15A:  // Tinian Island
      return CountryCharsToCountryID('U', 'S');
    case 0x134:  // Channel Islands
      return CountryCharsToCountryID('G', 'B');
    case 0x143:  // Guantanamo Bay
    default:
      return kCountryIDUnknown;
  }
}

#endif  // defined(OS_WIN)

int GetCountryIDFromPrefs(PrefService* prefs) {
  if (!prefs)
    return GetCurrentCountryID();

  // Cache first run Country ID value in prefs, and use it afterwards.  This
  // ensures that just because the user moves around, we won't automatically
  // make major changes to their available search providers, which would feel
  // surprising.
  if (!prefs->HasPrefPath(prefs::kCountryIDAtInstall)) {
    prefs->SetInteger(prefs::kCountryIDAtInstall, GetCurrentCountryID());
  }
  return prefs->GetInteger(prefs::kCountryIDAtInstall);
}

std::vector<std::unique_ptr<TemplateURLData>> GetPrepopulationSetFromCountryID(
    int country_id) {
  const PrepopulatedEngine* const* engines;
  size_t num_engines;
  // If you add a new country make sure to update the unit test for coverage.
  switch (country_id) {
#define CHAR_A 'A'
#define CHAR_B 'B'
#define CHAR_C 'C'
#define CHAR_D 'D'
#define CHAR_E 'E'
#define CHAR_F 'F'
#define CHAR_G 'G'
#define CHAR_H 'H'
#define CHAR_I 'I'
#define CHAR_J 'J'
#define CHAR_K 'K'
#define CHAR_L 'L'
#define CHAR_M 'M'
#define CHAR_N 'N'
#define CHAR_O 'O'
#define CHAR_P 'P'
#define CHAR_Q 'Q'
#define CHAR_R 'R'
#define CHAR_S 'S'
#define CHAR_T 'T'
#define CHAR_U 'U'
#define CHAR_V 'V'
#define CHAR_W 'W'
#define CHAR_X 'X'
#define CHAR_Y 'Y'
#define CHAR_Z 'Z'
#define CHAR(ch) CHAR_##ch
#define CODE_TO_ID(code1, code2)\
    (CHAR(code1) << 8 | CHAR(code2))

#define UNHANDLED_COUNTRY(code1, code2)\
    case CODE_TO_ID(code1, code2):
#define END_UNHANDLED_COUNTRIES(code1, code2)\
      engines = engines_##code1##code2;\
      num_engines = arraysize(engines_##code1##code2);\
      break;
#define DECLARE_COUNTRY(code1, code2)\
    UNHANDLED_COUNTRY(code1, code2)\
    END_UNHANDLED_COUNTRIES(code1, code2)

    // Countries with their own, dedicated engine set.
    DECLARE_COUNTRY(A, E)  // United Arab Emirates
    DECLARE_COUNTRY(A, L)  // Albania
    DECLARE_COUNTRY(A, R)  // Argentina
    DECLARE_COUNTRY(A, T)  // Austria
    DECLARE_COUNTRY(A, U)  // Australia
    DECLARE_COUNTRY(B, A)  // Bosnia and Herzegovina
    DECLARE_COUNTRY(B, E)  // Belgium
    DECLARE_COUNTRY(B, G)  // Bulgaria
    DECLARE_COUNTRY(B, H)  // Bahrain
    DECLARE_COUNTRY(B, I)  // Burundi
    DECLARE_COUNTRY(B, N)  // Brunei
    DECLARE_COUNTRY(B, O)  // Bolivia
    DECLARE_COUNTRY(B, R)  // Brazil
    DECLARE_COUNTRY(B, Y)  // Belarus
    DECLARE_COUNTRY(B, Z)  // Belize
    DECLARE_COUNTRY(C, A)  // Canada
    DECLARE_COUNTRY(C, H)  // Switzerland
    DECLARE_COUNTRY(C, L)  // Chile
    DECLARE_COUNTRY(C, N)  // China
    DECLARE_COUNTRY(C, O)  // Colombia
    DECLARE_COUNTRY(C, R)  // Costa Rica
    DECLARE_COUNTRY(C, Z)  // Czech Republic
    DECLARE_COUNTRY(D, E)  // Germany
    DECLARE_COUNTRY(D, K)  // Denmark
    DECLARE_COUNTRY(D, O)  // Dominican Republic
    DECLARE_COUNTRY(D, Z)  // Algeria
    DECLARE_COUNTRY(E, C)  // Ecuador
    DECLARE_COUNTRY(E, E)  // Estonia
    DECLARE_COUNTRY(E, G)  // Egypt
    DECLARE_COUNTRY(E, S)  // Spain
    DECLARE_COUNTRY(F, I)  // Finland
    DECLARE_COUNTRY(F, O)  // Faroe Islands
    DECLARE_COUNTRY(F, R)  // France
    DECLARE_COUNTRY(G, B)  // United Kingdom
    DECLARE_COUNTRY(G, R)  // Greece
    DECLARE_COUNTRY(G, T)  // Guatemala
    DECLARE_COUNTRY(H, K)  // Hong Kong
    DECLARE_COUNTRY(H, N)  // Honduras
    DECLARE_COUNTRY(H, R)  // Croatia
    DECLARE_COUNTRY(H, U)  // Hungary
    DECLARE_COUNTRY(I, D)  // Indonesia
    DECLARE_COUNTRY(I, E)  // Ireland
    DECLARE_COUNTRY(I, L)  // Israel
    DECLARE_COUNTRY(I, N)  // India
    DECLARE_COUNTRY(I, Q)  // Iraq
    DECLARE_COUNTRY(I, R)  // Iran
    DECLARE_COUNTRY(I, S)  // Iceland
    DECLARE_COUNTRY(I, T)  // Italy
    DECLARE_COUNTRY(J, M)  // Jamaica
    DECLARE_COUNTRY(J, O)  // Jordan
    DECLARE_COUNTRY(J, P)  // Japan
    DECLARE_COUNTRY(K, E)  // Kenya
    DECLARE_COUNTRY(K, R)  // South Korea
    DECLARE_COUNTRY(K, W)  // Kuwait
    DECLARE_COUNTRY(K, Z)  // Kazakhstan
    DECLARE_COUNTRY(L, B)  // Lebanon
    DECLARE_COUNTRY(L, I)  // Liechtenstein
    DECLARE_COUNTRY(L, T)  // Lithuania
    DECLARE_COUNTRY(L, U)  // Luxembourg
    DECLARE_COUNTRY(L, V)  // Latvia
    DECLARE_COUNTRY(L, Y)  // Libya
    DECLARE_COUNTRY(M, A)  // Morocco
    DECLARE_COUNTRY(M, C)  // Monaco
    DECLARE_COUNTRY(M, D)  // Moldova
    DECLARE_COUNTRY(M, E)  // Montenegro
    DECLARE_COUNTRY(M, K)  // Macedonia
    DECLARE_COUNTRY(M, X)  // Mexico
    DECLARE_COUNTRY(M, Y)  // Malaysia
    DECLARE_COUNTRY(N, I)  // Nicaragua
    DECLARE_COUNTRY(N, L)  // Netherlands
    DECLARE_COUNTRY(N, O)  // Norway
    DECLARE_COUNTRY(N, Z)  // New Zealand
    DECLARE_COUNTRY(O, M)  // Oman
    DECLARE_COUNTRY(P, A)  // Panama
    DECLARE_COUNTRY(P, E)  // Peru
    DECLARE_COUNTRY(P, H)  // Philippines
    DECLARE_COUNTRY(P, K)  // Pakistan
    DECLARE_COUNTRY(P, L)  // Poland
    DECLARE_COUNTRY(P, R)  // Puerto Rico
    DECLARE_COUNTRY(P, T)  // Portugal
    DECLARE_COUNTRY(P, Y)  // Paraguay
    DECLARE_COUNTRY(Q, A)  // Qatar
    DECLARE_COUNTRY(R, O)  // Romania
    DECLARE_COUNTRY(R, S)  // Serbia
    DECLARE_COUNTRY(R, U)  // Russia
    DECLARE_COUNTRY(R, W)  // Rwanda
    DECLARE_COUNTRY(S, A)  // Saudi Arabia
    DECLARE_COUNTRY(S, E)  // Sweden
    DECLARE_COUNTRY(S, G)  // Singapore
    DECLARE_COUNTRY(S, I)  // Slovenia
    DECLARE_COUNTRY(S, K)  // Slovakia
    DECLARE_COUNTRY(S, V)  // El Salvador
    DECLARE_COUNTRY(S, Y)  // Syria
    DECLARE_COUNTRY(T, H)  // Thailand
    DECLARE_COUNTRY(T, N)  // Tunisia
    DECLARE_COUNTRY(T, R)  // Turkey
    DECLARE_COUNTRY(T, T)  // Trinidad and Tobago
    DECLARE_COUNTRY(T, W)  // Taiwan
    DECLARE_COUNTRY(T, Z)  // Tanzania
    DECLARE_COUNTRY(U, A)  // Ukraine
    DECLARE_COUNTRY(U, S)  // United States
    DECLARE_COUNTRY(U, Y)  // Uruguay
    DECLARE_COUNTRY(V, E)  // Venezuela
    DECLARE_COUNTRY(V, N)  // Vietnam
    DECLARE_COUNTRY(Y, E)  // Yemen
    DECLARE_COUNTRY(Z, A)  // South Africa
    DECLARE_COUNTRY(Z, W)  // Zimbabwe

    // Countries using the "Australia" engine set.
    UNHANDLED_COUNTRY(C, C)  // Cocos Islands
    UNHANDLED_COUNTRY(C, X)  // Christmas Island
    UNHANDLED_COUNTRY(H, M)  // Heard Island and McDonald Islands
    UNHANDLED_COUNTRY(N, F)  // Norfolk Island
    END_UNHANDLED_COUNTRIES(A, U)

    // Countries using the "China" engine set.
    UNHANDLED_COUNTRY(M, O)  // Macao
    END_UNHANDLED_COUNTRIES(C, N)

    // Countries using the "Denmark" engine set.
    UNHANDLED_COUNTRY(G, L)  // Greenland
    END_UNHANDLED_COUNTRIES(D, K)

    // Countries using the "Spain" engine set.
    UNHANDLED_COUNTRY(A, D)  // Andorra
    END_UNHANDLED_COUNTRIES(E, S)

    // Countries using the "Finland" engine set.
    UNHANDLED_COUNTRY(A, X)  // Aland Islands
    END_UNHANDLED_COUNTRIES(F, I)

    // Countries using the "France" engine set.
    UNHANDLED_COUNTRY(B, F)  // Burkina Faso
    UNHANDLED_COUNTRY(B, J)  // Benin
    UNHANDLED_COUNTRY(C, D)  // Congo - Kinshasa
    UNHANDLED_COUNTRY(C, F)  // Central African Republic
    UNHANDLED_COUNTRY(C, G)  // Congo - Brazzaville
    UNHANDLED_COUNTRY(C, I)  // Ivory Coast
    UNHANDLED_COUNTRY(C, M)  // Cameroon
    UNHANDLED_COUNTRY(D, J)  // Djibouti
    UNHANDLED_COUNTRY(G, A)  // Gabon
    UNHANDLED_COUNTRY(G, F)  // French Guiana
    UNHANDLED_COUNTRY(G, N)  // Guinea
    UNHANDLED_COUNTRY(G, P)  // Guadeloupe
    UNHANDLED_COUNTRY(H, T)  // Haiti
#if defined(OS_WIN)
    UNHANDLED_COUNTRY(I, P)  // Clipperton Island ('IP' is an WinXP-ism; ISO
                             //                    includes it with France)
#endif
    UNHANDLED_COUNTRY(M, L)  // Mali
    UNHANDLED_COUNTRY(M, Q)  // Martinique
    UNHANDLED_COUNTRY(N, C)  // New Caledonia
    UNHANDLED_COUNTRY(N, E)  // Niger
    UNHANDLED_COUNTRY(P, F)  // French Polynesia
    UNHANDLED_COUNTRY(P, M)  // Saint Pierre and Miquelon
    UNHANDLED_COUNTRY(R, E)  // Reunion
    UNHANDLED_COUNTRY(S, N)  // Senegal
    UNHANDLED_COUNTRY(T, D)  // Chad
    UNHANDLED_COUNTRY(T, F)  // French Southern Territories
    UNHANDLED_COUNTRY(T, G)  // Togo
    UNHANDLED_COUNTRY(W, F)  // Wallis and Futuna
    UNHANDLED_COUNTRY(Y, T)  // Mayotte
    END_UNHANDLED_COUNTRIES(F, R)

    // Countries using the "Greece" engine set.
    UNHANDLED_COUNTRY(C, Y)  // Cyprus
    END_UNHANDLED_COUNTRIES(G, R)

    // Countries using the "Italy" engine set.
    UNHANDLED_COUNTRY(S, M)  // San Marino
    UNHANDLED_COUNTRY(V, A)  // Vatican
    END_UNHANDLED_COUNTRIES(I, T)

    // Countries using the "Morocco" engine set.
    UNHANDLED_COUNTRY(E, H)  // Western Sahara
    END_UNHANDLED_COUNTRIES(M, A)

    // Countries using the "Netherlands" engine set.
    UNHANDLED_COUNTRY(A, N)  // Netherlands Antilles
    UNHANDLED_COUNTRY(A, W)  // Aruba
    END_UNHANDLED_COUNTRIES(N, L)

    // Countries using the "Norway" engine set.
    UNHANDLED_COUNTRY(B, V)  // Bouvet Island
    UNHANDLED_COUNTRY(S, J)  // Svalbard and Jan Mayen
    END_UNHANDLED_COUNTRIES(N, O)

    // Countries using the "New Zealand" engine set.
    UNHANDLED_COUNTRY(C, K)  // Cook Islands
    UNHANDLED_COUNTRY(N, U)  // Niue
    UNHANDLED_COUNTRY(T, K)  // Tokelau
    END_UNHANDLED_COUNTRIES(N, Z)

    // Countries using the "Portugal" engine set.
    UNHANDLED_COUNTRY(C, V)  // Cape Verde
    UNHANDLED_COUNTRY(G, W)  // Guinea-Bissau
    UNHANDLED_COUNTRY(M, Z)  // Mozambique
    UNHANDLED_COUNTRY(S, T)  // Sao Tome and Principe
    UNHANDLED_COUNTRY(T, L)  // Timor-Leste
    END_UNHANDLED_COUNTRIES(P, T)

    // Countries using the "Russia" engine set.
    UNHANDLED_COUNTRY(A, M)  // Armenia
    UNHANDLED_COUNTRY(A, Z)  // Azerbaijan
    UNHANDLED_COUNTRY(K, G)  // Kyrgyzstan
    UNHANDLED_COUNTRY(T, J)  // Tajikistan
    UNHANDLED_COUNTRY(T, M)  // Turkmenistan
    UNHANDLED_COUNTRY(U, Z)  // Uzbekistan
    END_UNHANDLED_COUNTRIES(R, U)

    // Countries using the "Saudi Arabia" engine set.
    UNHANDLED_COUNTRY(M, R)  // Mauritania
    UNHANDLED_COUNTRY(P, S)  // Palestinian Territory
    UNHANDLED_COUNTRY(S, D)  // Sudan
    END_UNHANDLED_COUNTRIES(S, A)

    // Countries using the "United Kingdom" engine set.
    UNHANDLED_COUNTRY(B, M)  // Bermuda
    UNHANDLED_COUNTRY(F, K)  // Falkland Islands
    UNHANDLED_COUNTRY(G, G)  // Guernsey
    UNHANDLED_COUNTRY(G, I)  // Gibraltar
    UNHANDLED_COUNTRY(G, S)  // South Georgia and the South Sandwich
                             //   Islands
    UNHANDLED_COUNTRY(I, M)  // Isle of Man
    UNHANDLED_COUNTRY(I, O)  // British Indian Ocean Territory
    UNHANDLED_COUNTRY(J, E)  // Jersey
    UNHANDLED_COUNTRY(K, Y)  // Cayman Islands
    UNHANDLED_COUNTRY(M, S)  // Montserrat
    UNHANDLED_COUNTRY(M, T)  // Malta
    UNHANDLED_COUNTRY(P, N)  // Pitcairn Islands
    UNHANDLED_COUNTRY(S, H)  // Saint Helena, Ascension Island, and Tristan da
                             //   Cunha
    UNHANDLED_COUNTRY(T, C)  // Turks and Caicos Islands
    UNHANDLED_COUNTRY(V, G)  // British Virgin Islands
    END_UNHANDLED_COUNTRIES(G, B)

    // Countries using the "United States" engine set.
    UNHANDLED_COUNTRY(A, S)  // American Samoa
    UNHANDLED_COUNTRY(G, U)  // Guam
    UNHANDLED_COUNTRY(M, P)  // Northern Mariana Islands
    UNHANDLED_COUNTRY(U, M)  // U.S. Minor Outlying Islands
    UNHANDLED_COUNTRY(V, I)  // U.S. Virgin Islands
    END_UNHANDLED_COUNTRIES(U, S)

    // Countries using the "default" engine set.
    UNHANDLED_COUNTRY(A, F)  // Afghanistan
    UNHANDLED_COUNTRY(A, G)  // Antigua and Barbuda
    UNHANDLED_COUNTRY(A, I)  // Anguilla
    UNHANDLED_COUNTRY(A, O)  // Angola
    UNHANDLED_COUNTRY(A, Q)  // Antarctica
    UNHANDLED_COUNTRY(B, B)  // Barbados
    UNHANDLED_COUNTRY(B, D)  // Bangladesh
    UNHANDLED_COUNTRY(B, S)  // Bahamas
    UNHANDLED_COUNTRY(B, T)  // Bhutan
    UNHANDLED_COUNTRY(B, W)  // Botswana
    UNHANDLED_COUNTRY(C, U)  // Cuba
    UNHANDLED_COUNTRY(D, M)  // Dominica
    UNHANDLED_COUNTRY(E, R)  // Eritrea
    UNHANDLED_COUNTRY(E, T)  // Ethiopia
    UNHANDLED_COUNTRY(F, J)  // Fiji
    UNHANDLED_COUNTRY(F, M)  // Micronesia
    UNHANDLED_COUNTRY(G, D)  // Grenada
    UNHANDLED_COUNTRY(G, E)  // Georgia
    UNHANDLED_COUNTRY(G, H)  // Ghana
    UNHANDLED_COUNTRY(G, M)  // Gambia
    UNHANDLED_COUNTRY(G, Q)  // Equatorial Guinea
    UNHANDLED_COUNTRY(G, Y)  // Guyana
    UNHANDLED_COUNTRY(K, H)  // Cambodia
    UNHANDLED_COUNTRY(K, I)  // Kiribati
    UNHANDLED_COUNTRY(K, M)  // Comoros
    UNHANDLED_COUNTRY(K, N)  // Saint Kitts and Nevis
    UNHANDLED_COUNTRY(K, P)  // North Korea
    UNHANDLED_COUNTRY(L, A)  // Laos
    UNHANDLED_COUNTRY(L, C)  // Saint Lucia
    UNHANDLED_COUNTRY(L, K)  // Sri Lanka
    UNHANDLED_COUNTRY(L, R)  // Liberia
    UNHANDLED_COUNTRY(L, S)  // Lesotho
    UNHANDLED_COUNTRY(M, G)  // Madagascar
    UNHANDLED_COUNTRY(M, H)  // Marshall Islands
    UNHANDLED_COUNTRY(M, M)  // Myanmar
    UNHANDLED_COUNTRY(M, N)  // Mongolia
    UNHANDLED_COUNTRY(M, U)  // Mauritius
    UNHANDLED_COUNTRY(M, V)  // Maldives
    UNHANDLED_COUNTRY(M, W)  // Malawi
    UNHANDLED_COUNTRY(N, A)  // Namibia
    UNHANDLED_COUNTRY(N, G)  // Nigeria
    UNHANDLED_COUNTRY(N, P)  // Nepal
    UNHANDLED_COUNTRY(N, R)  // Nauru
    UNHANDLED_COUNTRY(P, G)  // Papua New Guinea
    UNHANDLED_COUNTRY(P, W)  // Palau
    UNHANDLED_COUNTRY(S, B)  // Solomon Islands
    UNHANDLED_COUNTRY(S, C)  // Seychelles
    UNHANDLED_COUNTRY(S, L)  // Sierra Leone
    UNHANDLED_COUNTRY(S, O)  // Somalia
    UNHANDLED_COUNTRY(S, R)  // Suriname
    UNHANDLED_COUNTRY(S, Z)  // Swaziland
    UNHANDLED_COUNTRY(T, O)  // Tonga
    UNHANDLED_COUNTRY(T, V)  // Tuvalu
    UNHANDLED_COUNTRY(U, G)  // Uganda
    UNHANDLED_COUNTRY(V, C)  // Saint Vincent and the Grenadines
    UNHANDLED_COUNTRY(V, U)  // Vanuatu
    UNHANDLED_COUNTRY(W, S)  // Samoa
    UNHANDLED_COUNTRY(Z, M)  // Zambia
    case kCountryIDUnknown:
    default:                // Unhandled location
    END_UNHANDLED_COUNTRIES(def, ault)
  }

  std::vector<std::unique_ptr<TemplateURLData>> t_urls;
  for (size_t i = 0; i < num_engines; ++i)
    t_urls.push_back(TemplateURLDataFromPrepopulatedEngine(*engines[i]));
  return t_urls;
}

std::vector<std::unique_ptr<TemplateURLData>> GetPrepopulatedTemplateURLData(
    PrefService* prefs) {
  std::vector<std::unique_ptr<TemplateURLData>> t_urls;
  if (!prefs)
    return t_urls;

  const base::ListValue* list = prefs->GetList(prefs::kSearchProviderOverrides);
  if (!list)
    return t_urls;

  size_t num_engines = list->GetSize();
  for (size_t i = 0; i != num_engines; ++i) {
    const base::DictionaryValue* engine;
    if (list->GetDictionary(i, &engine)) {
      auto t_url = TemplateURLDataFromOverrideDictionary(*engine);
      if (t_url)
        t_urls.push_back(std::move(t_url));
    }
  }
  return t_urls;
}

bool SameDomain(const GURL& given_url, const GURL& prepopulated_url) {
  return prepopulated_url.is_valid() &&
      net::registry_controlled_domains::SameDomainOrHost(
          given_url, prepopulated_url,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

}  // namespace

// Global functions -----------------------------------------------------------

void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterIntegerPref(prefs::kCountryIDAtInstall, kCountryIDUnknown);
  registry->RegisterListPref(prefs::kSearchProviderOverrides);
  registry->RegisterIntegerPref(prefs::kSearchProviderOverridesVersion, -1);
}

int GetDataVersion(PrefService* prefs) {
  // Allow tests to override the local version.
  return (prefs && prefs->HasPrefPath(prefs::kSearchProviderOverridesVersion)) ?
      prefs->GetInteger(prefs::kSearchProviderOverridesVersion) :
      kCurrentDataVersion;
}

std::vector<std::unique_ptr<TemplateURLData>> GetPrepopulatedEngines(
    PrefService* prefs,
    size_t* default_search_provider_index) {
  // If there is a set of search engines in the preferences file, it overrides
  // the built-in set.
  if (default_search_provider_index)
    *default_search_provider_index = 0;
  std::vector<std::unique_ptr<TemplateURLData>> t_urls =
      GetPrepopulatedTemplateURLData(prefs);
  if (!t_urls.empty())
    return t_urls;

  return GetPrepopulationSetFromCountryID(GetCountryIDFromPrefs(prefs));
}

std::unique_ptr<TemplateURLData> GetPrepopulatedEngine(PrefService* prefs,
                                                       int prepopulated_id) {
  size_t default_index;
  auto engines =
      TemplateURLPrepopulateData::GetPrepopulatedEngines(prefs, &default_index);
  for (auto& engine : engines) {
    if (engine->prepopulate_id == prepopulated_id)
      return std::move(engine);
  }
  return nullptr;
}

#if defined(OS_ANDROID)

std::vector<std::unique_ptr<TemplateURLData>> GetLocalPrepopulatedEngines(
    const std::string& locale,
    PrefService* prefs) {
  int country_id = CountryStringToCountryID(locale);
  if (country_id == kCountryIDUnknown ||
      country_id == GetCountryIDFromPrefs(prefs)) {
    return std::vector<std::unique_ptr<TemplateURLData>>();
  }

  return GetPrepopulationSetFromCountryID(country_id);
}

#endif

std::vector<const PrepopulatedEngine*> GetAllPrepopulatedEngines() {
  return std::vector<const PrepopulatedEngine*>(std::begin(kAllEngines),
                                                std::end(kAllEngines));
}

void ClearPrepopulatedEnginesInPrefs(PrefService* prefs) {
  if (!prefs)
    return;

  prefs->ClearPref(prefs::kSearchProviderOverrides);
  prefs->ClearPref(prefs::kSearchProviderOverridesVersion);
}

std::unique_ptr<TemplateURLData> GetPrepopulatedDefaultSearch(
    PrefService* prefs) {
  size_t default_search_index;
  // This could be more efficient.  We are loading all the URLs to only keep
  // the first one.
  std::vector<std::unique_ptr<TemplateURLData>> loaded_urls =
      GetPrepopulatedEngines(prefs, &default_search_index);

  return (default_search_index < loaded_urls.size())
             ? std::move(loaded_urls[default_search_index])
             : nullptr;
}

SearchEngineType GetEngineType(const GURL& url) {
  DCHECK(url.is_valid());

  // Check using TLD+1s, in order to more aggressively match search engine types
  // for data imported from other browsers.
  //
  // First special-case Google, because the prepopulate URL for it will not
  // convert to a GURL and thus won't have an origin.  Instead see if the
  // incoming URL's host is "[*.]google.<TLD>".
  if (google_util::IsGoogleHostname(url.host(),
                                    google_util::DISALLOW_SUBDOMAIN))
    return google.type;

  // Now check the rest of the prepopulate data.
  for (size_t i = 0; i < arraysize(kAllEngines); ++i) {
    // First check the main search URL.
    if (SameDomain(url, GURL(kAllEngines[i]->search_url)))
      return kAllEngines[i]->type;

    // Then check the alternate URLs.
    for (size_t j = 0; j < kAllEngines[i]->alternate_urls_size; ++j) {
      if (SameDomain(url, GURL(kAllEngines[i]->alternate_urls[j])))
        return kAllEngines[i]->type;
    }
  }

  return SEARCH_ENGINE_OTHER;
}

#if defined(OS_WIN)

int GetCurrentCountryID() {
  return GeoIDToCountryID(GetUserGeoID(GEOCLASS_NATION));
}

#elif defined(OS_MACOSX)

int GetCurrentCountryID() {
  base::ScopedCFTypeRef<CFLocaleRef> locale(CFLocaleCopyCurrent());
  CFStringRef country = (CFStringRef)CFLocaleGetValue(locale.get(),
                                                      kCFLocaleCountryCode);
  if (!country)
    return kCountryIDUnknown;

  UniChar isobuf[2];
  CFRange char_range = CFRangeMake(0, 2);
  CFStringGetCharacters(country, char_range, isobuf);

  return CountryCharsToCountryIDWithUpdate(static_cast<char>(isobuf[0]),
                                           static_cast<char>(isobuf[1]));
}

#elif defined(OS_ANDROID)

int GetCurrentCountryID() {
  return CountryStringToCountryID(base::android::GetDefaultCountryCode());
}

#elif defined(OS_POSIX)

int GetCurrentCountryID() {
  const char* locale = setlocale(LC_MESSAGES, NULL);
  if (!locale)
    return kCountryIDUnknown;

  // The format of a locale name is:
  // language[_territory][.codeset][@modifier], where territory is an ISO 3166
  // country code, which is what we want.

  // First remove the language portion.
  std::string locale_str(locale);
  size_t territory_delim = locale_str.find('_');
  if (territory_delim == std::string::npos)
    return kCountryIDUnknown;
  locale_str.erase(0, territory_delim + 1);

  // Next remove any codeset/modifier portion and uppercase.
  return CountryStringToCountryID(
      base::ToUpperASCII(locale_str.substr(0, locale_str.find_first_of(".@"))));
}

#endif  // OS_*

}  // namespace TemplateURLPrepopulateData
