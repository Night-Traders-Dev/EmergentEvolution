#include "physics/interface.h"
#include "physics/paths.h"
#include "physics/repository.h"
#include "physics/ui_data.h"
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// Format file size as human-readable string
static std::string format_size(uint64_t bytes) {
    if (bytes >= 1024 * 1024)
        return std::to_string(bytes / (1024 * 1024)) + "." +
               std::to_string((bytes % (1024 * 1024)) * 10 / (1024 * 1024)) + " MB";
    if (bytes >= 1024)
        return std::to_string(bytes / 1024) + "." +
               std::to_string((bytes % 1024) * 10 / 1024) + " KB";
    return std::to_string(bytes) + " B";
}

void PhysicsInterface::draw_repository() {
    if (!show_repository || !repository_ptr) return;

    const auto& tc = get_theme(prefs.theme);

    ImVec2 win_size(640, 560);
    if (retile_windows_) {
        ImGui::SetNextWindowPos(tile_pos_[TW_REPOSITORY]);
        ImGui::SetNextWindowSize(tile_size_[TW_REPOSITORY]);
    } else {
        ImGui::SetNextWindowPos(find_free_window_pos(win_size), ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(win_size, ImGuiCond_Appearing);
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(500, 400), ImVec2(800, 700));

    bool open = true;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, tc.bg);
    bool repo_vis = ImGui::Begin("Particle Repository###RepoDialog", &open,
            ImGuiWindowFlags_NoCollapse);
    record_window_rect(TW_REPOSITORY);
    if (repo_vis) {
        draw_minimize_button(TW_REPOSITORY);

        // ── Tab bar ──────────────────────────────────────────────────────
        bool tab_changed = false;
        if (ImGui::BeginTabBar("##RepoTabs")) {
            if (ImGui::BeginTabItem("Elements")) {
                if (repo_current_tab != 0) {
                    repo_current_tab = 0;
                    tab_changed = true;
                    if (repo_filter_mode >= 3) repo_filter_mode = 0;
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Molecules")) {
                if (repo_current_tab != 1) {
                    repo_current_tab = 1;
                    tab_changed = true;
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // Auto-fetch listing on tab switch or first open
        auto status = repository_ptr->status();
        if (tab_changed || status == RepoStatus::Idle) {
            repository_ptr->fetch_listing(repo_current_tab);
            status = repository_ptr->status();
        }

        // ── Refresh button ───────────────────────────────────────────────
        ImGui::SameLine(ImGui::GetWindowWidth() - 85);
        bool can_refresh = (status != RepoStatus::Loading &&
                            status != RepoStatus::Downloading &&
                            status != RepoStatus::Uploading);
        if (!can_refresh) ImGui::BeginDisabled();
        if (ImGui::Button("Refresh", ImVec2(75, 0))) {
            repository_ptr->fetch_listing(repo_current_tab);
        }
        if (!can_refresh) ImGui::EndDisabled();

        // ── Search bar ────────────────────────────────────────────────────
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 200);
        ImGui::InputTextWithHint("##repo_search", "Search by formula or name...",
                                 repo_search_buf, sizeof(repo_search_buf));
        ImGui::SameLine(0, 8);

        // Filter buttons
        auto filter_btn = [&](const char* label, int mode, ImVec4 col) {
            bool active = (repo_filter_mode == mode);
            if (!active) col = ImVec4(col.x * 0.4f, col.y * 0.4f, col.z * 0.4f, 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x * 0.2f, col.y * 0.2f, col.z * 0.2f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.4f, col.y * 0.4f, col.z * 0.4f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            if (ImGui::SmallButton(label)) repo_filter_mode = mode;
            ImGui::PopStyleColor(3);
        };
        filter_btn("All", 0, ImVec4(0.7f, 0.7f, 0.8f, 1.0f));
        ImGui::SameLine(0, 3);
        filter_btn("Cached", 1, ImVec4(0.3f, 0.9f, 0.4f, 1.0f));
        ImGui::SameLine(0, 3);
        filter_btn("New", 2, ImVec4(0.4f, 0.65f, 1.0f, 1.0f));
        if (repo_current_tab == 1) {
            ImGui::SameLine(0, 3);
            filter_btn("Chiral", 3, ImVec4(0.8f, 0.4f, 1.0f, 1.0f));
            ImGui::SameLine(0, 3);
            filter_btn("Achiral", 4, ImVec4(0.6f, 0.6f, 0.7f, 1.0f));
        }

        ImGui::Separator();

        // ── File listing ─────────────────────────────────────────────────
        float footer_h = 120.0f; // space for status + upload + token sections
        ImGui::BeginChild("##RepoFileList", ImVec2(0, -footer_h), true);
        {
            if (status == RepoStatus::Loading) {
                // Spinning dots animation
                float t = static_cast<float>(ImGui::GetTime());
                int dots = static_cast<int>(t * 3.0f) % 4;
                char loading[32];
                snprintf(loading, sizeof(loading), "Loading%.*s", dots, "...");
                ImGui::TextColored(tc.text_dim, "%s", loading);
            } else {
                auto listing = repository_ptr->get_listing();
                if (listing.empty()) {
                    ImGui::TextColored(tc.text_dim, "No files available yet.");
                    ImGui::TextColored(tc.text_dim,
                        "Upload %s files to populate the repository.",
                        repo_current_tab == 0 ? ".ppel" : ".ppmol");
                } else {
                    // Pre-compute search filter
                    size_t search_len = strlen(repo_search_buf);
                    char search_lower[64] = {};
                    for (size_t si = 0; si < search_len && si < 63; ++si) {
                        char c = repo_search_buf[si];
                        search_lower[si] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
                    }

                    // Count visible for stats
                    int visible_count = 0;

                    // Table — molecules get an extra Chirality column
                    int col_count = (repo_current_tab == 1) ? 6 : 5;
                    if (ImGui::BeginTable("##RepoFiles", col_count,
                            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_ScrollY)) {
                        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                        if (repo_current_tab == 1)
                            ImGui::TableSetupColumn("Chirality", ImGuiTableColumnFlags_WidthFixed, 56.0f);
                        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                        ImGui::TableHeadersRow();

                        for (int i = 0; i < static_cast<int>(listing.size()); i++) {
                            const auto& entry = listing[i];

                            // Apply status filter
                            if (repo_filter_mode == 1 && !entry.is_cached) continue;
                            if (repo_filter_mode == 2 && entry.is_cached) continue;
                            if (repo_filter_mode == 3 && entry.chirality != 1) continue;
                            if (repo_filter_mode == 4 && entry.chirality != 0) continue;

                            // Strip extension for display
                            std::string display_name = entry.name;
                            auto dot_pos = display_name.rfind('.');
                            if (dot_pos != std::string::npos)
                                display_name = display_name.substr(0, dot_pos);

                            // Look up common name
                            const char* common_name = nullptr;
                            if (repo_current_tab == 0) {
                                common_name = element_name_from_ppel_filename(display_name.c_str());
                            } else {
                                common_name = lookup_molecule_common_name(display_name.c_str());
                            }

                            // Apply search filter
                            if (search_len > 0) {
                                bool match = false;
                                // Check formula/filename (case-insensitive substring)
                                for (size_t fi = 0; fi < display_name.size() && !match; ++fi) {
                                    bool ok = true;
                                    for (size_t si = 0; si < search_len && ok; ++si) {
                                        if (fi + si >= display_name.size()) { ok = false; break; }
                                        char fc = display_name[fi + si];
                                        if (fc >= 'A' && fc <= 'Z') fc += 32;
                                        if (fc != search_lower[si]) ok = false;
                                    }
                                    if (ok) match = true;
                                }
                                // Check common name (case-insensitive substring)
                                if (!match && common_name) {
                                    size_t nlen = strlen(common_name);
                                    for (size_t ni = 0; ni < nlen && !match; ++ni) {
                                        bool ok = true;
                                        for (size_t si = 0; si < search_len && ok; ++si) {
                                            if (ni + si >= nlen) { ok = false; break; }
                                            char nc = common_name[ni + si];
                                            if (nc >= 'A' && nc <= 'Z') nc += 32;
                                            if (nc != search_lower[si]) ok = false;
                                        }
                                        if (ok) match = true;
                                    }
                                }
                                if (!match) continue;
                            }

                            visible_count++;
                            ImGui::TableNextRow();

                            // File (formula / isotope notation)
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(display_name.c_str());

                            // Common name
                            ImGui::TableSetColumnIndex(1);
                            if (common_name) {
                                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "%s", common_name);
                            }

                            // Chirality (molecules only)
                            int col_off = 2;
                            if (repo_current_tab == 1) {
                                ImGui::TableSetColumnIndex(col_off++);
                                if (entry.chirality == 1)
                                    ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "Chiral");
                                else if (entry.chirality == 0)
                                    ImGui::TextColored(tc.text_dim, "---");
                            }

                            // Size
                            ImGui::TableSetColumnIndex(col_off++);
                            ImGui::TextColored(tc.text_dim, "%s",
                                format_size(entry.size).c_str());

                            // Status
                            ImGui::TableSetColumnIndex(col_off++);
                            if (entry.is_cached) {
                                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "cached");
                            }

                            // Action
                            ImGui::TableSetColumnIndex(col_off);
                            ImGui::PushID(i);

                            bool busy = (status == RepoStatus::Downloading ||
                                         status == RepoStatus::Uploading);

                            if (entry.is_cached) {
                                if (busy) ImGui::BeginDisabled();
                                if (ImGui::Button("Import", ImVec2(90, 0))) {
                                    // Import the cached file
                                    std::string cache_dir = get_data_dir() + "repository/"
                                        + (repo_current_tab == 0 ? "elements/" : "molecules/");
                                    std::string path = cache_dir + entry.name;
                                    snprintf(save_filename, sizeof(save_filename),
                                             "%s", path.c_str());
                                    if (repo_current_tab == 0)
                                        request_import = true;
                                    else
                                        request_molecule_import = true;
                                    show_repository = false;
                                }
                                if (busy) ImGui::EndDisabled();
                            } else {
                                if (busy) ImGui::BeginDisabled();
                                if (ImGui::Button("Download", ImVec2(90, 0))) {
                                    repository_ptr->download_file(i);
                                }
                                if (busy) ImGui::EndDisabled();
                            }

                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }

                    // Show result count when filtering
                    if (search_len > 0 || repo_filter_mode != 0) {
                        ImGui::TextColored(tc.text_dim, "%d of %d shown",
                            visible_count, static_cast<int>(listing.size()));
                    }
                }
            }
        }
        ImGui::EndChild();

        // ── Status bar ───────────────────────────────────────────────────
        {
            std::string msg = repository_ptr->status_message();
            if (status == RepoStatus::Error) {
                ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "%s", msg.c_str());
            } else if (status == RepoStatus::Downloading) {
                float prog = repository_ptr->progress();
                ImGui::ProgressBar(prog, ImVec2(-1, 0), msg.c_str());
            } else if (!msg.empty()) {
                ImGui::TextColored(tc.text_dim, "%s", msg.c_str());
            }
        }

        ImGui::Separator();

        // ── Upload section (collapsible) ─────────────────────────────────
        if (ImGui::CollapsingHeader("Upload to Repository")) {
            if (!repository_ptr->has_token()) {
                ImGui::TextColored(tc.text_dim,
                    "Set a GitHub token below to enable uploads.");
            } else {
                ImGui::TextUnformatted("File:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
                ImGui::InputText("##upload_path", repo_upload_path,
                                 sizeof(repo_upload_path));
                ImGui::SameLine();

                bool can_upload = (strlen(repo_upload_path) > 0 &&
                                   status != RepoStatus::Uploading &&
                                   status != RepoStatus::Downloading);
                if (!can_upload) ImGui::BeginDisabled();
                if (ImGui::Button("Upload", ImVec2(80, 0))) {
                    repository_ptr->upload_file(repo_upload_path, repo_current_tab);
                }
                if (!can_upload) ImGui::EndDisabled();

                ImGui::TextColored(tc.text_dim,
                    "Paste the full path to a %s file.",
                    repo_current_tab == 0 ? ".ppel" : ".ppmol");
            }
        }

        // ── Token section (collapsible) ──────────────────────────────────
        if (ImGui::CollapsingHeader("GitHub Token")) {
            if (repository_ptr->has_token()) {
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f),
                    "Token configured");
                ImGui::SameLine();
                if (ImGui::Button("Clear Token")) {
                    repository_ptr->clear_token();
                    memset(repo_token_buf, 0, sizeof(repo_token_buf));
                }
            } else {
                ImGui::TextColored(tc.text_dim,
                    "A GitHub Personal Access Token with repo write access");
                ImGui::TextColored(tc.text_dim,
                    "is needed for uploads.");
            }

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
            ImGui::InputText("##token", repo_token_buf, sizeof(repo_token_buf),
                             ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            bool has_input = strlen(repo_token_buf) > 0;
            if (!has_input) ImGui::BeginDisabled();
            if (ImGui::Button("Save Token", ImVec2(80, 0))) {
                repository_ptr->set_token(repo_token_buf);
                memset(repo_token_buf, 0, sizeof(repo_token_buf));
            }
            if (!has_input) ImGui::EndDisabled();
        }
    }
    if (!open) show_repository = false;
    ImGui::End();
    ImGui::PopStyleColor();
}
