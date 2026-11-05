// ==WindhawkMod==
// @id              teams-calendar-flyout
// @name            Microsoft Teams Calendar Flyout
// @description     Displays upcoming Microsoft Teams meetings in the Windows 11 calendar flyout
// @version         1.0.0
// @author          Pedrofff23
// @github          https://github.com/Pedrofff23/teams-mod
// @include         ShellHost.exe
// @include         ShellExperienceHost.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lwininet -lshell32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Microsoft Teams Calendar Flyout

Integrates Microsoft Teams upcoming meetings directly into the native Windows 11 taskbar calendar flyout (Notification Center / Clock flyout).

## Features
- **Daily Agenda**: Displays today's schedule directly inside the calendar flyout when clicking the taskbar clock.
- **Dynamic Meeting Status**: Color-coded badges indicating meeting status:
  - **Live** (Green): Meeting is currently in progress.
  - **Soon** (Amber): Meeting starts within 15 minutes.
  - **Future**: Upcoming meeting later today.
  - **Past**: Completed meeting earlier today.
- **One-Click Join**: Quick "Join Meeting" button launching directly into Microsoft Teams.
- **Background Sync**: Periodically fetches and parses your Outlook / Teams calendar feed via WinINet.
- **Native Fluent UI**: Blends seamlessly with Windows 11 Dark and Light themes.
- **Privacy-first**: Connects directly to your calendar feed URL without intermediate proxy servers.

## How to Configure
1. Open the **Settings** tab of this mod in Windhawk.
2. In **Outlook / Teams ICS Feed URL**, paste your published calendar ICS link:
   - Go to [Outlook on the web](https://outlook.office.com/mail/) -> **Settings** (gear icon) -> **Calendar** -> **Shared calendars**.
   - Under **Publish a calendar**, select your calendar and set permissions to *Can view all details*, then click **Publish**.
   - Copy the **ICS** link and paste it into the mod settings.
3. Configure your desired **Refresh Interval** (default: 15 minutes).
4. Choose whether to show past meetings or enable the join button.
5. Click **Save** in Windhawk. Open your Windows 11 clock flyout to view your agenda!
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- feed_url: ""
  $name: Outlook / Teams ICS Feed URL
  $description: Published ICS calendar URL from Outlook Web or Microsoft Teams
- refresh_interval_minutes: 15
  $name: Refresh Interval (minutes)
  $description: How often to check for calendar updates (minimum 1)
- show_past_events: false
  $name: Show Past Events
  $description: Show meetings that have already ended earlier today
- enable_join_button: true
  $name: Enable One-Click Join Button
  $description: Display direct Join Meeting button for Microsoft Teams links
*/
// ==/WindhawkModSettings==

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "mod/teams_calendar_flyout.h"
#include "ui/agenda_view_model.h"
#include <windows.h>
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <vector>

#ifdef WH_MOD
#include <windhawk_api.h>
#ifndef WH_MOD_ID
#define WH_MOD_ID L"teams-calendar-flyout"
#endif
#include <windhawk_utils.h>

#undef GetCurrentTime
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.System.UserProfile.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/base.h>

namespace wf = winrt::Windows::Foundation;
namespace ws = winrt::Windows::System;
namespace wsup = winrt::Windows::System::UserProfile;
namespace wg = winrt::Windows::Globalization;
namespace wadt = winrt::Windows::ApplicationModel::DataTransfer;
namespace wux = winrt::Windows::UI::Xaml;
namespace wuxc = winrt::Windows::UI::Xaml::Controls;
namespace wuxi = winrt::Windows::UI::Xaml::Input;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuc = winrt::Windows::UI::Core;

static void DispatchUpdateEvents();
#else
#ifndef Wh_Log
#define Wh_Log(...) ((void)0)
#endif
#endif

namespace calendar {

ModLifecycleManager::ModLifecycleManager()
    : cache_(std::make_shared<CalendarCache>()),
      injector_(std::make_shared<XamlInjector>()) {}

ModLifecycleManager::~ModLifecycleManager() {
    Uninitialize();
}

ModLifecycleManager& ModLifecycleManager::Instance() {
    static ModLifecycleManager instance;
    return instance;
}

bool ModLifecycleManager::Initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        return true;
    }

    settings_.LoadFromWindhawk();

    if (!injector_) {
        injector_ = std::make_shared<XamlInjector>();
    }
    injector_->Initialize(cache_, settings_, custom_adapter_);

    cache_->SetOnUpdatedCallback([this]() {
        std::shared_ptr<XamlInjector> inj;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            inj = injector_;
        }
        if (inj) {
            inj->OnEventsUpdated();
        }
#ifdef WH_MOD
        DispatchUpdateEvents();
#endif
    });

    fetcher_ = std::make_shared<HttpFetcher>(cache_, custom_transport_);
    fetcher_->Start(settings_);

    initialized_ = true;
    return true;
}

void ModLifecycleManager::OnSettingsChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }

    settings_.LoadFromWindhawk();
    if (fetcher_) {
        fetcher_->UpdateSettings(settings_);
    }
    if (injector_) {
        injector_->OnSettingsChanged(settings_);
    }
#ifdef WH_MOD
    DispatchUpdateEvents();
#endif
}

void ModLifecycleManager::Uninitialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return;
    }

    if (fetcher_) {
        fetcher_->Stop();
        fetcher_.reset();
    }

    if (injector_) {
        injector_->Shutdown();
        injector_.reset();
    }

    if (cache_) {
        cache_->SetOnUpdatedCallback(nullptr);
        cache_->Clear();
    }

    initialized_ = false;
}

bool ModLifecycleManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

std::shared_ptr<CalendarCache> ModLifecycleManager::GetCache() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_;
}

std::shared_ptr<HttpFetcher> ModLifecycleManager::GetFetcher() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fetcher_;
}

std::shared_ptr<XamlInjector> ModLifecycleManager::GetInjector() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return injector_;
}

ModSettings ModLifecycleManager::GetSettings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

void ModLifecycleManager::SetCustomTransport(std::shared_ptr<IHttpTransport> transport) {
    std::lock_guard<std::mutex> lock(mutex_);
    custom_transport_ = std::move(transport);
    if (fetcher_ && initialized_) {
        fetcher_->Stop();
        fetcher_ = std::make_shared<HttpFetcher>(cache_, custom_transport_);
        fetcher_->Start(settings_);
    }
}

void ModLifecycleManager::SetCustomXamlAdapter(std::shared_ptr<IXamlHostAdapter> adapter) {
    std::lock_guard<std::mutex> lock(mutex_);
    custom_adapter_ = std::move(adapter);
    if (injector_ && initialized_) {
        injector_->Shutdown();
        injector_->Initialize(cache_, settings_, custom_adapter_);
    }
}

