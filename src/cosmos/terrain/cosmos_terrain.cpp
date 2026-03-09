#include "cosmos/terrain/cosmos_terrain.h"
#include <FastNoise/FastNoise.h>
#include <cmath>
#include <algorithm>

// ── Noise node storage ─────────────────────────────────────────────────────

struct CosmosTerrain::NoiseNodes {
    // Continental shapes (low frequency FBm)
    FastNoise::SmartNode<FastNoise::Generator> continent_gen;

    // Mountain ridges (ridged multifractal)
    FastNoise::SmartNode<FastNoise::Generator> ridge_gen;

    // Fine detail (high frequency simplex FBm)
    FastNoise::SmartNode<FastNoise::Generator> detail_gen;

    // Crater overlay (cellular / Voronoi)
    FastNoise::SmartNode<FastNoise::Generator> crater_gen;

    // Domain warp for erosion-like features
    FastNoise::SmartNode<FastNoise::Generator> erosion_gen;

    // Gas giant banding (1D-biased simplex)
    FastNoise::SmartNode<FastNoise::Generator> band_gen;

    // Stellar surface turbulence
    FastNoise::SmartNode<FastNoise::Generator> stellar_gen;
};

// ── TerrainParams from CelestialBody ───────────────────────────────────────

TerrainParams TerrainParams::from_body(const CelestialBody& body) {
    TerrainParams p;
    p.seed = body.seed;
    p.radius = body.radius;

    // Use cached visuals if available
    if (body.visuals_valid) {
        const auto& v = body.cached_visuals;
        p.terrain_amp = v.terrain_amp;
        p.terrain_freq = v.terrain_freq;
        p.ridge_amp = v.ridge_amp;
        p.crater_density = v.crater_density;
        p.roughness = v.roughness;
        p.volcanic_activity = v.volcanic_activity;
    } else {
        // Reasonable defaults based on body type
        p.terrain_amp = 0.02f;
        p.terrain_freq = 1.0f;
        p.ridge_amp = 0.01f;
        p.crater_density = (body.type == CTYPE_ASTEROID) ? 0.8f : 0.1f;
        p.roughness = 0.5f;
    }

    // Ocean / ice from planet properties
    if (body.props_valid) {
        const auto& pp = body.cached_props;
        p.ocean_level = pp.ocean_coverage * 0.5f;
        p.ice_coverage = pp.has_ice_sheets ? pp.ice_sheet_coverage : 0.0f;
    }

    p.is_gas_giant = (body.type == CTYPE_PLANET && body.mass > 5.0e-4f); // ~Jupiter class
    p.is_star = is_star_type(body.type);

    return p;
}

// ── Init ───────────────────────────────────────────────────────────────────

CosmosTerrain::CosmosTerrain() = default;
CosmosTerrain::~CosmosTerrain() = default;

