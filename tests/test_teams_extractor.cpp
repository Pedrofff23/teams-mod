#include "calendar_engine/teams_extractor.h"
#include "calendar_engine/meeting_status.h"
#include "calendar_engine/ics_parser.h"
#include <iostream>
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

bool TestStandardAndGovTeamsUrls() {
    std::cout << "Running TestStandardAndGovTeamsUrls..." << std::endl;

    // Standard commercial Teams join URL
    std::string standard_url = "https://teams.microsoft.com/l/meetup-join/19%3ameeting_12345%40thread.v2/0?context=%7b%22Tid%22%3a%22abc%22%7d";
    TEST_ASSERT(TeamsExtractor::IsTeamsUrl(standard_url), "Standard Teams URL must be recognized");

    std::string text_with_standard = "Join Teams Meeting:\r\n" + standard_url + "\r\nMeeting ID: 123 456";
    std::string extracted_standard = TeamsExtractor::ExtractTeamsUrl(text_with_standard);
    TEST_ASSERT(extracted_standard == standard_url, "Standard Teams URL should be extracted from text");

    // Consumer / Live Teams URL
    std::string consumer_url = "https://teams.live.com/meet/948372615029";
    TEST_ASSERT(TeamsExtractor::IsTeamsUrl(consumer_url), "Consumer Teams URL must be recognized");

    std::string text_with_consumer = "Please connect at " + consumer_url + " on Saturday.";
    std::string extracted_consumer = TeamsExtractor::ExtractTeamsUrl(text_with_consumer);
    TEST_ASSERT(extracted_consumer == consumer_url, "Consumer Teams URL should be extracted from text");

    // GCC / US Government Teams URL
    std::string gov_url = "https://gov.teams.microsoft.us/l/meetup-join/19%3ameeting_gov123%40thread.v2/0?context=%7b%22Tid%22%3a%22gov%22%7d";
    TEST_ASSERT(TeamsExtractor::IsTeamsUrl(gov_url), "Gov Teams URL must be recognized");

    std::string text_with_gov = "Official Call: " + gov_url;
    std::string extracted_gov = TeamsExtractor::ExtractTeamsUrl(text_with_gov);
    TEST_ASSERT(extracted_gov == gov_url, "Gov Teams URL should be extracted from text");

    return true;
}

bool TestSafeLinksDecoding() {
    std::cout << "Running TestSafeLinksDecoding..." << std::endl;

    // Outlook SafeLink wrapping a standard Teams URL
    std::string wrapped_teams = "https://teams.microsoft.com/l/meetup-join/19%3ameeting_abc%40thread.v2/0?context=%7b%22Tid%22%3a%22123%22%7d";
    std::string safelink_teams = "https://nam01.safelinks.protection.outlook.com/?url=https%3A%2F%2Fteams.microsoft.com%2Fl%2Fmeetup-join%2F19%253ameeting_abc%2540thread.v2%2F0%3Fcontext%3D%257b%2522Tid%2522%253a%2522123%2522%257d&data=04%7C01%7Cuser%40example.com&sdata=xyz";

    auto decoded_opt = TeamsExtractor::DecodeSafeLink(safelink_teams);
    TEST_ASSERT(decoded_opt.has_value(), "DecodeSafeLink should decode Outlook SafeLink");
    TEST_ASSERT(*decoded_opt == wrapped_teams, "Decoded SafeLink should match target Teams URL");

    // Extraction from text containing SafeLink
    std::string text_safelink = "Join on your device:\r\n" + safelink_teams + "\r\nTap to join";
    std::string extracted = TeamsExtractor::ExtractTeamsUrl(text_safelink);
    TEST_ASSERT(extracted == wrapped_teams, "ExtractTeamsUrl should extract decoded target from SafeLink");

    // Outlook SafeLink wrapping consumer Teams
    std::string safelink_consumer = "https://eur01.safelinks.protection.outlook.com/?url=https%3A%2F%2Fteams.live.com%2Fmeet%2F987654321&data=abc";
    std::string extracted_consumer = TeamsExtractor::ExtractTeamsUrl(safelink_consumer);
    TEST_ASSERT(extracted_consumer == "https://teams.live.com/meet/987654321", "Should extract consumer Teams from SafeLink");

    // Outlook SafeLink wrapping a non-Teams URL (e.g. google.com or company portal)
    std::string safelink_non_teams = "https://nam01.safelinks.protection.outlook.com/?url=https%3A%2F%2Fwww.google.com%2Fcalendar&data=foo";
    auto non_teams_target = TeamsExtractor::DecodeSafeLink(safelink_non_teams);
    TEST_ASSERT(non_teams_target.has_value() && *non_teams_target == "https://www.google.com/calendar", "Non-Teams SafeLink still decodes");
    std::string extracted_non_teams = TeamsExtractor::ExtractTeamsUrl(safelink_non_teams);
    TEST_ASSERT(extracted_non_teams.empty(), "ExtractTeamsUrl must not return non-Teams SafeLink target");

    return true;
}

