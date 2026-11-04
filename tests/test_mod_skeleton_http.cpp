#include "mod/mod_settings.h"
#include "mod/calendar_cache.h"
#include "mod/http_fetcher.h"
#include "mod/teams_calendar_flyout.h"
#include "calendar_engine/ics_parser.h"
#include "calendar_engine/meeting_status.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: (" #cond ") in " << __FUNCTION__ << " line " << __LINE__ << ": " << (msg) << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

using namespace calendar;

bool TestModSettingsDefaultsAndValidation() {
    std::cout << "Running TestModSettingsDefaultsAndValidation..." << std::endl;

    ModSettings defaults;
    TEST_ASSERT(defaults.feed_url.empty(), "Default feed_url must be empty");
    TEST_ASSERT(defaults.refresh_interval_minutes == 15, "Default refresh interval must be 15 minutes");
    TEST_ASSERT(!defaults.show_past_events, "Default show_past_events must be false");
    TEST_ASSERT(defaults.enable_join_button, "Default enable_join_button must be true");
    TEST_ASSERT(!defaults.IsFeedUrlConfigured(), "Empty feed_url is not configured");

    // Test validation and clamping of refresh interval
    ModSettings invalid_interval = ModSettings::FromValues(L"https://example.com/cal.ics", 0, true, false);
    TEST_ASSERT(invalid_interval.refresh_interval_minutes >= 1, "Interval 0 must be clamped to minimum 1");

    ModSettings negative_interval = ModSettings::FromValues(L"https://example.com/cal.ics", -10, true, false);
    TEST_ASSERT(negative_interval.refresh_interval_minutes >= 1, "Negative interval must be clamped to minimum 1");

    // Test whitespace trimming
    ModSettings trimmed = ModSettings::FromValues(L"   https://example.com/calendar.ics \t\r\n  ", 30, false, true);
    TEST_ASSERT(trimmed.feed_url == L"https://example.com/calendar.ics", "Whitespace should be trimmed from feed_url");
    TEST_ASSERT(trimmed.IsFeedUrlConfigured(), "Trimmed valid URL is configured");
    TEST_ASSERT(trimmed.GetFeedUrlUtf8() == "https://example.com/calendar.ics", "UTF-8 conversion of feed_url is correct");

    // Test equality operator
    ModSettings copy_trimmed = trimmed;
    TEST_ASSERT(copy_trimmed == trimmed, "Identical settings must compare equal");
    copy_trimmed.show_past_events = true;
    TEST_ASSERT(!(copy_trimmed == trimmed), "Modified settings must not compare equal");

    return true;
}

