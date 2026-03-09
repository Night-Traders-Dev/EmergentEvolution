#pragma once
// ── Cosmos Terrain Generator ───────────────────────────────────────────────
// CPU-side procedural terrain generation using FastNoise2.
// Generates heightmaps and terrain data for quad-sphere meshes.
// Designed to work with cosmos_quadsphere.h mesh generation.

#include "cosmos/terrain/cosmos_quadsphere.h"
#include "cosmos/cosmos_types.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <memory>

// Forward-declare FastNoise2 types (avoid pulling the full header everywhere)
namespace FastNoise { template<typename T> class SmartNode; class Generator; }

// ── Terrain layer types ────────────────────────────────────────────────────

enum class TerrainLayer : uint32_t {
    Continents,     // Large-scale landmass shapes (low freq FBm)
    Mountains,      // Ridge noise for mountain ranges
    Detail,         // High-frequency surface detail
    Craters,        // Impact crater overlay (cellular noise)
    Erosion,        // Hydraulic erosion simulation (domain-warped FBm)
    Volcanic,       // Volcanic hotspot features
    Count
};

// ── Terrain parameters (derived from CelestialBody) ────────────────────────

struct TerrainParams {
    uint32_t seed          = 0;
    float    radius        = 8.0f;     // body radius in sim units
    float    terrain_amp   = 0.02f;    // max displacement as fraction of radius
    float    terrain_freq  = 1.0f;     // base frequency multiplier
    float    ridge_amp     = 0.01f;    // mountain ridge strength
    float    crater_density = 0.0f;    // crater overlay strength (0-1)
    float    roughness     = 0.5f;     // surface roughness (affects detail layers)
    float    ocean_level   = 0.0f;     // normalized ocean level (0 = no ocean, 0.5 = half submerged)
    float    ice_coverage  = 0.0f;     // polar ice cap extent (0-1)
    float    volcanic_activity = 0.0f; // volcanic feature strength
    bool     is_gas_giant  = false;    // use banded noise instead of terrain
    bool     is_star       = false;    // use turbulent convection noise

    // Derived from CelestialBody + BodyVisualProperties
    static TerrainParams from_body(const CelestialBody& body);
};

// ── Heightmap result ───────────────────────────────────────────────────────

struct TerrainHeightmap {
    std::vector<float> heights;     // displacement values per vertex
    std::vector<float> moisture;    // moisture map for biome coloring (optional)
    std::vector<float> temperature_map; // local temperature variation (optional)
    uint32_t width  = 0;            // face resolution
    uint32_t height_res = 0;        // face resolution

    float at(uint32_t x, uint32_t y) const {
        return heights[y * width + x];
    }
};

// ── Terrain Generator ──────────────────────────────────────────────────────

class CosmosTerrain {
public:
    CosmosTerrain();
    ~CosmosTerrain();

    // Initialize noise generators (call once at startup)
    void init();

    // Generate a heightmap for one cube face of a body.
    // Positions are on the unit sphere; heights are world-space displacements.
    TerrainHeightmap generate_face_heightmap(
        uint32_t face, uint32_t resolution,
        const TerrainParams& params) const;

    // Generate heightmaps for all 6 faces
    std::array<TerrainHeightmap, 6> generate_all_faces(
        uint32_t resolution,
        const TerrainParams& params) const;

    // Create a HeightFunc callback for use with QuadSphere mesh generation.
    // This captures the terrain params and returns a lambda that can be
    // passed directly to QuadSphere::generate_sphere().
    HeightFunc make_height_func(const TerrainParams& params) const;

    // Generate a complete quad-sphere mesh with terrain applied.
    // Convenience method combining QuadSphere + noise in one call.
    QuadSphereMesh generate_terrain_mesh(
        uint32_t resolution,
        const TerrainParams& params,
        QuadSphere::Projection proj = QuadSphere::Projection::Analytic) const;

    // Generate an LOD terrain mesh centered at `center` viewed from `camera_pos`.
    QuadSphereMesh generate_lod_terrain_mesh(
        const glm::vec3& center,
        const glm::vec3& camera_pos,
        const TerrainParams& params,
        const QuadSphereLOD& lod = {},
        QuadSphere::Projection proj = QuadSphere::Projection::Analytic) const;

private:
    // Noise generator nodes (built once, reused across all terrain generation)
    struct NoiseNodes;
    std::unique_ptr<NoiseNodes> nodes_;

    // Internal: evaluate layered noise at sphere position
    float sample_terrain(const glm::vec3& sphere_pos,
                          const TerrainParams& params) const;
    float sample_gas_giant(const glm::vec3& sphere_pos,
                            const TerrainParams& params) const;
    float sample_star_surface(const glm::vec3& sphere_pos,
                               const TerrainParams& params) const;
};
