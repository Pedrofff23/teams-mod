#include "ui/agenda_view_model.h"
#include "ui/xaml_injector.h"
#include "calendar_engine/ics_parser.h"
#include "calendar_engine/meeting_status.h"
#include "mod/calendar_cache.h"
#include "mod/teams_calendar_flyout.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: (" #cond ") in " << __FUNCTION__ << " line " << __LINE__ << ": " << (msg) << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

using namespace calendar;

bool TestThemeModeAndPalettes() {
    std::cout << "Running TestThemeModeAndPalettes..." << std::endl;

    ThemePalette dark = ThemePalette::ForTheme(ThemeMode::Dark);
    TEST_ASSERT(!dark.card_background.empty(), "Dark card background should not be empty");
    TEST_ASSERT(!dark.card_border.empty(), "Dark card border should not be empty");
    TEST_ASSERT(!dark.primary_text.empty(), "Dark primary text should not be empty");
    TEST_ASSERT(dark.teams_accent == "#6264A7", "Dark Teams accent should be #6264A7");
    TEST_ASSERT(dark.live_badge_bg == "#107C41", "Live badge background should be #107C41");
    TEST_ASSERT(dark.soon_badge_bg == "#D83B01", "Starting soon badge background should be #D83B01");

    ThemePalette light = ThemePalette::ForTheme(ThemeMode::Light);
    TEST_ASSERT(!light.card_background.empty(), "Light card background should not be empty");
    TEST_ASSERT(light.card_background != dark.card_background, "Light card background must differ from dark");
    TEST_ASSERT(light.primary_text != dark.primary_text, "Light text color must differ from dark");
    TEST_ASSERT(light.teams_accent == "#464EB8", "Light Teams accent should be #464EB8");

    ThemePalette hc = ThemePalette::ForTheme(ThemeMode::HighContrast);
    TEST_ASSERT(hc.card_background == "#000000", "High contrast background should be black");
    TEST_ASSERT(hc.primary_text == "#FFFFFF", "High contrast primary text should be white");

    return true;
}

