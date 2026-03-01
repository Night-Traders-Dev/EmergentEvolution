// Procedural sound effects generator — creates 16-bit PCM mono WAV files at 44100 Hz.
// Build: g++ -O2 -std=c++17 -o build/gen_sfx tools/gen_sound_effects.cpp -lm
// Run:   ./build/gen_sfx   (outputs to assets/sfx/)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

static constexpr int   SAMPLE_RATE = 44100;
static constexpr float PI = 3.14159265358979f;
static constexpr float TAU = PI * 2.0f;

// ── WAV writer ──────────────────────────────────────────────────────────────

static bool write_wav(const char* path, const std::vector<int16_t>& samples) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    uint32_t data_size = (uint32_t)(samples.size() * sizeof(int16_t));
    uint32_t file_size = 36 + data_size;

    // RIFF header
    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;  // PCM
    uint16_t channels = 1;      // mono
    uint32_t sample_rate = SAMPLE_RATE;
    uint32_t byte_rate = SAMPLE_RATE * 2;  // 16-bit mono
    uint16_t block_align = 2;
    uint16_t bits_per_sample = 16;
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&audio_format, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits_per_sample, 2, 1, f);

    // data chunk
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    fwrite(samples.data(), sizeof(int16_t), samples.size(), f);

    fclose(f);
    return true;
}

// ── Synthesis primitives ────────────────────────────────────────────────────

static float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

static float sine(float freq, float t) {
    return sinf(TAU * freq * t);
}

static float saw(float freq, float t) {
    float phase = fmodf(freq * t, 1.0f);
    return 2.0f * phase - 1.0f;
}

static float noise() {
    return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

// Exponential decay envelope: attack in seconds, then decay
static float env_decay(float t, float attack, float decay_time) {
    if (t < attack) return t / attack;
    return expf(-(t - attack) / decay_time);
}

// ADSR envelope
static float env_adsr(float t, float a, float d, float s, float r, float dur) {
    if (t < a) return t / a;
    if (t < a + d) return 1.0f - (1.0f - s) * ((t - a) / d);
    if (t < dur - r) return s;
    if (t < dur) return s * (1.0f - (t - (dur - r)) / r);
    return 0.0f;
}

// Linear frequency sweep
static float sweep(float f0, float f1, float t, float dur) {
    float freq = f0 + (f1 - f0) * (t / dur);
    return sinf(TAU * (f0 * t + 0.5f * (f1 - f0) * t * t / dur));
}

// Exponential frequency sweep
static float sweep_exp(float f0, float f1, float t, float dur) {
    float ratio = f1 / f0;
    float freq = f0 * powf(ratio, t / dur);
    float phase = f0 * dur / logf(ratio) * (powf(ratio, t / dur) - 1.0f);
    return sinf(TAU * phase);
}

// One-pole low-pass filter (call per-sample, pass state by reference)
static float lowpass(float sample, float& state, float cutoff_norm) {
    float alpha = clampf(cutoff_norm, 0.0f, 1.0f);
    state += alpha * (sample - state);
    return state;
}

// Soft-clip distortion
static float distort(float sample, float drive) {
    return tanhf(sample * drive);
}

// ── Sound generators ────────────────────────────────────────────────────────

static std::vector<int16_t> make_samples(float duration) {
    int n = (int)(duration * SAMPLE_RATE);
    return std::vector<int16_t>(n, 0);
}

static void normalize(std::vector<int16_t>& samples, float target_peak = 0.9f) {
    int16_t peak = 0;
    for (auto s : samples) peak = std::max(peak, (int16_t)std::abs(s));
    if (peak == 0) return;
    float scale = target_peak * 32767.0f / (float)peak;
    for (auto& s : samples) s = (int16_t)clampf((float)s * scale, -32767, 32767);
}

// 0: Achievement — ascending major triad arpeggio with reverb
static void gen_achievement(std::vector<int16_t>& out) {
    float dur = 0.9f;
    out = make_samples(dur);
    // Notes: C5(523), E5(659), G5(784), C6(1047)
    float notes[] = { 523.25f, 659.25f, 783.99f, 1046.50f };
    float note_start[] = { 0.0f, 0.15f, 0.30f, 0.45f };

    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        float val = 0.0f;

        for (int n = 0; n < 4; ++n) {
            float nt = t - note_start[n];
            if (nt < 0.0f) continue;
            float env = env_decay(nt, 0.01f, 0.35f);
            // Fundamental + octave harmonic
            val += env * 0.5f * sine(notes[n], nt);
            val += env * 0.2f * sine(notes[n] * 2.0f, nt);
            // Slight detuned shimmer
            val += env * 0.1f * sine(notes[n] * 1.002f, nt);
        }

        // Reverb tail: delayed sum
        float reverb = 0.0f;
        int delay1 = (int)(0.037f * SAMPLE_RATE);
        int delay2 = (int)(0.061f * SAMPLE_RATE);
        if (i > delay1) reverb += 0.2f * (float)out[i - delay1] / 32767.0f;
        if (i > delay2) reverb += 0.15f * (float)out[i - delay2] / 32767.0f;
        val += reverb;

        out[i] = (int16_t)clampf(val * 20000.0f, -32767, 32767);
    }
    normalize(out);
}

