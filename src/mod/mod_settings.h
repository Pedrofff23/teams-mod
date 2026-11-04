#pragma once

#include <string>
#include <string_view>
#include <compare>

namespace calendar {

struct ModSettings {
    static constexpr int kDefaultRefreshIntervalMinutes = 15;
    static constexpr int kMinimumRefreshIntervalMinutes = 1;

    std::wstring feed_url;
    int refresh_interval_minutes{kDefaultRefreshIntervalMinutes};
    bool show_past_events{false};
    bool enable_join_button{true};

    auto operator<=>(const ModSettings&) const = default;
    bool operator==(const ModSettings&) const = default;

    // Validate and normalize settings (clamps refresh interval, trims whitespace)
    void Normalize();

    // Whether a non-empty feed URL is configured
    bool IsFeedUrlConfigured() const;

    // Get the feed URL converted to UTF-8
    std::string GetFeedUrlUtf8() const;

    // Create ModSettings from explicit values
    static ModSettings FromValues(
        std::wstring_view url,
        int refresh_minutes = kDefaultRefreshIntervalMinutes,
        bool show_past = false,
        bool enable_join = true
    );

    static ModSettings FromValuesUtf8(
        std::string_view url,
        int refresh_minutes = kDefaultRefreshIntervalMinutes,
        bool show_past = false,
        bool enable_join = true
    );

    // Read current settings from Windhawk API (if in mod context)
    void LoadFromWindhawk();
};

} // namespace calendar