bool TestAgendaItemViewModelTransformation() {
    std::cout << "Running TestAgendaItemViewModelTransformation..." << std::endl;

    // Reference time: 2026-11-04 10:05:00 UTC
    CalendarDateTime ref_time;
    ref_time.date = CalendarDate{2026, 11, 4};
    ref_time.time = CalendarTime{10, 5, 0};
    ref_time.is_utc = true;

    // 1. Live Teams Meeting (10:00 - 11:00)
    CalendarEvent ev_live;
    ev_live.uid = "ev-live";
    ev_live.summary = "Sprint Retrospective";
    ev_live.location = "Teams Bridge 1";
    ev_live.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{10, 0, 0}, true};
    ev_live.end_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{11, 0, 0}, true};
    ev_live.is_all_day = false;
    ev_live.is_teams_meeting = true;
    ev_live.teams_join_url = "https://teams.microsoft.com/l/meetup-join/19%3ameeting_live";
    ev_live.temporal_status = MeetingTemporalStatus::Live;

    AgendaItemViewModel vm_live = AgendaItemViewModel::FromCalendarEvent(
        ev_live, ThemeMode::Dark, ref_time, true
    );

    TEST_ASSERT(vm_live.title == "Sprint Retrospective", "Title must match summary");
    TEST_ASSERT(vm_live.formatted_time_range == "10:00 - 11:00", "Time range must format as HH:MM - HH:MM");
    TEST_ASSERT(vm_live.location == "Teams Bridge 1", "Location must match");
    TEST_ASSERT(vm_live.is_teams == true, "Must be Teams meeting");
    TEST_ASSERT(vm_live.join_url == ev_live.teams_join_url, "Join URL must match");
    TEST_ASSERT(vm_live.status_badge_label == "Live Now", "Badge label should be 'Live Now'");
    TEST_ASSERT(vm_live.status_badge_bg == "#107C41", "Live badge should have green background");
    TEST_ASSERT(vm_live.is_all_day == false, "Must not be all-day");

    // 2. Starting Soon Meeting (10:15 - 10:45)
    CalendarEvent ev_soon;
    ev_soon.uid = "ev-soon";
    ev_soon.summary = "1:1 Sync";
    ev_soon.location = "Meeting Room B";
    ev_soon.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{10, 15, 0}, true};
    ev_soon.end_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{10, 45, 0}, true};
    ev_soon.is_all_day = false;
    ev_soon.is_teams_meeting = false;
    ev_soon.temporal_status = MeetingTemporalStatus::Soon;

    AgendaItemViewModel vm_soon = AgendaItemViewModel::FromCalendarEvent(
        ev_soon, ThemeMode::Light, ref_time, true
    );

    TEST_ASSERT(vm_soon.title == "1:1 Sync", "Title must match");
    TEST_ASSERT(vm_soon.status_badge_label == "Starting Soon", "Badge label should be 'Starting Soon'");
    TEST_ASSERT(vm_soon.status_badge_bg == "#D83B01", "Starting soon badge should be orange #D83B01");
    TEST_ASSERT(!vm_soon.is_teams, "Non-teams meeting is_teams must be false");

    // 3. All Day Event
    CalendarEvent ev_allday;
    ev_allday.uid = "ev-allday";
    ev_allday.summary = "Company Hackathon";
    ev_allday.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{0, 0, 0}, true};
    ev_allday.end_time = CalendarDateTime{CalendarDate{2026, 11, 5}, CalendarTime{0, 0, 0}, true};
    ev_allday.is_all_day = true;
    ev_allday.temporal_status = MeetingTemporalStatus::Live;

    AgendaItemViewModel vm_allday = AgendaItemViewModel::FromCalendarEvent(
        ev_allday, ThemeMode::Dark, ref_time, true
    );

    TEST_ASSERT(vm_allday.formatted_time_range == "All Day", "All day event must format time as 'All Day'");
    TEST_ASSERT(vm_allday.is_all_day == true, "Must be marked all-day");

    // 4. Past Event
    CalendarEvent ev_past;
    ev_past.uid = "ev-past";
    ev_past.summary = "Daily Standup";
    ev_past.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{9, 0, 0}, true};
    ev_past.end_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{9, 30, 0}, true};
    ev_past.temporal_status = MeetingTemporalStatus::Past;

    AgendaItemViewModel vm_past = AgendaItemViewModel::FromCalendarEvent(
        ev_past, ThemeMode::Dark, ref_time, true
    );
    TEST_ASSERT(vm_past.status_badge_label == "Past", "Past event badge label should be 'Past'");

    // 5. Future Event
    CalendarEvent ev_future;
    ev_future.uid = "ev-future";
    ev_future.summary = "Tech Talk";
    ev_future.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{14, 0, 0}, true};
    ev_future.end_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{15, 0, 0}, true};
    ev_future.temporal_status = MeetingTemporalStatus::Future;

    AgendaItemViewModel vm_future = AgendaItemViewModel::FromCalendarEvent(
        ev_future, ThemeMode::Dark, ref_time, true
    );
    TEST_ASSERT(vm_future.status_badge_label == "Upcoming", "Future event badge label should be 'Upcoming'");

    return true;
}