bool TestCalendarCacheBasicAndOfflineResilience() {
    std::cout << "Running TestCalendarCacheBasicAndOfflineResilience..." << std::endl;

    CalendarCache cache;
    TEST_ASSERT(cache.GetStatus() == FetchStatus::Idle, "Initial status must be Idle");
    TEST_ASSERT(cache.EventCount() == 0, "Initial event count must be 0");
    TEST_ASSERT(!cache.HasEvents(), "Initial cache has no events");
    TEST_ASSERT(cache.GetLastError().empty(), "Initial last error must be empty");

    // Transition to Fetching
    cache.SetFetchStatus(FetchStatus::Fetching);
    TEST_ASSERT(cache.GetStatus() == FetchStatus::Fetching, "Status should be Fetching");

    // Populate events on successful fetch
    std::vector<CalendarEvent> events;
    CalendarEvent ev1;
    ev1.uid = "ev-101";
    ev1.summary = "Weekly Architecture Review";
    ev1.start_time = CalendarDateTime::from_unix_timestamp(1700000000);
    ev1.end_time = CalendarDateTime::from_unix_timestamp(1700003600);
    events.push_back(ev1);

    CalendarEvent ev2;
    ev2.uid = "ev-102";
    ev2.summary = "1:1 Sync with Manager";
    ev2.start_time = CalendarDateTime::from_unix_timestamp(1700010000);
    ev2.end_time = CalendarDateTime::from_unix_timestamp(1700013600);
    events.push_back(ev2);

    cache.UpdateEvents(events);
    TEST_ASSERT(cache.GetStatus() == FetchStatus::Succeeded, "Status after update must be Succeeded");
    TEST_ASSERT(cache.EventCount() == 2, "Event count must be 2");
    TEST_ASSERT(cache.HasEvents(), "Cache now has events");
    TEST_ASSERT(cache.GetLastError().empty(), "Last error should be cleared on success");

    auto snapshot = cache.GetEventsSnapshot();
    TEST_ASSERT(snapshot.size() == 2, "Snapshot must contain 2 events");
    TEST_ASSERT(snapshot[0].uid == "ev-101", "Event 1 UID matches");
    TEST_ASSERT(snapshot[1].uid == "ev-102", "Event 2 UID matches");

    // Offline resilience: Next fetch fails (e.g. Wi-Fi disconnect, server 503)
    cache.SetFetchStatus(FetchStatus::Fetching);
    TEST_ASSERT(cache.GetStatus() == FetchStatus::Fetching, "Status is Fetching during attempt");
    TEST_ASSERT(cache.EventCount() == 2, "Cached events are retained during fetching");

    cache.SetFetchStatus(FetchStatus::Failed, "WinINet error 12007: ERROR_INTERNET_NAME_NOT_RESOLVED");
    TEST_ASSERT(cache.GetStatus() == FetchStatus::Failed, "Status should now be Failed");
    TEST_ASSERT(cache.GetLastError() == "WinINet error 12007: ERROR_INTERNET_NAME_NOT_RESOLVED", "Error message recorded");

    // CRITICAL: Previously valid cache must remain intact and fully queryable!
    TEST_ASSERT(cache.HasEvents(), "Cache retains events even after fetch failure");
    TEST_ASSERT(cache.EventCount() == 2, "Event count remains 2 after failure");
    auto offline_snapshot = cache.GetEventsSnapshot();
    TEST_ASSERT(offline_snapshot.size() == 2, "Offline snapshot still returns 2 events");
    TEST_ASSERT(offline_snapshot[0].uid == "ev-101", "Cached event 1 preserved offline");
    TEST_ASSERT(offline_snapshot[1].uid == "ev-102", "Cached event 2 preserved offline");

    // Clear
    cache.Clear();
    TEST_ASSERT(cache.EventCount() == 0, "Event count 0 after clear");
    TEST_ASSERT(!cache.HasEvents(), "HasEvents false after clear");

    return true;
}

bool TestCalendarCacheDateFiltering() {
    std::cout << "Running TestCalendarCacheDateFiltering..." << std::endl;

    CalendarCache cache;

    // Create 3 events on 2026-11-04 and 1 event on 2026-11-05
    std::vector<CalendarEvent> events;

    CalendarEvent ev_past;
    ev_past.uid = "past-1";
    ev_past.summary = "Morning Standup (Past)";
    ev_past.start_time.date = CalendarDate{2026, 11, 4};
    ev_past.start_time.time = CalendarTime{9, 0, 0};
    ev_past.end_time.date = CalendarDate{2026, 11, 4};
    ev_past.end_time.time = CalendarTime{9, 30, 0};
    ev_past.temporal_status = MeetingTemporalStatus::Past;
    events.push_back(ev_past);

    CalendarEvent ev_live;
    ev_live.uid = "live-1";
    ev_live.summary = "Tech Demo (Live)";
    ev_live.start_time.date = CalendarDate{2026, 11, 4};
    ev_live.start_time.time = CalendarTime{10, 0, 0};
    ev_live.end_time.date = CalendarDate{2026, 11, 4};
    ev_live.end_time.time = CalendarTime{11, 0, 0};
    ev_live.temporal_status = MeetingTemporalStatus::Live;
    events.push_back(ev_live);

    CalendarEvent ev_future;
    ev_future.uid = "future-1";
    ev_future.summary = "Design Review (Future)";
    ev_future.start_time.date = CalendarDate{2026, 11, 4};
    ev_future.start_time.time = CalendarTime{14, 0, 0};
    ev_future.end_time.date = CalendarDate{2026, 11, 4};
    ev_future.end_time.time = CalendarTime{15, 0, 0};
    ev_future.temporal_status = MeetingTemporalStatus::Future;
    events.push_back(ev_future);

    CalendarEvent ev_next_day;
    ev_next_day.uid = "next-day-1";
    ev_next_day.summary = "Next Day Sync";
    ev_next_day.start_time.date = CalendarDate{2026, 11, 5};
    ev_next_day.start_time.time = CalendarTime{10, 0, 0};
    ev_next_day.end_time.date = CalendarDate{2026, 11, 5};
    ev_next_day.end_time.time = CalendarTime{11, 0, 0};
    ev_next_day.temporal_status = MeetingTemporalStatus::Future;
    events.push_back(ev_next_day);

    cache.UpdateEvents(events);

    CalendarDate today{2026, 11, 4};
    CalendarDate tomorrow{2026, 11, 5};
    CalendarDate other_day{2026, 11, 6};

    // Filter today including past events
    auto today_all = cache.GetEventsForDate(today, /*show_past_events=*/true);
    TEST_ASSERT(today_all.size() == 3, "Today has 3 total events when show_past_events=true");

    // Filter today hiding past events
    auto today_active = cache.GetEventsForDate(today, /*show_past_events=*/false);
    TEST_ASSERT(today_active.size() == 2, "Today has 2 active events when show_past_events=false");
    TEST_ASSERT(today_active[0].uid == "live-1", "First active event is Live");
    TEST_ASSERT(today_active[1].uid == "future-1", "Second active event is Future");

    // Filter tomorrow
    auto tomorrow_events = cache.GetEventsForDate(tomorrow, /*show_past_events=*/false);
    TEST_ASSERT(tomorrow_events.size() == 1, "Tomorrow has 1 event");
    TEST_ASSERT(tomorrow_events[0].uid == "next-day-1", "Tomorrow event UID matches");

    // Filter other day
    auto empty_events = cache.GetEventsForDate(other_day, /*show_past_events=*/true);
    TEST_ASSERT(empty_events.empty(), "Day with no events returns empty vector");

    return true;
}

