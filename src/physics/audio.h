#pragma once

// ── Background music playback via miniaudio ──────────────────────────────────
// Single MP3 file, looped continuously.  init() / destroy() bracketing.

#include "../miniaudio.h"
#include <string>
#include <cstdio>

struct AudioPlayer {
    ma_engine  engine{};
    ma_sound   music{};
    bool       engine_ok = false;
    bool       music_ok  = false;
    float      volume    = 0.5f;   // 0–1

    bool init(const char* mp3_path) {
        // Clean up any prior partial init (safe to call multiple times)
        if (music_ok)  { ma_sound_uninit(&music);  music_ok  = false; }
        if (engine_ok) { ma_engine_uninit(&engine); engine_ok = false; }

        ma_engine_config cfg = ma_engine_config_init();
        if (ma_engine_init(&cfg, &engine) != MA_SUCCESS) {
            fprintf(stderr, "[audio] failed to init engine\n");
            return false;
        }
        engine_ok = true;

        if (ma_sound_init_from_file(&engine, mp3_path, 0, nullptr, nullptr, &music) != MA_SUCCESS) {
            fprintf(stderr, "[audio] failed to load %s\n", mp3_path);
            return false;
        }
        music_ok = true;

        ma_sound_set_looping(&music, MA_TRUE);
        ma_sound_set_volume(&music, volume);
        ma_sound_start(&music);
        return true;
    }

    void set_volume(float v) {
        volume = v;
        if (music_ok) ma_sound_set_volume(&music, v);
    }

    void pause()  { if (music_ok) ma_sound_stop(&music); }
    void resume() { if (music_ok) ma_sound_start(&music); }
    bool is_playing() const { return music_ok && ma_sound_is_playing(const_cast<ma_sound*>(&music)); }

    void destroy() {
        if (music_ok)  { ma_sound_uninit(&music);  music_ok  = false; }
        if (engine_ok) { ma_engine_uninit(&engine); engine_ok = false; }
    }
};
