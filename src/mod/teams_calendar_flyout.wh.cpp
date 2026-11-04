// ==WindhawkMod==
// @id              teams-calendar-flyout
// @name            Microsoft Teams Calendar Flyout
// @description     Displays upcoming Microsoft Teams meetings in the Windows 11 calendar flyout
// @version         1.0.0
// @author          Pedrofff23
// @github          https://github.com/Pedrofff23/teams-mod
// @include         ShellHost.exe
// @include         ShellExperienceHost.exe
// @compilerOptions -lwininet -lole32 -lshell32
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

#include "mod/teams_calendar_flyout.h"
#include <windows.h>
#ifdef WH_MOD
#include <windhawk_api.h>
#ifndef WH_MOD_ID
#define WH_MOD_ID L"teams-calendar-flyout"
#endif
#include <windhawk_utils.h>
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
// Symbol hook declarations for ShellHost.exe calendar view
typedef void (WINAPI *CalendarView_OnSelectedDatesChanged_t)(void* pThis, void* pArgs);
static CalendarView_OnSelectedDatesChanged_t pOriginalCalendarView_OnSelectedDatesChanged = nullptr;

static void WINAPI Hook_CalendarView_OnSelectedDatesChanged(void* pThis, void* pArgs) {
    if (pOriginalCalendarView_OnSelectedDatesChanged) {
        pOriginalCalendarView_OnSelectedDatesChanged(pThis, pArgs);
    }
    Wh_Log(L"CalendarView selected date changed in ShellHost");
    auto injector = calendar::ModLifecycleManager::Instance().GetInjector();
    if (injector) {
        injector->OnDateSelected(injector->GetSelectedDate());
    }
}
#endif

// Windhawk mod entry points
extern "C" __declspec(dllexport) BOOL Wh_ModInit() {
    Wh_Log(L"Teams Calendar Flyout initializing");
#ifdef WH_MOD
    const WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {
            {
                L"private: void __cdecl winrt::Windows::UI::Xaml::Controls::CalendarView::OnSelectedDatesChanged(struct winrt::Windows::UI::Xaml::Controls::CalendarViewSelectedDatesChangedEventArgs const &)",
                L"?OnSelectedDatesChanged@CalendarView@Controls@Xaml@UI@winrt@@AEAAXAEBUCalendarViewSelectedDatesChangedEventArgs@2345@@Z"
            },
            &pOriginalCalendarView_OnSelectedDatesChanged,
            Hook_CalendarView_OnSelectedDatesChanged,
            true // optional
        }
    };
    WindhawkUtils::HookSymbols(GetModuleHandleW(nullptr), hooks, ARRAYSIZE(hooks));
#endif
    return calendar::ModLifecycleManager::Instance().Initialize() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void Wh_ModSettingsChanged() {
    Wh_Log(L"Teams Calendar Flyout settings changed");
    calendar::ModLifecycleManager::Instance().OnSettingsChanged();
}

extern "C" __declspec(dllexport) void Wh_ModUninit() {
    Wh_Log(L"Teams Calendar Flyout uninitializing");
    calendar::ModLifecycleManager::Instance().Uninitialize();
}