// 1: Spawn — quick rising chirp
static void gen_spawn(std::vector<int16_t>& out) {
    float dur = 0.15f;
    out = make_samples(dur);
    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        float env = env_decay(t, 0.005f, 0.06f);
        float val = env * sweep_exp(200.0f, 1200.0f, t, dur);
        // Add a subtle harmonic
        val += env * 0.3f * sweep_exp(400.0f, 2400.0f, t, dur);
        out[i] = (int16_t)clampf(val * 28000.0f, -32767, 32767);
    }
    normalize(out);
}

// 2: Decay — descending tone with filtered noise
static void gen_decay(std::vector<int16_t>& out) {
    float dur = 0.4f;
    out = make_samples(dur);
    float lp_state = 0.0f;
    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        float env = env_decay(t, 0.01f, 0.15f);
        // Descending sweep
        float val = env * sweep_exp(800.0f, 150.0f, t, dur);
        // Filtered noise component (simulates energy dissipation)
        float n = noise() * env * 0.25f;
        float cutoff = 0.05f + 0.15f * (1.0f - t / dur); // decreasing cutoff
        n = lowpass(n, lp_state, cutoff);
        val += n;
        // Sub-bass thump at start
        val += env_decay(t, 0.005f, 0.05f) * 0.4f * sine(100.0f, t);
        out[i] = (int16_t)clampf(val * 26000.0f, -32767, 32767);
    }
    normalize(out);
}

// 3: Fusion — deep bass impact + bright overtone ring
static void gen_fusion(std::vector<int16_t>& out) {
    float dur = 0.6f;
    out = make_samples(dur);
    float lp_state = 0.0f;
    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        float val = 0.0f;

        // Deep impact bass (60Hz, sharp attack)
        float bass_env = env_decay(t, 0.008f, 0.12f);
        val += bass_env * 0.6f * sine(60.0f, t);
        val += bass_env * 0.3f * sine(120.0f, t);  // octave

        // Bright harmonic overtone (880Hz, slightly delayed)
        float bright_t = t - 0.015f;
        if (bright_t > 0.0f) {
            float bright_env = env_decay(bright_t, 0.01f, 0.2f);
            val += bright_env * 0.25f * sine(880.0f, bright_t);
            val += bright_env * 0.12f * sine(1320.0f, bright_t);  // 3rd harmonic
        }

        // Noise transient at impact
        float noise_env = env_decay(t, 0.002f, 0.03f);
        float n = noise() * noise_env * 0.35f;
        n = lowpass(n, lp_state, 0.1f);
        val += n;

        // Resonant ring-down
        float ring_env = env_decay(t, 0.05f, 0.3f);
        val += ring_env * 0.1f * sine(440.0f, t);

        out[i] = (int16_t)clampf(val * 22000.0f, -32767, 32767);
    }
    normalize(out);
}

// 4: Fission — sharp crack + low rumble
static void gen_fission(std::vector<int16_t>& out) {
    float dur = 0.5f;
    out = make_samples(dur);
    float lp_state = 0.0f;
    float lp_state2 = 0.0f;
    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        float val = 0.0f;

        // Sharp noise crack (initial transient)
        float crack_env = env_decay(t, 0.001f, 0.015f);
        val += crack_env * noise() * 0.7f;

        // Mid-frequency crack body
        float body_env = env_decay(t, 0.003f, 0.04f);
        float body = noise() * body_env * 0.4f;
        body = lowpass(body, lp_state, 0.15f);
        val += body;

        // Low rumble decay (80Hz)
        float rumble_env = env_decay(t, 0.02f, 0.2f);
        val += rumble_env * 0.35f * sine(80.0f, t);
        val += rumble_env * 0.15f * sine(55.0f, t);

        // Descending noise sweep (debris flying apart)
        float sweep_env = env_decay(t, 0.01f, 0.15f);
        float swept_noise = noise() * sweep_env * 0.2f;
        float cutoff = 0.3f * expf(-t * 4.0f);
        swept_noise = lowpass(swept_noise, lp_state2, cutoff);
        val += swept_noise;

        out[i] = (int16_t)clampf(val * 24000.0f, -32767, 32767);
    }
    normalize(out);
}

// 5: Click — sharp UI click
static void gen_click(std::vector<int16_t>& out) {
    float dur = 0.03f;
    out = make_samples(dur);
    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        // Sharp impulse with damped 2kHz oscillation
        float env = expf(-t * 200.0f);
        float val = env * sine(2000.0f, t) * 0.6f;
        // Initial click transient
        if (t < 0.001f) val += (1.0f - t / 0.001f) * 0.4f;
        out[i] = (int16_t)clampf(val * 30000.0f, -32767, 32767);
    }
    normalize(out);
}