bool TestAgendaPresenterEmptyStateAndSorting() {
    std::cout << "Running TestAgendaPresenterEmptyStateAndSorting..." << std::endl;

    CalendarDate selected_date{2026, 11, 4};
    CalendarDateTime ref_time{selected_date, CalendarTime{10, 0, 0}, true};

    // Test 1: Empty cache produces empty state
    CalendarCache empty_cache;
    AgendaDayViewModel empty_vm = AgendaPresenter::PresentDay(
        empty_cache, selected_date, ThemeMode::Dark, ref_time, /*show_past_events=*/true, /*enable_join=*/true
    );

    TEST_ASSERT(empty_vm.is_empty(), "Empty cache should produce empty ViewModel");
    TEST_ASSERT(empty_vm.items.empty(), "Items vector should be empty");
    TEST_ASSERT(empty_vm.empty_state_message == "No meetings scheduled for this day", "Empty message mismatch");
    TEST_ASSERT(!empty_vm.formatted_date_header.empty(), "Date header should not be empty");

    // Test 2: Chronological sorting (all-day events first, then by start time)
    CalendarCache cache;
    std::vector<CalendarEvent> events;

    // Afternoon meeting (14:00)
    CalendarEvent ev1;
    ev1.uid = "ev-afternoon";
    ev1.summary = "Architecture Review";
    ev1.start_time = CalendarDateTime{selected_date, CalendarTime{14, 0, 0}, true};
    ev1.end_time = CalendarDateTime{selected_date, CalendarTime{15, 0, 0}, true};
    ev1.is_all_day = false;
    events.push_back(ev1);

    // All day event
    CalendarEvent ev2;
    ev2.uid = "ev-allday";
    ev2.summary = "Company Holiday / Training";
    ev2.start_time = CalendarDateTime{selected_date, CalendarTime{0, 0, 0}, true};
    ev2.end_time = CalendarDateTime{CalendarDate{2026, 11, 5}, CalendarTime{0, 0, 0}, true};
    ev2.is_all_day = true;
    events.push_back(ev2);

    // Morning meeting (09:00)
    CalendarEvent ev3;
    ev3.uid = "ev-morning";
    ev3.summary = "Morning Triage";
    ev3.start_time = CalendarDateTime{selected_date, CalendarTime{9, 0, 0}, true};
    ev3.end_time = CalendarDateTime{selected_date, CalendarTime{9, 30, 0}, true};
    ev3.is_all_day = false;
    events.push_back(ev3);

    // Midday meeting (11:00)
    CalendarEvent ev4;
    ev4.uid = "ev-midday";
    ev4.summary = "Product Sync";
    ev4.start_time = CalendarDateTime{selected_date, CalendarTime{11, 0, 0}, true};
    ev4.end_time = CalendarDateTime{selected_date, CalendarTime{12, 0, 0}, true};
    ev4.is_all_day = false;
    events.push_back(ev4);

    cache.UpdateEvents(events);

    AgendaDayViewModel day_vm = AgendaPresenter::PresentDay(
        cache, selected_date, ThemeMode::Dark, ref_time, /*show_past_events=*/true, /*enable_join=*/true
    );

    TEST_ASSERT(!day_vm.is_empty(), "ViewModel should not be empty");
    TEST_ASSERT(day_vm.items.size() == 4, "Should contain 4 items");

    // Order must be: 1. All-day, 2. Morning (09:00), 3. Midday (11:00), 4. Afternoon (14:00)
    TEST_ASSERT(day_vm.items[0].title == "Company Holiday / Training", "Item 0 must be all-day event");
    TEST_ASSERT(day_vm.items[0].is_all_day, "Item 0 must be all-day");
    TEST_ASSERT(day_vm.items[1].title == "Morning Triage", "Item 1 must be morning meeting");
    TEST_ASSERT(day_vm.items[1].formatted_time_range == "09:00 - 09:30", "Item 1 time format");
    TEST_ASSERT(day_vm.items[2].title == "Product Sync", "Item 2 must be midday meeting");
    TEST_ASSERT(day_vm.items[3].title == "Architecture Review", "Item 3 must be afternoon meeting");

    // Test 3: Filter past events when show_past_events is false
    // At ref_time 10:00, Morning Triage (09:00 - 09:30) is past!
    AgendaDayViewModel filtered_vm = AgendaPresenter::PresentDay(
        cache, selected_date, ThemeMode::Dark, ref_time, /*show_past_events=*/false, /*enable_join=*/true
    );

    TEST_ASSERT(filtered_vm.items.size() == 3, "Filtered items should exclude past morning triage");
    for (const auto& item : filtered_vm.items) {
        TEST_ASSERT(item.title != "Morning Triage", "Morning Triage should have been excluded");
    }

    // Test 4: Deduplicate identical meetings (e.g. duplicate series or dual invites)
    CalendarEvent dup1;
    dup1.uid = "dup-1";
    dup1.summary = "[GRUPO NRV] Fórum de IA";
    dup1.start_time = CalendarDateTime{selected_date, CalendarTime{11, 0, 0}, true};
    dup1.end_time = CalendarDateTime{selected_date, CalendarTime{12, 0, 0}, true};
    dup1.is_teams_meeting = true;
    dup1.teams_join_url = "https://teams.microsoft.com/l/meetup-join/123";

    CalendarEvent dup2;
    dup2.uid = "dup-2";
    dup2.summary = "[GRUPO NRV] Fórum de IA ";
    dup2.start_time = CalendarDateTime{selected_date, CalendarTime{11, 0, 0}, true};
    dup2.end_time = CalendarDateTime{selected_date, CalendarTime{12, 0, 0}, true};
    dup2.is_teams_meeting = true;
    dup2.teams_join_url = "https://teams.microsoft.com/l/meetup-join/123";

    std::vector<CalendarEvent> dups = {dup1, dup2};
    AgendaDayViewModel dedup_vm = AgendaPresenter::PresentEvents(
        dups, selected_date, ThemeMode::Dark, ref_time, true, true
    );
    TEST_ASSERT(dedup_vm.items.size() == 1, "Duplicate meetings must be merged into 1 item in AgendaPresenter");

    return true;
}

