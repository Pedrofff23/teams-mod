#include "ui/xaml_injector.h"
#include <windows.h>
#include <shellapi.h>
#include <sstream>
#include <ctime>
#include <cctype>

namespace calendar {

bool UrlSanitizer::IsSafeMeetingUrl(std::string_view url) {
    std::string sanitized;
    return SanitizeAndValidateUrl(url, &sanitized);
}

bool UrlSanitizer::SanitizeAndValidateUrl(std::string_view url, std::string* sanitized_out) {
    // 1. Trim leading and trailing whitespace
    size_t start = 0;
    while (start < url.size() && std::isspace(static_cast<unsigned char>(url[start]))) {
        start++;
    }
    size_t end = url.size();
    while (end > start && std::isspace(static_cast<unsigned char>(url[end - 1]))) {
        end--;
    }

    if (start >= end) {
        return false;
    }

    std::string_view trimmed = url.substr(start, end - start);

    // 2. Reject control characters (\r, \n, \0, tabs, etc.)
    for (char c : trimmed) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc < 32 || uc == 127) {
            return false;
        }
    }

    // 3. Find scheme separator ':'
    size_t colon_pos = trimmed.find(':');
    if (colon_pos == std::string_view::npos || colon_pos == 0) {
        return false;
    }

    std::string scheme;
    scheme.reserve(colon_pos);
    for (size_t i = 0; i < colon_pos; ++i) {
        char c = trimmed[i];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.') {
            return false;
        }
        scheme.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    // 4. Validate allowed schemes: http, https, msteams
    if (scheme == "https" || scheme == "http") {
        if (!trimmed.substr(colon_pos).starts_with("://")) {
            return false;
        }
        // Ensure host is non-empty
        size_t host_start = colon_pos + 3;
        if (host_start >= trimmed.size()) {
            return false;
        }
        size_t host_end = trimmed.find_first_of("/?#", host_start);
        std::string_view host = (host_end == std::string_view::npos)
            ? trimmed.substr(host_start)
            : trimmed.substr(host_start, host_end - host_start);
        if (host.empty()) {
            return false;
        }
    } else if (scheme == "msteams") {
        // e.g. msteams:/l/meetup-join/... or msteams://...
        size_t rest_start = colon_pos + 1;
        while (rest_start < trimmed.size() && trimmed[rest_start] == '/') {
            rest_start++;
        }
        if (rest_start >= trimmed.size()) {
            return false;
        }
    } else {
        // Reject all other schemes: file, javascript, data, cmd, powershell, etc.
        return false;
    }

    if (sanitized_out) {
        *sanitized_out = std::string(trimmed);
    }
    return true;
}

std::string XamlMarkupGenerator::EscapeXml(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 11 / 10);
    for (char c : text) {
        switch (c) {
        case '&':  out.append("&amp;"); break;
        case '<':  out.append("&lt;"); break;
        case '>':  out.append("&gt;"); break;
        case '"':  out.append("&quot;"); break;
        case '\'': out.append("&apos;"); break;
        default:   out.push_back(c); break;
        }
    }
    return out;
}

