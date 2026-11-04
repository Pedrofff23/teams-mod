#include "mod/mod_settings.h"
#include <windows.h>
#include <algorithm>
#include <cctype>

#ifdef WH_MOD
#include <windhawk_api.h>
#endif

namespace calendar {

namespace {

std::wstring TrimWhitespace(std::wstring_view sv) {
    size_t first = 0;
    while (first < sv.size() && (sv[first] == L' ' || sv[first] == L'\t' || sv[first] == L'\r' || sv[first] == L'\n')) {
        ++first;
    }
    if (first == sv.size()) {
        return L"";
    }
    size_t last = sv.size() - 1;
    while (last > first && (sv[last] == L' ' || sv[last] == L'\t' || sv[last] == L'\r' || sv[last] == L'\n')) {
        --last;
    }
    return std::wstring(sv.substr(first, last - first + 1));
}

std::wstring Utf8ToUtf16(std::string_view utf8) {
    if (utf8.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size_needed <= 0) return L"";
    std::wstring result(size_needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), size_needed);
    return result;
}

std::string Utf16ToUtf8(std::wstring_view utf16) {
    if (utf16.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) return "";
    std::string result(size_needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()), result.data(), size_needed, nullptr, nullptr);
    return result;
}

} // namespace

void ModSettings::Normalize() {
    feed_url = TrimWhitespace(feed_url);
    if (refresh_interval_minutes < kMinimumRefreshIntervalMinutes) {
        refresh_interval_minutes = kMinimumRefreshIntervalMinutes;
    }
}

bool ModSettings::IsFeedUrlConfigured() const {
    return !feed_url.empty();
}

std::string ModSettings::GetFeedUrlUtf8() const {
    return Utf16ToUtf8(feed_url);
}

ModSettings ModSettings::FromValues(
    std::wstring_view url,
    int refresh_minutes,
    bool show_past,
    bool enable_join
) {
    ModSettings settings;
    settings.feed_url = std::wstring(url);
    settings.refresh_interval_minutes = refresh_minutes;
    settings.show_past_events = show_past;
    settings.enable_join_button = enable_join;
    settings.Normalize();
    return settings;
}

ModSettings ModSettings::FromValuesUtf8(
    std::string_view url,
    int refresh_minutes,
    bool show_past,
    bool enable_join
) {
    return FromValues(Utf8ToUtf16(url), refresh_minutes, show_past, enable_join);
}

void ModSettings::LoadFromWindhawk() {
#ifdef WH_MOD
    PCWSTR url_setting = Wh_GetStringSetting(L"feed_url");
    if (url_setting) {
        feed_url = url_setting;
        Wh_FreeStringSetting(url_setting);
    } else {
        feed_url.clear();
    }

    refresh_interval_minutes = Wh_GetIntSetting(L"refresh_interval_minutes");
    if (refresh_interval_minutes <= 0) {
        refresh_interval_minutes = kDefaultRefreshIntervalMinutes;
    }

    show_past_events = Wh_GetIntSetting(L"show_past_events") != 0;
    enable_join_button = Wh_GetIntSetting(L"enable_join_button") != 0;

    Normalize();
#else
    Normalize();
#endif
}

} // namespace calendar
