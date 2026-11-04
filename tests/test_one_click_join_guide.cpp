#include "ui/agenda_view_model.h"
#include "ui/xaml_injector.h"
#include "calendar_engine/ics_parser.h"
#include "calendar_engine/meeting_status.h"
#include "calendar_engine/teams_extractor.h"
#include "mod/calendar_cache.h"
#include "mod/mod_settings.h"
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: (" #cond ") in " << __FUNCTION__ << " line " << __LINE__ << ": " << (msg) << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

using namespace calendar;

class MockJoinXamlHostAdapter : public IXamlHostAdapter {
public:
    int open_url_count{0};
    std::string last_opened_url;
    ThemeMode detected_theme{ThemeMode::Dark};

    bool FindTargetContainer(std::string_view container_name) override {
        return container_name == "CalendarCenterGrid";
    }

    bool InjectElement(std::string_view, std::string_view, std::string_view) override {
        return true;
    }

    bool RemoveElement(std::string_view, std::string_view) override {
        return true;
    }

    bool UpdateElement(std::string_view, std::string_view) override {
        return true;
    }

    ThemeMode DetectThemeMode() const override {
        return detected_theme;
    }

    bool OpenMeetingUrl(std::string_view url) override {
        ++open_url_count;
        last_opened_url = url;
        return true;
    }
};

bool TestJoinActionRoutingAndParameterHandling() {
    std::cout << "Running TestJoinActionRoutingAndParameterHandling..." << std::endl;

    auto cache = std::make_shared<CalendarCache>();
    auto mock_adapter = std::make_shared<MockJoinXamlHostAdapter>();
    ModSettings settings = ModSettings::FromValuesUtf8("https://example.com/cal.ics", 15, true, true);

    XamlInjector injector;
    bool init_ok = injector.Initialize(cache, settings, mock_adapter);
    TEST_ASSERT(init_ok, "XamlInjector initialization failed");

    // 1. Dispatch valid Teams join URL with query parameters
    std::string complex_url = "https://teams.microsoft.com/l/meetup-join/19%3ameeting_xyz123%40thread.v2/0?context=%7b%22Tid%22%3a%2272f988bf%22%2c%22Oid%22%3a%22abc%22%7d&anon=true";
    bool dispatched = injector.HandleJoinButtonClick(complex_url);
    TEST_ASSERT(dispatched, "Dispatched complex Teams URL should succeed");
    TEST_ASSERT(mock_adapter->open_url_count == 1, "Adapter OpenMeetingUrl should be called once");
    TEST_ASSERT(mock_adapter->last_opened_url == complex_url, "Dispatched URL must preserve query parameters intact");

    // 2. Dispatch msteams protocol URL
    std::string msteams_url = "msteams:/l/meetup-join/19%3ameeting_abc";
    bool msteams_dispatched = injector.HandleJoinButtonClick(msteams_url);
    TEST_ASSERT(msteams_dispatched, "Dispatched msteams scheme should succeed");
    TEST_ASSERT(mock_adapter->open_url_count == 2, "Adapter OpenMeetingUrl should be called twice");
    TEST_ASSERT(mock_adapter->last_opened_url == msteams_url, "Dispatched msteams URL mismatch");

    // 3. Dispatch Outlook SafeLink pointing to Teams
    std::string safelink_url = "https://nam01.safelinks.protection.outlook.com/?url=https%3A%2F%2Fteams.microsoft.com%2Fl%2Fmeetup-join%2Ftest&data=05%7C02";
    bool safelink_dispatched = injector.HandleJoinButtonClick(safelink_url);
    TEST_ASSERT(safelink_dispatched, "Outlook SafeLink pointing to Teams should be allowed");
    TEST_ASSERT(mock_adapter->open_url_count == 3, "Adapter count should be 3");

    return true;
}