std::string XamlMarkupGenerator::GenerateAgendaXaml(const AgendaDayViewModel& viewModel, int maxHeight) {
    ThemePalette palette = ThemePalette::ForTheme(viewModel.theme);
    std::ostringstream ss;

    ss << "<ScrollViewer x:Name=\"TeamsAgendaScrollViewer\" MaxHeight=\"" << maxHeight
       << "\" VerticalScrollBarVisibility=\"Auto\" HorizontalScrollBarVisibility=\"Disabled\" "
       << "Padding=\"4,4,4,4\" Margin=\"0,8,0,0\">\n";
    ss << "  <StackPanel x:Name=\"TeamsAgendaRootStackPanel\" Spacing=\"6\" HorizontalAlignment=\"Stretch\">\n";

    // Date header
    ss << "    <TextBlock x:Name=\"TeamsAgendaDateHeader\" Text=\""
       << EscapeXml(viewModel.formatted_date_header)
       << "\" FontSize=\"12\" FontWeight=\"SemiBold\" Foreground=\""
       << palette.header_text << "\" Margin=\"4,2,4,4\"/>\n";

    if (viewModel.is_empty()) {
        ss << "    <Border x:Name=\"TeamsAgendaEmptyBorder\" Padding=\"16\" HorizontalAlignment=\"Center\" VerticalAlignment=\"Center\">\n";
        ss << "      <TextBlock x:Name=\"TeamsAgendaEmptyText\" Text=\""
           << EscapeXml(viewModel.empty_state_message)
           << "\" Foreground=\"" << palette.empty_text
           << "\" FontStyle=\"Italic\" TextAlignment=\"Center\"/>\n";
        ss << "    </Border>\n";
    } else {
        for (size_t i = 0; i < viewModel.items.size(); ++i) {
            const auto& item = viewModel.items[i];
            ss << "    <Border x:Name=\"MeetingCard_" << i << "\" Background=\"" << item.card_bg()
               << "\" BorderBrush=\"" << item.card_border()
               << "\" BorderThickness=\"1\" CornerRadius=\"6\" Padding=\"10,8,10,8\" Margin=\"0,2,0,2\">\n";
            ss << "      <StackPanel Spacing=\"4\">\n";

            // Time and Status badge row
            ss << "        <Grid ColumnDefinitions=\"Auto,*,Auto\">\n";
            ss << "          <TextBlock Grid.Column=\"0\" Text=\""
               << EscapeXml(item.formatted_time_range)
               << "\" FontSize=\"12\" FontWeight=\"SemiBold\" Foreground=\""
               << item.secondary_text_color() << "\"/>\n";
            ss << "          <StackPanel Grid.Column=\"2\" Orientation=\"Horizontal\" Spacing=\"6\" HorizontalAlignment=\"Right\">\n";
            ss << "            <Border Background=\"" << item.status_badge_bg
               << "\" CornerRadius=\"3\" Padding=\"5,1,5,1\">\n";
            ss << "              <TextBlock Text=\"" << EscapeXml(item.status_badge_label)
               << "\" FontSize=\"10\" FontWeight=\"Bold\" Foreground=\""
               << item.status_badge_fg << "\"/>\n";
            ss << "            </Border>\n";

            if (item.is_teams) {
                ss << "            <Border Background=\"" << item.accent_color()
                   << "\" CornerRadius=\"3\" Padding=\"4,1,4,1\">\n";
                ss << "              <TextBlock Text=\"Teams\" FontSize=\"10\" FontWeight=\"Bold\" Foreground=\"#FFFFFF\"/>\n";
                ss << "            </Border>\n";
            }
            ss << "          </StackPanel>\n";
            ss << "        </Grid>\n";

            // Title
            ss << "        <TextBlock Text=\"" << EscapeXml(item.title)
               << "\" FontSize=\"13\" FontWeight=\"Bold\" TextWrapping=\"Wrap\" Foreground=\""
               << item.primary_text_color() << "\"/>\n";

            // Location if non-empty
            if (!item.location.empty()) {
                ss << "        <TextBlock Text=\"" << EscapeXml(item.location)
                   << "\" FontSize=\"11\" Foreground=\""
                   << item.secondary_text_color() << "\" TextTrimming=\"CharacterEllipsis\"/>\n";
            }

            // Teams Join Button (Fluent Design styled with rounded corners, icon, and hover-ready styling)
            if (item.is_teams && item.enable_join_button && !item.join_url.empty()) {
                ss << "        <Button x:Name=\"JoinButton_" << i << "\" Tag=\""
                   << EscapeXml(item.join_url)
                   << "\" Background=\"" << item.accent_color()
                   << "\" Foreground=\"#FFFFFF\" BorderThickness=\"1\" BorderBrush=\"#00000000\" "
                   << "CornerRadius=\"4\" Padding=\"10,4,10,4\" HorizontalAlignment=\"Left\" Margin=\"0,4,0,0\">\n";
                ss << "          <StackPanel Orientation=\"Horizontal\" Spacing=\"6\" VerticalAlignment=\"Center\">\n";
                ss << "            <TextBlock Text=\"&#xE714;\" FontFamily=\"Segoe Fluent Icons\" FontSize=\"12\" VerticalAlignment=\"Center\"/>\n";
                ss << "            <TextBlock Text=\"Join Meeting\" FontWeight=\"SemiBold\" FontSize=\"12\" VerticalAlignment=\"Center\"/>\n";
                ss << "          </StackPanel>\n";
                ss << "        </Button>\n";
            }

            ss << "      </StackPanel>\n";
            ss << "    </Border>\n";
        }
    }

    ss << "  </StackPanel>\n";
    ss << "</ScrollViewer>";
    return ss.str();
}

bool WinUiXamlHostAdapter::FindTargetContainer(std::string_view container_name) {
    return !container_name.empty();
}

bool WinUiXamlHostAdapter::InjectElement(std::string_view container_name, std::string_view element_id, std::string_view xaml_content) {
    return !container_name.empty() && !element_id.empty() && !xaml_content.empty();
}

bool WinUiXamlHostAdapter::RemoveElement(std::string_view container_name, std::string_view element_id) {
    return !container_name.empty() && !element_id.empty();
}

bool WinUiXamlHostAdapter::UpdateElement(std::string_view element_id, std::string_view xaml_content) {
    return !element_id.empty() && !xaml_content.empty();
}

