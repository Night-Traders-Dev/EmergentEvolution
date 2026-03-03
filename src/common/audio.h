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
    std::string asset_prefix;      // "" or "../" depending on CWD

    // ── Sound effects ─────────────────────────────────────────────────────
    static constexpr int SFX_COUNT = 10;
    enum SfxId : int {
        SFX_ACHIEVEMENT = 0,
        SFX_SPAWN,
        SFX_DECAY,
        SFX_FUSION,
        SFX_FISSION,
        SFX_CLICK,
        SFX_ANNIHILATION,
        SFX_BOND,
        SFX_COLLISION,
        SFX_PHOTON,
    };
    ma_sound   sfx[SFX_COUNT]{};
    bool       sfx_ok[SFX_COUNT]{};
    float      sfx_volume = 0.7f;  // 0–1 sfx volume
    bool       sfx_muted  = false;
    uint32_t   sfx_last_frame[SFX_COUNT]{};  // per-channel cooldown

    // base_dir: asset directory prefix ("" for project root, "../" for build/)
    bool init(const char* base_dir = "") {
        // Clean up any prior partial init (safe to call multiple times)
        stop_voice();
        for (int i = 0; i < SFX_COUNT; i++) {
            if (sfx_ok[i]) { ma_sound_uninit(&sfx[i]); sfx_ok[i] = false; }
        }
        if (music_ok)  { ma_sound_uninit(&music);  music_ok  = false; }
        if (engine_ok) { ma_engine_uninit(&engine); engine_ok = false; }

        asset_prefix = base_dir;
        std::string prefix = base_dir;

        ma_engine_config cfg = ma_engine_config_init();
        if (ma_engine_init(&cfg, &engine) != MA_SUCCESS) {
            fprintf(stderr, "[audio] failed to init engine\n");
            return false;
        }
        engine_ok = true;

        std::string music_path = prefix + "assets/sound.mp3";
        if (ma_sound_init_from_file(&engine, music_path.c_str(), 0, nullptr, nullptr, &music) != MA_SUCCESS) {
            fprintf(stderr, "[audio] failed to load %s\n", music_path.c_str());
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
            "assets/sfx/annihilation.wav",
            "assets/sfx/bond.wav",
            "assets/sfx/collision.wav",
            "assets/sfx/photon.wav",
        };
        int sfx_loaded = 0;
        for (int i = 0; i < SFX_COUNT; i++) {
            std::string path = prefix + SFX_FILES[i];
            if (ma_sound_init_from_file(&engine, path.c_str(), 0, nullptr, nullptr, &sfx[i]) == MA_SUCCESS) {
                sfx_ok[i] = true;
                ma_sound_set_volume(&sfx[i], sfx_volume);
                sfx_loaded++;
            }
        }
        if (sfx_loaded > 0)
            fprintf(stderr, "[audio] loaded %d/%d sound effects\n", sfx_loaded, SFX_COUNT);

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

    void play(SfxId id, uint32_t frame = 0) {
        if (sfx_muted || !sfx_ok[id]) return;
        // Cooldown: skip if same sound played within 10 frames (~166ms at 60fps)
        if (frame > 0 && (frame - sfx_last_frame[id]) < 10) return;
        sfx_last_frame[id] = frame;
        // Seek to start and play (fire-and-forget one-shot)
        ma_sound_seek_to_pcm_frame(&sfx[id], 0);
        ma_sound_start(&sfx[id]);
    }

    void play_achievement()   { play(SFX_ACHIEVEMENT); }
    void play_spawn()         { play(SFX_SPAWN); }
    void play_decay()         { play(SFX_DECAY); }
    void play_fusion()        { play(SFX_FUSION); }
    void play_fission()       { play(SFX_FISSION); }
    void play_click()         { play(SFX_CLICK); }
    void play_annihilation()  { play(SFX_ANNIHILATION); }
    void play_bond()          { play(SFX_BOND); }
    void play_collision()     { play(SFX_COLLISION); }
    void play_photon()        { play(SFX_PHOTON); }

    // ── Voice-over (cutscene narration) ─────────────────────────────────────
    ma_sound   voice{};
    bool       voice_ok     = false;
    bool       voice_loaded = false;  // a file is currently loaded
    float      voice_volume = 0.8f;   // 0–1 voice volume
    bool       voice_muted  = false;
    float      voice_duration = 0.0f; // duration of current clip in seconds

    // Load and play a voice WAV file. Stops any currently playing voice first.
    void play_voice(const std::string& path) {
        stop_voice();
        if (!engine_ok) return;
        if (ma_sound_init_from_file(&engine, path.c_str(), 0, nullptr, nullptr, &voice) != MA_SUCCESS)
            return;
        voice_ok = true;
        voice_loaded = true;
        // Query clip duration
        float len = 0.0f;
        if (ma_sound_get_length_in_seconds(&voice, &len) == MA_SUCCESS)
            voice_duration = len;
        else
            voice_duration = 0.0f;
        ma_sound_set_volume(&voice, voice_muted ? 0.0f : voice_volume);
        ma_sound_start(&voice);
    }

    void stop_voice() {
        if (voice_ok) {
            ma_sound_stop(&voice);
            ma_sound_uninit(&voice);
            voice_ok = false;
            voice_loaded = false;
            voice_duration = 0.0f;
        }
    }

    bool is_voice_playing() const {
        return voice_ok && ma_sound_is_playing(const_cast<ma_sound*>(&voice));
    }

    void set_voice_volume(float v) {
        voice_volume = v;
        if (voice_ok) ma_sound_set_volume(&voice, voice_muted ? 0.0f : v);
    }

    // Play voice-over for a cutscene line.
    // kind: "intro" or "comp", scenario: scenario index, line: line index within cutscene
    void play_cutscene_voice(const char* kind, int scenario, int line) {
        char buf[256];
        snprintf(buf, sizeof(buf), "%sassets/voice/%s_%d_%d.wav",
                 asset_prefix.c_str(), kind, scenario, line);
        play_voice(std::string(buf));
    }

    void pause()  { if (music_ok) ma_sound_stop(&music); }
    void resume() { if (music_ok) ma_sound_start(&music); }
    bool is_playing() const { return music_ok && ma_sound_is_playing(const_cast<ma_sound*>(&music)); }

    void destroy() {
        stop_voice();
        for (int i = 0; i < SFX_COUNT; i++) {
            if (sfx_ok[i]) { ma_sound_uninit(&sfx[i]); sfx_ok[i] = false; }
        }
        if (music_ok)  { ma_sound_uninit(&music);  music_ok  = false; }
        if (engine_ok) { ma_engine_uninit(&engine); engine_ok = false; }
    }
};
