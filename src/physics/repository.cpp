#include "physics/repository.h"
#include "physics/save_load.h"
#include "physics/paths.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef HAS_CURL
#include <curl/curl.h>
#include "cjson/cJSON.h"
#endif

namespace fs = std::filesystem;

// ── Static helpers ──────────────────────────────────────────────────────────

bool ParticleRepository::is_available() {
#ifdef HAS_CURL
    return true;
#else
    return false;
#endif
}

// ── Init / Destroy ──────────────────────────────────────────────────────────

void ParticleRepository::init() {
    cache_dir_       = get_data_dir() + "repository/";
    elements_cache_  = cache_dir_ + "elements/";
    molecules_cache_ = cache_dir_ + "molecules/";

    std::error_code ec;
    fs::create_directories(elements_cache_, ec);
    fs::create_directories(molecules_cache_, ec);

    // Load GitHub token if present
    std::string token_path = get_data_dir() + "github_token.txt";
    std::ifstream tf(token_path);
    if (tf.is_open()) {
        std::getline(tf, github_token_);
        // Trim whitespace
        while (!github_token_.empty() &&
               (github_token_.back() == '\n' || github_token_.back() == '\r' ||
                github_token_.back() == ' '))
            github_token_.pop_back();
    }

#ifdef HAS_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
}

void ParticleRepository::destroy() {
    join_worker();
#ifdef HAS_CURL
    curl_global_cleanup();
#endif
}

// ── Thread management ───────────────────────────────────────────────────────

void ParticleRepository::join_worker() {
    if (worker_.joinable())
        worker_.join();
}

// ── Thread-safe accessors ───────────────────────────────────────────────────

std::string ParticleRepository::status_message() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return status_msg_;
}

std::vector<RepoEntry> ParticleRepository::get_listing() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return listing_;
}

std::string ParticleRepository::last_downloaded_path() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return last_download_path_;
}

bool ParticleRepository::has_token() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return !github_token_.empty();
}

void ParticleRepository::set_token(const std::string& token) {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        github_token_ = token;
        // Trim
        while (!github_token_.empty() &&
               (github_token_.back() == '\n' || github_token_.back() == '\r' ||
                github_token_.back() == ' '))
            github_token_.pop_back();
    }
    // Save to file
    std::string path = get_data_dir() + "github_token.txt";
    std::ofstream f(path);
    if (f.is_open()) {
        f << token;
    }
}

void ParticleRepository::clear_token() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        github_token_.clear();
    }
    std::string path = get_data_dir() + "github_token.txt";
    std::error_code ec;
    fs::remove(path, ec);
}

void ParticleRepository::update_cache_flags() {
    // Must be called with mutex_ held
    for (auto& e : listing_) {
        const std::string& dir = (current_category_ == 0) ? elements_cache_ : molecules_cache_;
        std::string path = dir + e.name;
        e.is_cached = fs::exists(path);

        // Peek chirality from cached .ppmol v3+ files
        if (e.is_cached && current_category_ == 1) {
            e.chirality = static_cast<int8_t>(peek_ppmol_chirality(path));
        } else {
            e.chirality = -1;
        }
    }
}

// ── Base64 encoding ─────────────────────────────────────────────────────────

std::string ParticleRepository::base64_encode(const std::vector<uint8_t>& data) {
    static const char TABLE[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    for (size_t i = 0; i < data.size(); i += 3) {
        uint32_t a = data[i];
        uint32_t b = (i + 1 < data.size()) ? data[i + 1] : 0;
        uint32_t c = (i + 2 < data.size()) ? data[i + 2] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out += TABLE[(triple >> 18) & 0x3F];
        out += TABLE[(triple >> 12) & 0x3F];
        out += (i + 1 < data.size()) ? TABLE[(triple >> 6) & 0x3F] : '=';
        out += (i + 2 < data.size()) ? TABLE[triple & 0x3F] : '=';
    }
    return out;
}

// ── libcurl callbacks ───────────────────────────────────────────────────────

#ifdef HAS_CURL

static size_t curl_write_string_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::string*>(userdata);
    buf->append(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

static size_t curl_write_file_cb(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* f = static_cast<std::ofstream*>(userdata);
    f->write(static_cast<char*>(ptr), static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

static int curl_progress_cb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* progress = static_cast<std::atomic<float>*>(clientp);
    if (dltotal > 0)
        progress->store(static_cast<float>(dlnow) / static_cast<float>(dltotal));
    return 0;
}

#endif // HAS_CURL

// ── Public async API ────────────────────────────────────────────────────────

void ParticleRepository::fetch_listing(int category) {
#ifdef HAS_CURL
    join_worker();
    status_.store(RepoStatus::Loading);
    progress_.store(0.0f);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Fetching file list...";
    }
    worker_ = std::thread(&ParticleRepository::worker_fetch_listing, this, category);
#else
    (void)category;
    status_.store(RepoStatus::Error);
    std::lock_guard<std::mutex> lk(mutex_);
    status_msg_ = "Online features not available (built without libcurl)";
#endif
}

void ParticleRepository::download_file(int index) {
#ifdef HAS_CURL
    std::string url, local_path;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (index < 0 || index >= static_cast<int>(listing_.size())) return;
        const auto& entry = listing_[index];
        url = entry.download_url;
        const std::string& dir = (current_category_ == 0) ? elements_cache_ : molecules_cache_;
        local_path = dir + entry.name;
    }
    if (url.empty()) return;

    join_worker();
    status_.store(RepoStatus::Downloading);
    progress_.store(0.0f);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Downloading...";
    }
    worker_ = std::thread(&ParticleRepository::worker_download, this,
                          std::move(url), std::move(local_path));
#else
    (void)index;
#endif
}