// 6: Annihilation — Star Wars blaster "pew" (fast descending sweep + metallic zing)
static void gen_annihilation(std::vector<int16_t>& out) {
    float dur = 0.3f;
    out = make_samples(dur);
    float lp_state = 0.0f;
    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        float val = 0.0f;

        // Main "pew" — fast exponential descending sweep 2800→250Hz
        float pew_env = env_decay(t, 0.002f, 0.09f);
        val += pew_env * 0.55f * sweep_exp(2800.0f, 250.0f, t, dur * 0.4f);

        // 2nd harmonic — metallic zing character
        val += pew_env * 0.25f * sweep_exp(5600.0f, 500.0f, t, dur * 0.4f);

        // Initial transient snap (noise burst, first 3ms)
        float snap_env = env_decay(t, 0.0005f, 0.003f);
        val += snap_env * noise() * 0.5f;

        // Mid-body resonance ring (~800Hz, gives it weight)
        float ring_env = env_decay(t, 0.01f, 0.12f);
        val += ring_env * 0.15f * sine(800.0f, t);

        // Slight tail echo (delayed copy, quieter)
        int echo_delay = (int)(0.045f * SAMPLE_RATE);
        if (i > echo_delay)
            val += 0.15f * (float)out[i - echo_delay] / 32767.0f;

        // Soft-clip saturation for that crunchy blaster quality
        val = distort(val, 1.8f);

        out[i] = (int16_t)clampf(val * 26000.0f, -32767, 32767);
    }
    normalize(out);
}

// 7: Bond — perfect fifth harmonic chord
static void gen_bond(std::vector<int16_t>& out) {
    float dur = 0.25f;
    out = make_samples(dur);
    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        float env = env_adsr(t, 0.02f, 0.04f, 0.5f, 0.08f, dur);
        // C4 (262Hz) + G4 (392Hz) = perfect fifth
        float val = env * 0.4f * sine(261.63f, t);
        val += env * 0.35f * sine(392.0f, t);
        // Subtle octave
        val += env * 0.1f * sine(523.25f, t);
        // Soft warmth
        val += env * 0.08f * sine(261.63f * 2.005f, t);  // slight detune
        out[i] = (int16_t)clampf(val * 28000.0f, -32767, 32767);
    }
    normalize(out);
}

// 8: Collision — short percussive thud
static void gen_collision(std::vector<int16_t>& out) {
    float dur = 0.1f;
    out = make_samples(dur);
    float lp_state = 0.0f;
    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        float env = env_decay(t, 0.001f, 0.025f);
        // Low thump
        float val = env * 0.5f * sine(100.0f, t);
        val += env * 0.25f * sine(200.0f, t);
        // Noise impact
        float n = noise() * env_decay(t, 0.0005f, 0.01f) * 0.4f;
        n = lowpass(n, lp_state, 0.08f);
        val += n;
        out[i] = (int16_t)clampf(val * 28000.0f, -32767, 32767);
    }
    normalize(out);
}

// 9: Photon — quick crystalline ping
static void gen_photon(std::vector<int16_t>& out) {
    float dur = 0.2f;
    out = make_samples(dur);
    for (int i = 0; i < (int)out.size(); ++i) {
        float t = (float)i / SAMPLE_RATE;
        float env = env_decay(t, 0.003f, 0.06f);
        // High-frequency pure tone
        float val = env * 0.45f * sine(2093.0f, t);  // C7
        // Harmonic shimmer
        val += env * 0.2f * sine(2093.0f * 2.0f, t);
        val += env * 0.1f * sine(2093.0f * 3.0f, t);
        // Quick bright attack
        float attack = env_decay(t, 0.001f, 0.02f);
        val += attack * 0.15f * sine(4186.0f, t);
        out[i] = (int16_t)clampf(val * 26000.0f, -32767, 32767);
    }
    normalize(out);
}

// ── Main ────────────────────────────────────────────────────────────────────

struct SoundDef {
    const char* name;
    void (*generate)(std::vector<int16_t>&);
};

static const SoundDef SOUNDS[] = {
    { "achievement",   gen_achievement },
    { "spawn",         gen_spawn },
    { "decay",         gen_decay },
    { "fusion",        gen_fusion },
    { "fission",       gen_fission },
    { "click",         gen_click },
    { "annihilation",  gen_annihilation },
    { "bond",          gen_bond },
    { "collision",     gen_collision },
    { "photon",        gen_photon },
};

static constexpr int SOUND_COUNT = sizeof(SOUNDS) / sizeof(SOUNDS[0]);

int main() {
    std::filesystem::create_directories("assets/sfx");
    srand(42);  // deterministic

    std::vector<int16_t> samples;
    int count = 0;

    for (int i = 0; i < SOUND_COUNT; ++i) {
        SOUNDS[i].generate(samples);

        std::string path = std::string("assets/sfx/") + SOUNDS[i].name + ".wav";
        float dur = (float)samples.size() / SAMPLE_RATE;

        if (write_wav(path.c_str(), samples)) {
            printf("[%2d/%d] %-20s %5.0f ms  %6zu bytes\n",
                   i + 1, SOUND_COUNT, path.c_str(),
                   dur * 1000.0f, samples.size() * 2 + 44);
            ++count;
        } else {
            fprintf(stderr, "FAILED: %s\n", path.c_str());
        }
    }

    printf("\nGenerated %d sound effects in assets/sfx/\n", count);
    return 0;
}