bool TestXmlEscaping() {
    std::cout << "Running TestXmlEscaping..." << std::endl;

    std::string plain = "Regular Meeting Title";
    TEST_ASSERT(XamlMarkupGenerator::EscapeXml(plain) == "Regular Meeting Title", "Plain text unchanged");

    std::string special = "Review <Q&A> \"Top Secret\" 'Launch'";
    std::string escaped = XamlMarkupGenerator::EscapeXml(special);
    TEST_ASSERT(escaped == "Review &lt;Q&amp;A&gt; &quot;Top Secret&quot; &apos;Launch&apos;", "Special chars properly escaped");

    return true;
}

bool TestXamlMarkupGeneration() {
    std::cout << "Running TestXamlMarkupGeneration..." << std::endl;

    CalendarDate selected_date{2026, 11, 4};
    CalendarDateTime ref_time{selected_date, CalendarTime{10, 0, 0}, true};

    // 1. Empty state XAML
    CalendarCache empty_cache;
    AgendaDayViewModel empty_vm = AgendaPresenter::PresentDay(
        empty_cache, selected_date, ThemeMode::Dark, ref_time
    );

    std::string empty_xaml = XamlMarkupGenerator::GenerateAgendaXaml(empty_vm, 260);

    TEST_ASSERT(!empty_xaml.empty(), "Empty XAML should not be empty");
    TEST_ASSERT(empty_xaml.find("<ScrollViewer") != std::string::npos, "Must contain ScrollViewer root");
    TEST_ASSERT(empty_xaml.find("MaxHeight=\"260\"") != std::string::npos, "Must respect max height 260");
    TEST_ASSERT(empty_xaml.find("VerticalScrollBarVisibility=\"Auto\"") != std::string::npos, "Vertical scroll must be auto");
    TEST_ASSERT(empty_xaml.find("HorizontalScrollBarVisibility=\"Disabled\"") != std::string::npos, "Horizontal scroll disabled");
    TEST_ASSERT(empty_xaml.find("No meetings scheduled for this day") != std::string::npos, "Must contain empty state message");
    TEST_ASSERT(empty_xaml.find("</ScrollViewer>") != std::string::npos, "Must properly close ScrollViewer tag");

    // 2. Populated agenda XAML with Teams meeting and regular meeting
    std::vector<CalendarEvent> events;

    CalendarEvent ev1;
    ev1.uid = "ev-teams-1";
    ev1.summary = "Global Townhall & Discussion <CEO>";
    ev1.location = "Teams Online Room";
    ev1.start_time = CalendarDateTime{selected_date, CalendarTime{10, 0, 0}, true};
    ev1.end_time = CalendarDateTime{selected_date, CalendarTime{11, 0, 0}, true};
    ev1.is_teams_meeting = true;
    ev1.teams_join_url = "https://teams.microsoft.com/l/meetup-join/19%3ameeting";
    ev1.temporal_status = MeetingTemporalStatus::Live;
    events.push_back(ev1);

    CalendarEvent ev2;
    ev2.uid = "ev-inperson-2";
    ev2.summary = "Team Lunch";
    ev2.location = "Cafeteria";
    ev2.start_time = CalendarDateTime{selected_date, CalendarTime{12, 0, 0}, true};
    ev2.end_time = CalendarDateTime{selected_date, CalendarTime{13, 0, 0}, true};
    ev2.is_teams_meeting = false;
    ev2.temporal_status = MeetingTemporalStatus::Future;
    events.push_back(ev2);

    CalendarCache populated_cache;
    populated_cache.UpdateEvents(events);

    AgendaDayViewModel day_vm = AgendaPresenter::PresentDay(
        populated_cache, selected_date, ThemeMode::Dark, ref_time, /*show_past_events=*/true, /*enable_join_button=*/true
    );

    std::string xaml = XamlMarkupGenerator::GenerateAgendaXaml(day_vm, 260);

    TEST_ASSERT(xaml.find("Global Townhall &amp; Discussion &lt;CEO&gt;") != std::string::npos, "Escaped summary present");
    TEST_ASSERT(xaml.find("<CEO>") == std::string::npos, "Raw unescaped <CEO> tag must not exist");
    TEST_ASSERT(xaml.find("Live Now") != std::string::npos, "Live Now badge label must exist");
    TEST_ASSERT(xaml.find("#107C41") != std::string::npos, "Live Now badge color must be used");
    TEST_ASSERT(xaml.find("Join Meeting") != std::string::npos, "Join Meeting button text must be present for Teams");
    TEST_ASSERT(xaml.find("https://teams.microsoft.com/l/meetup-join/19%3ameeting") != std::string::npos, "Join URL in button tag");
    TEST_ASSERT(xaml.find("Team Lunch") != std::string::npos, "Second meeting must be present");
    TEST_ASSERT(xaml.find("Cafeteria") != std::string::npos, "Location must be present");

    // 3. When enable_join_button is false, button should not appear
    AgendaDayViewModel no_join_vm = AgendaPresenter::PresentDay(
        populated_cache, selected_date, ThemeMode::Dark, ref_time, /*show_past_events=*/true, /*enable_join_button=*/false
    );
    std::string no_join_xaml = XamlMarkupGenerator::GenerateAgendaXaml(no_join_vm, 260);
    TEST_ASSERT(no_join_xaml.find("Join Meeting") == std::string::npos, "Join Meeting button must be omitted when disabled");

    return true;
}

