#include "calendar_engine/teams_extractor.h"
#include "calendar_engine/ics_parser.h"
#include <algorithm>
#include <cctype>
#include <vector>

namespace calendar {

namespace {

std::string ToLower(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (char c : sv) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

std::string UnescapeHtmlEntities(std::string_view sv) {
    std::string result;
    result.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '&') {
            if (sv.substr(i).starts_with("&amp;")) {
                result.push_back('&');
                i += 4;
                continue;
            } else if (sv.substr(i).starts_with("&quot;")) {
                result.push_back('"');
                i += 5;
                continue;
            } else if (sv.substr(i).starts_with("&apos;")) {
                result.push_back('\'');
                i += 5;
                continue;
            } else if (sv.substr(i).starts_with("&#39;")) {
                result.push_back('\'');
                i += 4;
                continue;
            } else if (sv.substr(i).starts_with("&lt;")) {
                result.push_back('<');
                i += 3;
                continue;
            } else if (sv.substr(i).starts_with("&gt;")) {
                result.push_back('>');
                i += 3;
                continue;
            }
        }
        result.push_back(sv[i]);
    }
    return result;
}

// Clean trailing punctuation attached to URLs in plain text sentences
std::string_view TrimTrailingUrlPunctuation(std::string_view url) {
    while (!url.empty()) {
        char back = url.back();
        if (back == '.' || back == ',' || back == ';' || back == ')' || 
            back == ']' || back == '}' || back == '>' || back == '"' || 
            back == '\'' || back == '\\' || back == '\r' || back == '\n') {
            url.remove_suffix(1);
        } else {
            break;
        }
    }
    return url;
}

// Extract host and path from a URL
bool ParseUrlHostAndPath(std::string_view url, std::string& out_host, std::string& out_path) {
    size_t scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) {
        return false;
    }

    size_t host_start = scheme_pos + 3;
    size_t host_end = url.find_first_of("/:?#", host_start);

    if (host_end == std::string::npos) {
        out_host = ToLower(url.substr(host_start));
        out_path = "/";
    } else {
        out_host = ToLower(url.substr(host_start, host_end - host_start));
        out_path = std::string(url.substr(host_end));
    }

    return true;
}

} // namespace

bool TeamsExtractor::IsTeamsUrl(std::string_view url) {
    std::string host, path;
    if (!ParseUrlHostAndPath(url, host, path)) {
        return false;
    }

    // Check Teams host domains
    bool is_teams_host = false;
    if (host == "teams.microsoft.com" || 
        (host.size() > 18 && host.ends_with(".teams.microsoft.com"))) {
        is_teams_host = true;
    } else if (host == "teams.live.com" || 
               (host.size() > 15 && host.ends_with(".teams.live.com"))) {
        is_teams_host = true;
    } else if (host == "teams.microsoft.us" || 
               (host.size() > 19 && host.ends_with(".teams.microsoft.us"))) {
        is_teams_host = true;
    }

    if (!is_teams_host) {
        return false;
    }

    // Check meeting path indicators
    std::string lower_path = ToLower(path);
    if (lower_path.find("/l/meetup-join/") != std::string::npos ||
        lower_path.find("/meet/") != std::string::npos) {
        return true;
    }

    return false;
}

std::string TeamsExtractor::UrlDecode(std::string_view encoded) {
    std::string result;
    result.reserve(encoded.size());

    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            char h1 = encoded[i + 1];
            char h2 = encoded[i + 2];
            auto from_hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int d1 = from_hex(h1);
            int d2 = from_hex(h2);
            if (d1 >= 0 && d2 >= 0) {
                result.push_back(static_cast<char>((d1 << 4) | d2));
                i += 2;
                continue;
            }
        }
        result.push_back(encoded[i]);
    }

    return result;
}

std::optional<std::string> TeamsExtractor::DecodeSafeLink(std::string_view safelink_url) {
    std::string unescaped = UnescapeHtmlEntities(safelink_url);
    std::string host, path;
    if (!ParseUrlHostAndPath(unescaped, host, path)) {
        return std::nullopt;
    }

    if (host != "safelinks.protection.outlook.com" &&
        !(host.size() > 32 && host.ends_with(".safelinks.protection.outlook.com"))) {
        return std::nullopt;
    }

    // Look for ?url= or &url=
    size_t url_param_pos = unescaped.find("?url=");
    if (url_param_pos == std::string_view::npos) {
        url_param_pos = unescaped.find("&url=");
    }
    if (url_param_pos == std::string_view::npos) {
        return std::nullopt;
    }

    size_t val_start = url_param_pos + 5;
    size_t val_end = unescaped.find_first_of("&#", val_start);
    if (val_end == std::string_view::npos) {
        val_end = unescaped.size();
    }

    std::string_view encoded_target = std::string_view(unescaped).substr(val_start, val_end - val_start);
    std::string decoded = UrlDecode(encoded_target);
    if (!decoded.empty()) {
        return decoded;
    }

    return std::nullopt;
}