void ModLifecycleManager::SetSettingsForTesting(const ModSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
    if (fetcher_ && initialized_) {
        fetcher_->UpdateSettings(settings_);
    }
    if (injector_ && initialized_) {
        injector_->OnSettingsChanged(settings_);
    }
}

void ModLifecycleManager::ResetForTesting() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fetcher_) {
        fetcher_->Stop();
        fetcher_.reset();
    }
    if (injector_) {
        injector_->Shutdown();
        injector_.reset();
    }
    if (cache_) {
        cache_->SetOnUpdatedCallback(nullptr);
        cache_->Clear();
    }
    custom_transport_.reset();
    custom_adapter_.reset();
    settings_ = ModSettings();
    initialized_ = false;
}

} // namespace calendar

#ifdef WH_MOD

// Global UI elements and state
[[clang::no_destroy]] static wuxc::ScrollViewer m_hostScrollViewer{nullptr};
[[clang::no_destroy]] static winrt::Windows::Foundation::IInspectable m_originalCalendarContent{nullptr};
static wuxc::ScrollBarVisibility m_originalVerticalScrollBarVisibility{wuxc::ScrollBarVisibility::Auto};
static wuxc::ScrollBarVisibility m_originalHorizontalScrollBarVisibility{wuxc::ScrollBarVisibility::Disabled};

[[clang::no_destroy]] static wuxc::Grid m_rootGrid{nullptr};
[[clang::no_destroy]] static wuxc::CalendarView m_calendarView{nullptr};
[[clang::no_destroy]] static wuxc::Grid m_headerGrid{nullptr};
[[clang::no_destroy]] static wuxc::Button m_prevDayButton{nullptr};
[[clang::no_destroy]] static wuxc::Button m_nextDayButton{nullptr};
[[clang::no_destroy]] static wuxc::Button m_dateButton{nullptr};
[[clang::no_destroy]] static wuxc::TextBlock m_dateButtonText{nullptr};
[[clang::no_destroy]] static wuxc::Button m_refreshButton{nullptr};
[[clang::no_destroy]] static wuxc::ScrollViewer m_eventsScrollViewer{nullptr};
[[clang::no_destroy]] static wuxc::ItemsControl m_itemsControl{nullptr};

static winrt::event_token m_calendarViewSelectionChangedToken{};
static winrt::event_token m_prevBtnToken{};
static winrt::event_token m_nextBtnToken{};
static winrt::event_token m_dateBtnClickToken{};
static winrt::event_token m_refreshBtnToken{};
static winrt::event_token m_rootGridThemeChangedToken{};

static std::atomic<DWORD> g_ownerThreadId{0};
static std::atomic<ULONGLONG> g_lastOpenTick{0};
static SYSTEMTIME m_currentFilterDate{0};

struct CoreWindowData {
    wuc::CoreWindow coreWindow{nullptr};
    winrt::event_token activatedToken{};
    winrt::event_token visibilityChangedToken{};
    wux::FrameworkElement layoutUpdatedFe{nullptr};
    winrt::event_token layoutUpdatedToken{};
};
static thread_local CoreWindowData t_coreWindowData;

// Forward declarations
static void OnCalendarOpened();
static void RefreshAgendaList();
static void RegisterCoreWindowEvents();
static void UnregisterCoreWindowEvents();
static void RevokeLayoutUpdated();
static void RestoreCalendarContent();
static void WalkVisualTree(winrt::Windows::UI::Xaml::DependencyObject const& root);
static void ReplaceCalendarContent(wuxc::ScrollViewer const& host);

static bool IsDarkTheme(wux::FrameworkElement const& fe = nullptr) {
    if (fe) {
        try {
            return fe.ActualTheme() == wux::ElementTheme::Dark;
        } catch (...) {}
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
            return false;
        }
    }
    return true;
}

static winrt::Windows::UI::Color ParseHexColor(std::string_view hex, uint8_t defaultAlpha = 255) {
    if (hex.starts_with("#")) hex.remove_prefix(1);
    unsigned int val = 0;
    std::stringstream ss;
    ss << std::hex << hex;
    ss >> val;
    if (hex.length() == 8) {
        return winrt::Windows::UI::Color{
            static_cast<uint8_t>((val >> 24) & 0xFF),
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF)
        };
    } else if (hex.length() == 6) {
        return winrt::Windows::UI::Color{
            defaultAlpha,
            static_cast<uint8_t>((val >> 16) & 0xFF),
            static_cast<uint8_t>((val >> 8) & 0xFF),
            static_cast<uint8_t>(val & 0xFF)
        };
    }
    return winrt::Windows::UI::Colors::Transparent();
}

static std::wstring FormatFilterDate(const SYSTEMTIME& st) {
    WCHAR buf[128];
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, buf, ARRAYSIZE(buf), nullptr) != 0) {
        return std::wstring(buf);
    }
    swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return std::wstring(buf);
}

