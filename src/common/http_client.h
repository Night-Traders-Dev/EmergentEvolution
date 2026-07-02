#pragma once
// Platform-agnostic HTTP client for ParticleRepository.
// Uses libcurl on Linux, WinHTTP on Windows.
// When neither is available, all calls return errors.

#include <string>
#include <vector>
#include <atomic>

struct HttpResponse {
    long        status_code = 0;
    std::string body;
    std::string error_msg;          // empty on success
};

bool         http_is_available();
void         http_global_init();
void         http_global_cleanup();

// GET → response body as string
HttpResponse http_get(const std::string& url,
                      const std::vector<std::string>& headers = {},
                      long timeout_sec = 15);

// GET → save to file, with optional progress (0.0–1.0)
HttpResponse http_get_file(const std::string& url,
                           const std::string& output_path,
                           const std::vector<std::string>& headers = {},
                           std::atomic<float>* progress = nullptr,
                           long timeout_sec = 30);

// PUT with body string
HttpResponse http_put(const std::string& url,
                      const std::string& body,
                      const std::vector<std::string>& headers = {},
                      long timeout_sec = 60);
