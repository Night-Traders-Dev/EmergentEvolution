#include "physics/save_load.h"
#include <fstream>
#include <cstring>

static constexpr uint32_t PPSG_MAGIC   = 0x47535050;  // "PPSG" little-endian
static constexpr uint32_t PPSG_VERSION = 1;

// Helper: write raw bytes
template<typename T>
static bool write_val(std::ofstream& f, const T& v) {
    f.write(reinterpret_cast<const char*>(&v), sizeof(T));
    return f.good();
}
template<typename T>
static bool write_vec(std::ofstream& f, const std::vector<T>& v) {
    uint32_t count = static_cast<uint32_t>(v.size());
    write_val(f, count);
    if (count > 0)
        f.write(reinterpret_cast<const char*>(v.data()), count * sizeof(T));
    return f.good();
}

// Helper: read raw bytes
template<typename T>
static bool read_val(std::ifstream& f, T& v) {
    f.read(reinterpret_cast<char*>(&v), sizeof(T));
    return f.good();
}
template<typename T>
static bool read_vec(std::ifstream& f, std::vector<T>& v) {
    uint32_t count = 0;
    if (!read_val(f, count)) return false;
    if (count > 10'000'000) return false;  // sanity limit
    v.resize(count);
    if (count > 0)
        f.read(reinterpret_cast<char*>(v.data()), count * sizeof(T));
    return f.good();
}

SaveResult save_simulation(
    const std::string& filepath,
    const SimConfig& cfg,
    const Particles& particles,
    const std::vector<glm::vec2>& positions,
    const std::vector<glm::vec2>& velocities,
    const std::vector<float>& energies,
    const ForceObject* force_objects,
    uint32_t force_object_count,
    bool field_em, bool field_strong, bool field_weak,
    bool field_gravity, bool field_higgs,
    float field_intensity, float log_temperature)
{
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open())
        return { false, "Cannot open file for writing" };

    // Header
    write_val(f, PPSG_MAGIC);
    write_val(f, PPSG_VERSION);

    // SimConfig (POD)
    write_val(f, cfg);

    // Particle data (GPU-authoritative positions/velocities/energies, CPU-side types/genomes/angles)
    write_vec(f, positions);
    write_vec(f, velocities);
    write_vec(f, energies);
    write_vec(f, particles.types);
    write_vec(f, particles.angles);
    write_vec(f, particles.angular_velocities);
    write_vec(f, particles.genomes);

    // Per-type data
    f.write(reinterpret_cast<const char*>(particles.forces.data()),
            MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES * sizeof(float));
    f.write(reinterpret_cast<const char*>(particles.colors.data()),
            MAX_PARTICLE_TYPES * sizeof(glm::vec4));
    f.write(reinterpret_cast<const char*>(particles.behavior_flags),
            MAX_PARTICLE_TYPES * sizeof(uint32_t));

    // Force objects
    write_val(f, force_object_count);
    f.write(reinterpret_cast<const char*>(force_objects),
            MAX_FORCE_OBJECTS * sizeof(ForceObject));

    // UI state
    uint8_t field_bits = 0;
    if (field_em)      field_bits |= (1 << 0);
    if (field_strong)  field_bits |= (1 << 1);
    if (field_weak)    field_bits |= (1 << 2);
    if (field_gravity) field_bits |= (1 << 3);
    if (field_higgs)   field_bits |= (1 << 4);
    write_val(f, field_bits);
    write_val(f, field_intensity);
    write_val(f, log_temperature);

    if (!f.good())
        return { false, "Write error" };

    return { true, "Saved!" };
}