static void PopulateItemsControl(const std::vector<calendar::AgendaItemViewModel>& items) {
    if (!m_itemsControl) return;
    m_itemsControl.Items().Clear();

    bool dark = IsDarkTheme(m_rootGrid);

    if (items.empty()) {
        auto border = wuxc::Border();
        border.Margin(wux::Thickness{0, 4, 0, 4});
        border.Padding(wux::Thickness{12, 16, 12, 16});
        border.CornerRadius(wux::CornerRadius{6, 6, 6, 6});
        border.Background(wuxm::SolidColorBrush(dark ? winrt::Windows::UI::Color{20, 255, 255, 255} : winrt::Windows::UI::Color{15, 0, 0, 0}));
        border.BorderThickness(wux::Thickness{1, 1, 1, 1});
        border.BorderBrush(wuxm::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));

        auto tb = wuxc::TextBlock();
        auto settings = calendar::ModLifecycleManager::Instance().GetSettings();
        if (settings.feed_url.empty()) {
            tb.Text(L"Please set your Outlook / Teams ICS URL in the mod settings.");
        } else {
            SYSTEMTIME today;
            GetLocalTime(&today);
            bool isToday = (m_currentFilterDate.wYear == today.wYear &&
                            m_currentFilterDate.wMonth == today.wMonth &&
                            m_currentFilterDate.wDay == today.wDay);
            tb.Text(isToday ? L"No upcoming Teams meetings today" : L"No upcoming Teams meetings for this date");
        }
        tb.Opacity(0.7);
        tb.TextWrapping(wux::TextWrapping::Wrap);
        tb.HorizontalAlignment(wux::HorizontalAlignment::Center);
        tb.IsTextSelectionEnabled(true);
        border.Child(tb);
        m_itemsControl.Items().Append(border);
        return;
    }

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];

        auto border = wuxc::Border();
        border.Margin(wux::Thickness{0, 2, 0, 4});
        border.Padding(wux::Thickness{10, 8, 10, 8});
        border.CornerRadius(wux::CornerRadius{6, 6, 6, 6});
        border.Background(wuxm::SolidColorBrush(ParseHexColor(item.card_bg(), 255)));
        border.BorderThickness(wux::Thickness{1, 1, 1, 1});
        border.BorderBrush(wuxm::SolidColorBrush(ParseHexColor(item.card_border(), 255)));

        auto sp = wuxc::StackPanel();
        sp.Spacing(4);

        // Header Row: Time and badges
        auto headerGrid = wuxc::Grid();
        wuxc::ColumnDefinition c0{}; c0.Width(wux::GridLength{1, wux::GridUnitType::Star}); headerGrid.ColumnDefinitions().Append(c0);
        wuxc::ColumnDefinition c1{}; c1.Width(wux::GridLength{0, wux::GridUnitType::Auto}); headerGrid.ColumnDefinitions().Append(c1);

        auto timeTb = wuxc::TextBlock();
        timeTb.Text(winrt::to_hstring(item.formatted_time_range));
        timeTb.FontSize(11.0);
        timeTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        timeTb.Foreground(wuxm::SolidColorBrush(ParseHexColor(item.accent_color())));
        timeTb.VerticalAlignment(wux::VerticalAlignment::Center);
        timeTb.IsTextSelectionEnabled(true);
        wuxc::Grid::SetColumn(timeTb, 0);
        headerGrid.Children().Append(timeTb);

        auto badgesSp = wuxc::StackPanel();
        badgesSp.Orientation(wuxc::Orientation::Horizontal);
        badgesSp.Spacing(4);
        badgesSp.VerticalAlignment(wux::VerticalAlignment::Center);

        // Status Badge (Live / Soon / Past)
        if (!item.status_badge_label.empty()) {
            auto badgeBorder = wuxc::Border();
            badgeBorder.CornerRadius(wux::CornerRadius{3, 3, 3, 3});
            badgeBorder.Padding(wux::Thickness{4, 1, 4, 1});
            badgeBorder.Background(wuxm::SolidColorBrush(ParseHexColor(item.status_badge_bg)));

            auto badgeTb = wuxc::TextBlock();
            badgeTb.Text(winrt::to_hstring(item.status_badge_label));
            badgeTb.FontSize(9.5);
            badgeTb.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
            badgeTb.Foreground(wuxm::SolidColorBrush(ParseHexColor(item.status_badge_fg)));
            badgeBorder.Child(badgeTb);
            badgesSp.Children().Append(badgeBorder);
        }

        // Teams Badge
        if (item.is_teams) {
            auto teamsBorder = wuxc::Border();
            teamsBorder.CornerRadius(wux::CornerRadius{3, 3, 3, 3});
            teamsBorder.Padding(wux::Thickness{4, 1, 4, 1});
            teamsBorder.Background(wuxm::SolidColorBrush(ParseHexColor(item.accent_color())));

            auto teamsTb = wuxc::TextBlock();
            teamsTb.Text(L"Teams");
            teamsTb.FontSize(9.5);
            teamsTb.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
            teamsTb.Foreground(wuxm::SolidColorBrush(winrt::Windows::UI::Colors::White()));
            teamsBorder.Child(teamsTb);
            badgesSp.Children().Append(teamsBorder);
        }

        wuxc::Grid::SetColumn(badgesSp, 1);
        headerGrid.Children().Append(badgesSp);
        sp.Children().Append(headerGrid);

        // Title
        auto titleTb = wuxc::TextBlock();
        titleTb.Text(winrt::to_hstring(item.title));
        titleTb.FontSize(13.0);
        titleTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        titleTb.TextWrapping(wux::TextWrapping::Wrap);
        titleTb.Foreground(wuxm::SolidColorBrush(ParseHexColor(item.primary_text_color())));
        titleTb.IsTextSelectionEnabled(true);
        sp.Children().Append(titleTb);

        // Location
        if (!item.location.empty()) {
            auto locTb = wuxc::TextBlock();
            locTb.Text(winrt::to_hstring(item.location));
            locTb.FontSize(11.0);
            locTb.Foreground(wuxm::SolidColorBrush(ParseHexColor(item.secondary_text_color())));
            locTb.TextTrimming(wux::TextTrimming::CharacterEllipsis);
            locTb.IsTextSelectionEnabled(true);
            sp.Children().Append(locTb);
        }

        // Teams Join Meeting Button
        if (item.is_teams && item.enable_join_button && !item.join_url.empty()) {
            auto joinBtn = wuxc::Button();
            joinBtn.CornerRadius(wux::CornerRadius{4, 4, 4, 4});
            joinBtn.Padding(wux::Thickness{10, 4, 10, 4});
            joinBtn.Margin(wux::Thickness{0, 4, 0, 0});
            joinBtn.Background(wuxm::SolidColorBrush(ParseHexColor(item.accent_color())));
            joinBtn.BorderThickness(wux::Thickness{0, 0, 0, 0});
            joinBtn.HorizontalAlignment(wux::HorizontalAlignment::Left);

            auto btnSp = wuxc::StackPanel();
            btnSp.Orientation(wuxc::Orientation::Horizontal);
            btnSp.Spacing(6);
            btnSp.VerticalAlignment(wux::VerticalAlignment::Center);

            auto iconTb = wuxc::FontIcon();
            iconTb.Glyph(L"\uE714");
            iconTb.FontSize(12.0);
            iconTb.Foreground(wuxm::SolidColorBrush(winrt::Windows::UI::Colors::White()));
            btnSp.Children().Append(iconTb);

            auto btnTb = wuxc::TextBlock();
            btnTb.Text(L"Join Meeting");
            btnTb.FontSize(12.0);
            btnTb.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            btnTb.Foreground(wuxm::SolidColorBrush(winrt::Windows::UI::Colors::White()));
            btnSp.Children().Append(btnTb);

            joinBtn.Content(btnSp);

            std::string urlCopy = item.join_url;
            joinBtn.Click([urlCopy](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                std::string sanitized;
                if (calendar::UrlSanitizer::SanitizeAndValidateUrl(urlCopy, &sanitized)) {
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, sanitized.c_str(), -1, nullptr, 0);
                    if (wlen > 0) {
                        std::wstring wurl(wlen, L'\0');
                        MultiByteToWideChar(CP_UTF8, 0, sanitized.c_str(), -1, wurl.data(), wlen);
                        ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    }
                }
            });

            sp.Children().Append(joinBtn);
        }

        border.Child(sp);
        m_itemsControl.Items().Append(border);
    }
}