class MockXamlHostAdapter : public IXamlHostAdapter {
public:
    int inject_count{0};
    int update_count{0};
    int remove_count{0};
    int open_url_count{0};
    std::string last_injected_container;
    std::string last_injected_id;
    std::string last_injected_content;
    std::string last_updated_id;
    std::string last_updated_content;
    std::string last_removed_container;
    std::string last_removed_id;
    std::string last_opened_url;
    ThemeMode detected_theme{ThemeMode::Dark};

    bool FindTargetContainer(std::string_view container_name) override {
        return container_name == "CalendarCenterGrid";
    }

    bool InjectElement(std::string_view container_name, std::string_view element_id, std::string_view xaml_content) override {
        ++inject_count;
        last_injected_container = container_name;
        last_injected_id = element_id;
        last_injected_content = xaml_content;
        return true;
    }

    bool RemoveElement(std::string_view container_name, std::string_view element_id) override {
        ++remove_count;
        last_removed_container = container_name;
        last_removed_id = element_id;
        return true;
    }

    bool UpdateElement(std::string_view element_id, std::string_view xaml_content) override {
        ++update_count;
        last_updated_id = element_id;
        last_updated_content = xaml_content;
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

bool TestXamlInjectorDateSelectionAndReactivity() {
    std::cout << "Running TestXamlInjectorDateSelectionAndReactivity..." << std::endl;

    auto cache = std::make_shared<CalendarCache>();
    auto mock_adapter = std::make_shared<MockXamlHostAdapter>();
    ModSettings settings = ModSettings::FromValuesUtf8("https://example.com/cal.ics", 15, true, true);

    std::vector<CalendarEvent> events;

    // Day 1: 2026-11-04
    CalendarEvent ev1;
    ev1.uid = "ev-day1";
    ev1.summary = "Sprint Planning";
    ev1.location = "Room 101";
    ev1.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{9, 0, 0}, true};
    ev1.end_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{10, 0, 0}, true};
    ev1.is_teams_meeting = true;
    ev1.teams_join_url = "https://teams.microsoft.com/l/meetup-join/sprint-planning";
    events.push_back(ev1);