bool TestUrlDispatchSanitization() {
    std::cout << "Running TestUrlDispatchSanitization..." << std::endl;

    // Direct UrlSanitizer tests
    TEST_ASSERT(UrlSanitizer::IsSafeMeetingUrl("https://teams.microsoft.com/l/meetup-join/19:meeting"), "Valid HTTPS URL must be safe");
    TEST_ASSERT(UrlSanitizer::IsSafeMeetingUrl("http://teams.microsoft.com/l/meetup-join/19:meeting"), "Valid HTTP URL must be safe");
    TEST_ASSERT(UrlSanitizer::IsSafeMeetingUrl("msteams:/l/meetup-join/19:meeting"), "Valid msteams: URL must be safe");
    TEST_ASSERT(UrlSanitizer::IsSafeMeetingUrl("msteams://teams.microsoft.com/l/meetup-join/19:meeting"), "Valid msteams:// URL must be safe");
    TEST_ASSERT(UrlSanitizer::IsSafeMeetingUrl("https://eur02.safelinks.protection.outlook.com/?url=https%3A%2F%2Fteams.microsoft.com"), "Valid SafeLink must be safe");

    // Unsafe and malicious URLs must be rejected
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl(""), "Empty URL must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("   "), "Whitespace-only URL must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("file:///C:/Windows/System32/cmd.exe"), "file:// protocol must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("javascript:alert(document.cookie)"), "javascript: protocol must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("data:text/html,<script>evil()</script>"), "data: protocol must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("cmd.exe /c calc.exe"), "Command execution must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("powershell.exe -ExecutionPolicy Bypass"), "PowerShell execution must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("ms-settings:privacy"), "ms-settings protocol must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("c:\\program files\\malware.exe"), "Direct file path must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("https://teams.microsoft.com/meet\r\ncmd.exe"), "URL with CRLF injection must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("https://"), "Scheme without host must be rejected");
    TEST_ASSERT(!UrlSanitizer::IsSafeMeetingUrl("http://"), "HTTP Scheme without host must be rejected");

    // Test rejection via XamlInjector routing
    auto cache = std::make_shared<CalendarCache>();
    auto mock_adapter = std::make_shared<MockJoinXamlHostAdapter>();
    ModSettings settings = ModSettings::FromValuesUtf8("https://example.com/cal.ics", 15, true, true);

    XamlInjector injector;
    injector.Initialize(cache, settings, mock_adapter);

    bool res1 = injector.HandleJoinButtonClick("file:///C:/evil.bat");
    TEST_ASSERT(!res1, "Injector must reject file:// URL");
    TEST_ASSERT(mock_adapter->open_url_count == 0, "Adapter must not be called for unsafe file:// URL");

    bool res2 = injector.HandleJoinButtonClick("cmd.exe /c whoami");
    TEST_ASSERT(!res2, "Injector must reject cmd.exe invocation");
    TEST_ASSERT(mock_adapter->open_url_count == 0, "Adapter must not be called for cmd.exe");

    // Test WinUiXamlHostAdapter::OpenMeetingUrl rejects unsafe URLs
    WinUiXamlHostAdapter real_adapter;
    TEST_ASSERT(!real_adapter.OpenMeetingUrl(""), "Real adapter must reject empty URL");
    TEST_ASSERT(!real_adapter.OpenMeetingUrl("file:///C:/test.bat"), "Real adapter must reject file://");
    TEST_ASSERT(!real_adapter.OpenMeetingUrl("cmd.exe"), "Real adapter must reject cmd.exe");

    return true;
}