static void RefreshAgendaList() {
    calendar::CalendarDate target_date{m_currentFilterDate.wYear, m_currentFilterDate.wMonth, m_currentFilterDate.wDay};
    auto cache = calendar::ModLifecycleManager::Instance().GetCache();
    auto settings = calendar::ModLifecycleManager::Instance().GetSettings();
    auto events = cache ? cache->GetEventsForDate(target_date) : std::vector<calendar::CalendarEvent>{};
    calendar::ThemeMode theme = IsDarkTheme(m_rootGrid) ? calendar::ThemeMode::Dark : calendar::ThemeMode::Light;
    calendar::CalendarDateTime now = calendar::CalendarDateTime::now_local();
    auto viewModel = calendar::AgendaPresenter::PresentEvents(
        events, target_date, theme, now, settings.show_past_events, settings.enable_join_button);
    PopulateItemsControl(viewModel.items);
}

static void DispatchUpdateEvents() {
    wuxc::ItemsControl itemsControl{nullptr};
    if (m_itemsControl) {
        itemsControl = m_itemsControl;
    }
    if (!itemsControl) return;
    auto dispatcher = itemsControl.Dispatcher();
    if (!dispatcher) return;

    dispatcher.TryRunAsync(wuc::CoreDispatcherPriority::Normal, []() {
        RefreshAgendaList();
    });
}