std::string TeamsExtractor::ExtractFromEvent(const CalendarEvent& event) {
    if (!event.teams_join_url.empty() && IsTeamsUrl(event.teams_join_url)) {
        return event.teams_join_url;
    }
    if (!event.teams_url.empty() && IsTeamsUrl(event.teams_url)) {
        return event.teams_url;
    }
    if (!event.location.empty()) {
        std::string from_loc = ExtractTeamsUrl(event.location);
        if (!from_loc.empty()) return from_loc;
    }
    if (!event.description.empty()) {
        std::string from_desc = ExtractTeamsUrl(event.description);
        if (!from_desc.empty()) return from_desc;
    }
    if (!event.summary.empty()) {
        std::string from_sum = ExtractTeamsUrl(event.summary);
        if (!from_sum.empty()) return from_sum;
    }
    return "";
}

std::string TeamsExtractor::ExtractTeamsUrl(std::string_view text) {
    // 1. Search for HTML anchor hrefs first: <a ... href="..." ...>
    size_t a_pos = 0;
    while (a_pos < text.size()) {
        size_t tag_start = text.find("<a", a_pos);
        if (tag_start == std::string_view::npos) {
            tag_start = text.find("<A", a_pos);
        }
        if (tag_start == std::string_view::npos) {
            break;
        }

        // Must be followed by whitespace
        if (tag_start + 2 < text.size() && !std::isspace(static_cast<unsigned char>(text[tag_start + 2]))) {
            a_pos = tag_start + 2;
            continue;
        }

        size_t tag_end = text.find('>', tag_start);
        if (tag_end == std::string_view::npos) {
            break;
        }

        std::string_view tag_content = text.substr(tag_start, tag_end - tag_start);
        // Find href=
        std::string lower_tag = ToLower(tag_content);
        size_t href_pos = lower_tag.find("href=");
        if (href_pos != std::string::npos) {
            size_t val_start = href_pos + 5;
            while (val_start < tag_content.size() && std::isspace(static_cast<unsigned char>(tag_content[val_start]))) {
                val_start++;
            }
            if (val_start < tag_content.size()) {
                char quote = tag_content[val_start];
                std::string raw_url;
                if (quote == '"' || quote == '\'') {
                    val_start++;
                    size_t quote_end = tag_content.find(quote, val_start);
                    if (quote_end != std::string_view::npos) {
                        raw_url = std::string(tag_content.substr(val_start, quote_end - val_start));
                    }
                } else {
                    size_t val_end = tag_content.find_first_of(" >", val_start);
                    raw_url = std::string(tag_content.substr(val_start, val_end - val_start));
                }

                if (!raw_url.empty()) {
                    std::string unescaped_url = UnescapeHtmlEntities(raw_url);
                    if (IsTeamsUrl(unescaped_url)) {
                        return unescaped_url;
                    }
                    auto decoded_sl = DecodeSafeLink(unescaped_url);
                    if (decoded_sl.has_value() && IsTeamsUrl(*decoded_sl)) {
                        return *decoded_sl;
                    }
                }
            }
        }

        a_pos = tag_end + 1;
    }

    // 2. Fallback to scanning URLs in arbitrary plain or mixed text
    size_t search_pos = 0;
    while (search_pos < text.size()) {
        size_t http_pos = text.find("http://", search_pos);
        size_t https_pos = text.find("https://", search_pos);
        size_t start_pos = std::string_view::npos;

        if (http_pos != std::string_view::npos && https_pos != std::string_view::npos) {
            start_pos = std::min(http_pos, https_pos);
        } else if (http_pos != std::string_view::npos) {
            start_pos = http_pos;
        } else if (https_pos != std::string_view::npos) {
            start_pos = https_pos;
        } else {
            break;
        }

        // Find end of URL (whitespace, quotes, angle brackets)
        size_t end_pos = text.find_first_of(" \t\r\n\"'<>", start_pos);
        if (end_pos == std::string_view::npos) {
            end_pos = text.size();
        }

        std::string_view raw_candidate = text.substr(start_pos, end_pos - start_pos);
        std::string_view cleaned_candidate = TrimTrailingUrlPunctuation(raw_candidate);
        std::string unescaped_candidate = UnescapeHtmlEntities(cleaned_candidate);

        // Check if direct Teams URL
        if (IsTeamsUrl(unescaped_candidate)) {
            return unescaped_candidate;
        }

        // Check if Outlook SafeLink pointing to Teams
        auto decoded_opt = DecodeSafeLink(unescaped_candidate);
        if (decoded_opt.has_value() && IsTeamsUrl(*decoded_opt)) {
            return *decoded_opt;
        }

        search_pos = end_pos;
    }

    return "";
}

} // namespace calendar
