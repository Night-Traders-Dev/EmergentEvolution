#pragma once
// ParticleRepository: browse, download, upload .ppel/.ppmol files
// via GitHub API. All HTTP is async (background thread).
// When HAS_CURL is not defined, all functions are no-ops.

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstdint>

// Entry in the remote repository listing
struct RepoEntry {
    std::string name;           // "H-1.ppel"
    std::string download_url;   // raw.githubusercontent.com URL
    std::string sha;            // git blob SHA (needed for upload/update)
    uint64_t    size = 0;       // file size in bytes
    bool        is_cached = false; // true if local copy exists
    int8_t      chirality = -1; // -1=unknown, 0=achiral, 1=chiral (molecules only, from v3+ cache)
};

// Async operation status
enum class RepoStatus {
    Idle,
    Loading,        // fetching directory listing
    Downloading,    // downloading a file
    Uploading,      // uploading a file
    Success,
    Error
};

class ParticleRepository {
public:
    // Initialize (creates cache dirs, loads token if present)
    void init();

    // Start async fetch of directory listing
    // category: 0 = elements, 1 = molecules
    void fetch_listing(int category);

    // Start async download of a file by index in current listing
    void download_file(int index);

    // Start async upload of a local file to the repository
    void upload_file(const std::string& local_path, int category);

    // Check if curl is available (HAS_CURL defined)
    static bool is_available();

    // --- Thread-safe accessors (called from UI thread) ---

    RepoStatus status() const { return status_.load(); }
    std::string status_message() const;
    float       progress() const { return progress_.load(); }

    // Get current listing (lock-protected copy)
    std::vector<RepoEntry> get_listing() const;

    // Get path to most recently downloaded file
    std::string last_downloaded_path() const;

    // Token management
    bool has_token() const;
    void set_token(const std::string& token);
    void clear_token();

    // Cleanup
    void destroy();

private:
    static constexpr const char* REPO_OWNER = "Night-Traders-Dev";
    static constexpr const char* REPO_NAME  = "ParticleRepository";

    mutable std::mutex mutex_;
    std::thread        worker_;
    std::atomic<RepoStatus> status_{RepoStatus::Idle};
    std::atomic<float>      progress_{0.0f};

    std::string status_msg_;
    std::string last_download_path_;
    std::string github_token_;
    std::string cache_dir_;           // <data_dir>/repository/
    std::string elements_cache_;      // <data_dir>/repository/elements/
    std::string molecules_cache_;     // <data_dir>/repository/molecules/

    std::vector<RepoEntry> listing_;
    int current_category_ = 0;        // 0=elements, 1=molecules

    void join_worker();
    void worker_fetch_listing(int category);
    void worker_download(std::string url, std::string local_path);
    void worker_upload(std::string local_path, std::string remote_name, int category);
    void update_cache_flags();

    static std::string base64_encode(const std::vector<uint8_t>& data);
};
