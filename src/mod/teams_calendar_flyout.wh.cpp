// ==WindhawkMod==
// @id              teams-calendar-flyout
// @name            Microsoft Teams Calendar Flyout
// @description     Displays upcoming Microsoft Teams meetings in the Windows 11 calendar flyout
// @version         1.0.0
// @author          Pedrofff23
// @include         ShellHost.exe
// @include         ShellExperienceHost.exe
// @compilerOptions -lwininet -lole32 -lshell32
// ==/WindhawkMod==

// ==WindhawkModSettings==
// - feed_url: ""
//   $name: Outlook / Teams ICS Feed URL
//   $description: Published ICS calendar URL from Outlook Web or Microsoft Teams
// - refresh_interval_minutes: 15
//   $name: Refresh Interval (minutes)
//   $description: How often to check for calendar updates (minimum 1)
// - show_past_events: false
//   $name: Show Past Events
//   $description: Show meetings that have already ended earlier today
// - enable_join_button: true
//   $name: Enable One-Click Join Button
//   $description: Display direct Join Meeting button for Microsoft Teams links
// ==/WindhawkModSettings==

#include "mod/teams_calendar_flyout.h"
#include <windows.h>
#ifdef WH_MOD
#include <windhawk_api.h>
#endif

namespace calendar {

ModLifecycleManager::ModLifecycleManager()
    : cache_(std::make_shared<CalendarCache>()) {}

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

    if (cache_) {
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

void ModLifecycleManager::SetSettingsForTesting(const ModSettings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
    if (fetcher_ && initialized_) {
        fetcher_->UpdateSettings(settings_);
    }
}

void ModLifecycleManager::ResetForTesting() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fetcher_) {
        fetcher_->Stop();
        fetcher_.reset();
    }
    if (cache_) {
        cache_->Clear();
    }
    custom_transport_.reset();
    settings_ = ModSettings();
    initialized_ = false;
}

} // namespace calendar

// Windhawk mod entry points
extern "C" __declspec(dllexport) BOOL Wh_ModInit() {
    return calendar::ModLifecycleManager::Instance().Initialize() ? TRUE : FALSE;
}

extern "C" __declspec(dllexport) void Wh_ModSettingsChanged() {
    calendar::ModLifecycleManager::Instance().OnSettingsChanged();
}

extern "C" __declspec(dllexport) void Wh_ModUninit() {
    calendar::ModLifecycleManager::Instance().Uninitialize();
}