void ParticleRepository::upload_file(const std::string& local_path, int category) {
#ifdef HAS_CURL
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (github_token_.empty()) {
            status_msg_ = "GitHub token required for upload";
            status_.store(RepoStatus::Error);
            return;
        }
    }

    // Extract filename from path
    std::string remote_name = fs::path(local_path).filename().string();

    join_worker();
    status_.store(RepoStatus::Uploading);
    progress_.store(0.0f);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Uploading...";
    }
    worker_ = std::thread(&ParticleRepository::worker_upload, this,
                          local_path, std::move(remote_name), category);
#else
    (void)local_path;
    (void)category;
#endif
}

// ── Worker threads ──────────────────────────────────────────────────────────

void ParticleRepository::worker_fetch_listing(int category) {
#ifdef HAS_CURL
    const char* subdir = (category == 0) ? "elements" : "molecules";
    const char* ext = (category == 0) ? ".ppel" : ".ppmol";

    char url[512];
    snprintf(url, sizeof(url),
        "https://api.github.com/repos/%s/%s/contents/%s",
        REPO_OWNER, REPO_NAME, subdir);

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Failed to initialize HTTP client";
        status_.store(RepoStatus::Error);
        return;
    }

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "User-Agent: ParticlePlayground");

    std::string token;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        token = github_token_;
    }
    std::string auth_header;
    if (!token.empty()) {
        auth_header = "Authorization: Bearer " + token;
        headers = curl_slist_append(headers, auth_header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = std::string("Network error: ") + curl_easy_strerror(res);
        status_.store(RepoStatus::Error);
        return;
    }

    if (http_code == 404) {
        // Empty repository or directory not created yet
        std::lock_guard<std::mutex> lk(mutex_);
        listing_.clear();
        current_category_ = category;
        status_msg_ = "No files available yet";
        status_.store(RepoStatus::Success);
        return;
    }

    if (http_code != 200) {
        std::lock_guard<std::mutex> lk(mutex_);
        char msg[128];
        snprintf(msg, sizeof(msg), "GitHub API error (HTTP %ld)", http_code);
        status_msg_ = msg;
        status_.store(RepoStatus::Error);
        return;
    }

    // Parse JSON array
    cJSON* root = cJSON_Parse(response.c_str());
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Invalid API response";
        status_.store(RepoStatus::Error);
        return;
    }

    std::vector<RepoEntry> entries;
    size_t ext_len = strlen(ext);
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        cJSON* j_name = cJSON_GetObjectItem(item, "name");
        cJSON* j_url  = cJSON_GetObjectItem(item, "download_url");
        cJSON* j_sha  = cJSON_GetObjectItem(item, "sha");
        cJSON* j_size = cJSON_GetObjectItem(item, "size");
        cJSON* j_type = cJSON_GetObjectItem(item, "type");

        if (!j_name || !cJSON_IsString(j_name)) continue;
        if (!j_type || strcmp(j_type->valuestring, "file") != 0) continue;

        std::string name = j_name->valuestring;
        // Filter by extension
        if (name.size() < ext_len ||
            name.substr(name.size() - ext_len) != ext) continue;

        RepoEntry e;
        e.name = name;
        e.download_url = (j_url && cJSON_IsString(j_url)) ? j_url->valuestring : "";
        e.sha  = (j_sha && cJSON_IsString(j_sha)) ? j_sha->valuestring : "";
        e.size = (j_size && cJSON_IsNumber(j_size)) ?
                 static_cast<uint64_t>(j_size->valuedouble) : 0;

        // Check if cached locally
        const std::string& dir = (category == 0) ? elements_cache_ : molecules_cache_;
        e.is_cached = fs::exists(dir + name);

        entries.push_back(std::move(e));
    }
    cJSON_Delete(root);

    // Sort alphabetically
    std::sort(entries.begin(), entries.end(),
        [](const RepoEntry& a, const RepoEntry& b) { return a.name < b.name; });

    {
        std::lock_guard<std::mutex> lk(mutex_);
        listing_ = std::move(entries);
        current_category_ = category;
        char msg[64];
        snprintf(msg, sizeof(msg), "%zu files available",listing_.size());
        status_msg_ = msg;
    }
    status_.store(RepoStatus::Success);