bool TestCalendarCacheConcurrencyThreadSafety() {
    std::cout << "Running TestCalendarCacheConcurrencyThreadSafety..." << std::endl;

    CalendarCache cache;
    std::atomic<bool> stop_flag{false};
    std::atomic<int> read_ops{0};
    std::atomic<int> write_ops{0};

    // Writer thread updating cache in rapid succession
    std::thread writer([&]() {
        int counter = 0;
        while (!stop_flag.load(std::memory_order_relaxed)) {
            std::vector<CalendarEvent> batch;
            for (int i = 0; i < 10; ++i) {
                CalendarEvent ev;
                ev.uid = "ev-thread-" + std::to_string(counter) + "-" + std::to_string(i);
                ev.summary = "Meeting " + std::to_string(i);
                ev.start_time.date = CalendarDate{2026, 11, 4};
                ev.start_time.time = CalendarTime{9 + i, 0, 0};
                ev.end_time.date = CalendarDate{2026, 11, 4};
                ev.end_time.time = CalendarTime{10 + i, 0, 0};
                batch.push_back(ev);
            }
            cache.UpdateEvents(batch);
            write_ops.fetch_add(1, std::memory_order_relaxed);
            ++counter;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // 4 Reader threads reading snapshot and querying date concurrently
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&]() {
            CalendarDate query_date{2026, 11, 4};
            while (!stop_flag.load(std::memory_order_relaxed)) {
                auto snap = cache.GetEventsSnapshot();
                auto date_events = cache.GetEventsForDate(query_date, true);
                auto status = cache.GetStatus();
                (void)status;
                read_ops.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    // Let concurrent execution run for 150ms
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    stop_flag.store(true, std::memory_order_relaxed);

    writer.join();
    for (auto& r : readers) {
        r.join();
    }

    std::cout << "  Completed " << write_ops.load() << " writes and " << read_ops.load() << " reads safely." << std::endl;
    TEST_ASSERT(write_ops.load() > 5, "Writer executed multiple write batches");
    TEST_ASSERT(read_ops.load() > 20, "Readers executed multiple read operations");
    TEST_ASSERT(cache.EventCount() == 10, "Final cache has expected count");

    return true;
}

bool TestHttpFetcherPipelineSuccess() {
    std::cout << "Running TestHttpFetcherPipelineSuccess..." << std::endl;

    auto cache = std::make_shared<CalendarCache>();
    auto mock_transport = std::make_shared<MockHttpTransport>();

    std::string sample_ics =
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//Microsoft Corporation//Outlook 16.0 MIMEDIR//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:sample-event-1\r\n"
        "SUMMARY:Product Strategy Sync\r\n"
        "DTSTART:20261104T140000Z\r\n"
        "DTEND:20261104T150000Z\r\n"
        "DESCRIPTION:Join Microsoft Teams Meeting\\nhttps://teams.microsoft.com/l/meetup-join/19%3ameeting_xyz%40thread.v2/0\\n\r\n"
        "LOCATION:Microsoft Teams Meeting\r\n"
        "STATUS:CONFIRMED\r\n"
        "END:VEVENT\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:sample-event-2\r\n"
        "SUMMARY:In-Person Coffee Catchup\r\n"
        "DTSTART:20261104T160000Z\r\n"
        "DTEND:20261104T163000Z\r\n"
        "LOCATION:Cafeteria Level 2\r\n"
        "STATUS:CONFIRMED\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    mock_transport->SetResponse(200, sample_ics);

    HttpFetcher fetcher(cache, mock_transport);
    bool ok = fetcher.FetchOnce(L"https://outlook.office365.com/owa/calendar/test.ics");
    TEST_ASSERT(ok, "FetchOnce should report success for HTTP 200");
    TEST_ASSERT(cache->GetStatus() == FetchStatus::Succeeded, "Cache status should be Succeeded");
    TEST_ASSERT(cache->EventCount() == 2, "Cache should contain 2 parsed events");

    auto events = cache->GetEventsSnapshot();
    TEST_ASSERT(events.size() == 2, "Events count matches");

    // Verify first event has Teams URL extracted and enriched
    const auto& teams_ev = events[0];
    TEST_ASSERT(teams_ev.uid == "sample-event-1", "UID matches event 1");
    TEST_ASSERT(teams_ev.summary == "Product Strategy Sync", "Summary matches");
    TEST_ASSERT(teams_ev.is_teams_meeting, "Event 1 must be identified as Teams meeting");
    TEST_ASSERT(!teams_ev.teams_join_url.empty(), "Event 1 teams_join_url must be populated");
    TEST_ASSERT(teams_ev.teams_join_url.find("teams.microsoft.com/l/meetup-join") != std::string::npos, "URL contains meetup-join");

    // Verify second event is not a Teams meeting
    const auto& in_person_ev = events[1];
    TEST_ASSERT(in_person_ev.uid == "sample-event-2", "UID matches event 2");
    TEST_ASSERT(!in_person_ev.is_teams_meeting, "Coffee catchup is not a Teams meeting");
    TEST_ASSERT(in_person_ev.teams_join_url.empty(), "No teams join URL for in-person event");

    return true;
}

bool TestHttpFetcherOfflineNetworkFailure() {
    std::cout << "Running TestHttpFetcherOfflineNetworkFailure..." << std::endl;

    auto cache = std::make_shared<CalendarCache>();
    auto mock_transport = std::make_shared<MockHttpTransport>();

    // Initial successful fetch
    std::string sample_ics =
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:cached-event-1\r\n"
        "SUMMARY:Pre-existing Meeting\r\n"
        "DTSTART:20261104T100000Z\r\n"
        "DTEND:20261104T110000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    mock_transport->SetResponse(200, sample_ics);

    HttpFetcher fetcher(cache, mock_transport);
    TEST_ASSERT(fetcher.FetchOnce(L"https://example.com/calendar.ics"), "Initial fetch succeeds");
    TEST_ASSERT(cache->EventCount() == 1, "Cache holds 1 event");

    // Next fetch simulates network failure (e.g. HTTP 503 or DNS error)
    mock_transport->SetResponse(503, "", "HTTP 503 Service Unavailable");
    bool fetch_failed_res = fetcher.FetchOnce(L"https://example.com/calendar.ics");
    TEST_ASSERT(!fetch_failed_res, "FetchOnce should return false on HTTP error");
    TEST_ASSERT(cache->GetStatus() == FetchStatus::Failed, "Cache status updated to Failed");
    TEST_ASSERT(cache->GetLastError().find("503") != std::string::npos, "Last error contains HTTP 503");

    // CRITICAL: Cache must retain previously parsed events!
    TEST_ASSERT(cache->EventCount() == 1, "Offline resilience: cached event is preserved");
    auto snapshot = cache->GetEventsSnapshot();
    TEST_ASSERT(snapshot.size() == 1, "Snapshot size is 1");
    TEST_ASSERT(snapshot[0].uid == "cached-event-1", "Pre-existing event preserved intact");

    return true;
}

bool TestHttpFetcherBackgroundWorkerLifecycleAndTrigger() {
    std::cout << "Running TestHttpFetcherBackgroundWorkerLifecycleAndTrigger..." << std::endl;

    auto cache = std::make_shared<CalendarCache>();
    auto mock_transport = std::make_shared<MockHttpTransport>();

    std::string sample_ics =
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:worker-event-1\r\n"
        "SUMMARY:Live Sync Event\r\n"
        "DTSTART:20261104T120000Z\r\n"
        "DTEND:20261104T130000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";
    mock_transport->SetResponse(200, sample_ics);

    HttpFetcher fetcher(cache, mock_transport);
    TEST_ASSERT(!fetcher.IsRunning(), "Initially fetcher is not running");

    ModSettings settings = ModSettings::FromValues(L"https://example.com/feed1.ics", 15);
    fetcher.Start(settings);
    TEST_ASSERT(fetcher.IsRunning(), "Fetcher is running after Start");

    // Wait for worker to complete initial fetch
    int wait_cycles = 0;
    while (cache->GetStatus() != FetchStatus::Succeeded && wait_cycles < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_cycles++;
    }
    TEST_ASSERT(cache->GetStatus() == FetchStatus::Succeeded, "Initial worker fetch completed");
    TEST_ASSERT(cache->EventCount() == 1, "Worker parsed 1 event");
    TEST_ASSERT(mock_transport->GetFetchCount() >= 1, "Transport fetch called");
    TEST_ASSERT(mock_transport->GetLastRequestedUrl() == L"https://example.com/feed1.ics", "Feed 1 requested");

    // Update settings with a new feed URL -> triggers refresh immediately
    ModSettings new_settings = ModSettings::FromValues(L"https://example.com/feed2.ics", 15);
    int prev_fetch_count = mock_transport->GetFetchCount();
    fetcher.UpdateSettings(new_settings);

    wait_cycles = 0;
    while (mock_transport->GetFetchCount() == prev_fetch_count && wait_cycles < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_cycles++;
    }
    TEST_ASSERT(mock_transport->GetLastRequestedUrl() == L"https://example.com/feed2.ics", "Updated feed 2 requested");

    // Trigger explicit fetch
    prev_fetch_count = mock_transport->GetFetchCount();
    fetcher.TriggerFetch();
    wait_cycles = 0;
    while (mock_transport->GetFetchCount() == prev_fetch_count && wait_cycles < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_cycles++;
    }
    TEST_ASSERT(mock_transport->GetFetchCount() > prev_fetch_count, "TriggerFetch caused new fetch");

    // Clean Stop
    fetcher.Stop();
    TEST_ASSERT(!fetcher.IsRunning(), "Fetcher is not running after Stop");

    return true;
}

bool TestHttpFetcherRapidStartStop() {
    std::cout << "Running TestHttpFetcherRapidStartStop..." << std::endl;

    auto cache = std::make_shared<CalendarCache>();
    auto mock_transport = std::make_shared<MockHttpTransport>();
    mock_transport->SetResponse(200, "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nEND:VCALENDAR\r\n");

    HttpFetcher fetcher(cache, mock_transport);
    ModSettings settings = ModSettings::FromValues(L"https://example.com/rapid.ics", 15);

    // Rapid start/stop cycling to ensure no thread leaks, race conditions, or hangs
    for (int i = 0; i < 5; ++i) {
        fetcher.Start(settings);
        TEST_ASSERT(fetcher.IsRunning(), "Fetcher running");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        fetcher.Stop();
        TEST_ASSERT(!fetcher.IsRunning(), "Fetcher stopped");
    }

    return true;
}

bool TestModLifecycleInitSettingsUninit() {
    std::cout << "Running TestModLifecycleInitSettingsUninit..." << std::endl;

    auto& manager = ModLifecycleManager::Instance();
    manager.ResetForTesting();
    TEST_ASSERT(!manager.IsInitialized(), "Initial state is uninitialized");

    // Configure mock transport before initialization
    auto mock_transport = std::make_shared<MockHttpTransport>();
    mock_transport->SetResponse(200,
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:mod-lifecycle-1\r\n"
        "SUMMARY:Exec Staff Sync\r\n"
        "DTSTART:20261104T150000Z\r\n"
        "DTEND:20261104T160000Z\r\n"
        "DESCRIPTION:https://teams.microsoft.com/l/meetup-join/19%3ameeting_xyz%40thread.v2/0\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n"
    );
    manager.SetCustomTransport(mock_transport);

    // Call mod initialization (as Windhawk would)
    BOOL init_result = Wh_ModInit();
    TEST_ASSERT(init_result == TRUE, "Wh_ModInit must succeed");
    TEST_ASSERT(manager.IsInitialized(), "Manager is initialized");
    TEST_ASSERT(manager.GetCache() != nullptr, "Cache is initialized");
    TEST_ASSERT(manager.GetFetcher() != nullptr, "Fetcher is initialized");

    // Provide a test feed URL and notify settings changed
    ModSettings test_settings = ModSettings::FromValues(L"https://example.com/teams_feed.ics", 30, false, true);
    manager.SetSettingsForTesting(test_settings);
    Wh_ModSettingsChanged();

    // Verify fetch occurs
    int wait_cycles = 0;
    while (manager.GetCache()->GetStatus() != FetchStatus::Succeeded && wait_cycles < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        wait_cycles++;
    }
    TEST_ASSERT(manager.GetCache()->GetStatus() == FetchStatus::Succeeded, "Cache updated after settings changed");
    TEST_ASSERT(manager.GetCache()->EventCount() == 1, "Cache holds 1 event");

    auto events = manager.GetCache()->GetEventsSnapshot();
    TEST_ASSERT(events[0].is_teams_meeting, "Event is Teams meeting");
    TEST_ASSERT(events[0].summary == "Exec Staff Sync", "Summary matches");

    // Uninitialize mod (as Windhawk would when mod is disabled or unloaded)
    Wh_ModUninit();
    TEST_ASSERT(!manager.IsInitialized(), "Manager is uninitialized after Wh_ModUninit");
    TEST_ASSERT(manager.GetFetcher() == nullptr, "Fetcher released on uninit");
    TEST_ASSERT(manager.GetCache()->EventCount() == 0, "Cache cleared on uninit");

    // Re-initialize cleanly
    BOOL reinit_result = Wh_ModInit();
    TEST_ASSERT(reinit_result == TRUE, "Re-init succeeds");
    TEST_ASSERT(manager.IsInitialized(), "Manager re-initialized");

    Wh_ModUninit();
    manager.ResetForTesting();

    return true;
}

} // namespace

int RunAllModSkeletonHttpTests() {
    std::cout << "=========================================" << std::endl;
    std::cout << "Starting Mod Skeleton & HTTP Tests       " << std::endl;
    std::cout << "=========================================" << std::endl;

    if (!TestModSettingsDefaultsAndValidation()) return 1;
    if (!TestCalendarCacheBasicAndOfflineResilience()) return 2;
    if (!TestCalendarCacheDateFiltering()) return 3;
    if (!TestCalendarCacheConcurrencyThreadSafety()) return 4;
    if (!TestHttpFetcherPipelineSuccess()) return 5;
    if (!TestHttpFetcherOfflineNetworkFailure()) return 6;
    if (!TestHttpFetcherBackgroundWorkerLifecycleAndTrigger()) return 7;
    if (!TestHttpFetcherRapidStartStop()) return 8;
    if (!TestModLifecycleInitSettingsUninit()) return 9;

    std::cout << "=========================================" << std::endl;
    std::cout << "ALL MOD SKELETON TESTS PASSED!           " << std::endl;
    std::cout << "=========================================" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int RunTestsDll() {
    return RunAllModSkeletonHttpTests();
}

int main() {
    return RunAllModSkeletonHttpTests();
}

