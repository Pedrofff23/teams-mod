#pragma once

#include "mod/calendar_cache.h"
#include "mod/mod_settings.h"
#include <string>
#include <string_view>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace calendar {

struct HttpResponse {
    int status_code{0};
    std::string body;
    std::string error_message;

    bool is_success() const { return status_code >= 200 && status_code < 300; }
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse Fetch(const std::wstring& url) = 0;
};

// Production HTTP transport using WinINet (InternetOpenW, InternetOpenUrlW, InternetReadFile)
class WinINetHttpTransport : public IHttpTransport {
public:
    WinINetHttpTransport();
    ~WinINetHttpTransport() override;

    HttpResponse Fetch(const std::wstring& url) override;
};

// Mock HTTP transport for unit and offline tests
class MockHttpTransport : public IHttpTransport {
public:
    MockHttpTransport() = default;

    void SetResponse(int status_code, std::string body, std::string error_message = "");

    HttpResponse Fetch(const std::wstring& url) override;

    int GetFetchCount() const;
    std::wstring GetLastRequestedUrl() const;

private:
    mutable std::mutex mutex_;
    int status_code_{200};
    std::string body_;
    std::string error_message_;
    int fetch_count_{0};
    std::wstring last_requested_url_;
};

class HttpFetcher {
public:
    explicit HttpFetcher(
        std::shared_ptr<CalendarCache> cache,
        std::shared_ptr<IHttpTransport> transport = nullptr
    );
    ~HttpFetcher();

    // Prevent copy/move
    HttpFetcher(const HttpFetcher&) = delete;
    HttpFetcher& operator=(const HttpFetcher&) = delete;

    // Start background polling worker thread with initial settings
    void Start(const ModSettings& settings);

    // Stop background worker thread cleanly
    void Stop();

    // Dynamically update settings (if URL changed or interval changed)
    void UpdateSettings(const ModSettings& settings);

    // Trigger an immediate fetch in the background thread
    void TriggerFetch();

    // Execute a synchronous fetch of the specified URL (useful for direct testing or explicit triggers)
    bool FetchOnce(const std::wstring& url);

    // Check if background worker thread is running
    bool IsRunning() const;

private:
    void WorkerLoop();

    std::shared_ptr<CalendarCache> cache_;
    std::shared_ptr<IHttpTransport> transport_;

    std::thread worker_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_requested_{false};
    bool refresh_requested_{false};
    bool is_running_{false};

    ModSettings current_settings_;
};

} // namespace calendar