bool TestFluentButtonStylingGeneration() {
    std::cout << "Running TestFluentButtonStylingGeneration..." << std::endl;

    CalendarDate selected_date{2026, 11, 4};
    CalendarDateTime ref_time{selected_date, CalendarTime{10, 0, 0}, true};

    // 1. Teams event with join button enabled
    CalendarEvent ev;
    ev.uid = "ev-fluent-btn-test";
    ev.summary = "Architecture Review <Design>";
    ev.start_time = CalendarDateTime{selected_date, CalendarTime{10, 0, 0}, true};
    ev.end_time = CalendarDateTime{selected_date, CalendarTime{11, 0, 0}, true};
    ev.is_teams_meeting = true;
    ev.teams_join_url = "https://teams.microsoft.com/l/meetup-join/19%3ameeting_arch?context=%7b%22tid%22%3a%22123%22%7d&p=1";
    ev.temporal_status = MeetingTemporalStatus::Live;

    CalendarCache cache;
    cache.UpdateEvents({ev});

    AgendaDayViewModel vm_enabled = AgendaPresenter::PresentDay(
        cache, selected_date, ThemeMode::Dark, ref_time, true, /*enable_join_button=*/true
    );

    std::string xaml = XamlMarkupGenerator::GenerateAgendaXaml(vm_enabled, 260);

    // Verify Fluent styling attributes
    TEST_ASSERT(xaml.find("CornerRadius=\"4\"") != std::string::npos || xaml.find("CornerRadius=\"6\"") != std::string::npos,
                "Button or card must feature rounded corners (CornerRadius 4 or 6)");
    TEST_ASSERT(xaml.find("Join Meeting") != std::string::npos, "Join Meeting button text must exist");
    TEST_ASSERT(xaml.find("Segoe Fluent Icons") != std::string::npos || xaml.find("&#xE714;") != std::string::npos,
                "Fluent Teams glyph / icon must be rendered");
    TEST_ASSERT(xaml.find("#6264A7") != std::string::npos, "Fluent Teams accent color brush must be used in dark mode");

    // Verify URL XML escaping
    TEST_ASSERT(xaml.find("https://teams.microsoft.com/l/meetup-join/19%3ameeting_arch?context=&quot;tid&quot;:&quot;123&quot;&amp;p=1") != std::string::npos ||
                xaml.find("&amp;p=1") != std::string::npos,
                "Query parameter ampersands in Tag attribute must be escaped as &amp;");

    // 2. Non-Teams event must not generate join button
    CalendarEvent non_teams_ev;
    non_teams_ev.uid = "ev-non-teams";
    non_teams_ev.summary = "In-Person Whiteboard";
    non_teams_ev.start_time = CalendarDateTime{selected_date, CalendarTime{13, 0, 0}, true};
    non_teams_ev.end_time = CalendarDateTime{selected_date, CalendarTime{14, 0, 0}, true};
    non_teams_ev.is_teams_meeting = false;
    cache.UpdateEvents({non_teams_ev});

    AgendaDayViewModel vm_non_teams = AgendaPresenter::PresentDay(
        cache, selected_date, ThemeMode::Dark, ref_time, true, true
    );
    std::string non_teams_xaml = XamlMarkupGenerator::GenerateAgendaXaml(vm_non_teams, 260);
    TEST_ASSERT(non_teams_xaml.find("In-Person Whiteboard") != std::string::npos, "Non-teams event must be listed");
    TEST_ASSERT(non_teams_xaml.find("Join Meeting") == std::string::npos, "Join button must NOT be generated for non-Teams event");

    // 3. When enable_join_button is false, button is suppressed even for Teams events
    AgendaDayViewModel vm_disabled = AgendaPresenter::PresentDay(
        cache, selected_date, ThemeMode::Dark, ref_time, true, /*enable_join_button=*/false
    );
    std::string disabled_xaml = XamlMarkupGenerator::GenerateAgendaXaml(vm_disabled, 260);
    TEST_ASSERT(disabled_xaml.find("Join Meeting") == std::string::npos, "Join button must be suppressed when enable_join_button is false");

    return true;
}