ThemeMode WinUiXamlHostAdapter::DetectThemeMode() const {
    HIGHCONTRASTW hc = { sizeof(hc) };
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON)) {
        return ThemeMode::HighContrast;
    }

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD appsUseLightTheme = 0;
        DWORD dataSize = sizeof(appsUseLightTheme);
        DWORD type = 0;
        LONG res = RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, &type,
                                    reinterpret_cast<LPBYTE>(&appsUseLightTheme), &dataSize);
        RegCloseKey(hKey);

        if (res == ERROR_SUCCESS && appsUseLightTheme == 1) {
            return ThemeMode::Light;
        }
    }

    return ThemeMode::Dark;
}

bool WinUiXamlHostAdapter::OpenMeetingUrl(std::string_view url) {
    std::string sanitized;
    if (!UrlSanitizer::SanitizeAndValidateUrl(url, &sanitized)) {
        return false;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, sanitized.data(), static_cast<int>(sanitized.size()), nullptr, 0);
    if (wlen <= 0) {
        return false;
    }
    std::wstring wurl(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, sanitized.data(), static_cast<int>(sanitized.size()), wurl.data(), wlen);

    HINSTANCE hInst = ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(hInst) > 32;
}

XamlInjector::XamlInjector() = default;

XamlInjector::~XamlInjector() {
    Shutdown();
}

bool XamlInjector::Initialize(
    std::shared_ptr<CalendarCache> cache,
    const ModSettings& settings,
    std::shared_ptr<IXamlHostAdapter> adapter
) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_ = std::move(cache);
    settings_ = settings;
    adapter_ = std::move(adapter);
    if (!adapter_) {
        adapter_ = std::make_shared<WinUiXamlHostAdapter>();
    }

    current_theme_ = adapter_->DetectThemeMode();

    // Default selected date to today (local system date)
    std::time_t t = std::time(nullptr);
    std::tm tm_local{};
#if defined(_WIN32)
    localtime_s(&tm_local, &t);
#else
    localtime_r(&t, &tm_local);
#endif
    selected_date_ = CalendarDate{tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday};

    RenderAndInjectLocked(std::nullopt);
    return true;
}

void XamlInjector::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (is_injected_ && adapter_) {
        adapter_->RemoveElement(kDefaultTargetContainer, kAgendaPanelElementId);
        is_injected_ = false;
    }
    current_xaml_.clear();
    current_view_model_ = AgendaDayViewModel();
}

void XamlInjector::OnDateSelected(const CalendarDate& new_date, std::optional<CalendarDateTime> reference_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    selected_date_ = new_date;
    RenderAndInjectLocked(reference_time);
}

void XamlInjector::OnThemeChanged(ThemeMode new_theme) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_theme_ = new_theme;
    RenderAndInjectLocked(std::nullopt);
}

void XamlInjector::OnSettingsChanged(const ModSettings& new_settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = new_settings;
    RenderAndInjectLocked(std::nullopt);
}

void XamlInjector::OnEventsUpdated(std::optional<CalendarDateTime> reference_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    RenderAndInjectLocked(reference_time);
}

bool XamlInjector::HandleJoinButtonClick(std::string_view join_url) {
    if (!UrlSanitizer::IsSafeMeetingUrl(join_url)) {
        return false;
    }

    std::shared_ptr<IXamlHostAdapter> adapter;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        adapter = adapter_;
    }
    if (adapter) {
        return adapter->OpenMeetingUrl(join_url);
    }
    return false;
}

const CalendarDate& XamlInjector::GetSelectedDate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return selected_date_;
}

ThemeMode XamlInjector::GetCurrentTheme() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_theme_;
}

bool XamlInjector::IsInjected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return is_injected_;
}

std::string XamlInjector::GetCurrentXaml() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_xaml_;
}

AgendaDayViewModel XamlInjector::GetCurrentViewModel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_view_model_;
}

void XamlInjector::RenderAndInjectLocked(std::optional<CalendarDateTime> reference_time) {
    CalendarDateTime ref_time = reference_time.has_value() ? *reference_time : CalendarDateTime::now_local();

    if (cache_) {
        current_view_model_ = AgendaPresenter::PresentDay(
            *cache_,
            selected_date_,
            current_theme_,
            ref_time,
            settings_.show_past_events,
            settings_.enable_join_button
        );
    } else {
        current_view_model_ = AgendaDayViewModel();
        current_view_model_.selected_date = selected_date_;
        current_view_model_.formatted_date_header = AgendaPresenter::FormatDateHeader(selected_date_);
        current_view_model_.theme = current_theme_;
    }

    current_xaml_ = XamlMarkupGenerator::GenerateAgendaXaml(current_view_model_, kDefaultMaxHeight);

    if (adapter_) {
        if (!is_injected_) {
            if (adapter_->InjectElement(kDefaultTargetContainer, kAgendaPanelElementId, current_xaml_)) {
                is_injected_ = true;
            }
        } else {
            adapter_->UpdateElement(kAgendaPanelElementId, current_xaml_);
        }
    }
}

} // namespace calendar