bool TestHtmlDescriptionExtraction() {
    std::cout << "Running TestHtmlDescriptionExtraction..." << std::endl;

    // Standard HTML <a> tag
    std::string html1 = 
        "<div style=\"font-family: Arial;\">\n"
        "  <h2>Weekly Architecture Sync</h2>\n"
        "  <p>Click the link below to join:</p>\n"
        "  <a href=\"https://teams.microsoft.com/l/meetup-join/19%3ameeting_html01%40thread.v2/0?context=%7b%22Tid%22%3a%22corp%22%7d\" target=\"_blank\">Join Microsoft Teams Meeting</a>\n"
        "</div>";
    std::string extracted1 = TeamsExtractor::ExtractTeamsUrl(html1);
    TEST_ASSERT(extracted1 == "https://teams.microsoft.com/l/meetup-join/19%3ameeting_html01%40thread.v2/0?context=%7b%22Tid%22%3a%22corp%22%7d",
                "HTML anchor href should be extracted");

    // Single quotes and HTML entity &amp; in query parameters
    std::string html2 = 
        "<p>Meeting link: <a HREF='https://teams.microsoft.com/l/meetup-join/19%3ameeting_html02%40thread.v2/0?context=%7b%22Tid%22%3a%22corp%22%7d&amp;anon=true'>Join</a></p>";
    std::string extracted2 = TeamsExtractor::ExtractTeamsUrl(html2);
    TEST_ASSERT(extracted2 == "https://teams.microsoft.com/l/meetup-join/19%3ameeting_html02%40thread.v2/0?context=%7b%22Tid%22%3a%22corp%22%7d&anon=true",
                "HTML &amp; entity in href should be unescaped to &");

    // Outlook SafeLink inside an HTML anchor
    std::string html_safelink = 
        "<html><body><a href=\"https://nam02.safelinks.protection.outlook.com/?url=https%3A%2F%2Fteams.microsoft.com%2Fl%2Fmeetup-join%2F19%253ameeting_html03%2540thread.v2%2F0%3Fcontext%3D%257b%2522Tid%2522%253a%2522test%2522%257d&amp;data=05%7C02\">Join</a></body></html>";
    std::string extracted_sl = TeamsExtractor::ExtractTeamsUrl(html_safelink);
    TEST_ASSERT(extracted_sl == "https://teams.microsoft.com/l/meetup-join/19%3ameeting_html03%40thread.v2/0?context=%7b%22Tid%22%3a%22test%22%7d",
                "SafeLink inside HTML anchor should be decoded");

    return true;
}