static void ChangeSelectedDay(int deltaDays) {
    if (deltaDays == 0) return;
    FILETIME ft{};
    SystemTimeToFileTime(&m_currentFilterDate, &ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    ULONGLONG dayTicks = 864000000000ULL;
    if (deltaDays > 0) {
        uli.QuadPart += (ULONGLONG)deltaDays * dayTicks;
    } else {
        uli.QuadPart -= (ULONGLONG)(-deltaDays) * dayTicks;
    }
    ft.dwLowDateTime = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    SYSTEMTIME newSt{};
    FileTimeToSystemTime(&ft, &newSt);
    m_currentFilterDate = newSt;

    if (m_dateButtonText) {
        m_dateButtonText.Text(winrt::hstring(FormatFilterDate(newSt)));
    }

    if (m_calendarView) {
        try {
            m_calendarView.SelectedDates().Clear();
            winrt::Windows::Foundation::DateTime dt = winrt::clock::from_file_time(ft);
            m_calendarView.SelectedDates().Append(dt);
            m_calendarView.SetDisplayDate(dt);
        } catch (...) {}
    }

    RefreshAgendaList();
}

static void OnCalendarViewSelectedDatesChanged(winrt::Windows::Foundation::DateTime dt) {
    FILETIME ft = winrt::clock::to_file_time(dt);
    SYSTEMTIME stUtc{}, stLocal{};
    FileTimeToSystemTime(&ft, &stUtc);
    SystemTimeToTzSpecificLocalTime(nullptr, &stUtc, &stLocal);
    m_currentFilterDate = stLocal;

    if (m_dateButtonText) {
        m_dateButtonText.Text(winrt::hstring(FormatFilterDate(m_currentFilterDate)));
    }
    RefreshAgendaList();
}

static void ResetDatePickerToToday() {
    GetLocalTime(&m_currentFilterDate);
    if (m_dateButtonText) {
        m_dateButtonText.Text(winrt::hstring(FormatFilterDate(m_currentFilterDate)));
    }
    if (m_calendarView) {
        try {
            FILETIME ft{};
            SystemTimeToFileTime(&m_currentFilterDate, &ft);
            winrt::Windows::Foundation::DateTime dt = winrt::clock::from_file_time(ft);
            m_calendarView.SelectedDates().Clear();
            m_calendarView.SelectedDates().Append(dt);
            m_calendarView.SetDisplayDate(dt);
        } catch (...) {}
    }
    RefreshAgendaList();
}

static void ReplaceCalendarContent(wuxc::ScrollViewer const& host) {
    if (!host) return;
    DWORD currentThread = GetCurrentThreadId();
    DWORD expected = 0;
    if (!g_ownerThreadId.compare_exchange_strong(expected, currentThread) && expected != currentThread) {
        return;
    }
    RegisterCoreWindowEvents();

    if (m_currentFilterDate.wYear == 0) {
        GetLocalTime(&m_currentFilterDate);
    }

    try {
        if (m_hostScrollViewer && m_hostScrollViewer != host) {
            try {
                if (m_originalCalendarContent) m_hostScrollViewer.Content(m_originalCalendarContent);
                m_hostScrollViewer.VerticalScrollBarVisibility(m_originalVerticalScrollBarVisibility);
                m_hostScrollViewer.HorizontalScrollBarVisibility(m_originalHorizontalScrollBarVisibility);
            } catch (...) {}
            m_originalCalendarContent = nullptr;
            m_hostScrollViewer = nullptr;
        }

        if (!m_itemsControl) {
            m_itemsControl = wuxc::ItemsControl();
        }

        if (!m_eventsScrollViewer) {
            m_eventsScrollViewer = wuxc::ScrollViewer();
            m_eventsScrollViewer.Name(L"TeamsCalendarScrollViewer");
            m_eventsScrollViewer.Margin(wux::Thickness{12, 0, 12, 0});
            m_eventsScrollViewer.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Auto);
            m_eventsScrollViewer.HorizontalScrollBarVisibility(wuxc::ScrollBarVisibility::Disabled);
            m_eventsScrollViewer.MaxHeight(260.0);
            m_eventsScrollViewer.Content(m_itemsControl);
        }

        if (!m_calendarView) {
            m_calendarView = wuxc::CalendarView();
            m_calendarView.Name(L"CustomCalendarView");
            m_calendarView.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
            m_calendarView.Margin(wux::Thickness{0, 0, 0, 4});
            m_calendarView.BorderThickness(wux::Thickness{0, 0, 0, 0});
            m_calendarView.Background(wuxm::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            m_calendarView.IsTodayHighlighted(true);
            m_calendarView.SelectionMode(wuxc::CalendarViewSelectionMode::Single);
            m_calendarView.Visibility(wux::Visibility::Visible);

            m_calendarViewSelectionChangedToken = m_calendarView.SelectedDatesChanged(
                [](wuxc::CalendarView const&, wuxc::CalendarViewSelectedDatesChangedEventArgs const& args) {
                    if (args.AddedDates().Size() > 0) {
                        OnCalendarViewSelectedDatesChanged(args.AddedDates().GetAt(0));
                    }
                });
        }

        if (!m_prevDayButton) {
            m_prevDayButton = wuxc::Button();
            m_prevDayButton.Name(L"TeamsCalendarPrevDayButton");
            m_prevDayButton.Width(32);
            m_prevDayButton.Height(32);
            m_prevDayButton.Padding(wux::Thickness{0, 0, 0, 0});
            m_prevDayButton.Margin(wux::Thickness{0, 0, 4, 4});
            m_prevDayButton.VerticalAlignment(wux::VerticalAlignment::Center);
            auto prevIcon = wuxc::FontIcon();
            prevIcon.Glyph(L"\uE76B");
            prevIcon.FontSize(12);
            m_prevDayButton.Content(prevIcon);
            wuxc::ToolTipService::SetToolTip(m_prevDayButton, winrt::box_value(L"Previous day"));
            m_prevBtnToken = m_prevDayButton.Click([](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                ChangeSelectedDay(-1);
            });
        }

        if (!m_nextDayButton) {
            m_nextDayButton = wuxc::Button();
            m_nextDayButton.Name(L"TeamsCalendarNextDayButton");
            m_nextDayButton.Width(32);
            m_nextDayButton.Height(32);
            m_nextDayButton.Padding(wux::Thickness{0, 0, 0, 0});
            m_nextDayButton.Margin(wux::Thickness{4, 0, 4, 4});
            m_nextDayButton.VerticalAlignment(wux::VerticalAlignment::Center);
            auto nextIcon = wuxc::FontIcon();
            nextIcon.Glyph(L"\uE76C");
            nextIcon.FontSize(12);
            m_nextDayButton.Content(nextIcon);
            wuxc::ToolTipService::SetToolTip(m_nextDayButton, winrt::box_value(L"Next day"));
            m_nextBtnToken = m_nextDayButton.Click([](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                ChangeSelectedDay(1);
            });
        }

        if (!m_refreshButton) {
            m_refreshButton = wuxc::Button();
            m_refreshButton.Name(L"TeamsCalendarRefreshButton");
            m_refreshButton.Width(32);
            m_refreshButton.Height(32);
            m_refreshButton.Padding(wux::Thickness{0, 0, 0, 0});
            m_refreshButton.Margin(wux::Thickness{0, 0, 0, 4});
            m_refreshButton.VerticalAlignment(wux::VerticalAlignment::Center);
            auto refreshIcon = wuxc::FontIcon();
            refreshIcon.Glyph(L"\uE72C");
            refreshIcon.FontSize(12);
            m_refreshButton.Content(refreshIcon);
            wuxc::ToolTipService::SetToolTip(m_refreshButton, winrt::box_value(L"Refresh events"));
            m_refreshBtnToken = m_refreshButton.Click([](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                auto fetcher = calendar::ModLifecycleManager::Instance().GetFetcher();
                if (fetcher) {
                    fetcher->TriggerFetch();
                }
                RefreshAgendaList();
            });
        }

        if (!m_dateButton) {
            m_dateButton = wuxc::Button();
            m_dateButton.Name(L"TeamsCalendarDateButton");
            m_dateButton.HorizontalAlignment(wux::HorizontalAlignment::Stretch);
            m_dateButton.HorizontalContentAlignment(wux::HorizontalAlignment::Stretch);
            m_dateButton.VerticalAlignment(wux::VerticalAlignment::Center);
            m_dateButton.Margin(wux::Thickness{0, 0, 0, 4});
            wuxc::ToolTipService::SetToolTip(m_dateButton, winrt::box_value(L"Toggle month calendar"));

            m_dateButtonText = wuxc::TextBlock();
            m_dateButtonText.Text(winrt::hstring(FormatFilterDate(m_currentFilterDate)));
            m_dateButtonText.VerticalAlignment(wux::VerticalAlignment::Center);
            m_dateButtonText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            m_dateButton.Content(m_dateButtonText);

            m_dateBtnClickToken = m_dateButton.Click([](wf::IInspectable const&, wux::RoutedEventArgs const&) {
                if (!m_calendarView) return;
                if (m_calendarView.Visibility() == wux::Visibility::Visible) {
                    m_calendarView.Visibility(wux::Visibility::Collapsed);
                } else {
                    m_calendarView.Visibility(wux::Visibility::Visible);
                }
            });
        }

        if (!m_headerGrid) {
            m_headerGrid = wuxc::Grid();
            m_headerGrid.Name(L"TeamsCalendarHeaderGrid");
            m_headerGrid.Margin(wux::Thickness{12, 6, 12, 4});

            wuxc::ColumnDefinition col0{}; col0.Width(wux::GridLength{0, wux::GridUnitType::Auto}); m_headerGrid.ColumnDefinitions().Append(col0);
            wuxc::ColumnDefinition col1{}; col1.Width(wux::GridLength{1, wux::GridUnitType::Star}); m_headerGrid.ColumnDefinitions().Append(col1);
            wuxc::ColumnDefinition col2{}; col2.Width(wux::GridLength{0, wux::GridUnitType::Auto}); m_headerGrid.ColumnDefinitions().Append(col2);
            wuxc::ColumnDefinition col3{}; col3.Width(wux::GridLength{0, wux::GridUnitType::Auto}); m_headerGrid.ColumnDefinitions().Append(col3);

            wuxc::Grid::SetColumn(m_prevDayButton, 0); m_headerGrid.Children().Append(m_prevDayButton);
            wuxc::Grid::SetColumn(m_dateButton, 1);     m_headerGrid.Children().Append(m_dateButton);
            wuxc::Grid::SetColumn(m_nextDayButton, 2); m_headerGrid.Children().Append(m_nextDayButton);
            wuxc::Grid::SetColumn(m_refreshButton, 3); m_headerGrid.Children().Append(m_refreshButton);
        }

        if (!m_rootGrid) {
            m_rootGrid = wuxc::Grid();
            m_rootGrid.Name(L"TeamsCalendarRootGrid");
            m_rootGrid.Margin(wux::Thickness{0, 0, 0, 4});

            wuxc::RowDefinition row0{}; row0.Height(wux::GridLength{0, wux::GridUnitType::Auto}); m_rootGrid.RowDefinitions().Append(row0);
            wuxc::RowDefinition row1{}; row1.Height(wux::GridLength{0, wux::GridUnitType::Auto}); m_rootGrid.RowDefinitions().Append(row1);
            wuxc::RowDefinition row2{}; row2.Height(wux::GridLength{0, wux::GridUnitType::Auto}); m_rootGrid.RowDefinitions().Append(row2);

            wuxc::Grid::SetRow(m_calendarView, 0);       m_rootGrid.Children().Append(m_calendarView);
            wuxc::Grid::SetRow(m_headerGrid, 1);         m_rootGrid.Children().Append(m_headerGrid);
            wuxc::Grid::SetRow(m_eventsScrollViewer, 2);  m_rootGrid.Children().Append(m_eventsScrollViewer);

            m_rootGridThemeChangedToken = m_rootGrid.ActualThemeChanged([](auto const&, auto const&) {
                RefreshAgendaList();
            });
        }

        ResetDatePickerToToday();

        if (m_rootGrid) {
            if (auto parent = m_rootGrid.Parent()) {
                if (auto parentContentControl = parent.try_as<wuxc::ContentControl>()) {
                    if (parentContentControl.Content() == m_rootGrid) parentContentControl.Content(nullptr);
                } else if (auto parentPanel = parent.try_as<wuxc::Panel>()) {
                    uint32_t index = 0;
                    if (parentPanel.Children().IndexOf(m_rootGrid, index)) {
                        parentPanel.Children().RemoveAt(index);
                    }
                }
            }
        }

        if (!m_originalCalendarContent && host.Content() != m_rootGrid) {
            m_originalCalendarContent = host.Content();
            m_originalVerticalScrollBarVisibility = host.VerticalScrollBarVisibility();
            m_originalHorizontalScrollBarVisibility = host.HorizontalScrollBarVisibility();
        }
        m_hostScrollViewer = host;

        host.VerticalScrollBarVisibility(wuxc::ScrollBarVisibility::Disabled);
        host.HorizontalScrollBarVisibility(wuxc::ScrollBarVisibility::Disabled);
        host.Content(m_rootGrid);
        RevokeLayoutUpdated();
        OnCalendarOpened();
    } catch (...) {
        Wh_Log(L"Exception in ReplaceCalendarContent");
    }
}

static void WalkVisualTree(winrt::Windows::UI::Xaml::DependencyObject const& root) {
    if (!root) return;
    DWORD owner = g_ownerThreadId.load();
    if (owner != 0 && owner != GetCurrentThreadId()) return;

    try {
        if (auto fe = root.try_as<wux::FrameworkElement>()) {
            auto name = fe.Name();
            if (name == L"CalendarControlScrollViewer") {
                if (auto scrollViewer = fe.try_as<wuxc::ScrollViewer>()) {
                    if (scrollViewer != m_hostScrollViewer) {
                        ReplaceCalendarContent(scrollViewer);
                    }
                }
            }
        }
        int count = wuxm::VisualTreeHelper::GetChildrenCount(root);
        for (int i = 0; i < count; i++) {
            WalkVisualTree(wuxm::VisualTreeHelper::GetChild(root, i));
        }
    } catch (...) {}
}

static void RevokeLayoutUpdated() {
    if (t_coreWindowData.layoutUpdatedFe && t_coreWindowData.layoutUpdatedToken.value != 0) {
        try {
            t_coreWindowData.layoutUpdatedFe.LayoutUpdated(t_coreWindowData.layoutUpdatedToken);
        } catch (...) {}
    }
    t_coreWindowData.layoutUpdatedToken = {};
    t_coreWindowData.layoutUpdatedFe = nullptr;
}

static void OnCalendarOpened() {
    DWORD owner = g_ownerThreadId.load();
    if (owner != 0 && owner != GetCurrentThreadId()) {
        return;
    }

    ULONGLONG currentTick = GetTickCount64();
    if (m_rootGrid && currentTick - g_lastOpenTick < 300) {
        return;
    }
    g_lastOpenTick = currentTick;

    Wh_Log(L"OnCalendarOpened: scanning visual tree");

    ResetDatePickerToToday();

    auto fetcher = calendar::ModLifecycleManager::Instance().GetFetcher();
    if (fetcher) {
        fetcher->TriggerFetch();
    }

    try {
        auto window = winrt::Windows::UI::Xaml::Window::Current();
        if (window && window.Content()) {
            if (auto fe = window.Content().try_as<winrt::Windows::UI::Xaml::FrameworkElement>()) {
                if (!m_rootGrid && t_coreWindowData.layoutUpdatedToken.value == 0) {
                    t_coreWindowData.layoutUpdatedFe = fe;
                    t_coreWindowData.layoutUpdatedToken = fe.LayoutUpdated([](wf::IInspectable const&, wf::IInspectable const&) {
                        if (m_rootGrid) {
                            RevokeLayoutUpdated();
                            return;
                        }
                        auto w = winrt::Windows::UI::Xaml::Window::Current();
                        if (w) {
                            if (w.Content()) WalkVisualTree(w.Content());
                            auto popups = winrt::Windows::UI::Xaml::Media::VisualTreeHelper::GetOpenPopups(w);
                            for (auto popup : popups) {
                                if (popup.Child()) WalkVisualTree(popup.Child());
                            }
                        }
                        if (m_rootGrid) {
                            RevokeLayoutUpdated();
                        }
                    });
                }
            }
            WalkVisualTree(window.Content());
            auto popups = winrt::Windows::UI::Xaml::Media::VisualTreeHelper::GetOpenPopups(window);
            for (auto popup : popups) {
                if (popup.Child()) WalkVisualTree(popup.Child());
            }
        }
    } catch (...) {
        Wh_Log(L"Exception getting Window::Current()");
    }
}

static void RegisterCoreWindowEvents() {
    try {
        auto coreWindow = wuc::CoreWindow::GetForCurrentThread();
        if (!coreWindow || t_coreWindowData.coreWindow == coreWindow) {
            return;
        }
        Wh_Log(L"Registering CoreWindow Activated & VisibilityChanged for thread %u", GetCurrentThreadId());
        t_coreWindowData.activatedToken = coreWindow.Activated(
            [](auto&&, wuc::WindowActivatedEventArgs const& args) {
                if (args.WindowActivationState() != wuc::CoreWindowActivationState::Deactivated) {
                    Wh_Log(L"CoreWindow Activated");
                    OnCalendarOpened();
                }
            });
        t_coreWindowData.visibilityChangedToken = coreWindow.VisibilityChanged(
            [](auto&&, wuc::VisibilityChangedEventArgs const& args) {
                if (args.Visible()) {
                    Wh_Log(L"CoreWindow VisibilityChanged: Visible");
                    OnCalendarOpened();
                }
            });
        t_coreWindowData.coreWindow = coreWindow;
    } catch (...) {
        Wh_Log(L"Failed to register CoreWindow events");
    }
}

static void UnregisterCoreWindowEvents() {
    try {
        if (t_coreWindowData.coreWindow) {
            if (t_coreWindowData.activatedToken.value != 0) {
                t_coreWindowData.coreWindow.Activated(t_coreWindowData.activatedToken);
                t_coreWindowData.activatedToken = {};
            }
            if (t_coreWindowData.visibilityChangedToken.value != 0) {
                t_coreWindowData.coreWindow.VisibilityChanged(t_coreWindowData.visibilityChangedToken);
                t_coreWindowData.visibilityChangedToken = {};
            }
            t_coreWindowData.coreWindow = nullptr;
        }
    } catch (...) {}
}

static void RestoreCalendarContent() {
    try {
        if (m_prevDayButton && m_prevBtnToken.value != 0) {
            m_prevDayButton.Click(m_prevBtnToken);
            m_prevBtnToken = {};
        }
        if (m_nextDayButton && m_nextBtnToken.value != 0) {
            m_nextDayButton.Click(m_nextBtnToken);
            m_nextBtnToken = {};
        }
        if (m_dateButton && m_dateBtnClickToken.value != 0) {
            m_dateButton.Click(m_dateBtnClickToken);
            m_dateBtnClickToken = {};
        }
        if (m_refreshButton && m_refreshBtnToken.value != 0) {
            m_refreshButton.Click(m_refreshBtnToken);
            m_refreshBtnToken = {};
        }
        if (m_calendarView && m_calendarViewSelectionChangedToken.value != 0) {
            m_calendarView.SelectedDatesChanged(m_calendarViewSelectionChangedToken);
            m_calendarViewSelectionChangedToken = {};
        }
        if (m_rootGrid && m_rootGridThemeChangedToken.value != 0) {
            m_rootGrid.ActualThemeChanged(m_rootGridThemeChangedToken);
            m_rootGridThemeChangedToken = {};
        }
    } catch (...) {}

    if (m_hostScrollViewer) {
        try {
            if (m_originalCalendarContent) {
                m_hostScrollViewer.Content(m_originalCalendarContent);
            } else if (m_hostScrollViewer.Content() == m_rootGrid) {
                m_hostScrollViewer.Content(nullptr);
            }
            m_hostScrollViewer.VerticalScrollBarVisibility(m_originalVerticalScrollBarVisibility);
            m_hostScrollViewer.HorizontalScrollBarVisibility(m_originalHorizontalScrollBarVisibility);
        } catch (...) {}
        m_originalCalendarContent = nullptr;
        m_hostScrollViewer = nullptr;
    }

    if (m_rootGrid) {
        try { m_rootGrid.Children().Clear(); } catch (...) {}
        m_rootGrid = nullptr;
    }
    if (m_itemsControl) {
        try { m_itemsControl.Items().Clear(); } catch (...) {}
        m_itemsControl = nullptr;
    }
    m_eventsScrollViewer = nullptr;
    m_headerGrid = nullptr;
    m_prevDayButton = nullptr;
    m_nextDayButton = nullptr;
    m_refreshButton = nullptr;
    m_dateButton = nullptr;
    m_dateButtonText = nullptr;
    m_calendarView = nullptr;
    g_ownerThreadId.store(0);
    g_lastOpenTick.store(0);
}

// Window thread execution helpers
using RunFromWindowThreadProc_t = void(WINAPI*)(PVOID parameter);

static bool RunFromWindowThreadViaPostMessage(HWND hWnd,
                                              RunFromWindowThreadProc_t proc,
                                              PVOID procParam) {
    static const UINT runFromWindowThreadRegisteredMsgViaPostMessage =
        RegisterWindowMessage(L"Windhawk_RunFromWindowThreadViaPostMessage_" WH_MOD_ID);

    struct RUN_FROM_WINDOW_THREAD_PARAM {
        RunFromWindowThreadProc_t proc;
        PVOID procParam;
        HHOOK hook;
    };

    DWORD dwThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (dwThreadId == 0) return false;

    HHOOK hook = SetWindowsHookEx(
        WH_GETMESSAGE,
        [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (nCode == HC_ACTION) {
                const MSG* msg = (const MSG*)lParam;
                if (msg->message == runFromWindowThreadRegisteredMsgViaPostMessage) {
                    auto* param = (RUN_FROM_WINDOW_THREAD_PARAM*)msg->lParam;
                    if (param) {
                        param->proc(param->procParam);
                        UnhookWindowsHookEx(param->hook);
                        delete param;
                        const_cast<MSG*>(msg)->lParam = 0;
                    }
                }
            }
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        },
        nullptr, dwThreadId);
    if (!hook) return false;

    auto* param = new RUN_FROM_WINDOW_THREAD_PARAM{proc, procParam, hook};
    if (!PostMessage(hWnd, runFromWindowThreadRegisteredMsgViaPostMessage, 0, (LPARAM)param)) {
        UnhookWindowsHookEx(hook);
        delete param;
        return false;
    }
    return true;
}

static bool RunFromWindowThread(HWND hWnd,
                                RunFromWindowThreadProc_t proc,
                                PVOID procParam) {
    static const UINT runFromWindowThreadRegisteredMsg =
        RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);

    struct RUN_FROM_WINDOW_THREAD_PARAM {
        RunFromWindowThreadProc_t proc;
        PVOID procParam;
    };

    DWORD dwThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (dwThreadId == 0) return false;

    if (dwThreadId == GetCurrentThreadId()) {
        proc(procParam);
        return true;
    }

    HHOOK hook = SetWindowsHookEx(
        WH_CALLWNDPROC,
        [](int nCode, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (nCode == HC_ACTION) {
                const CWPSTRUCT* cwp = (const CWPSTRUCT*)lParam;
                if (cwp->message == runFromWindowThreadRegisteredMsg) {
                    auto* param = (RUN_FROM_WINDOW_THREAD_PARAM*)cwp->lParam;
                    if (param) param->proc(param->procParam);
                }
            }
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        },
        nullptr, dwThreadId);
    if (!hook) return false;

    RUN_FROM_WINDOW_THREAD_PARAM param{proc, procParam};
    SendMessage(hWnd, runFromWindowThreadRegisteredMsg, 0, (LPARAM)&param);
    UnhookWindowsHookEx(hook);
    return true;
}