void CosmosTerrain::init() {
    nodes_ = std::make_unique<NoiseNodes>();

    // ── Continental noise: Simplex FBm, low frequency ──────────────────
    {
        auto simplex = FastNoise::New<FastNoise::Simplex>();
        auto fractal = FastNoise::New<FastNoise::FractalFBm>();
        fractal->SetSource(simplex);
        fractal->SetOctaveCount(5);
        fractal->SetLacunarity(2.0f);
        fractal->SetGain(0.5f);
        nodes_->continent_gen = fractal;
    }

    // ── Ridge noise: Simplex Ridged, medium frequency ──────────────────
    {
        auto simplex = FastNoise::New<FastNoise::Simplex>();
        auto fractal = FastNoise::New<FastNoise::FractalRidged>();
        fractal->SetSource(simplex);
        fractal->SetOctaveCount(4);
        fractal->SetLacunarity(2.12f);
        fractal->SetGain(0.5f);
        nodes_->ridge_gen = fractal;
    }

    // ── Detail noise: High-frequency Simplex FBm ───────────────────────
    {
        auto simplex = FastNoise::New<FastNoise::Simplex>();
        auto fractal = FastNoise::New<FastNoise::FractalFBm>();
        fractal->SetSource(simplex);
        fractal->SetOctaveCount(6);
        fractal->SetLacunarity(2.2f);
        fractal->SetGain(0.45f);
        nodes_->detail_gen = fractal;
    }

    // ── Crater noise: Cellular (Voronoi distance) ──────────────────────
    {
        auto cellular = FastNoise::New<FastNoise::CellularDistance>();
        auto fractal = FastNoise::New<FastNoise::FractalFBm>();
        fractal->SetSource(cellular);
        fractal->SetOctaveCount(2);
        fractal->SetLacunarity(2.5f);
        fractal->SetGain(0.4f);
        nodes_->crater_gen = fractal;
    }

    // ── Erosion noise: Domain-warped Simplex ───────────────────────────
    {
        auto simplex = FastNoise::New<FastNoise::Simplex>();
        auto warp = FastNoise::New<FastNoise::DomainWarpGradient>();
        warp->SetSource(simplex);
        warp->SetWarpAmplitude(30.0f);
        auto fractal = FastNoise::New<FastNoise::FractalFBm>();
        fractal->SetSource(warp);
        fractal->SetOctaveCount(4);
        nodes_->erosion_gen = fractal;
    }

    // ── Gas giant banding: Simplex FBm (sampled primarily along latitude) ─
    {
        auto simplex = FastNoise::New<FastNoise::Simplex>();
        auto fractal = FastNoise::New<FastNoise::FractalFBm>();
        fractal->SetSource(simplex);
        fractal->SetOctaveCount(6);
        fractal->SetLacunarity(1.8f);
        fractal->SetGain(0.55f);
        nodes_->band_gen = fractal;
    }

    // ── Stellar turbulence: Simplex FBm with domain warp ──────────────
    {
        auto simplex = FastNoise::New<FastNoise::Simplex>();
        auto warp = FastNoise::New<FastNoise::DomainWarpGradient>();
        warp->SetSource(simplex);
        warp->SetWarpAmplitude(60.0f);
        auto fractal = FastNoise::New<FastNoise::FractalFBm>();
        fractal->SetSource(warp);
        fractal->SetOctaveCount(5);
        fractal->SetLacunarity(2.0f);
        fractal->SetGain(0.5f);
        nodes_->stellar_gen = fractal;
    }
}

// ── Terrain sampling ───────────────────────────────────────────────────────

float CosmosTerrain::sample_terrain(const glm::vec3& sp,
                                     const TerrainParams& params) const {
    if (!nodes_) return 0.0f;

    int seed = static_cast<int>(params.seed);
    float freq = params.terrain_freq;

    // Scale sphere position by frequency for noise sampling
    float x = sp.x * freq;
    float y = sp.y * freq;
    float z = sp.z * freq;

    // Layer 1: Continental base
    float continent = nodes_->continent_gen->GenSingle3D(x, y, z, seed);

    // Layer 2: Mountain ridges (higher frequency)
    float ridge = nodes_->ridge_gen->GenSingle3D(
        x * 2.5f, y * 2.5f, z * 2.5f, seed + 1);

    // Layer 3: Fine detail
    float detail = nodes_->detail_gen->GenSingle3D(
        x * 8.0f, y * 8.0f, z * 8.0f, seed + 2);

    // Layer 4: Craters (if applicable)
    float craters = 0.0f;
    if (params.crater_density > 0.01f) {
        craters = nodes_->crater_gen->GenSingle3D(
            x * 4.0f, y * 4.0f, z * 4.0f, seed + 3);
        // Invert and shape for crater-like depressions
        craters = std::max(0.0f, 1.0f - std::abs(craters)) * -1.0f;
    }

    // Layer 5: Erosion (domain-warped, subtle)
    float erosion = nodes_->erosion_gen->GenSingle3D(
        x * 3.0f, y * 3.0f, z * 3.0f, seed + 4);

    // Combine layers
    float height = continent * params.terrain_amp * params.radius
                 + ridge * params.ridge_amp * params.radius
                 + detail * params.roughness * params.terrain_amp * params.radius * 0.15f
                 + craters * params.crater_density * params.terrain_amp * params.radius * 0.4f
                 + erosion * params.terrain_amp * params.radius * 0.08f;

    // Ocean floor clamping
    if (params.ocean_level > 0.01f) {
        float ocean_floor = -params.ocean_level * params.terrain_amp * params.radius;
        height = std::max(height, ocean_floor);
    }

    return height;
}

