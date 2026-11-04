#include "calendar_engine/ics_parser.h"
#include <iostream>
#include <cassert>
#include <sstream>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: (" #cond ") in " << __FUNCTION__ << " line " << __LINE__ << ": " << (msg) << std::endl; \
            return false; \
        } \
    } while (0)

namespace {

using namespace calendar;

bool TestLineUnfolding() {
    std::cout << "Running TestLineUnfolding..." << std::endl;

    // Standard RFC 5545 folding with CRLF + space and CRLF + tab
    std::string input = 
        "BEGIN:VCALENDAR\r\n"
        "DESCRIPTION:This is a long description that has b\r\n"
        " een folded into multiple lines with a space.\r\n"
        "SUMMARY:Folded with a\r\n"
        "\ttab continuation\r\n"
        "END:VCALENDAR\r\n";

    std::string unfolded = IcsParser::UnfoldLines(input);

    TEST_ASSERT(unfolded.find("DESCRIPTION:This is a long description that has been folded into multiple lines with a space.\r\n") != std::string::npos,
                "Space continuation line should be unfolded");
    TEST_ASSERT(unfolded.find("SUMMARY:Folded with atab continuation\r\n") != std::string::npos,
                "Tab continuation line should be unfolded");

    // Unix LF line ending unfolding
    std::string lf_input = "SUMMARY:Folded\n with LF\n";
    std::string lf_unfolded = IcsParser::UnfoldLines(lf_input);
    TEST_ASSERT(lf_unfolded.find("SUMMARY:Foldedwith LF\n") != std::string::npos,
                "LF continuation should unfold properly");

    return true;
}

bool TestUnescapeText() {
    std::cout << "Running TestUnescapeText..." << std::endl;

    std::string escaped = "Meeting\\, Project X\\; Room \\\\ 101\\nNext line\\NLast line";
    std::string unescaped = IcsParser::UnescapeText(escaped);
    std::string expected = "Meeting, Project X; Room \\ 101\nNext line\nLast line";

    TEST_ASSERT(unescaped == expected, "Escaped characters \\, \\; \\\\ \\n \\N should be unescaped");

    std::string plain = "Plain string without escapes";
    TEST_ASSERT(IcsParser::UnescapeText(plain) == plain, "Plain string should remain unchanged");

    return true;
}

bool TestDateTimeParsing() {
    std::cout << "Running TestDateTimeParsing..." << std::endl;

    IcsParser parser(0); // UTC offset 0 for test

    // UTC timestamp
    auto dt_utc = parser.ParseDateTime("20261104T133045Z");
    TEST_ASSERT(dt_utc.date.year == 2026, "Year should be 2026");
    TEST_ASSERT(dt_utc.date.month == 11, "Month should be 11");
    TEST_ASSERT(dt_utc.date.day == 4, "Day should be 4");
    TEST_ASSERT(dt_utc.time.hour == 13, "Hour should be 13");
    TEST_ASSERT(dt_utc.time.minute == 30, "Minute should be 30");
    TEST_ASSERT(dt_utc.time.second == 45, "Second should be 45");
    TEST_ASSERT(dt_utc.is_utc, "Should be marked UTC");

    // Local floating timestamp
    auto dt_local = parser.ParseDateTime("20261104T090000");
    TEST_ASSERT(!dt_local.is_utc, "Should not be UTC");
    TEST_ASSERT(dt_local.date.year == 2026 && dt_local.date.day == 4, "Date should match");
    TEST_ASSERT(dt_local.time.hour == 9 && dt_local.time.minute == 0, "Time should match");

    // Date-only (all day)
    auto dt_date = parser.ParseDateTime("20261104", "VALUE=DATE");
    TEST_ASSERT(dt_date.date.year == 2026 && dt_date.date.month == 11 && dt_date.date.day == 4, "Date should match");
    TEST_ASSERT(dt_date.time.hour == 0 && dt_date.time.minute == 0, "Time should default to 00:00:00");

    // TZID in parameter
    auto dt_tz = parser.ParseDateTime("20261104T140000", "TZID=\"W. Europe Standard Time\"");
    TEST_ASSERT(dt_tz.tzid == "W. Europe Standard Time", "TZID should be captured");

    return true;
}

bool TestBasicVEventParsing() {
    std::cout << "Running TestBasicVEventParsing..." << std::endl;

    std::string ics = 
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//Test//EN\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:event-12345@test.com\r\n"
        "SUMMARY:Weekly Sprint Sync\r\n"
        "DESCRIPTION:Review sprint backlog and blockers.\r\n"
        "LOCATION:Conference Room Alpha\r\n"
        "DTSTART:20261104T140000Z\r\n"
        "DTEND:20261104T150000Z\r\n"
        "STATUS:CONFIRMED\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    IcsParser parser(0); // UTC
    std::vector<CalendarEvent> events = parser.Parse(ics);

    TEST_ASSERT(events.size() == 1, "Should parse exactly 1 event");
    const auto& ev = events[0];
    TEST_ASSERT(ev.uid == "event-12345@test.com", "UID match");
    TEST_ASSERT(ev.summary == "Weekly Sprint Sync", "SUMMARY match");
    TEST_ASSERT(ev.description == "Review sprint backlog and blockers.", "DESCRIPTION match");
    TEST_ASSERT(ev.location == "Conference Room Alpha", "LOCATION match");
    TEST_ASSERT(ev.start_time.date.year == 2026 && ev.start_time.date.month == 11 && ev.start_time.date.day == 4, "Start date match");
    TEST_ASSERT(ev.start_time.time.hour == 14 && ev.start_time.time.minute == 0, "Start time match");
    TEST_ASSERT(ev.end_time.time.hour == 15 && ev.end_time.time.minute == 0, "End time match");
    TEST_ASSERT(!ev.is_all_day, "Not all day");
    TEST_ASSERT(ev.status == EventStatus::Confirmed, "Status confirmed");

    return true;
}

bool TestTimezoneNormalization() {
    std::cout << "Running TestTimezoneNormalization..." << std::endl;

    // Test with local timezone offset = -180 minutes (UTC-3, e.g. Sao Paulo / BRT)
    IcsParser parser_utc_minus_3(-180);

    // Event 1: 15:00 UTC -> should become 12:00 local time
    std::string ics1 = 
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:ev1\r\n"
        "SUMMARY:Afternoon Meeting\r\n"
        "DTSTART:20261104T150000Z\r\n"
        "DTEND:20261104T160000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    auto events1 = parser_utc_minus_3.Parse(ics1);
    TEST_ASSERT(events1.size() == 1, "Parsed 1 event");
    TEST_ASSERT(events1[0].start_time.time.hour == 12, "15:00 UTC in UTC-3 should be 12:00");
    TEST_ASSERT(events1[0].end_time.time.hour == 13, "16:00 UTC in UTC-3 should be 13:00");
    TEST_ASSERT(events1[0].start_time.date.day == 4, "Date is still day 4");

    // Event 2: Date boundary crossing
    // 01:30 UTC on Nov 4 -> should become 22:30 on Nov 3 in UTC-3!
    std::string ics2 = 
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:ev2\r\n"
        "SUMMARY:Late Night Call\r\n"
        "DTSTART:20261104T013000Z\r\n"
        "DTEND:20261104T023000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    auto events2 = parser_utc_minus_3.Parse(ics2);
    TEST_ASSERT(events2.size() == 1, "Parsed 1 event");
    TEST_ASSERT(events2[0].start_time.date.year == 2026, "Year 2026");
    TEST_ASSERT(events2[0].start_time.date.month == 11, "Month 11");
    TEST_ASSERT(events2[0].start_time.date.day == 3, "Day should roll back to Nov 3");
    TEST_ASSERT(events2[0].start_time.time.hour == 22, "Hour should be 22");
    TEST_ASSERT(events2[0].start_time.time.minute == 30, "Minute should be 30");

    // Event with TZID parameter: Eastern Standard Time (UTC-5 = -300 minutes)
    // 14:00 EST -> UTC is 19:00 -> in UTC-3 is 16:00
    std::string ics_tzid = 
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:ev3\r\n"
        "SUMMARY:EST Call\r\n"
        "DTSTART;TZID=\"Eastern Standard Time\":20261104T140000\r\n"
        "DTEND;TZID=\"Eastern Standard Time\":20261104T150000\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    auto events_tzid = parser_utc_minus_3.Parse(ics_tzid);
    TEST_ASSERT(events_tzid.size() == 1, "Parsed 1 event");
    TEST_ASSERT(events_tzid[0].start_time.time.hour == 16, "14:00 EST converted to UTC-3 should be 16:00");

    return true;
}

bool TestAllDayEvents() {
    std::cout << "Running TestAllDayEvents..." << std::endl;

    std::string ics = 
        "BEGIN:VCALENDAR\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:all-day-1\r\n"
        "SUMMARY:Company Holiday\r\n"
        "DTSTART;VALUE=DATE:20261104\r\n"
        "DTEND;VALUE=DATE:20261105\r\n"
        "END:VEVENT\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:all-day-multi\r\n"
        "SUMMARY:Hackathon (2 days)\r\n"
        "DTSTART;VALUE=DATE:20261104\r\n"
        "DTEND;VALUE=DATE:20261106\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    IcsParser parser(0);
    auto events = parser.Parse(ics);
    TEST_ASSERT(events.size() == 2, "Parsed 2 all-day events");
    TEST_ASSERT(events[0].is_all_day, "Event 1 is all day");
    TEST_ASSERT(events[1].is_all_day, "Event 2 is all day");

    // Event 1 (Single day: Nov 4)
    TEST_ASSERT(events[0].occurs_on(CalendarDate{2026, 11, 4}), "Event 1 occurs on Nov 4");
    TEST_ASSERT(!events[0].occurs_on(CalendarDate{2026, 11, 5}), "Event 1 does NOT occur on Nov 5 (exclusive end date)");
    TEST_ASSERT(!events[0].occurs_on(CalendarDate{2026, 11, 3}), "Event 1 does NOT occur on Nov 3");

    // Event 2 (Multi-day: Nov 4 to Nov 6 exclusive -> Nov 4 and Nov 5)
    TEST_ASSERT(events[1].occurs_on(CalendarDate{2026, 11, 4}), "Hackathon occurs on Nov 4");
    TEST_ASSERT(events[1].occurs_on(CalendarDate{2026, 11, 5}), "Hackathon occurs on Nov 5");
    TEST_ASSERT(!events[1].occurs_on(CalendarDate{2026, 11, 6}), "Hackathon does NOT occur on Nov 6 (exclusive end)");

    return true;
}

bool TestChronologicalSortingAndFiltering() {
    std::cout << "Running TestChronologicalSortingAndFiltering..." << std::endl;

    std::string ics = 
        "BEGIN:VCALENDAR\r\n"
        // 14:00 meeting on Nov 4
        "BEGIN:VEVENT\r\n"
        "UID:ev-afternoon\r\n"
        "SUMMARY:Afternoon Sync\r\n"
        "DTSTART:20261104T140000Z\r\n"
        "DTEND:20261104T150000Z\r\n"
        "END:VEVENT\r\n"
        // 09:00 meeting on Nov 4
        "BEGIN:VEVENT\r\n"
        "UID:ev-morning\r\n"
        "SUMMARY:Morning Standup\r\n"
        "DTSTART:20261104T090000Z\r\n"
        "DTEND:20261104T093000Z\r\n"
        "END:VEVENT\r\n"
        // All-day event on Nov 4
        "BEGIN:VEVENT\r\n"
        "UID:ev-allday\r\n"
        "SUMMARY:Team Offsite\r\n"
        "DTSTART;VALUE=DATE:20261104\r\n"
        "DTEND;VALUE=DATE:20261105\r\n"
        "END:VEVENT\r\n"
        // 11:30 meeting on Nov 4
        "BEGIN:VEVENT\r\n"
        "UID:ev-midday\r\n"
        "SUMMARY:Tech Design Review\r\n"
        "DTSTART:20261104T113000Z\r\n"
        "DTEND:20261104T123000Z\r\n"
        "END:VEVENT\r\n"
        // Event on another day (Nov 5)
        "BEGIN:VEVENT\r\n"
        "UID:ev-nextday\r\n"
        "SUMMARY:Tomorrow Planning\r\n"
        "DTSTART:20261105T100000Z\r\n"
        "DTEND:20261105T110000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    IcsParser parser(0);
    auto all_events = parser.Parse(ics);
    TEST_ASSERT(all_events.size() == 5, "Total 5 events in feed");

    auto day_events = parser.GetEventsForDate(all_events, CalendarDate{2026, 11, 4});
    TEST_ASSERT(day_events.size() == 4, "Should return exactly 4 events for Nov 4");

    // Expected order: All-day event first, then 09:00, 11:30, 14:00
    TEST_ASSERT(day_events[0].uid == "ev-allday", "1st event should be all-day Team Offsite");
    TEST_ASSERT(day_events[1].uid == "ev-morning", "2nd event should be 09:00 Morning Standup");
    TEST_ASSERT(day_events[2].uid == "ev-midday", "3rd event should be 11:30 Tech Design Review");
    TEST_ASSERT(day_events[3].uid == "ev-afternoon", "4th event should be 14:00 Afternoon Sync");

    // Query Nov 5
    auto day5_events = parser.GetEventsForDate(all_events, CalendarDate{2026, 11, 5});
    TEST_ASSERT(day5_events.size() == 1, "Should return exactly 1 event for Nov 5");
    TEST_ASSERT(day5_events[0].uid == "ev-nextday", "Event for Nov 5 match");

    // Query Nov 6 (empty)
    auto day6_events = parser.GetEventsForDate(all_events, CalendarDate{2026, 11, 6});
    TEST_ASSERT(day6_events.empty(), "Should return 0 events for Nov 6");

    return true;
}

bool TestRealWorldFeedWithEscapingAndMultiline() {
    std::cout << "Running TestRealWorldFeedWithEscapingAndMultiline..." << std::endl;

    // Real-world Outlook / Teams export simulation
    std::string complex_feed = 
        "BEGIN:VCALENDAR\r\n"
        "METHOD:PUBLISH\r\n"
        "PRODID:Microsoft Exchange Server 2010\r\n"
        "VERSION:2.0\r\n"
        "X-WR-CALNAME:Calendar\r\n"
        "BEGIN:VTIMEZONE\r\n"
        "TZID:UTC\r\n"
        "BEGIN:STANDARD\r\n"
        "DTSTART:16010101T000000\r\n"
        "TZOFFSETFROM:+0000\r\n"
        "TZOFFSETTO:+0000\r\n"
        "END:STANDARD\r\n"
        "END:VTIMEZONE\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:040000008200E00074C5B7101A82E0080000000080479E2B3C69D801000000000000000\r\n"
        " 01000000095B5FAFC899F84F817117C6415053AA\r\n"
        "SUMMARY:Architecture Sync: Microservices\\, APIs & Security\r\n"
        "DTSTART:20261104T130000Z\r\n"
        "DTEND:20261104T140000Z\r\n"
        "CLASS:PUBLIC\r\n"
        "PRIORITY:5\r\n"
        "STATUS:CONFIRMED\r\n"
        "LOCATION:Teams Call\\; Building 4\\, Room 201\r\n"
        "DESCRIPTION:_________________________________________________________________\r\n"
        " _______________\\nMicrosoft Teams meeting\\nJoin on your computer\\, mobile \r\n"
        " app or room device\\nhttps://teams.microsoft.com/l/meetup-join/19%3ameeting\r\n"
        " _test%40thread.v2/0?context=%7b%22Tid%22%3a%22test%22%7d\\nMeeting ID: 293 \r\n"
        " 049 182 391\\nPasscode: 9eKf82\\n__________________________________________\r\n"
        " ______________________________________\r\n"
        "END:VEVENT\r\n"
        "BEGIN:VEVENT\r\n"
        "UID:cancelled-meeting-999\r\n"
        "SUMMARY:Cancelled Project Retro\r\n"
        "DTSTART:20261104T160000Z\r\n"
        "DTEND:20261104T170000Z\r\n"
        "STATUS:CANCELLED\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    IcsParser parser(0);
    auto events = parser.Parse(complex_feed);

    TEST_ASSERT(events.size() == 2, "Should parse 2 events (including cancelled)");

    const auto& meeting = events[0];
    TEST_ASSERT(meeting.uid == "040000008200E00074C5B7101A82E0080000000080479E2B3C69D80100000000000000001000000095B5FAFC899F84F817117C6415053AA",
                "Folded UID should be properly joined without space");
    TEST_ASSERT(meeting.summary == "Architecture Sync: Microservices, APIs & Security", "Summary unescaped");
    TEST_ASSERT(meeting.location == "Teams Call; Building 4, Room 201", "Location unescaped");
    TEST_ASSERT(meeting.status == EventStatus::Confirmed, "Confirmed status");
    TEST_ASSERT(meeting.description.find("https://teams.microsoft.com/l/meetup-join/19%3ameeting_test%40thread.v2/0?context=%7b%22Tid%22%3a%22test%22%7d") != std::string::npos,
                "Description should contain fully assembled Teams URL without folded spaces");

    const auto& cancelled = events[1];
    TEST_ASSERT(cancelled.status == EventStatus::Cancelled, "Cancelled status should be recognized");

    return true;
}

bool TestEdgeCasesAndRobustness() {
    std::cout << "Running TestEdgeCasesAndRobustness..." << std::endl;

    std::string feed = 
        "BEGIN:VCALENDAR\r\n"
        // Event with no DTEND (should default start + 1 hour)
        "BEGIN:VEVENT\r\n"
        "UID:no-dtend\r\n"
        "SUMMARY:Quick Coffee\r\n"
        "DTSTART:20261104T100000Z\r\n"
        "STATUS:TENTATIVE\r\n"
        // VALARM inside VEVENT with its own SUMMARY/DESCRIPTION
        "BEGIN:VALARM\r\n"
        "ACTION:DISPLAY\r\n"
        "DESCRIPTION:Reminder\r\n"
        "TRIGGER:-PT15M\r\n"
        "END:VALARM\r\n"
        "END:VEVENT\r\n"
        // Multiday timed event (spanning 3 days: Nov 4 20:00 to Nov 6 12:00)
        "BEGIN:VEVENT\r\n"
        "UID:conference-span\r\n"
        "SUMMARY:Tech Conference\r\n"
        "DTSTART:20261104T200000Z\r\n"
        "DTEND:20261106T120000Z\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    IcsParser parser(0);
    auto events = parser.Parse(feed);
    TEST_ASSERT(events.size() == 2, "Parsed 2 events");

    // Event 1 checks
    TEST_ASSERT(events[0].status == EventStatus::Tentative, "Tentative status");
    TEST_ASSERT(events[0].summary == "Quick Coffee", "Summary should not be overwritten by alarm");
    TEST_ASSERT(events[0].end_time.time.hour == 11, "Default end time should be +1 hour (11:00)");

    // Multiday span checks
    TEST_ASSERT(events[1].occurs_on(CalendarDate{2026, 11, 4}), "Occurs on start day (Nov 4)");
    TEST_ASSERT(events[1].occurs_on(CalendarDate{2026, 11, 5}), "Occurs on middle day (Nov 5)");
    TEST_ASSERT(events[1].occurs_on(CalendarDate{2026, 11, 6}), "Occurs on end day (Nov 6)");
    TEST_ASSERT(!events[1].occurs_on(CalendarDate{2026, 11, 7}), "Does not occur on Nov 7");

    return true;
}

bool TestRecurrenceRuleExpansion() {
    std::cout << "Running TestRecurrenceRuleExpansion..." << std::endl;

    std::string feed =
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        // 1. Weekly Standup on Monday, Wednesday, Friday
        "BEGIN:VEVENT\r\n"
        "UID:recurring-standup\r\n"
        "SUMMARY:Engineering Standup\r\n"
        "DTSTART:20261102T090000Z\r\n" // Nov 2, 2026 is a Monday
        "DTEND:20261102T093000Z\r\n"
        "RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR\r\n"
        "DESCRIPTION:Daily standup via Teams\r\n"
        "END:VEVENT\r\n"
        // 2. Daily Standup with UNTIL Nov 5
        "BEGIN:VEVENT\r\n"
        "UID:recurring-daily-until\r\n"
        "SUMMARY:Sprint Daily Sync\r\n"
        "DTSTART:20261101T140000Z\r\n"
        "DTEND:20261101T143000Z\r\n"
        "RRULE:FREQ=DAILY;UNTIL=20261105T000000Z\r\n"
        "END:VEVENT\r\n"
        // 3. Daily with COUNT=3
        "BEGIN:VEVENT\r\n"
        "UID:recurring-daily-count\r\n"
        "SUMMARY:3-Day Workshop\r\n"
        "DTSTART:20261110T100000Z\r\n"
        "DTEND:20261110T120000Z\r\n"
        "RRULE:FREQ=DAILY;COUNT=3\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    IcsParser parser(0);
    auto all_events = parser.Parse(feed);
    TEST_ASSERT(all_events.size() == 3, "Parsed 3 recurring events");

    // Nov 2, 2026 is Monday -> Standup matches
    auto monday_events = parser.GetEventsForDate(all_events, CalendarDate{2026, 11, 2});
    TEST_ASSERT(monday_events.size() >= 2, "Monday has Standup + Daily Sync");
    bool found_standup_mon = false;
    for (const auto& ev : monday_events) {
        if (ev.uid == "recurring-standup") {
            found_standup_mon = true;
            TEST_ASSERT(ev.start_time.date == (CalendarDate{2026, 11, 2}), "Instance start date is Nov 2");
            TEST_ASSERT(ev.start_time.time.hour == 9, "Start time hour is 9");
        }
    }
    TEST_ASSERT(found_standup_mon, "Standup occurs on Monday");

    // Nov 3, 2026 is Tuesday -> Standup should NOT occur (only MO,WE,FR)
    auto tuesday_events = parser.GetEventsForDate(all_events, CalendarDate{2026, 11, 3});
    for (const auto& ev : tuesday_events) {
        TEST_ASSERT(ev.uid != "recurring-standup", "Standup must not occur on Tuesday");
    }

    // Nov 4, 2026 is Wednesday -> Standup DOES occur
    auto wednesday_events = parser.GetEventsForDate(all_events, CalendarDate{2026, 11, 4});
    bool found_standup_wed = false;
    for (const auto& ev : wednesday_events) {
        if (ev.uid == "recurring-standup") {
            found_standup_wed = true;
            TEST_ASSERT(ev.start_time.date == (CalendarDate{2026, 11, 4}), "Wednesday instance date matches");
        }
    }
    TEST_ASSERT(found_standup_wed, "Standup occurs on Wednesday");

    // Nov 5, 2026 -> Daily sync UNTIL Nov 5 00:00:00Z should NOT occur (or expired)
    auto nov6_events = parser.GetEventsForDate(all_events, CalendarDate{2026, 11, 6});
    for (const auto& ev : nov6_events) {
        TEST_ASSERT(ev.uid != "recurring-daily-until", "Daily sync must not occur past UNTIL date");
    }

    // COUNT=3 check: Nov 10 (1), Nov 11 (2), Nov 12 (3), but NOT Nov 13 (4)
    auto nov12_events = parser.GetEventsForDate(all_events, CalendarDate{2026, 11, 12});
    bool found_workshop_nov12 = false;
    for (const auto& ev : nov12_events) {
        if (ev.uid == "recurring-daily-count") found_workshop_nov12 = true;
    }
    TEST_ASSERT(found_workshop_nov12, "Workshop occurs on 3rd day (Nov 12)");

    auto nov13_events = parser.GetEventsForDate(all_events, CalendarDate{2026, 11, 13});
    for (const auto& ev : nov13_events) {
        TEST_ASSERT(ev.uid != "recurring-daily-count", "Workshop must not occur on 4th day (exceeds COUNT=3)");
    }

    return true;
}

} // namespace

int RunAllTests() {
    std::cout << "=========================================" << std::endl;
    std::cout << "Starting Calendar Engine ICS Parser Tests" << std::endl;
    std::cout << "=========================================" << std::endl;

    if (!TestLineUnfolding()) return 1;
    if (!TestUnescapeText()) return 2;
    if (!TestDateTimeParsing()) return 3;
    if (!TestBasicVEventParsing()) return 4;
    if (!TestTimezoneNormalization()) return 5;
    if (!TestAllDayEvents()) return 6;
    if (!TestChronologicalSortingAndFiltering()) return 7;
    if (!TestRealWorldFeedWithEscapingAndMultiline()) return 8;
    if (!TestEdgeCasesAndRobustness()) return 9;
    if (!TestRecurrenceRuleExpansion()) return 10;

    std::cout << "=========================================" << std::endl;
    std::cout << "ALL 10 TEST SUITES PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "=========================================" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int RunTestsDll() {
    return RunAllTests();
}

int main() {
    return RunAllTests();
}