static std::vector<HWND> GetCoreWnds() {
    struct ENUM_WINDOWS_PARAM {
        std::vector<HWND>* hWnds;
    };
    std::vector<HWND> hWnds;
    ENUM_WINDOWS_PARAM param = {&hWnds};
    EnumWindows(
        [](HWND hWnd, LPARAM lParam) -> BOOL {
            auto& p = *(ENUM_WINDOWS_PARAM*)lParam;
            DWORD dwProcessId = 0;
            if (!GetWindowThreadProcessId(hWnd, &dwProcessId) || dwProcessId != GetCurrentProcessId()) {
                return TRUE;
            }
            WCHAR szClassName[64];
            if (GetClassNameW(hWnd, szClassName, ARRAYSIZE(szClassName)) == 0) {
                return TRUE;
            }
            if (_wcsicmp(szClassName, L"Windows.UI.Core.CoreWindow") == 0) {
                p.hWnds->push_back(hWnd);
            }
            return TRUE;
        },
        (LPARAM)&param);
    return hWnds;
}

// Window creation hooks
using CreateWindowInBand_t = HWND(WINAPI*)(DWORD dwExStyle,
                                           LPCWSTR lpClassName,
                                           LPCWSTR lpWindowName,
                                           DWORD dwStyle,
                                           int X,
                                           int Y,
                                           int nWidth,
                                           int nHeight,
                                           HWND hWndParent,
                                           HMENU hMenu,
                                           HINSTANCE hInstance,
                                           PVOID lpParam,
                                           DWORD dwBand);