bool TestUrlEncodingAndNonTeamsEvents() {
    std::cout << "Running TestUrlEncodingAndNonTeamsEvents..." << std::endl;

    // Complex query parameters and JSON context encoding
    std::string complex_url = 
        "https://teams.microsoft.com/l/meetup-join/19%3ameeting_NzQ5OWE4YTItYmExZS00%40thread.v2/0?"
        "context=%7b%22Tid%22%3a%2272f988bf-86f1-41af-91ab-2d7cd011db47%22%2c%22Oid%22%3a%22550e8400-e29b-41d4-a716-446655440000%22%7d";
    
    // Wrapped in prose with parenthesis and trailing period
    std::string prose = "Daily standup details can be found at (" + complex_url + "). Please join on time!";
    std::string extracted = TeamsExtractor::ExtractTeamsUrl(prose);
    TEST_ASSERT(extracted == complex_url, "ExtractTeamsUrl should strip surrounding parentheses and trailing period");

    // Non-Teams meetings: Zoom
    std::string zoom_text = "Join our Zoom meeting: https://zoom.us/j/91234567890?pwd=abc at 10:00";
    TEST_ASSERT(TeamsExtractor::ExtractTeamsUrl(zoom_text).empty(), "Zoom link should not be extracted as Teams");

    // Non-Teams meetings: Google Meet
    std::string gmeet_text = "Sync on Google Meet: https://meet.google.com/abc-defg-hij";
    TEST_ASSERT(TeamsExtractor::ExtractTeamsUrl(gmeet_text).empty(), "Google Meet should not be extracted as Teams");

    // Non-Teams meetings: Webex
    std::string webex_text = "Webex Room: https://mycompany.webex.com/meet/john";
    TEST_ASSERT(TeamsExtractor::ExtractTeamsUrl(webex_text).empty(), "Webex should not be extracted as Teams");

    // Plain physical appointment without links
    std::string physical_text = "Team Lunch at Main Cafeteria - Building 3, 2nd floor";
    TEST_ASSERT(TeamsExtractor::ExtractTeamsUrl(physical_text).empty(), "Physical event description should return empty");

    // Teams domain but non-meeting URL (e.g. blog or docs)
    std::string teams_docs = "Read documentation at https://teams.microsoft.com/about/features for instructions.";
    TEST_ASSERT(!TeamsExtractor::IsTeamsUrl(teams_docs), "Non-meeting Teams URL should not be recognized as Teams meeting");
    TEST_ASSERT(TeamsExtractor::ExtractTeamsUrl(teams_docs).empty(), "Non-meeting Teams URL should not be extracted");

    return true;
}

