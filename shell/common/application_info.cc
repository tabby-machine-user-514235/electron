// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/application_info.h"

#include "base/i18n/rtl.h"
#include "base/no_destructor.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/chrome_version.h"
#include "components/embedder_support/user_agent_utils.h"
#include "electron/electron_version.h"
#include "shell/browser/browser.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"

namespace electron {

std::string& OverriddenApplicationName() {
  static base::NoDestructor<std::string> overridden_application_name;
  return *overridden_application_name;
}

std::string& OverriddenApplicationVersion() {
  static base::NoDestructor<std::string> overridden_application_version;
  return *overridden_application_version;
}

std::string GetPossiblyOverriddenApplicationName() {
  std::string ret = OverriddenApplicationName();
  if (!ret.empty())
    return ret;
  return GetApplicationName();
}

std::string GetApplicationUserAgent() {
  // Construct user agent string.
  std::string user_agent = "Chrome/" CHROME_VERSION_STRING;
  return embedder_support::BuildUserAgentFromProduct(user_agent);
}

bool IsAppRTL() {
  const std::string& locale = g_browser_process->GetApplicationLocale();
  base::i18n::TextDirection text_direction =
      base::i18n::GetTextDirectionForLocaleInStartUp(locale.c_str());
  return text_direction == base::i18n::RIGHT_TO_LEFT;
}

}  // namespace electron