#else
    (void)category;
    status_.store(RepoStatus::Error);
#endif
}

void ParticleRepository::worker_download(std::string url, std::string local_path) {
#ifdef HAS_CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Failed to initialize HTTP client";
        status_.store(RepoStatus::Error);
        return;
    }

    std::ofstream file(local_path, std::ios::binary);
    if (!file.is_open()) {
        curl_easy_cleanup(curl);
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Failed to create local file";
        status_.store(RepoStatus::Error);
        return;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: ParticlePlayground");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_file_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    file.close();

    if (res != CURLE_OK) {
        // Remove partial download
        std::error_code ec;
        fs::remove(local_path, ec);
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = std::string("Download failed: ") + curl_easy_strerror(res);
        status_.store(RepoStatus::Error);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        last_download_path_ = local_path;
        status_msg_ = "Download complete";
        update_cache_flags();
    }
    progress_.store(1.0f);
    status_.store(RepoStatus::Success);
#else
    (void)url;
    (void)local_path;
    status_.store(RepoStatus::Error);
#endif
}

void ParticleRepository::worker_upload(std::string local_path, std::string remote_name, int category) {
#ifdef HAS_CURL
    // Read file into memory
    std::ifstream f(local_path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Failed to read local file";
        status_.store(RepoStatus::Error);
        return;
    }
    auto file_size = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(file_size));
    f.read(reinterpret_cast<char*>(data.data()), file_size);
    f.close();

    std::string content_b64 = base64_encode(data);
    const char* subdir = (category == 0) ? "elements" : "molecules";

    // Check listing for existing SHA (to update rather than create)
    std::string existing_sha;
    std::string token;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        token = github_token_;
        for (const auto& e : listing_) {
            if (e.name == remote_name) {
                existing_sha = e.sha;
                break;
            }
        }
    }

    // Build JSON payload
    cJSON* payload = cJSON_CreateObject();
    std::string commit_msg = "Add " + remote_name + " via Particle Playground";
    cJSON_AddStringToObject(payload, "message", commit_msg.c_str());
    cJSON_AddStringToObject(payload, "content", content_b64.c_str());
    if (!existing_sha.empty())
        cJSON_AddStringToObject(payload, "sha", existing_sha.c_str());
    char* json_str = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);

    char url[512];
    snprintf(url, sizeof(url),
        "https://api.github.com/repos/%s/%s/contents/%s/%s",
        REPO_OWNER, REPO_NAME, subdir, remote_name.c_str());

    CURL* curl = curl_easy_init();
    if (!curl) {
        cJSON_free(json_str);
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Failed to initialize HTTP client";
        status_.store(RepoStatus::Error);
        return;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github.v3+json");
    headers = curl_slist_append(headers, "User-Agent: ParticlePlayground");
    std::string auth = "Authorization: Bearer " + token;
    headers = curl_slist_append(headers, auth.c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    cJSON_free(json_str);

    if (res != CURLE_OK) {
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = std::string("Upload failed: ") + curl_easy_strerror(res);
        status_.store(RepoStatus::Error);
        return;
    }

    if (http_code != 200 && http_code != 201) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (http_code == 401 || http_code == 403)
            status_msg_ = "Authentication failed — check your token";
        else if (http_code == 422)
            status_msg_ = "File already exists (refresh listing first)";
        else {
            char msg[128];
            snprintf(msg, sizeof(msg), "Upload failed (HTTP %ld)", http_code);
            status_msg_ = msg;
        }
        status_.store(RepoStatus::Error);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        status_msg_ = "Upload complete: " + remote_name;
    }
    status_.store(RepoStatus::Success);
#else
    (void)local_path;
    (void)remote_name;
    (void)category;
    status_.store(RepoStatus::Error);
#endif
}
