#include "mod/http_fetcher.h"
#include "calendar_engine/ics_parser.h"
#include "calendar_engine/meeting_status.h"
#include <windows.h>
#include <wininet.h>
#include <ctime>
#include <algorithm>

namespace calendar {

// --- WinINetHttpTransport Implementation ---

WinINetHttpTransport::WinINetHttpTransport() = default;

WinINetHttpTransport::~WinINetHttpTransport() = default;

HttpResponse WinINetHttpTransport::Fetch(const std::wstring& url) {
    if (url.empty()) {
        return HttpResponse{0, "", "Empty URL specified"};
    }

    HINTERNET hInternet = InternetOpenW(
        L"TeamsCalendarFlyout/1.0",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr,
        nullptr,
        0
    );
    if (!hInternet) {
        DWORD err = GetLastError();
        return HttpResponse{0, "", "InternetOpenW failed with code " + std::to_string(err)};
    }

    DWORD timeout_connect_ms = 10000;
    DWORD timeout_receive_ms = 15000;
    InternetSetOptionW(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout_connect_ms, sizeof(timeout_connect_ms));
    InternetSetOptionW(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout_receive_ms, sizeof(timeout_receive_ms));

    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE;
    if (url.rfind(L"https://", 0) == 0 || url.rfind(L"HTTPS://", 0) == 0) {
        flags |= INTERNET_FLAG_SECURE;
    }

    HINTERNET hUrl = InternetOpenUrlW(hInternet, url.c_str(), nullptr, 0, flags, 0);
    if (!hUrl) {
        DWORD err = GetLastError();
        InternetCloseHandle(hInternet);
        return HttpResponse{0, "", "InternetOpenUrlW failed with code " + std::to_string(err)};
    }

    DWORD status_code = 0;
    DWORD status_code_size = sizeof(status_code);
    DWORD index = 0;
    if (!HttpQueryInfoW(hUrl, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status_code, &status_code_size, &index)) {
        status_code = 200; // Fallback if server / source omitted HTTP response header
    }

    std::string body;
    char buffer[8192];
    DWORD bytes_read = 0;
    while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytes_read) && bytes_read > 0) {
        body.append(buffer, bytes_read);
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    if (status_code < 200 || status_code >= 300) {
        return HttpResponse{
            static_cast<int>(status_code),
            body,
            "HTTP error " + std::to_string(status_code)
        };
    }

    return HttpResponse{static_cast<int>(status_code), body, ""};
}

// --- MockHttpTransport Implementation ---

void MockHttpTransport::SetResponse(int status_code, std::string body, std::string error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_code_ = status_code;
    body_ = std::move(body);
    error_message_ = std::move(error_message);
}

HttpResponse MockHttpTransport::Fetch(const std::wstring& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    fetch_count_++;
    last_requested_url_ = url;
    return HttpResponse{status_code_, body_, error_message_};
}

int MockHttpTransport::GetFetchCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fetch_count_;
}

std::wstring MockHttpTransport::GetLastRequestedUrl() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_requested_url_;
}

// --- HttpFetcher Implementation ---

HttpFetcher::HttpFetcher(
    std::shared_ptr<CalendarCache> cache,
    std::shared_ptr<IHttpTransport> transport
) : cache_(std::move(cache)), transport_(std::move(transport)) {
    if (!transport_) {
        transport_ = std::make_shared<WinINetHttpTransport>();
    }
}

HttpFetcher::~HttpFetcher() {
    Stop();
}

void HttpFetcher::Start(const ModSettings& settings) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (is_running_) return;
        stop_requested_ = false;
        refresh_requested_ = false;
        current_settings_ = settings;
        is_running_ = true;
    }
    worker_thread_ = std::thread(&HttpFetcher::WorkerLoop, this);
}

void HttpFetcher::Stop() {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!is_running_) return;
        stop_requested_ = true;
        is_running_ = false;
        cv_.notify_all();
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void HttpFetcher::UpdateSettings(const ModSettings& settings) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool url_changed = (current_settings_.feed_url != settings.feed_url);
    current_settings_ = settings;
    if (url_changed) {
        refresh_requested_ = true;
        cv_.notify_all();
    }
}

void HttpFetcher::TriggerFetch() {
    std::unique_lock<std::mutex> lock(mutex_);
    refresh_requested_ = true;
    cv_.notify_all();
}

bool HttpFetcher::FetchOnce(const std::wstring& url) {
    if (url.empty()) {
        cache_->SetFetchStatus(FetchStatus::Idle);
        return false;
    }

    cache_->SetFetchStatus(FetchStatus::Fetching);
    auto response = transport_->Fetch(url);

    if (!response.is_success()) {
        std::string err = response.error_message.empty()
            ? ("HTTP " + std::to_string(response.status_code))
            : response.error_message;
        cache_->SetFetchStatus(FetchStatus::Failed, err);
        return false;
    }

    try {
        IcsParser parser;
        auto events = parser.Parse(response.body);

        // Enrich events with Teams meeting links and temporal status relative to system clock
        auto now_unix = static_cast<int64_t>(std::time(nullptr));
        auto ref_now = CalendarDateTime::from_unix_timestamp(now_unix);
        MeetingStatusCalculator::EnrichEvents(events, ref_now);

        cache_->UpdateEvents(std::move(events));
        return true;
    } catch (const std::exception& e) {
        cache_->SetFetchStatus(FetchStatus::Failed, std::string("Parser error: ") + e.what());
        return false;
    }
}

bool HttpFetcher::IsRunning() const {
    return is_running_;
}

void HttpFetcher::WorkerLoop() {
    while (true) {
        std::wstring url_to_fetch;
        int interval_minutes = 15;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stop_requested_) break;
            url_to_fetch = current_settings_.feed_url;
            interval_minutes = current_settings_.refresh_interval_minutes;
            refresh_requested_ = false;
        }

        if (!url_to_fetch.empty()) {
            FetchOnce(url_to_fetch);
        }

        // Wait for next scheduled refresh or explicit trigger/stop
        std::unique_lock<std::mutex> lock(mutex_);
        auto wait_duration = std::chrono::minutes(std::max(1, interval_minutes));
        cv_.wait_for(lock, wait_duration, [this] {
            return stop_requested_ || refresh_requested_;
        });

        if (stop_requested_) break;
    }
}

} // namespace calendar