static CreateWindowInBand_t CreateWindowInBand_Original = nullptr;

using CreateWindowInBandEx_t = HWND(WINAPI*)(DWORD dwExStyle,
                                             LPCWSTR lpClassName,
                                             LPCWSTR lpWindowName,
                                             DWORD dwStyle,
                                             int X,
                                             int Y,
                                             int nWidth,
                                             int nHeight,
                                             HWND hWndParent,
                                             HMENU hMenu,
                                             HINSTANCE hInstance,
                                             PVOID lpParam,
                                             DWORD dwBand,
                                             DWORD dwTypeFlags);
static CreateWindowInBandEx_t CreateWindowInBandEx_Original = nullptr;

static void OnWindowCreated(HWND hWnd, LPCWSTR lpClassName, const char* caller) {
    if (!hWnd) return;
    WCHAR szClassName[64] = {0};
    if (lpClassName) {
        wcsncpy_s(szClassName, lpClassName, _TRUNCATE);
    } else {
        GetClassNameW(hWnd, szClassName, ARRAYSIZE(szClassName));
    }
    if (_wcsicmp(szClassName, L"Windows.UI.Core.CoreWindow") == 0) {
        Wh_Log(L"CoreWindow created (%s), dispatching initialization to window thread", (caller ? L"band" : L"unknown"));
        RunFromWindowThreadViaPostMessage(hWnd, [](PVOID) {
            RegisterCoreWindowEvents();
            OnCalendarOpened();
        }, nullptr);
    }
}