bool TestMeetingTemporalStatusTimedEvents() {
    std::cout << "Running TestMeetingTemporalStatusTimedEvents..." << std::endl;

    CalendarEvent event;
    event.uid = "timed-event-1";
    event.summary = "Sprint Planning";
    event.is_all_day = false;
    event.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{14, 0, 0}, false, ""};
    event.end_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{15, 0, 0}, false, ""};

    // 1. Live/Now: during meeting (14:30)
    CalendarDateTime now_live{CalendarDate{2026, 11, 4}, CalendarTime{14, 30, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(event, now_live) == MeetingTemporalStatus::Live,
                "Status during meeting must be Live");

    // Live at boundary: exact start (14:00:00)
    CalendarDateTime now_start{CalendarDate{2026, 11, 4}, CalendarTime{14, 0, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(event, now_start) == MeetingTemporalStatus::Live,
                "Status at exact start must be Live");

    // Live at boundary: exact end (15:00:00)
    CalendarDateTime now_end{CalendarDate{2026, 11, 4}, CalendarTime{15, 0, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(event, now_end) == MeetingTemporalStatus::Live,
                "Status at exact end must be Live");

    // 2. Soon: starting within 15 minutes (13:50 -> 10 minutes before)
    CalendarDateTime now_soon{CalendarDate{2026, 11, 4}, CalendarTime{13, 50, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(event, now_soon) == MeetingTemporalStatus::Soon,
                "Status 10 min before start must be Soon");

    // Soon boundary: exactly 15 minutes before (13:45:00)
    CalendarDateTime now_soon_bound{CalendarDate{2026, 11, 4}, CalendarTime{13, 45, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(event, now_soon_bound) == MeetingTemporalStatus::Soon,
                "Status exactly 15 min before start must be Soon");

    // 3. Future: more than 15 minutes before (13:44:59 -> 15 min and 1 sec)
    CalendarDateTime now_future_bound{CalendarDate{2026, 11, 4}, CalendarTime{13, 44, 59}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(event, now_future_bound) == MeetingTemporalStatus::Future,
                "Status 15m01s before start must be Future");

    CalendarDateTime now_future{CalendarDate{2026, 11, 4}, CalendarTime{10, 0, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(event, now_future) == MeetingTemporalStatus::Future,
                "Status 4 hours before start must be Future");

    // 4. Past: 1 second after end (15:00:01)
    CalendarDateTime now_past_bound{CalendarDate{2026, 11, 4}, CalendarTime{15, 0, 1}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(event, now_past_bound) == MeetingTemporalStatus::Past,
                "Status 1 second after end must be Past");

    CalendarDateTime now_past{CalendarDate{2026, 11, 4}, CalendarTime{16, 30, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(event, now_past) == MeetingTemporalStatus::Past,
                "Status 1.5 hours after end must be Past");

    return true;
}

bool TestAllDayEventTemporalStatus() {
    std::cout << "Running TestAllDayEventTemporalStatus..." << std::endl;

    // Single-day all-day event on Nov 4, 2026
    CalendarEvent all_day_single;
    all_day_single.uid = "all-day-1";
    all_day_single.summary = "Company Holiday";
    all_day_single.is_all_day = true;
    all_day_single.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{0, 0, 0}, false, ""};
    all_day_single.end_time = CalendarDateTime{CalendarDate{2026, 11, 5}, CalendarTime{0, 0, 0}, false, ""};

    // Midday of event -> Live
    CalendarDateTime now_midday{CalendarDate{2026, 11, 4}, CalendarTime{12, 0, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(all_day_single, now_midday) == MeetingTemporalStatus::Live,
                "All-day event during the day should be Live");

    // Midnight start boundary -> Live
    CalendarDateTime now_midnight_start{CalendarDate{2026, 11, 4}, CalendarTime{0, 0, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(all_day_single, now_midnight_start) == MeetingTemporalStatus::Live,
                "All-day event at 00:00:00 should be Live");

    // 23:59:59 end boundary -> Live
    CalendarDateTime now_end_boundary{CalendarDate{2026, 11, 4}, CalendarTime{23, 59, 59}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(all_day_single, now_end_boundary) == MeetingTemporalStatus::Live,
                "All-day event at 23:59:59 should be Live");

    // 10 minutes before start (Nov 3 23:50:00) -> Soon
    CalendarDateTime now_soon{CalendarDate{2026, 11, 3}, CalendarTime{23, 50, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(all_day_single, now_soon) == MeetingTemporalStatus::Soon,
                "All-day event 10 minutes before start should be Soon");

    // Exactly 15 minutes before start (Nov 3 23:45:00) -> Soon
    CalendarDateTime now_soon_15m{CalendarDate{2026, 11, 3}, CalendarTime{23, 45, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(all_day_single, now_soon_15m) == MeetingTemporalStatus::Soon,
                "All-day event exactly 15 minutes before start should be Soon");

    // 16 minutes before start (Nov 3 23:44:00) -> Future
    CalendarDateTime now_future_16m{CalendarDate{2026, 11, 3}, CalendarTime{23, 44, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(all_day_single, now_future_16m) == MeetingTemporalStatus::Future,
                "All-day event 16 minutes before start should be Future");

    // Next day midnight (Nov 5 00:00:00) -> Past
    CalendarDateTime now_next_day{CalendarDate{2026, 11, 5}, CalendarTime{0, 0, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(all_day_single, now_next_day) == MeetingTemporalStatus::Past,
                "All-day event on following day should be Past");

    // Multiday all-day event: Nov 4 to Nov 7 (DTEND is Nov 7 00:00:00)
    CalendarEvent multiday;
    multiday.uid = "multiday-1";
    multiday.summary = "Hackathon";
    multiday.is_all_day = true;
    multiday.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{0, 0, 0}, false, ""};
    multiday.end_time = CalendarDateTime{CalendarDate{2026, 11, 7}, CalendarTime{0, 0, 0}, false, ""};

    // Nov 5 (middle day) -> Live
    CalendarDateTime now_middle{CalendarDate{2026, 11, 5}, CalendarTime{14, 0, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(multiday, now_middle) == MeetingTemporalStatus::Live,
                "Multiday event on middle day should be Live");

    // Nov 6 at 23:59:59 (last second of hackathon) -> Live
    CalendarDateTime now_last_sec{CalendarDate{2026, 11, 6}, CalendarTime{23, 59, 59}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(multiday, now_last_sec) == MeetingTemporalStatus::Live,
                "Multiday event at last second should be Live");

    // Nov 7 at 00:00:00 (ended) -> Past
    CalendarDateTime now_hackathon_ended{CalendarDate{2026, 11, 7}, CalendarTime{0, 0, 0}, false, ""};
    TEST_ASSERT(MeetingStatusCalculator::CalculateStatus(multiday, now_hackathon_ended) == MeetingTemporalStatus::Past,
                "Multiday event after end date should be Past");

    return true;
}

bool TestCalendarEventEnrichmentAndIcsIntegration() {
    std::cout << "Running TestCalendarEventEnrichmentAndIcsIntegration..." << std::endl;

    // 1. Direct event enrichment
    CalendarEvent single_ev;
    single_ev.uid = "enrich-1";
    single_ev.summary = "Quarterly Business Review";
    single_ev.description = 
        "Please join Microsoft Teams Meeting:\r\n"
        "https://teams.microsoft.com/l/meetup-join/19%3ameeting_qbr%40thread.v2/0?context=%7b%22Tid%22%3a%22qbr-id%22%7d";
    single_ev.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{10, 0, 0}, false, ""};
    single_ev.end_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{11, 0, 0}, false, ""};

    CalendarDateTime ref_live{CalendarDate{2026, 11, 4}, CalendarTime{10, 20, 0}, false, ""};
    MeetingStatusCalculator::EnrichEvent(single_ev, ref_live);

    TEST_ASSERT(single_ev.is_teams_meeting, "Direct event should be identified as Teams meeting");
    TEST_ASSERT(single_ev.teams_join_url == "https://teams.microsoft.com/l/meetup-join/19%3ameeting_qbr%40thread.v2/0?context=%7b%22Tid%22%3a%22qbr-id%22%7d",
                "teams_join_url should be set");
    TEST_ASSERT(single_ev.teams_url == single_ev.teams_join_url, "teams_url should match teams_join_url");
    TEST_ASSERT(single_ev.temporal_status == MeetingTemporalStatus::Live, "Status should be Live at 10:20");
    TEST_ASSERT(to_string(single_ev.temporal_status) == "Live", "to_string(Live) should be 'Live'");

    // 2. Direct event enrichment with non-Teams appointment
    CalendarEvent non_teams_ev;
    non_teams_ev.uid = "enrich-2";
    non_teams_ev.summary = "Doctor Checkup";
    non_teams_ev.location = "Clinic Room 4";
    non_teams_ev.description = "Routine health checkup. Please arrive 15 minutes early.";
    non_teams_ev.start_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{14, 0, 0}, false, ""};
    non_teams_ev.end_time = CalendarDateTime{CalendarDate{2026, 11, 4}, CalendarTime{15, 0, 0}, false, ""};

    MeetingStatusCalculator::EnrichEvent(non_teams_ev, ref_live);
    TEST_ASSERT(!non_teams_ev.is_teams_meeting, "Doctor checkup must not be identified as Teams meeting");
    TEST_ASSERT(non_teams_ev.teams_join_url.empty(), "teams_join_url must be empty");
    TEST_ASSERT(non_teams_ev.teams_url.empty(), "teams_url must be empty");
    TEST_ASSERT(non_teams_ev.temporal_status == MeetingTemporalStatus::Future, "Doctor checkup at 14:00 is Future relative to 10:20");

    // 3. Full parsed ICS feed integration
    std::string feed = 
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//Microsoft Corporation//Outlook 16.0 MIMEDIR//EN\r\n"
        // Event A: Standup with standard Teams URL in DESCRIPTION (Past relative to ref)
        "BEGIN:VEVENT\r\n"
        "UID:feed-event-a\r\n"
        "SUMMARY:Morning Standup\r\n"
        "DTSTART:20261104T090000Z\r\n"
        "DTEND:20261104T100000Z\r\n"
        "DESCRIPTION:Join Teams: https://teams.microsoft.com/l/meetup-join/19%3ameeting_a%40thread.v2/0?context=%7b%22Tid%22%3a%22a%22%7d\r\n"
        "END:VEVENT\r\n"
        // Event B: Hybrid Planning with SafeLink in LOCATION (Live relative to ref 10:05)
        "BEGIN:VEVENT\r\n"
        "UID:feed-event-b\r\n"
        "SUMMARY:Hybrid Sprint Planning\r\n"
        "DTSTART:20261104T100000Z\r\n"
        "DTEND:20261104T110000Z\r\n"
        "LOCATION:Room 101 / https://nam01.safelinks.protection.outlook.com/?url=https%3A%2F%2Fgov.teams.microsoft.us%2Fl%2Fmeetup-join%2F19%3ameeting_b%40thread.v2%2F0&data=01\r\n"
        "END:VEVENT\r\n"
        // Event C: 1:1 Sync with Teams consumer link in URL property (Soon relative to ref 10:05, starts at 10:15)
        "BEGIN:VEVENT\r\n"
        "UID:feed-event-c\r\n"
        "SUMMARY:1:1 Coffee Chat\r\n"
        "DTSTART:20261104T101500Z\r\n"
        "DTEND:20261104T110000Z\r\n"
        "URL:https://teams.live.com/meet/777888999\r\n"
        "END:VEVENT\r\n"
        // Event D: Non-Teams dentist appointment (Future relative to ref 10:05)
        "BEGIN:VEVENT\r\n"
        "UID:feed-event-d\r\n"
        "SUMMARY:Dentist Cleaning\r\n"
        "DTSTART:20261104T140000Z\r\n"
        "DTEND:20261104T150000Z\r\n"
        "LOCATION:Downtown Dental Clinic\r\n"
        "END:VEVENT\r\n"
        // Event E: External Zoom webinar (Future relative to ref 10:05)
        "BEGIN:VEVENT\r\n"
        "UID:feed-event-e\r\n"
        "SUMMARY:Cloud Webinar\r\n"
        "DTSTART:20261104T160000Z\r\n"
        "DTEND:20261104T170000Z\r\n"
        "DESCRIPTION:Join zoom: https://zoom.us/j/99887766\r\n"
        "END:VEVENT\r\n"
        // Event F: All-day company innovation day (Live relative to ref 10:05 on Nov 4)
        "BEGIN:VEVENT\r\n"
        "UID:feed-event-f\r\n"
        "SUMMARY:Innovation Day\r\n"
        "DTSTART;VALUE=DATE:20261104\r\n"
        "DTEND;VALUE=DATE:20261105\r\n"
        "DESCRIPTION:All day hackathon. Virtual lobby: https://teams.microsoft.com/l/meetup-join/19%3ameeting_hack%40thread.v2/0?context=%7b%7d\r\n"
        "END:VEVENT\r\n"
        "END:VCALENDAR\r\n";

    IcsParser parser(0);
    auto events = parser.Parse(feed);
    TEST_ASSERT(events.size() == 6, "Must parse 6 events from feed");

    // Verify detection during initial parse
    TEST_ASSERT(events[0].is_teams_meeting, "Event A is Teams meeting");
    TEST_ASSERT(events[0].teams_join_url == "https://teams.microsoft.com/l/meetup-join/19%3ameeting_a%40thread.v2/0?context=%7b%22Tid%22%3a%22a%22%7d", "Event A URL");

    TEST_ASSERT(events[1].is_teams_meeting, "Event B is Teams meeting");
    TEST_ASSERT(events[1].teams_join_url == "https://gov.teams.microsoft.us/l/meetup-join/19:meeting_b@thread.v2/0", "Event B decoded SafeLink URL");

    TEST_ASSERT(events[2].is_teams_meeting, "Event C is Teams meeting");
    TEST_ASSERT(events[2].teams_join_url == "https://teams.live.com/meet/777888999", "Event C consumer URL");

    TEST_ASSERT(!events[3].is_teams_meeting, "Event D is not Teams");
    TEST_ASSERT(events[3].teams_join_url.empty(), "Event D has empty teams_join_url");

    TEST_ASSERT(!events[4].is_teams_meeting, "Event E is not Teams");
    TEST_ASSERT(events[4].teams_join_url.empty(), "Event E has empty teams_join_url");

    TEST_ASSERT(events[5].is_teams_meeting, "Event F is Teams meeting");
    TEST_ASSERT(events[5].is_all_day, "Event F is all-day");

    // Enrich all events relative to 2026-11-04 10:05:00 UTC
    CalendarDateTime ref_now{CalendarDate{2026, 11, 4}, CalendarTime{10, 5, 0}, true, ""};
    MeetingStatusCalculator::EnrichEvents(events, ref_now);

    TEST_ASSERT(events[0].temporal_status == MeetingTemporalStatus::Past, "Event A (09:00-10:00) is Past at 10:05");
    TEST_ASSERT(events[1].temporal_status == MeetingTemporalStatus::Live, "Event B (10:00-11:00) is Live at 10:05");
    TEST_ASSERT(events[2].temporal_status == MeetingTemporalStatus::Soon, "Event C (10:15-11:00) is Soon (starts in 10m)");
    TEST_ASSERT(events[3].temporal_status == MeetingTemporalStatus::Future, "Event D (14:00-15:00) is Future");
    TEST_ASSERT(events[4].temporal_status == MeetingTemporalStatus::Future, "Event E (16:00-17:00) is Future");
    TEST_ASSERT(events[5].temporal_status == MeetingTemporalStatus::Live, "Event F (All day Nov 4) is Live at 10:05");

    return true;
}

} // namespace

int RunAllTeamsTests() {
    std::cout << "=========================================" << std::endl;
    std::cout << "Starting Teams Extractor & Status Tests  " << std::endl;
    std::cout << "=========================================" << std::endl;

    if (!TestStandardAndGovTeamsUrls()) return 1;
    if (!TestSafeLinksDecoding()) return 2;
    if (!TestHtmlDescriptionExtraction()) return 3;
    if (!TestUrlEncodingAndNonTeamsEvents()) return 4;
    if (!TestMeetingTemporalStatusTimedEvents()) return 5;
    if (!TestAllDayEventTemporalStatus()) return 6;
    if (!TestCalendarEventEnrichmentAndIcsIntegration()) return 7;

    std::cout << "=========================================" << std::endl;
    std::cout << "ALL TEAMS TESTS PASSED SUCCESSFULLY!     " << std::endl;
    std::cout << "=========================================" << std::endl;
    return 0;
}

extern "C" __declspec(dllexport) int RunTestsDll() {
    return RunAllTeamsTests();
}

int main() {
    return RunAllTeamsTests();
}