LoadResult load_simulation(const std::string& filepath) {
    LoadResult r;
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) {
        r.message = "Cannot open file";
        return r;
    }

    // Header
    uint32_t magic = 0, version = 0;
    read_val(f, magic);
    read_val(f, version);
    if (magic != PPSG_MAGIC) {
        r.message = "Not a valid .ppsg file";
        return r;
    }
    if (version != PPSG_VERSION) {
        r.message = "Unsupported version";
        return r;
    }

    // SimConfig
    if (!read_val(f, r.cfg)) {
        r.message = "Failed to read config";
        return r;
    }

    // Particle data
    if (!read_vec(f, r.positions) ||
        !read_vec(f, r.velocities) ||
        !read_vec(f, r.energies) ||
        !read_vec(f, r.types) ||
        !read_vec(f, r.angles) ||
        !read_vec(f, r.angular_velocities) ||
        !read_vec(f, r.genomes)) {
        r.message = "Failed to read particle data";
        return r;
    }

    // Per-type data
    f.read(reinterpret_cast<char*>(r.forces),
           MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES * sizeof(float));
    f.read(reinterpret_cast<char*>(r.colors),
           MAX_PARTICLE_TYPES * sizeof(glm::vec4));
    f.read(reinterpret_cast<char*>(r.behavior_flags),
           MAX_PARTICLE_TYPES * sizeof(uint32_t));

    // Force objects
    read_val(f, r.force_object_count);
    f.read(reinterpret_cast<char*>(r.force_objects),
           MAX_FORCE_OBJECTS * sizeof(ForceObject));

    // UI state
    uint8_t field_bits = 0;
    read_val(f, field_bits);
    r.field_em      = (field_bits & (1 << 0)) != 0;
    r.field_strong  = (field_bits & (1 << 1)) != 0;
    r.field_weak    = (field_bits & (1 << 2)) != 0;
    r.field_gravity = (field_bits & (1 << 3)) != 0;
    r.field_higgs   = (field_bits & (1 << 4)) != 0;
    read_val(f, r.field_intensity);
    read_val(f, r.log_temperature);

    if (!f.good()) {
        r.message = "Truncated file";
        return r;
    }

    r.success = true;
    r.message = "Loaded!";
    return r;
}

// ── Element export/import (.ppel) ────────────────────────────────────────────

static constexpr uint32_t PPEL_MAGIC   = 0x4C455050;  // "PPEL" little-endian
static constexpr uint32_t PPEL_VERSION = 1;

SaveResult export_element(
    const std::string& filepath,
    int Z, int N, int electrons,
    const std::vector<ElementExportData>& particles)
{
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open())
        return { false, "Cannot open file for writing" };

    write_val(f, PPEL_MAGIC);
    write_val(f, PPEL_VERSION);
    write_val(f, static_cast<int32_t>(Z));
    write_val(f, static_cast<int32_t>(N));
    write_val(f, static_cast<int32_t>(electrons));

    uint32_t count = static_cast<uint32_t>(particles.size());
    write_val(f, count);
    for (const auto& p : particles) {
        write_val(f, p.dx);
        write_val(f, p.dy);
        write_val(f, p.vx);
        write_val(f, p.vy);
        write_val(f, p.energy);
        write_val(f, p.type);
        f.write(reinterpret_cast<const char*>(p.genome), GENOME_SIZE * sizeof(float));
    }

    if (!f.good())
        return { false, "Write error" };

    return { true, "Exported!" };
}

ImportElementResult import_element(const std::string& filepath) {
    ImportElementResult r;
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) {
        r.message = "Cannot open file";
        return r;
    }

    uint32_t magic = 0, version = 0;
    read_val(f, magic);
    read_val(f, version);
    if (magic != PPEL_MAGIC) {
        r.message = "Not a valid .ppel file";
        return r;
    }
    if (version != PPEL_VERSION) {
        r.message = "Unsupported .ppel version";
        return r;
    }

    int32_t z, n, e;
    read_val(f, z);
    read_val(f, n);
    read_val(f, e);
    r.Z = z;
    r.N = n;
    r.electrons = e;

    uint32_t count = 0;
    read_val(f, count);
    if (count > 10000) {
        r.message = "Element too large";
        return r;
    }

    r.particles.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        auto& p = r.particles[i];
        read_val(f, p.dx);
        read_val(f, p.dy);
        read_val(f, p.vx);
        read_val(f, p.vy);
        read_val(f, p.energy);
        read_val(f, p.type);
        f.read(reinterpret_cast<char*>(p.genome), GENOME_SIZE * sizeof(float));
    }

    if (!f.good()) {
        r.message = "Truncated file";
        return r;
    }

    r.success = true;
    r.message = "Imported!";
    return r;
}