    // Day 2: 2026-11-05
    CalendarEvent ev2;
    ev2.uid = "ev-day2";
    ev2.summary = "Customer Demo";
    ev2.location = "Virtual Event Room";
    ev2.start_time = CalendarDateTime{CalendarDate{2026, 11, 5}, CalendarTime{14, 0, 0}, true};
    ev2.end_time = CalendarDateTime{CalendarDate{2026, 11, 5}, CalendarTime{15, 30, 0}, true};
    ev2.is_teams_meeting = true;
    ev2.teams_join_url = "https://teams.microsoft.com/l/meetup-join/customer-demo";
    events.push_back(ev2);

    cache->UpdateEvents(events);

    XamlInjector injector;
    bool init_ok = injector.Initialize(cache, settings, mock_adapter);
    TEST_ASSERT(init_ok, "XamlInjector initialization must succeed");
    TEST_ASSERT(injector.IsInjected(), "Injector must be marked as injected");
    TEST_ASSERT(mock_adapter->inject_count == 1, "InjectElement must be called once");
    TEST_ASSERT(mock_adapter->last_injected_container == "CalendarCenterGrid", "Target container must be CalendarCenterGrid");

    CalendarDateTime ref_time{CalendarDate{2026, 11, 4}, CalendarTime{8, 30, 0}, true};

    // User selects 2026-11-04
    injector.OnDateSelected(CalendarDate{2026, 11, 4}, ref_time);
    TEST_ASSERT(injector.GetSelectedDate() == (CalendarDate{2026, 11, 4}), "Selected date must be 2026-11-04");
    AgendaDayViewModel vm_day1 = injector.GetCurrentViewModel();
    TEST_ASSERT(vm_day1.items.size() == 1, "Day 1 must have 1 event");
    TEST_ASSERT(vm_day1.items[0].title == "Sprint Planning", "Day 1 event title mismatch");
    TEST_ASSERT(mock_adapter->last_updated_content.find("Sprint Planning") != std::string::npos, "XAML must contain Sprint Planning");

    // User navigates / clicks to 2026-11-05
    injector.OnDateSelected(CalendarDate{2026, 11, 5}, ref_time);
    TEST_ASSERT(injector.GetSelectedDate() == (CalendarDate{2026, 11, 5}), "Selected date must update to 2026-11-05");
    AgendaDayViewModel vm_day2 = injector.GetCurrentViewModel();
    TEST_ASSERT(vm_day2.items.size() == 1, "Day 2 must have 1 event");
    TEST_ASSERT(vm_day2.items[0].title == "Customer Demo", "Day 2 event title must be Customer Demo");
    TEST_ASSERT(vm_day2.formatted_date_header == "Thursday, November 5, 2026", "Header must be Thursday, November 5, 2026");
    TEST_ASSERT(mock_adapter->last_updated_content.find("Customer Demo") != std::string::npos, "XAML must contain Customer Demo");
    TEST_ASSERT(mock_adapter->last_updated_content.find("Sprint Planning") == std::string::npos, "XAML must NOT contain Sprint Planning from previous day");