float CosmosTerrain::sample_gas_giant(const glm::vec3& sp,
                                       const TerrainParams& params) const {
    if (!nodes_) return 0.0f;

    int seed = static_cast<int>(params.seed);

    // Gas giants use latitude-biased banding
    // Sample primarily along Y (latitude) with some longitude variation
    float lat = std::asin(std::clamp(sp.y, -1.0f, 1.0f));
    float lon = std::atan2(sp.z, sp.x);

    // Band structure: high frequency along latitude, low along longitude
    float band_x = lon * 0.5f;
    float band_y = lat * 12.0f;  // strong banding
    float band_z = 0.0f;

    float bands = nodes_->band_gen->GenSingle3D(band_x, band_y, band_z, seed);

    // Storm features (large vortices)
    float storm = nodes_->continent_gen->GenSingle3D(
        sp.x * 2.0f, sp.y * 2.0f, sp.z * 2.0f, seed + 10);

    // Very subtle displacement (gas giants don't have solid terrain)
    return (bands * 0.3f + storm * 0.1f) * params.terrain_amp * params.radius * 0.1f;
}

float CosmosTerrain::sample_star_surface(const glm::vec3& sp,
                                          const TerrainParams& params) const {
    if (!nodes_) return 0.0f;

    int seed = static_cast<int>(params.seed);

    // Stellar convection cells and granulation
    float turbulence = nodes_->stellar_gen->GenSingle3D(
        sp.x * 3.0f, sp.y * 3.0f, sp.z * 3.0f, seed);

    // Fine granulation
    float granulation = nodes_->detail_gen->GenSingle3D(
        sp.x * 15.0f, sp.y * 15.0f, sp.z * 15.0f, seed + 20);

    return (turbulence * 0.6f + granulation * 0.15f) * params.terrain_amp * params.radius * 0.05f;
}

// ── Heightmap generation ───────────────────────────────────────────────────

TerrainHeightmap CosmosTerrain::generate_face_heightmap(
    uint32_t face, uint32_t resolution,
    const TerrainParams& params) const {

    TerrainHeightmap hm;
    hm.width = resolution;
    hm.height_res = resolution;
    uint32_t total = resolution * resolution;
    hm.heights.resize(total);

    // Get sphere positions for this face
    std::vector<float> px, py, pz;
    QuadSphere::get_face_positions(face, resolution, px, py, pz);

    // Sample terrain at each position
    for (uint32_t i = 0; i < total; ++i) {
        glm::vec3 sp(px[i], py[i], pz[i]);

        if (params.is_star) {
            hm.heights[i] = sample_star_surface(sp, params);
        } else if (params.is_gas_giant) {
            hm.heights[i] = sample_gas_giant(sp, params);
        } else {
            hm.heights[i] = sample_terrain(sp, params);
        }
    }

    return hm;
}

std::array<TerrainHeightmap, 6> CosmosTerrain::generate_all_faces(
    uint32_t resolution, const TerrainParams& params) const {

    std::array<TerrainHeightmap, 6> faces;
    for (uint32_t f = 0; f < 6; ++f) {
        faces[f] = generate_face_heightmap(f, resolution, params);
    }
    return faces;
}

// ── HeightFunc factory ─────────────────────────────────────────────────────

HeightFunc CosmosTerrain::make_height_func(const TerrainParams& params) const {
    // Capture `this` and params by value for the lambda
    const CosmosTerrain* self = this;
    TerrainParams p = params;

    return [self, p](const glm::vec3& sphere_pos, uint32_t /*seed*/) -> float {
        if (p.is_star) {
            return self->sample_star_surface(sphere_pos, p);
        } else if (p.is_gas_giant) {
            return self->sample_gas_giant(sphere_pos, p);
        } else {
            return self->sample_terrain(sphere_pos, p);
        }
    };
}

// ── Convenience mesh generators ────────────────────────────────────────────

QuadSphereMesh CosmosTerrain::generate_terrain_mesh(
    uint32_t resolution, const TerrainParams& params,
    QuadSphere::Projection proj) const {

    return QuadSphere::generate_sphere(
        resolution, params.radius, proj,
        make_height_func(params), params.seed);
}

QuadSphereMesh CosmosTerrain::generate_lod_terrain_mesh(
    const glm::vec3& center, const glm::vec3& camera_pos,
    const TerrainParams& params, const QuadSphereLOD& lod,
    QuadSphere::Projection proj) const {

    return QuadSphere::generate_lod_sphere(
        center, params.radius, camera_pos,
        lod, proj, make_height_func(params), params.seed);
}
