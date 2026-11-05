#pragma once

#include "mod/calendar_cache.h"
#include "mod/http_fetcher.h"
#include "mod/mod_settings.h"
#include "ui/xaml_injector.h"
#include <windows.h>
#include <memory>
#include <mutex>

namespace calendar {

class ModLifecycleManager {
public:
    static ModLifecycleManager& Instance();

    // Prevent copying and moving
    ModLifecycleManager(const ModLifecycleManager&) = delete;
    ModLifecycleManager& operator=(const ModLifecycleManager&) = delete;

    // Mod lifecycle operations
    bool Initialize();
    void OnSettingsChanged();
    void Uninitialize();

    bool IsInitialized() const;

    std::shared_ptr<CalendarCache> GetCache() const;
    std::shared_ptr<HttpFetcher> GetFetcher() const;
    std::shared_ptr<XamlInjector> GetInjector() const;
    ModSettings GetSettings() const;

    // Injection seam for testing
    void SetCustomTransport(std::shared_ptr<IHttpTransport> transport);
    void SetCustomXamlAdapter(std::shared_ptr<IXamlHostAdapter> adapter);
    void SetSettingsForTesting(const ModSettings& settings);
    void ResetForTesting();

private:
    ModLifecycleManager();
    ~ModLifecycleManager();

    mutable std::mutex mutex_;
    std::shared_ptr<CalendarCache> cache_;
    std::shared_ptr<HttpFetcher> fetcher_;
    std::shared_ptr<XamlInjector> injector_;
    std::shared_ptr<IHttpTransport> custom_transport_;
    std::shared_ptr<IXamlHostAdapter> custom_adapter_;
    ModSettings settings_;
    bool initialized_{false};
};

} // namespace calendar

// Windhawk mod entry points (C++ linkage matching Windhawk's engine ABI)
BOOL Wh_ModInit();
void Wh_ModAfterInit();
void Wh_ModSettingsChanged();
void Wh_ModUninit();
