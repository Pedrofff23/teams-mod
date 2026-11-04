# 01: Core RFC 5545 ICS Feed Parser and Agenda Data Model

**What to build:** A standalone calendar parser that ingests standard iCalendar (RFC 5545) feeds, parses meeting events (title, start/end timestamps, location, raw description), resolves timezone differences to local time, and provides a chronological query interface for events on any selected day. Tested via a dedicated test harness without requiring Windows Shell or GUI dependencies.

**Blocked by:** None (can start immediately)

**Status:** ready-for-agent

- [ ] Parsing standard VEVENT components from RFC 5545 string/stream inputs
- [ ] Proper extraction of SUMMARY, DTSTART, DTEND, LOCATION, and DESCRIPTION fields
- [ ] Timezone resolution and local datetime normalization for date filtering
- [ ] Chronological sorting of meetings for a target calendar date
- [ ] Headless test suite verifying parsing against diverse real-world sample calendar feeds

