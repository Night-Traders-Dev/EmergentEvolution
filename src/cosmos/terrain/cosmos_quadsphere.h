#pragma once
// ── Quad-Sphere Mesh Generator ─────────────────────────────────────────────
// Generates sphere meshes by projecting 6 cube faces onto a unit sphere.
// Supports quadtree-based LOD for camera-distance-aware detail levels.
// Output: vertex/index buffers suitable for Vulkan rendering.

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <array>
#include <functional>

// ── Vertex format ──────────────────────────────────────────────────────────

struct QuadSphereVertex {
    glm::vec3 position;      // world-space position (on unit sphere × radius)
    glm::vec3 normal;        // surface normal (= normalized position for sphere)
    glm::vec2 uv;            // per-face UV coordinates [0,1]²
    glm::vec3 cube_coord;    // original cube-space coordinate (for 3D noise sampling)
    uint32_t  face_id;       // which cube face (0-5)
};

// ── Mesh output ────────────────────────────────────────────────────────────

struct QuadSphereMesh {
    std::vector<QuadSphereVertex> vertices;
    std::vector<uint32_t>         indices;

    void clear() { vertices.clear(); indices.clear(); }
    bool empty() const { return vertices.empty(); }
    size_t triangle_count() const { return indices.size() / 3; }
    size_t vertex_count() const { return vertices.size(); }
};

// ── Patch (quadtree node) ──────────────────────────────────────────────────

struct QuadSpherePatch {
    uint32_t face;            // cube face index (0-5)
    uint32_t depth;           // quadtree depth (0 = root)
    glm::vec2 uv_min;        // min UV on face grid [0,1]
    glm::vec2 uv_max;        // max UV on face grid [0,1]
    glm::vec3 center_sphere; // center of patch projected onto unit sphere
    float     angular_size;  // approximate angular extent (radians)

    // Children (null if leaf)
    bool has_children = false;
    std::array<int, 4> children{-1, -1, -1, -1}; // indices into patch array
};

// ── LOD parameters ─────────────────────────────────────────────────────────

struct QuadSphereLOD {
    uint32_t max_depth       = 7;      // max subdivision depth (7 = ~163k verts/face)
    uint32_t min_depth       = 2;      // minimum depth (always subdivide at least this far)
    uint32_t grid_resolution = 16;     // vertices per patch edge (NxN grid per leaf)
    float    split_factor    = 2.0f;   // split when angular_size / distance > split_factor / screen_height
    float    screen_height   = 1080.0f;
};

// ── Height function callback ───────────────────────────────────────────────
// Takes a 3D position on the unit sphere, returns height displacement.
// The mesh generator will call this for each vertex if provided.

using HeightFunc = std::function<float(const glm::vec3& sphere_pos, uint32_t seed)>;

// ── Main API ───────────────────────────────────────────────────────────────

namespace QuadSphere {

// Cube-to-sphere projection methods
enum class Projection {
    Normalize,      // Simple normalization (fastest, most distortion)
    Analytic,       // Analytic spherification (~1.3:1 area ratio, good balance)
};

// Project a cube-face point to sphere surface.
// face_uv is in [-1,1]², face is 0-5 (+X,-X,+Y,-Y,+Z,-Z).
glm::vec3 cube_to_sphere(glm::vec2 face_uv, uint32_t face,
                          Projection proj = Projection::Analytic);

// Generate a uniform mesh for a single cube face at fixed resolution.
// resolution = vertices per edge (e.g., 64 → 64×64 grid → 63×63×2 triangles).
QuadSphereMesh generate_face(uint32_t face, uint32_t resolution,
                              float radius = 1.0f,
                              Projection proj = Projection::Analytic,
                              HeightFunc height_fn = nullptr,
                              uint32_t seed = 0);

// Generate the complete sphere (6 faces) at uniform resolution.
QuadSphereMesh generate_sphere(uint32_t resolution,
                                float radius = 1.0f,
                                Projection proj = Projection::Analytic,
                                HeightFunc height_fn = nullptr,
                                uint32_t seed = 0);

// Generate an LOD-aware sphere mesh using quadtree subdivision.
// Patches closer to `camera_pos` are subdivided more.
QuadSphereMesh generate_lod_sphere(const glm::vec3& center,
                                    float radius,
                                    const glm::vec3& camera_pos,
                                    const QuadSphereLOD& lod,
                                    Projection proj = Projection::Analytic,
                                    HeightFunc height_fn = nullptr,
                                    uint32_t seed = 0);

// Get raw sphere-space positions for noise sampling.
// Returns N×N positions on the unit sphere for a given face, suitable for
// batch noise evaluation (e.g., FastNoise2::GenPositionArray3D).
void get_face_positions(uint32_t face, uint32_t resolution,
                        std::vector<float>& out_x,
                        std::vector<float>& out_y,
                        std::vector<float>& out_z,
                        Projection proj = Projection::Analytic);

} // namespace QuadSphere