bool TestSelfContainedModBundle() {
    std::cout << "Running TestSelfContainedModBundle..." << std::endl;

    // Check root teams-calendar-flyout.wh.cpp
    std::ifstream mod_file("teams-calendar-flyout.wh.cpp");
    TEST_ASSERT(mod_file.is_open(), "Root teams-calendar-flyout.wh.cpp must exist and be readable");

    std::stringstream buffer;
    buffer << mod_file.rdbuf();
    std::string mod_src = buffer.str();

    TEST_ASSERT(!mod_src.empty(), "Mod source code must not be empty");
    TEST_ASSERT(mod_src.find("// ==WindhawkMod==") != std::string::npos, "Must contain WindhawkMod metadata block");
    TEST_ASSERT(mod_src.find("@id              teams-calendar-flyout") != std::string::npos, "Must declare mod id teams-calendar-flyout");
    TEST_ASSERT(mod_src.find("// ==WindhawkModSettings==") != std::string::npos, "Must contain WindhawkModSettings block");
    TEST_ASSERT(mod_src.find("feed_url:") != std::string::npos, "Must contain feed_url setting");
    TEST_ASSERT(mod_src.find("refresh_interval_minutes:") != std::string::npos, "Must contain refresh_interval_minutes setting");
    TEST_ASSERT(mod_src.find("Wh_ModInit") != std::string::npos, "Must declare Wh_ModInit export");
    TEST_ASSERT(mod_src.find("Wh_ModSettingsChanged") != std::string::npos, "Must declare Wh_ModSettingsChanged export");
    TEST_ASSERT(mod_src.find("Wh_ModUninit") != std::string::npos, "Must declare Wh_ModUninit export");

    // Ensure it is 100% self-contained: no #include "src/..." or relative directory includes
    TEST_ASSERT(mod_src.find("#include \"src/") == std::string::npos, "Self-contained bundle must NOT include relative src/ files");
    TEST_ASSERT(mod_src.find("#include \"calendar_engine/") == std::string::npos, "Self-contained bundle must not include relative calendar_engine/ headers");
    TEST_ASSERT(mod_src.find("#include \"mod/") == std::string::npos, "Self-contained bundle must not include relative mod/ headers");
    TEST_ASSERT(mod_src.find("#include \"ui/") == std::string::npos, "Self-contained bundle must not include relative ui/ headers");

    return true;
}

bool TestDocumentationCompleteness() {
    std::cout << "Running TestDocumentationCompleteness..." << std::endl;

    std::ifstream doc_file("TUTORIAL_MOD_WINDHAWK.md");
    TEST_ASSERT(doc_file.is_open(), "TUTORIAL_MOD_WINDHAWK.md must exist in root");

    std::stringstream buf;
    buf << doc_file.rdbuf();
    std::string doc = buf.str();

    TEST_ASSERT(!doc.empty(), "TUTORIAL_MOD_WINDHAWK.md must not be empty");

    // Verify key sections and content required by Ticket 05 & Spec 0001
    TEST_ASSERT(doc.find("ShellHost.exe") != std::string::npos, "Must document ShellHost.exe shell architecture");
    TEST_ASSERT(doc.find("CalendarCenterGrid") != std::string::npos, "Must document CalendarCenterGrid injection container");
    TEST_ASSERT(doc.find("Publish a calendar") != std::string::npos, "Must document Outlook publish calendar feature");
    TEST_ASSERT(doc.find("ICS") != std::string::npos, "Must document ICS link");
    TEST_ASSERT(doc.find("Developer Mode") != std::string::npos, "Must document Windhawk Developer Mode");
    TEST_ASSERT(doc.find("Wh_Log") != std::string::npos, "Must document Windhawk Mod Log / Wh_Log inspection");
    TEST_ASSERT(doc.find("Smart App Control") != std::string::npos, "Must document Smart App Control troubleshooting");
    TEST_ASSERT(doc.find("enable_join_button") != std::string::npos, "Must document enable_join_button setting");
    TEST_ASSERT(doc.find("refresh_interval_minutes") != std::string::npos, "Must document refresh_interval_minutes setting");
    TEST_ASSERT(doc.find("show_past_events") != std::string::npos, "Must document show_past_events setting");

    return true;
}

} // namespace

extern "C" __declspec(dllexport) int RunTestsDll() {
    std::cout << "=========================================" << std::endl;
    std::cout << "Starting One-Click Join & Guide Tests    " << std::endl;
    std::cout << "=========================================" << std::endl;

    if (!TestJoinActionRoutingAndParameterHandling()) return 1;
    if (!TestUrlDispatchSanitization()) return 2;
    if (!TestFluentButtonStylingGeneration()) return 3;
    if (!TestSelfContainedModBundle()) return 4;
    if (!TestDocumentationCompleteness()) return 5;

    std::cout << "=========================================" << std::endl;
    std::cout << "ALL ONE-CLICK JOIN & GUIDE TESTS PASSED! " << std::endl;
    std::cout << "=========================================" << std::endl;
    return 0;
}