static HWND WINAPI CreateWindowInBand_Hook(DWORD dwExStyle,
                                           LPCWSTR lpClassName,
                                           LPCWSTR lpWindowName,
                                           DWORD dwStyle,
                                           int X,
                                           int Y,
                                           int nWidth,
                                           int nHeight,
                                           HWND hWndParent,
                                           HMENU hMenu,
                                           HINSTANCE hInstance,
                                           PVOID lpParam,
                                           DWORD dwBand) {
    HWND hWnd = CreateWindowInBand_Original(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam, dwBand);
    if (!hWnd) return hWnd;
    OnWindowCreated(hWnd, lpClassName, __FUNCTION__);
    return hWnd;
}

static HWND WINAPI CreateWindowInBandEx_Hook(DWORD dwExStyle,
                                             LPCWSTR lpClassName,
                                             LPCWSTR lpWindowName,
                                             DWORD dwStyle,
                                             int X,
                                             int Y,
                                             int nWidth,
                                             int nHeight,
                                             HWND hWndParent,
                                             HMENU hMenu,
                                             HINSTANCE hInstance,
                                             PVOID lpParam,
                                             DWORD dwBand,
                                             DWORD dwTypeFlags) {
    HWND hWnd = CreateWindowInBandEx_Original(
        dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight,
        hWndParent, hMenu, hInstance, lpParam, dwBand, dwTypeFlags);
    if (!hWnd) return hWnd;
    OnWindowCreated(hWnd, lpClassName, __FUNCTION__);
    return hWnd;
}

#endif // WH_MOD

// Windhawk mod entry points
extern "C" __declspec(dllexport) BOOL Wh_ModInit() {
    Wh_Log(L"Teams Calendar Flyout initializing");
#ifdef WH_MOD
    HMODULE user32Module = GetModuleHandleW(L"user32.dll");
    if (user32Module) {
        auto pCreateWindowInBand = (CreateWindowInBand_t)GetProcAddress(user32Module, "CreateWindowInBand");
        if (pCreateWindowInBand) {
            WindhawkUtils::SetFunctionHook(pCreateWindowInBand, CreateWindowInBand_Hook, &CreateWindowInBand_Original);
        }
        auto pCreateWindowInBandEx = (CreateWindowInBandEx_t)GetProcAddress(user32Module, "CreateWindowInBandEx");
        if (pCreateWindowInBandEx) {
            WindhawkUtils::SetFunctionHook(pCreateWindowInBandEx, CreateWindowInBandEx_Hook, &CreateWindowInBandEx_Original);
        }
    }
#endif
    return calendar::ModLifecycleManager::Instance().Initialize() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void Wh_ModAfterInit() {
    Wh_Log(L"Teams Calendar Flyout after init");
#ifdef WH_MOD
    for (HWND hCoreWnd : GetCoreWnds()) {
        Wh_Log(L"Initializing for existing CoreWindow: %08X", (DWORD)(ULONG_PTR)hCoreWnd);
        RunFromWindowThread(
            hCoreWnd,
            [](PVOID) {
                RegisterCoreWindowEvents();
                OnCalendarOpened();
            },
            nullptr);
    }
#endif
}

extern "C" __declspec(dllexport) void Wh_ModSettingsChanged() {
    Wh_Log(L"Teams Calendar Flyout settings changed");
    calendar::ModLifecycleManager::Instance().OnSettingsChanged();
}

extern "C" __declspec(dllexport) void Wh_ModUninit() {
    Wh_Log(L"Teams Calendar Flyout uninitializing");
#ifdef WH_MOD
    for (HWND hCoreWnd : GetCoreWnds()) {
        Wh_Log(L"Uninitializing for %08X", (DWORD)(ULONG_PTR)hCoreWnd);
        RunFromWindowThread(
            hCoreWnd,
            [](PVOID) {
                RevokeLayoutUpdated();
                UnregisterCoreWindowEvents();
                RestoreCalendarContent();
            },
            nullptr);
    }
#endif
    calendar::ModLifecycleManager::Instance().Uninitialize();
}