    // User clicks a day with no events: 2026-11-06
    injector.OnDateSelected(CalendarDate{2026, 11, 6}, ref_time);
    AgendaDayViewModel vm_day3 = injector.GetCurrentViewModel();
    TEST_ASSERT(vm_day3.is_empty(), "Day 3 must be empty");
    TEST_ASSERT(mock_adapter->last_updated_content.find("No meetings scheduled for this day") != std::string::npos, "Empty state rendered");

    return true;
}

bool TestXamlInjectorThemeAndActionDispatch() {
    std::cout << "Running TestXamlInjectorThemeAndActionDispatch..." << std::endl;

    auto cache = std::make_shared<CalendarCache>();
    auto mock_adapter = std::make_shared<MockXamlHostAdapter>();
    ModSettings settings = ModSettings::FromValuesUtf8("https://example.com/cal.ics", 15, true, true);

    CalendarEvent ev;
    ev.uid = "ev-theme-test";
    ev.summary = "Executive Briefing";
    ev.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{11, 0, 0}, true};
    ev.end_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{12, 0, 0}, true};
    ev.is_teams_meeting = true;
    ev.teams_join_url = "https://teams.microsoft.com/l/meetup-join/exec-briefing";
    std::vector<CalendarEvent> events{ev};
    cache->UpdateEvents(events);

    XamlInjector injector;
    injector.Initialize(cache, settings, mock_adapter);
    CalendarDateTime ref_time{CalendarDate{2026, 11, 4}, CalendarTime{10, 0, 0}, true};
    injector.OnDateSelected(CalendarDate{2026, 11, 4}, ref_time);

    // Initial theme is Dark
    TEST_ASSERT(injector.GetCurrentTheme() == ThemeMode::Dark, "Initial theme should be Dark");
    TEST_ASSERT(mock_adapter->last_updated_content.find("#6264A7") != std::string::npos, "Dark Teams accent used");

    // 1. Switch to Light theme
    injector.OnThemeChanged(ThemeMode::Light);
    TEST_ASSERT(injector.GetCurrentTheme() == ThemeMode::Light, "Theme must change to Light");
    TEST_ASSERT(mock_adapter->last_updated_content.find("#464EB8") != std::string::npos, "Light Teams accent used");
    TEST_ASSERT(mock_adapter->last_updated_content.find("#FFFFFF") != std::string::npos, "Light card background used");

    // 2. Action dispatch: user clicks Join Meeting button
    bool joined = injector.HandleJoinButtonClick("https://teams.microsoft.com/l/meetup-join/exec-briefing");
    TEST_ASSERT(joined, "Join button click handling must succeed");
    TEST_ASSERT(mock_adapter->open_url_count == 1, "OpenMeetingUrl should be called once");
    TEST_ASSERT(mock_adapter->last_opened_url == "https://teams.microsoft.com/l/meetup-join/exec-briefing", "URL mismatch in open call");

    // 3. Settings update: disable join button
    settings.enable_join_button = false;
    injector.OnSettingsChanged(settings);
    TEST_ASSERT(mock_adapter->last_updated_content.find("Join Meeting") == std::string::npos, "Join meeting button removed after settings changed");

    // 4. Shutdown and cleanup
    injector.Shutdown();
    TEST_ASSERT(!injector.IsInjected(), "Injector should no longer be injected after Shutdown");
    TEST_ASSERT(mock_adapter->remove_count == 1, "RemoveElement should be called once");
    TEST_ASSERT(mock_adapter->last_removed_id == "TeamsAgendaPanel", "Element id TeamsAgendaPanel should be removed");

    return true;
}

