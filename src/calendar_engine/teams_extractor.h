#pragma once

#include <string>
#include <string_view>
#include <optional>

namespace calendar {

struct CalendarEvent;

class TeamsExtractor {
public:
    // Extract a direct Microsoft Teams join URL from arbitrary text (plain text, HTML, etc.)
    static std::string ExtractTeamsUrl(std::string_view text);

    // Extract a Microsoft Teams join URL from an event (checks location, description, url fields)
    static std::string ExtractFromEvent(const CalendarEvent& event);

    // Check if a URL is a valid Microsoft Teams meeting URL
    static bool IsTeamsUrl(std::string_view url);

    // URL-decode a percent-encoded string
    static std::string UrlDecode(std::string_view encoded);

    // Decode an Outlook SafeLinks URL and extract the underlying target URL if present
    static std::optional<std::string> DecodeSafeLink(std::string_view safelink_url);
};

} // namespace calendar
