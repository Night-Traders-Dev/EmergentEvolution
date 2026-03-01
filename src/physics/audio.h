#pragma once

// ── Background music + sound effects via miniaudio ──────────────────────────
// Music: single MP3, looped continuously.
// SFX: one-shot sounds loaded from assets/sfx/, fire-and-forget playback.

#include "../miniaudio.h"
#include <string>
#include <cstdio>

struct AudioPlayer {
    ma_engine  engine{};
    ma_sound   music{};
    bool       engine_ok = false;
    bool       music_ok  = false;
    float      volume    = 0.5f;   // 0–1 music volume

    // ── Sound effects ─────────────────────────────────────────────────────
    static constexpr int SFX_COUNT = 6;
    enum SfxId : int {
        SFX_ACHIEVEMENT = 0,
        SFX_SPAWN,
        SFX_DECAY,
        SFX_FUSION,
        SFX_FISSION,
        SFX_CLICK,
    };
    ma_sound   sfx[SFX_COUNT]{};
    bool       sfx_ok[SFX_COUNT]{};
    float      sfx_volume = 0.7f;  // 0–1 sfx volume
    bool       sfx_muted  = false;

    bool init(const char* mp3_path) {
        // Clean up any prior partial init (safe to call multiple times)
        for (int i = 0; i < SFX_COUNT; i++) {
            if (sfx_ok[i]) { ma_sound_uninit(&sfx[i]); sfx_ok[i] = false; }
        }
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
            // Continue — SFX can still work even without music
        } else {
            music_ok = true;
            ma_sound_set_looping(&music, MA_TRUE);
            ma_sound_set_volume(&music, volume);
            ma_sound_start(&music);
        }

        // Load SFX files (gracefully skip missing files)
        static const char* SFX_FILES[SFX_COUNT] = {
            "assets/sfx/achievement.wav",
            "assets/sfx/spawn.wav",
            "assets/sfx/decay.wav",
            "assets/sfx/fusion.wav",
            "assets/sfx/fission.wav",
            "assets/sfx/click.wav",
        };
        for (int i = 0; i < SFX_COUNT; i++) {
            if (ma_sound_init_from_file(&engine, SFX_FILES[i], 0, nullptr, nullptr, &sfx[i]) == MA_SUCCESS) {
                sfx_ok[i] = true;
                ma_sound_set_volume(&sfx[i], sfx_volume);
            }
        }

        return engine_ok;
    }

    void set_volume(float v) {
        volume = v;
        if (music_ok) ma_sound_set_volume(&music, v);
    }

    void set_sfx_volume(float v) {
        sfx_volume = v;
        for (int i = 0; i < SFX_COUNT; i++) {
            if (sfx_ok[i]) ma_sound_set_volume(&sfx[i], v);
        }
    }

    void play(SfxId id) {
        if (sfx_muted || !sfx_ok[id]) return;
        // Seek to start and play (fire-and-forget one-shot)
        ma_sound_seek_to_pcm_frame(&sfx[id], 0);
        ma_sound_start(&sfx[id]);
    }

    void play_achievement() { play(SFX_ACHIEVEMENT); }
    void play_spawn()       { play(SFX_SPAWN); }
    void play_decay()       { play(SFX_DECAY); }
    void play_fusion()      { play(SFX_FUSION); }
    void play_fission()     { play(SFX_FISSION); }
    void play_click()       { play(SFX_CLICK); }

    void pause()  { if (music_ok) ma_sound_stop(&music); }
    void resume() { if (music_ok) ma_sound_start(&music); }
    bool is_playing() const { return music_ok && ma_sound_is_playing(const_cast<ma_sound*>(&music)); }

    void destroy() {
        for (int i = 0; i < SFX_COUNT; i++) {
            if (sfx_ok[i]) { ma_sound_uninit(&sfx[i]); sfx_ok[i] = false; }
        }
        if (music_ok)  { ma_sound_uninit(&music);  music_ok  = false; }
        if (engine_ok) { ma_engine_uninit(&engine); engine_ok = false; }
    }
};
