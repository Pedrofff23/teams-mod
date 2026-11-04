#pragma once

#include "ui/agenda_view_model.h"
#include "mod/calendar_cache.h"
#include "mod/mod_settings.h"
#include <string>
#include <string_view>
#include <memory>
#include <mutex>
#include <optional>

namespace calendar {

// Abstract interface for host XAML tree manipulation
class IXamlHostAdapter {
public:
    virtual ~IXamlHostAdapter() = default;

    virtual bool FindTargetContainer(std::string_view container_name) = 0;
    virtual bool InjectElement(std::string_view container_name, std::string_view element_id, std::string_view xaml_content) = 0;
    virtual bool RemoveElement(std::string_view container_name, std::string_view element_id) = 0;
    virtual bool UpdateElement(std::string_view element_id, std::string_view xaml_content) = 0;
    virtual ThemeMode DetectThemeMode() const = 0;
    virtual bool OpenMeetingUrl(std::string_view url) = 0;
};

// Generates WinUI / XAML markup strings for the agenda panel
class XamlMarkupGenerator {
public:
    static std::string EscapeXml(std::string_view text);
    static std::string GenerateAgendaXaml(const AgendaDayViewModel& viewModel, int maxHeight = 260);
};

// Default WinUI adapter that targets Windows 11 ShellHost.exe
class WinUiXamlHostAdapter : public IXamlHostAdapter {
public:
    WinUiXamlHostAdapter() = default;
    ~WinUiXamlHostAdapter() override = default;

    bool FindTargetContainer(std::string_view container_name) override;
    bool InjectElement(std::string_view container_name, std::string_view element_id, std::string_view xaml_content) override;
    bool RemoveElement(std::string_view container_name, std::string_view element_id) override;
    bool UpdateElement(std::string_view element_id, std::string_view xaml_content) override;
    ThemeMode DetectThemeMode() const override;
    bool OpenMeetingUrl(std::string_view url) override;
};

// Controller for Windows 11 Calendar Center Grid XAML visual tree injection
class XamlInjector {
public:
    static constexpr const char* kDefaultTargetContainer = "CalendarCenterGrid";
    static constexpr const char* kAgendaPanelElementId = "TeamsAgendaPanel";
    static constexpr int kDefaultMaxHeight = 260;

    XamlInjector();
    ~XamlInjector();

    // Prevent copy and move
    XamlInjector(const XamlInjector&) = delete;
    XamlInjector& operator=(const XamlInjector&) = delete;

    bool Initialize(
        std::shared_ptr<CalendarCache> cache,
        const ModSettings& settings,
        std::shared_ptr<IXamlHostAdapter> adapter = nullptr
    );

    void Shutdown();

    // Invoked when user clicks/selects a new date on the calendar grid
    void OnDateSelected(const CalendarDate& new_date, std::optional<CalendarDateTime> reference_time = std::nullopt);

    // Invoked when system theme changes (Dark / Light / HighContrast)
    void OnThemeChanged(ThemeMode new_theme);

    // Invoked when user updates settings (e.g. show_past_events, enable_join_button)
    void OnSettingsChanged(const ModSettings& new_settings);

    // Invoked when background calendar fetch finishes and cache is updated
    void OnEventsUpdated(std::optional<CalendarDateTime> reference_time = std::nullopt);

    // Invoked when user clicks a Join Meeting button
    bool HandleJoinButtonClick(std::string_view join_url);

    // Accessors for state inspection and testing
    const CalendarDate& GetSelectedDate() const;
    ThemeMode GetCurrentTheme() const;
    bool IsInjected() const;
    std::string GetCurrentXaml() const;
    AgendaDayViewModel GetCurrentViewModel() const;

private:
    void RenderAndInjectLocked(std::optional<CalendarDateTime> reference_time);

    mutable std::mutex mutex_;
    std::shared_ptr<CalendarCache> cache_;
    std::shared_ptr<IXamlHostAdapter> adapter_;
    ModSettings settings_;
    CalendarDate selected_date_;
    ThemeMode current_theme_{ThemeMode::Dark};
    bool is_injected_{false};
    std::string current_xaml_;
    AgendaDayViewModel current_view_model_;
};

} // namespace calendar