bool TestModLifecycleManagerWithXamlInjection() {
    std::cout << "Running TestModLifecycleManagerWithXamlInjection..." << std::endl;

    auto& manager = ModLifecycleManager::Instance();
    manager.ResetForTesting();

    auto mock_adapter = std::make_shared<MockXamlHostAdapter>();
    manager.SetCustomXamlAdapter(mock_adapter);

    auto mock_transport = std::make_shared<MockHttpTransport>();
    std::string sample_ics =
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:ev-lifecycle-1\r\n"
        "SUMMARY:Live Sync With Team\r\n"
        "DTSTART:20261104T100000Z\r\n"
        "DTEND:20261104T110000Z\r\n"
        "DESCRIPTION:Join at https://teams.microsoft.com/l/meetup-join/lifecycle\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    mock_transport->SetResponse(200, sample_ics);
    manager.SetCustomTransport(mock_transport);

    ModSettings settings = ModSettings::FromValuesUtf8("https://example.com/test.ics", 15, true, true);
    manager.SetSettingsForTesting(settings);

    bool init_ok = manager.Initialize();
    TEST_ASSERT(init_ok, "Lifecycle manager Initialize must succeed");
    TEST_ASSERT(manager.IsInitialized(), "Manager must report initialized");
    TEST_ASSERT(manager.GetInjector() != nullptr, "Injector must be instantiated");
    TEST_ASSERT(manager.GetInjector()->IsInjected(), "Injector must be injected");
    TEST_ASSERT(mock_adapter->inject_count == 1, "Mock adapter must have received 1 inject call");

    // Verify cache update propagates to injector UI
    manager.GetFetcher()->FetchOnce(L"https://example.com/test.ics");
    TEST_ASSERT(manager.GetCache()->EventCount() == 1, "Cache must contain 1 event");

    // Set selected date on injector to 2026-11-04
    CalendarDateTime ref_time{CalendarDate{2026, 11, 4}, CalendarTime{10, 0, 0}, true};
    manager.GetInjector()->OnDateSelected(CalendarDate{2026, 11, 4}, ref_time);

    std::string xaml = manager.GetInjector()->GetCurrentXaml();
    TEST_ASSERT(xaml.find("Live Sync With Team") != std::string::npos, "XAML must contain Live Sync With Team");
    TEST_ASSERT(xaml.find("Join Meeting") != std::string::npos, "XAML must contain Join Meeting button");

    // Change settings to disable join button
    settings.enable_join_button = false;
    manager.SetSettingsForTesting(settings);
    manager.OnSettingsChanged();

    std::string xaml_no_join = manager.GetInjector()->GetCurrentXaml();
    TEST_ASSERT(xaml_no_join.find("Join Meeting") == std::string::npos, "Join Meeting button must be removed after settings change");

    // Uninitialize mod
    manager.Uninitialize();
    TEST_ASSERT(!manager.IsInitialized(), "Manager must report uninitialized");
    TEST_ASSERT(mock_adapter->remove_count == 1, "Adapter remove count should be 1");
    TEST_ASSERT(manager.GetCache()->EventCount() == 0, "Cache cleared on uninit");

    manager.ResetForTesting();
    return true;
}

} // namespace

extern "C" __declspec(dllexport) int RunTestsDll() {
    std::cout << "=========================================" << std::endl;
    std::cout << "Starting Agenda UI & View Model Tests   " << std::endl;
    std::cout << "=========================================" << std::endl;

    if (!TestThemeModeAndPalettes()) return 1;
    if (!TestAgendaItemViewModelTransformation()) return 2;
    if (!TestAgendaPresenterEmptyStateAndSorting()) return 3;
    if (!TestXmlEscaping()) return 4;
    if (!TestXamlMarkupGeneration()) return 5;
    if (!TestXamlInjectorDateSelectionAndReactivity()) return 6;
    if (!TestXamlInjectorThemeAndActionDispatch()) return 7;
    if (!TestModLifecycleManagerWithXamlInjection()) return 8;

    std::cout << "=========================================" << std::endl;
    std::cout << "ALL AGENDA UI TESTS PASSED SUCCESSFULLY! " << std::endl;
    std::cout << "=========================================" << std::endl;
    return 0;
}
