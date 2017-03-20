// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/common/extension_l10n_util.h"

#include <stddef.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_file_value_serializer.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "extensions/common/constants.h"
#include "extensions/common/error_utils.h"
#include "extensions/common/file_util.h"
#include "extensions/common/manifest_constants.h"
#include "extensions/common/message_bundle.h"
#include "third_party/icu/source/common/unicode/uloc.h"
#include "ui/base/l10n/l10n_util.h"

namespace errors = extensions::manifest_errors;
namespace keys = extensions::manifest_keys;

namespace {

// Loads contents of the messages file for given locale. If file is not found,
// or there was parsing error we return NULL and set |error|.
// Caller owns the returned object.
std::unique_ptr<base::DictionaryValue> LoadMessageFile(
    const base::FilePath& locale_path,
    const std::string& locale,
    std::string* error) {
  base::FilePath file =
      locale_path.AppendASCII(locale).Append(extensions::kMessagesFilename);
  JSONFileValueDeserializer messages_deserializer(file);
  std::unique_ptr<base::DictionaryValue> dictionary =
      base::DictionaryValue::From(
          messages_deserializer.Deserialize(NULL, error));
  if (!dictionary) {
    if (error->empty()) {
      // JSONFileValueSerializer just returns NULL if file cannot be found. It
      // doesn't set the error, so we have to do it.
      *error = base::StringPrintf("Catalog file is missing for locale %s.",
                                  locale.c_str());
    } else {
      *error = extensions::ErrorUtils::FormatErrorMessage(
          errors::kLocalesInvalidLocale,
          base::UTF16ToUTF8(file.LossyDisplayName()),
          *error);
    }
  }

  return dictionary;
}

// Localizes manifest value of string type for a given key.
bool LocalizeManifestValue(const std::string& key,
                           const extensions::MessageBundle& messages,
                           base::DictionaryValue* manifest,
                           std::string* error) {
  std::string result;
  if (!manifest->GetString(key, &result))
    return true;

  if (!messages.ReplaceMessages(&result, error))
    return false;

  manifest->SetString(key, result);
  return true;
}

// Localizes manifest value of list type for a given key.
bool LocalizeManifestListValue(const std::string& key,
                               const extensions::MessageBundle& messages,
                               base::DictionaryValue* manifest,
                               std::string* error) {
  base::ListValue* list = NULL;
  if (!manifest->GetList(key, &list))
    return true;

  bool ret = true;
  for (size_t i = 0; i < list->GetSize(); ++i) {
    std::string result;
    if (list->GetString(i, &result)) {
      if (messages.ReplaceMessages(&result, error))
        list->Set(i, new base::StringValue(result));
      else
        ret = false;
    }
  }
  return ret;
}

std::string& GetProcessLocale() {
  CR_DEFINE_STATIC_LOCAL(std::string, locale, ());
  return locale;
}

}  // namespace

