#include "physics/save_load.h"
#include <fstream>
#include <cstring>

static constexpr uint32_t PPSG_MAGIC   = 0x47535050;  // "PPSG" little-endian
static constexpr uint32_t PPSG_VERSION = 5;
static constexpr uint32_t PPSG_V1_MAX_TYPES = 36;  // MAX_PARTICLE_TYPES in version 1

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
    bool field_gravity, bool field_higgs, bool field_dark_energy,
    float field_intensity, float log_temperature)
{
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open())
        return { false, "Cannot open file for writing" };

    // Header
    write_val(f, PPSG_MAGIC);
    write_val(f, PPSG_VERSION);
    uint32_t saved_max_types = MAX_PARTICLE_TYPES;
    write_val(f, saved_max_types);

    // SimConfig (POD) — size-prefixed for forward compatibility
    uint32_t cfg_size = static_cast<uint32_t>(sizeof(SimConfig));
    write_val(f, cfg_size);
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
    if (field_higgs)       field_bits |= (1 << 4);
    if (field_dark_energy) field_bits |= (1 << 5);
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
    if (version < 1 || version > PPSG_VERSION) {
        r.message = "Unsupported version";
        return r;
    }

    // Determine saved type count
    uint32_t saved_max_types = PPSG_V1_MAX_TYPES;  // v1 default
    if (version >= 2) {
        read_val(f, saved_max_types);
        if (saved_max_types > MAX_PARTICLE_TYPES) saved_max_types = MAX_PARTICLE_TYPES;
    }

    // SimConfig — size-prefixed in v3+, raw in v1-v2
    if (version >= 3) {
        uint32_t saved_cfg_size = 0;
        read_val(f, saved_cfg_size);
        r.cfg = SimConfig{};  // default-initialize (new fields get defaults)
        uint32_t read_size = std::min(saved_cfg_size, static_cast<uint32_t>(sizeof(SimConfig)));
        f.read(reinterpret_cast<char*>(&r.cfg), read_size);
        // Skip any extra bytes if saved config was larger (future version)
        if (saved_cfg_size > sizeof(SimConfig))
            f.seekg(saved_cfg_size - sizeof(SimConfig), std::ios::cur);
        if (!f.good()) {
            r.message = "Failed to read config";
            return r;
        }
    } else {
        // v1-v2: SimConfig was written without size prefix.
        // New fields are appended at the end, so old layout is a prefix.
        // Read only the bytes the old save wrote (= sizeof(SimConfig) minus new fields).
        // Fields not in v1-v2: orbital params (36 bytes), fusion/fission v3 (56 bytes),
        // Casimir v4 (12 bytes), fissility v5 (4 bytes) = 108 bytes total.
        // Note: orbital + shell transition params were inserted before show_trails,
        // so v1-v2 load is approximate — new fields get defaults from SimConfig{}.
        r.cfg = SimConfig{};  // defaults for new fields
        uint32_t old_cfg_size = static_cast<uint32_t>(sizeof(SimConfig)) - 108u;
        f.read(reinterpret_cast<char*>(&r.cfg), old_cfg_size);
        if (!f.good()) {
            r.message = "Failed to read config";
            return r;
        }
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

    // Per-type data — read with backward compatibility
    // Allocate and zero-fill, then read saved portion
    r.forces.resize(MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES, 0.0f);
    r.colors.resize(MAX_PARTICLE_TYPES, glm::vec4(0.0f));
    r.behavior_flags.resize(MAX_PARTICLE_TYPES, 0);

    if (saved_max_types == MAX_PARTICLE_TYPES) {
        // Same size — direct read
        f.read(reinterpret_cast<char*>(r.forces.data()),
               MAX_PARTICLE_TYPES * MAX_PARTICLE_TYPES * sizeof(float));
        f.read(reinterpret_cast<char*>(r.colors.data()),
               MAX_PARTICLE_TYPES * sizeof(glm::vec4));
        f.read(reinterpret_cast<char*>(r.behavior_flags.data()),
               MAX_PARTICLE_TYPES * sizeof(uint32_t));
    } else {
        // Smaller saved size — read row-by-row for force matrix, then colors/flags
        // Force matrix: saved_max_types × saved_max_types
        for (uint32_t row = 0; row < saved_max_types; ++row) {
            f.read(reinterpret_cast<char*>(&r.forces[row * MAX_PARTICLE_TYPES]),
                   saved_max_types * sizeof(float));
        }
        // Colors
        f.read(reinterpret_cast<char*>(r.colors.data()),
               saved_max_types * sizeof(glm::vec4));
        // Behavior flags
        f.read(reinterpret_cast<char*>(r.behavior_flags.data()),
               saved_max_types * sizeof(uint32_t));
    }

    // Force objects
    read_val(f, r.force_object_count);
    r.force_objects.resize(MAX_FORCE_OBJECTS);
    f.read(reinterpret_cast<char*>(r.force_objects.data()),
           MAX_FORCE_OBJECTS * sizeof(ForceObject));

    // UI state
    uint8_t field_bits = 0;
    read_val(f, field_bits);
    r.field_em      = (field_bits & (1 << 0)) != 0;
    r.field_strong  = (field_bits & (1 << 1)) != 0;
    r.field_weak    = (field_bits & (1 << 2)) != 0;
    r.field_gravity = (field_bits & (1 << 3)) != 0;
    r.field_higgs       = (field_bits & (1 << 4)) != 0;
    r.field_dark_energy = (field_bits & (1 << 5)) != 0;
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

// ── Molecule export/import (.ppmol) ──────────────────────────────────────────

static constexpr uint32_t PPMOL_MAGIC   = 0x4D4F4C50;  // "PLOM" little-endian
static constexpr uint32_t PPMOL_VERSION = 2;  // v2 adds name field

SaveResult export_molecule(
    const std::string& filepath,
    const std::string& formula,
    const std::vector<MoleculeAtomData>& atoms,
    const std::vector<MoleculeBondData>& bonds,
    const std::string& name)
{
    std::ofstream f(filepath, std::ios::binary);
    if (!f.is_open())
        return { false, "Cannot open file for writing" };

    write_val(f, PPMOL_MAGIC);
    write_val(f, PPMOL_VERSION);

    // Formula string (length-prefixed)
    uint32_t flen = static_cast<uint32_t>(formula.size());
    write_val(f, flen);
    f.write(formula.data(), flen);

    // Name string (v2+, length-prefixed)
    uint32_t nlen = static_cast<uint32_t>(name.size());
    write_val(f, nlen);
    if (nlen > 0) f.write(name.data(), nlen);

    // Atom count + bond count
    uint32_t atom_count = static_cast<uint32_t>(atoms.size());
    uint32_t bond_count = static_cast<uint32_t>(bonds.size());
    write_val(f, atom_count);
    write_val(f, bond_count);

    // Per-atom data
    for (const auto& a : atoms) {
        write_val(f, a.Z);
        write_val(f, a.N);
        write_val(f, a.electrons);
        write_val(f, a.cx_offset);
        write_val(f, a.cy_offset);
        uint32_t pcount = static_cast<uint32_t>(a.particles.size());
        write_val(f, pcount);
        for (const auto& p : a.particles) {
            write_val(f, p.dx);
            write_val(f, p.dy);
            write_val(f, p.vx);
            write_val(f, p.vy);
            write_val(f, p.energy);
            write_val(f, p.type);
            f.write(reinterpret_cast<const char*>(p.genome), GENOME_SIZE * sizeof(float));
        }
    }

    // Bond data
    for (const auto& b : bonds) {
        write_val(f, b.atom_a);
        write_val(f, b.atom_b);
    }

    if (!f.good())
        return { false, "Write error" };

    return { true, "Molecule exported!" };
}

ImportMoleculeResult import_molecule(const std::string& filepath) {
    ImportMoleculeResult r;
    std::ifstream f(filepath, std::ios::binary);
    if (!f.is_open()) {
        r.message = "Cannot open file";
        return r;
    }

    uint32_t magic = 0, version = 0;
    read_val(f, magic);
    read_val(f, version);
    if (magic != PPMOL_MAGIC) {
        r.message = "Not a valid .ppmol file";
        return r;
    }
    if (version < 1 || version > PPMOL_VERSION) {
        r.message = "Unsupported .ppmol version";
        return r;
    }

    // Formula
    uint32_t flen = 0;
    read_val(f, flen);
    if (flen > 256) { r.message = "Formula too long"; return r; }
    r.formula.resize(flen);
    f.read(r.formula.data(), flen);

    // Name (v2+)
    if (version >= 2) {
        uint32_t nlen = 0;
        read_val(f, nlen);
        if (nlen > 256) { r.message = "Name too long"; return r; }
        if (nlen > 0) {
            r.name.resize(nlen);
            f.read(r.name.data(), nlen);
        }
    }

    // Counts
    uint32_t atom_count = 0, bond_count = 0;
    read_val(f, atom_count);
    read_val(f, bond_count);
    if (atom_count > 1000) { r.message = "Too many atoms"; return r; }
    if (bond_count > 10000) { r.message = "Too many bonds"; return r; }

    // Per-atom data
    r.atoms.resize(atom_count);
    for (uint32_t ai = 0; ai < atom_count; ++ai) {
        auto& a = r.atoms[ai];
        read_val(f, a.Z);
        read_val(f, a.N);
        read_val(f, a.electrons);
        read_val(f, a.cx_offset);
        read_val(f, a.cy_offset);
        uint32_t pcount = 0;
        read_val(f, pcount);
        if (pcount > 10000) { r.message = "Atom too large"; return r; }
        a.particles.resize(pcount);
        for (uint32_t pi = 0; pi < pcount; ++pi) {
            auto& p = a.particles[pi];
            read_val(f, p.dx);
            read_val(f, p.dy);
            read_val(f, p.vx);
            read_val(f, p.vy);
            read_val(f, p.energy);
            read_val(f, p.type);
            f.read(reinterpret_cast<char*>(p.genome), GENOME_SIZE * sizeof(float));
        }
    }

    // Bond data
    r.bonds.resize(bond_count);
    for (uint32_t bi = 0; bi < bond_count; ++bi) {
        read_val(f, r.bonds[bi].atom_a);
        read_val(f, r.bonds[bi].atom_b);
    }

    if (!f.good()) {
        r.message = "Truncated file";
        return r;
    }

    r.success = true;
    r.message = "Molecule imported!";
    return r;
}
