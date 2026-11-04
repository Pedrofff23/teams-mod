# 02: Microsoft Teams Meeting URL Extractor and Status Calculator

**What to build:** Enrichment of parsed calendar events with Microsoft Teams intelligence. The system automatically inspects event attributes (description, location, URL properties) to detect Microsoft Teams meeting links, extracts the direct join URL, and computes the dynamic status of the meeting relative to the current clock time (Past, Live/Now, Soon/Within 15 minutes, Future).

**Blocked by:** 01: Core RFC 5545 ICS Feed Parser and Agenda Data Model

**Status:** ready-for-agent

- [ ] Accurate detection and extraction of Microsoft Teams join URLs (matching `https://teams.microsoft.com/l/meetup-join/...` and related meeting domains)
- [ ] Safe handling of URL-encoded characters, HTML formatting, and plain-text descriptions in event bodies
- [ ] Classification of meeting temporal status relative to current system time: Live Now (ongoing), Soon (starting within 15 minutes), Future, and Past
- [ ] Clear tagging of whether an event is a Microsoft Teams meeting versus a general calendar appointment
- [ ] Automated tests validating detection against varied Microsoft Teams calendar payloads and edge cases