namespace extension_l10n_util {

void SetProcessLocale(const std::string& locale) {
  GetProcessLocale() = locale;
}

std::string GetDefaultLocaleFromManifest(const base::DictionaryValue& manifest,
                                         std::string* error) {
  std::string default_locale;
  if (manifest.GetString(keys::kDefaultLocale, &default_locale))
    return default_locale;

  *error = errors::kInvalidDefaultLocale;
  return std::string();
}

bool ShouldRelocalizeManifest(const base::DictionaryValue* manifest) {
  if (!manifest)
    return false;

  if (!manifest->HasKey(keys::kDefaultLocale))
    return false;

  std::string manifest_current_locale;
  manifest->GetString(keys::kCurrentLocale, &manifest_current_locale);
  return manifest_current_locale != CurrentLocaleOrDefault();
}

bool LocalizeManifest(const extensions::MessageBundle& messages,
                      base::DictionaryValue* manifest,
                      std::string* error) {
  // Initialize name.
  std::string result;
  if (!manifest->GetString(keys::kName, &result)) {
    *error = errors::kInvalidName;
    return false;
  }
  if (!LocalizeManifestValue(keys::kName, messages, manifest, error)) {
    return false;
  }

  // Initialize short name.
  if (!LocalizeManifestValue(keys::kShortName, messages, manifest, error))
    return false;

  // Initialize description.
  if (!LocalizeManifestValue(keys::kDescription, messages, manifest, error))
    return false;

  // Initialize browser_action.default_title
  std::string key(keys::kBrowserAction);
  key.append(".");
  key.append(keys::kPageActionDefaultTitle);
  if (!LocalizeManifestValue(key, messages, manifest, error))
    return false;

  // Initialize page_action.default_title
  key.assign(keys::kPageAction);
  key.append(".");
  key.append(keys::kPageActionDefaultTitle);
  if (!LocalizeManifestValue(key, messages, manifest, error))
    return false;

  // Initialize omnibox.keyword.
  if (!LocalizeManifestValue(keys::kOmniboxKeyword, messages, manifest, error))
    return false;

  base::ListValue* file_handlers = NULL;
  if (manifest->GetList(keys::kFileBrowserHandlers, &file_handlers)) {
    key.assign(keys::kFileBrowserHandlers);
    for (size_t i = 0; i < file_handlers->GetSize(); i++) {
      base::DictionaryValue* handler = NULL;
      if (!file_handlers->GetDictionary(i, &handler)) {
        *error = errors::kInvalidFileBrowserHandler;
        return false;
      }
      if (!LocalizeManifestValue(
              keys::kPageActionDefaultTitle, messages, handler, error))
        return false;
    }
  }

  // Initialize all input_components
  base::ListValue* input_components = NULL;
  if (manifest->GetList(keys::kInputComponents, &input_components)) {
    for (size_t i = 0; i < input_components->GetSize(); ++i) {
      base::DictionaryValue* module = NULL;
      if (!input_components->GetDictionary(i, &module)) {
        *error = errors::kInvalidInputComponents;
        return false;
      }
      if (!LocalizeManifestValue(keys::kName, messages, module, error))
        return false;
      if (!LocalizeManifestValue(keys::kDescription, messages, module, error))
        return false;
    }
  }

  // Initialize app.launch.local_path.
  if (!LocalizeManifestValue(keys::kLaunchLocalPath, messages, manifest, error))
    return false;

  // Initialize app.launch.web_url.
  if (!LocalizeManifestValue(keys::kLaunchWebURL, messages, manifest, error))
    return false;

  // Initialize description of commmands.
  base::DictionaryValue* commands_handler = NULL;
  if (manifest->GetDictionary(keys::kCommands, &commands_handler)) {
    for (base::DictionaryValue::Iterator iter(*commands_handler);
         !iter.IsAtEnd();
         iter.Advance()) {
      key.assign(
          base::StringPrintf("commands.%s.description", iter.key().c_str()));
      if (!LocalizeManifestValue(key, messages, manifest, error))
        return false;
    }
  }

  // Initialize search_provider fields.
  base::DictionaryValue* search_provider = NULL;
  if (manifest->GetDictionary(keys::kOverrideSearchProvider,
                              &search_provider)) {
    for (base::DictionaryValue::Iterator iter(*search_provider);
         !iter.IsAtEnd();
         iter.Advance()) {
      key.assign(base::StringPrintf(
          "%s.%s", keys::kOverrideSearchProvider, iter.key().c_str()));
      bool success =
          (key == keys::kSettingsOverrideAlternateUrls)
              ? LocalizeManifestListValue(key, messages, manifest, error)
              : LocalizeManifestValue(key, messages, manifest, error);
      if (!success)
        return false;
    }
  }

  // Initialize chrome_settings_overrides.homepage.
  if (!LocalizeManifestValue(
          keys::kOverrideHomepage, messages, manifest, error))
    return false;

  // Initialize chrome_settings_overrides.startup_pages.
  if (!LocalizeManifestListValue(
          keys::kOverrideStartupPage, messages, manifest, error))
    return false;

  // Add current locale key to the manifest, so we can overwrite prefs
  // with new manifest when chrome locale changes.
  manifest->SetString(keys::kCurrentLocale, CurrentLocaleOrDefault());
  return true;
}

bool LocalizeExtension(const base::FilePath& extension_path,
                       base::DictionaryValue* manifest,
                       std::string* error) {
  DCHECK(manifest);

  std::string default_locale = GetDefaultLocaleFromManifest(*manifest, error);

  std::unique_ptr<extensions::MessageBundle> message_bundle(
      extensions::file_util::LoadMessageBundle(extension_path, default_locale,
                                               error));

  if (!message_bundle && !error->empty())
    return false;

  if (message_bundle && !LocalizeManifest(*message_bundle, manifest, error))
    return false;

  return true;
}

bool AddLocale(const std::set<std::string>& chrome_locales,
               const base::FilePath& locale_folder,
               const std::string& locale_name,
               std::set<std::string>* valid_locales,
               std::string* error) {
  // Accept name that starts with a . but don't add it to the list of supported
  // locales.
  if (base::StartsWith(locale_name, ".", base::CompareCase::SENSITIVE))
    return true;
  if (chrome_locales.find(locale_name) == chrome_locales.end()) {
    // Warn if there is an extension locale that's not in the Chrome list,
    // but don't fail.
    DLOG(WARNING) << base::StringPrintf("Supplied locale %s is not supported.",
                                        locale_name.c_str());
    return true;
  }
  // Check if messages file is actually present (but don't check content).
  if (!base::PathExists(locale_folder.Append(extensions::kMessagesFilename))) {
    *error = base::StringPrintf("Catalog file is missing for locale %s.",
                                locale_name.c_str());
    return false;
  }

  valid_locales->insert(locale_name);
  return true;
}

std::string CurrentLocaleOrDefault() {
  std::string current_locale = l10n_util::NormalizeLocale(GetProcessLocale());
  if (current_locale.empty())
    current_locale = "en";

  return current_locale;
}

void GetAllLocales(std::set<std::string>* all_locales) {
  const std::vector<std::string>& available_locales =
      l10n_util::GetAvailableLocales();
  // Add all parents of the current locale to the available locales set.
  // I.e. for sr_Cyrl_RS we add sr_Cyrl_RS, sr_Cyrl and sr.
  for (size_t i = 0; i < available_locales.size(); ++i) {
    std::vector<std::string> result;
    l10n_util::GetParentLocales(available_locales[i], &result);
    all_locales->insert(result.begin(), result.end());
  }
}

void GetAllFallbackLocales(const std::string& application_locale,
                           const std::string& default_locale,
                           std::vector<std::string>* all_fallback_locales) {
  DCHECK(all_fallback_locales);
  if (!application_locale.empty() && application_locale != default_locale)
    l10n_util::GetParentLocales(application_locale, all_fallback_locales);
  all_fallback_locales->push_back(default_locale);
}

bool GetValidLocales(const base::FilePath& locale_path,
                     std::set<std::string>* valid_locales,
                     std::string* error) {
  std::set<std::string> chrome_locales;
  GetAllLocales(&chrome_locales);

  // Enumerate all supplied locales in the extension.
  base::FileEnumerator locales(
      locale_path, false, base::FileEnumerator::DIRECTORIES);
  base::FilePath locale_folder;
  while (!(locale_folder = locales.Next()).empty()) {
    std::string locale_name = locale_folder.BaseName().MaybeAsASCII();
    if (locale_name.empty()) {
      NOTREACHED();
      continue;  // Not ASCII.
    }
    if (!AddLocale(
            chrome_locales, locale_folder, locale_name, valid_locales, error)) {
      valid_locales->clear();
      return false;
    }
  }

  if (valid_locales->empty()) {
    *error = errors::kLocalesNoValidLocaleNamesListed;
    return false;
  }

  return true;
}

extensions::MessageBundle* LoadMessageCatalogs(
    const base::FilePath& locale_path,
    const std::string& default_locale,
    const std::string& application_locale,
    std::string* error) {
  std::vector<std::string> all_fallback_locales;
  GetAllFallbackLocales(
      application_locale, default_locale, &all_fallback_locales);

  std::vector<std::unique_ptr<base::DictionaryValue>> catalogs;
  for (size_t i = 0; i < all_fallback_locales.size(); ++i) {
    // Skip all parent locales that are not supplied.
    base::FilePath this_locale_path =
        locale_path.AppendASCII(all_fallback_locales[i]);
    if (!base::PathExists(this_locale_path))
      continue;
    std::unique_ptr<base::DictionaryValue> catalog =
        LoadMessageFile(locale_path, all_fallback_locales[i], error);
    if (!catalog.get()) {
      // If locale is valid, but messages.json is corrupted or missing, return
      // an error.
      return nullptr;
    } else {
      catalogs.push_back(std::move(catalog));
    }
  }

  return extensions::MessageBundle::Create(catalogs, error);
}

bool ValidateExtensionLocales(const base::FilePath& extension_path,
                              const base::DictionaryValue* manifest,
                              std::string* error) {
  std::string default_locale = GetDefaultLocaleFromManifest(*manifest, error);

  if (default_locale.empty())
    return true;

  base::FilePath locale_path = extension_path.Append(extensions::kLocaleFolder);

  std::set<std::string> valid_locales;
  if (!GetValidLocales(locale_path, &valid_locales, error))
    return false;

  for (std::set<std::string>::const_iterator locale = valid_locales.begin();
       locale != valid_locales.end();
       ++locale) {
    std::string locale_error;
    std::unique_ptr<base::DictionaryValue> catalog =
        LoadMessageFile(locale_path, *locale, &locale_error);

    if (!locale_error.empty()) {
      if (!error->empty())
        error->append(" ");
      error->append(locale_error);
    }
  }

  return error->empty();
}

bool ShouldSkipValidation(const base::FilePath& locales_path,
                          const base::FilePath& locale_path,
                          const std::set<std::string>& all_locales) {
  // Since we use this string as a key in a DictionaryValue, be paranoid about
  // skipping any strings with '.'. This happens sometimes, for example with
  // '.svn' directories.
  base::FilePath relative_path;
  if (!locales_path.AppendRelativePath(locale_path, &relative_path)) {
    NOTREACHED();
    return true;
  }
  std::string subdir = relative_path.MaybeAsASCII();
  if (subdir.empty())
    return true;  // Non-ASCII.

  if (std::find(subdir.begin(), subdir.end(), '.') != subdir.end())
    return true;

  if (all_locales.find(subdir) == all_locales.end())
    return true;

  return false;
}

ScopedLocaleForTest::ScopedLocaleForTest()
    : locale_(extension_l10n_util::CurrentLocaleOrDefault()) {}

ScopedLocaleForTest::ScopedLocaleForTest(const std::string& locale)
    : locale_(extension_l10n_util::CurrentLocaleOrDefault()) {
  extension_l10n_util::SetProcessLocale(locale);
}

ScopedLocaleForTest::~ScopedLocaleForTest() {
  extension_l10n_util::SetProcessLocale(locale_);
}

}  // namespace extension_l10n_util
