#pragma once

#include "mod/calendar_cache.h"
#include "mod/http_fetcher.h"
#include "mod/mod_settings.h"
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
    ModSettings GetSettings() const;

    // Injection seam for testing
    void SetCustomTransport(std::shared_ptr<IHttpTransport> transport);
    void SetSettingsForTesting(const ModSettings& settings);
    void ResetForTesting();

private:
    ModLifecycleManager();
    ~ModLifecycleManager();

    mutable std::mutex mutex_;
    std::shared_ptr<CalendarCache> cache_;
    std::shared_ptr<HttpFetcher> fetcher_;
    std::shared_ptr<IHttpTransport> custom_transport_;
    ModSettings settings_;
    bool initialized_{false};
};

} // namespace calendar

// Windhawk mod entry points
extern "C" {
__declspec(dllexport) BOOL Wh_ModInit();
__declspec(dllexport) void Wh_ModSettingsChanged();
__declspec(dllexport) void Wh_ModUninit();
}
