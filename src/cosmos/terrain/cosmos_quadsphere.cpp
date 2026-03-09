#include "cosmos/terrain/cosmos_quadsphere.h"
#include <cmath>
#include <algorithm>

namespace QuadSphere {

// ── Cube face definitions ──────────────────────────────────────────────────
// Face 0: +X, Face 1: -X, Face 2: +Y, Face 3: -Y, Face 4: +Z, Face 5: -Z

static glm::vec3 face_origin(uint32_t face) {
    switch (face) {
    case 0: return { 1, 0, 0};  // +X
    case 1: return {-1, 0, 0};  // -X
    case 2: return { 0, 1, 0};  // +Y
    case 3: return { 0,-1, 0};  // -Y
    case 4: return { 0, 0, 1};  // +Z
    case 5: return { 0, 0,-1};  // -Z
    default: return {0, 0, 1};
    }
}

// Returns the two tangent axes for a face
static void face_axes(uint32_t face, glm::vec3& right, glm::vec3& up) {
    switch (face) {
    case 0: right = { 0, 0,-1}; up = { 0, 1, 0}; break; // +X
    case 1: right = { 0, 0, 1}; up = { 0, 1, 0}; break; // -X
    case 2: right = { 1, 0, 0}; up = { 0, 0, 1}; break; // +Y
    case 3: right = { 1, 0, 0}; up = { 0, 0,-1}; break; // -Y
    case 4: right = { 1, 0, 0}; up = { 0, 1, 0}; break; // +Z
    case 5: right = {-1, 0, 0}; up = { 0, 1, 0}; break; // -Z
    default: right = {1, 0, 0}; up = {0, 1, 0}; break;
    }
}

// ── Projection ─────────────────────────────────────────────────────────────

glm::vec3 cube_to_sphere(glm::vec2 face_uv, uint32_t face, Projection proj) {
    glm::vec3 origin = face_origin(face);
    glm::vec3 right, up;
    face_axes(face, right, up);

    // Cube point: origin + face_uv.x * right + face_uv.y * up
    glm::vec3 cube_pos = origin + face_uv.x * right + face_uv.y * up;

    if (proj == Projection::Normalize) {
        return glm::normalize(cube_pos);
    }

    // Analytic spherification for more uniform distribution
    // https://mathproofs.blogspot.com/2005/07/mapping-cube-to-sphere.html
    float x2 = cube_pos.x * cube_pos.x;
    float y2 = cube_pos.y * cube_pos.y;
    float z2 = cube_pos.z * cube_pos.z;

    glm::vec3 s;
    s.x = cube_pos.x * std::sqrt(std::max(0.0f, 1.0f - y2 * 0.5f - z2 * 0.5f + y2 * z2 / 3.0f));
    s.y = cube_pos.y * std::sqrt(std::max(0.0f, 1.0f - x2 * 0.5f - z2 * 0.5f + x2 * z2 / 3.0f));
    s.z = cube_pos.z * std::sqrt(std::max(0.0f, 1.0f - x2 * 0.5f - y2 * 0.5f + x2 * y2 / 3.0f));

    float len = glm::length(s);
    return len > 1e-8f ? s / len : glm::vec3(0, 0, 1);
}

// ── Single face mesh generation ────────────────────────────────────────────

QuadSphereMesh generate_face(uint32_t face, uint32_t resolution,
                              float radius, Projection proj,
                              HeightFunc height_fn, uint32_t seed) {
    QuadSphereMesh mesh;
    if (resolution < 2) resolution = 2;

    const uint32_t verts_per_edge = resolution;
    const uint32_t total_verts = verts_per_edge * verts_per_edge;
    mesh.vertices.reserve(total_verts);

    // Generate vertices
    for (uint32_t iy = 0; iy < verts_per_edge; ++iy) {
        for (uint32_t ix = 0; ix < verts_per_edge; ++ix) {
            float u = static_cast<float>(ix) / static_cast<float>(verts_per_edge - 1);
            float v = static_cast<float>(iy) / static_cast<float>(verts_per_edge - 1);

            // Map [0,1] → [-1,1] for cube face
            glm::vec2 face_uv(u * 2.0f - 1.0f, v * 2.0f - 1.0f);

            glm::vec3 sphere_pos = cube_to_sphere(face_uv, face, proj);
            float r = radius;
            if (height_fn) {
                r += height_fn(sphere_pos, seed);
            }

            // Compute cube coordinate for noise sampling
            glm::vec3 origin = face_origin(face);
            glm::vec3 right, up_dir;
            face_axes(face, right, up_dir);
            glm::vec3 cube_coord = origin + face_uv.x * right + face_uv.y * up_dir;

            QuadSphereVertex vert;
            vert.position = sphere_pos * r;
            vert.normal = sphere_pos; // unit sphere normal
            vert.uv = {u, v};
            vert.cube_coord = cube_coord;
            vert.face_id = face;
            mesh.vertices.push_back(vert);
        }
    }

    // Generate triangle indices (two triangles per quad cell)
    uint32_t quads_per_edge = verts_per_edge - 1;
    mesh.indices.reserve(quads_per_edge * quads_per_edge * 6);

    for (uint32_t iy = 0; iy < quads_per_edge; ++iy) {
        for (uint32_t ix = 0; ix < quads_per_edge; ++ix) {
            uint32_t i00 = iy * verts_per_edge + ix;
            uint32_t i10 = i00 + 1;
            uint32_t i01 = i00 + verts_per_edge;
            uint32_t i11 = i01 + 1;

            // Two triangles per quad (CCW winding for outward-facing)
            mesh.indices.push_back(i00);
            mesh.indices.push_back(i01);
            mesh.indices.push_back(i10);

            mesh.indices.push_back(i10);
            mesh.indices.push_back(i01);
            mesh.indices.push_back(i11);
        }
    }

    return mesh;
}

// ── Full sphere generation ─────────────────────────────────────────────────

QuadSphereMesh generate_sphere(uint32_t resolution, float radius,
                                Projection proj, HeightFunc height_fn,
                                uint32_t seed) {
    QuadSphereMesh combined;

    for (uint32_t face = 0; face < 6; ++face) {
        QuadSphereMesh face_mesh = generate_face(face, resolution, radius,
                                                  proj, height_fn, seed);
        uint32_t base_vertex = static_cast<uint32_t>(combined.vertices.size());

        combined.vertices.insert(combined.vertices.end(),
                                  face_mesh.vertices.begin(),
                                  face_mesh.vertices.end());

        for (uint32_t idx : face_mesh.indices) {
            combined.indices.push_back(base_vertex + idx);
        }
    }

    return combined;
}

// ── LOD sphere generation ──────────────────────────────────────────────────

// Internal: compute angular size of a patch on the unit sphere
static float patch_angular_size(glm::vec2 uv_min, glm::vec2 uv_max,
                                 uint32_t face, Projection proj) {
    glm::vec2 center_uv = (uv_min + uv_max) * 0.5f;
    glm::vec2 face_center(center_uv.x * 2.0f - 1.0f, center_uv.y * 2.0f - 1.0f);

    glm::vec2 corner_uv = uv_max;
    glm::vec2 face_corner(corner_uv.x * 2.0f - 1.0f, corner_uv.y * 2.0f - 1.0f);

    glm::vec3 c = cube_to_sphere(face_center, face, proj);
    glm::vec3 e = cube_to_sphere(face_corner, face, proj);

    return std::acos(std::clamp(glm::dot(c, e), -1.0f, 1.0f));
}

// Internal: recursively subdivide and generate patches
static void subdivide_and_generate(
    const glm::vec3& center, float radius, const glm::vec3& camera_pos,
    const QuadSphereLOD& lod, Projection proj,
    HeightFunc height_fn, uint32_t seed,
    uint32_t face, uint32_t depth,
    glm::vec2 uv_min, glm::vec2 uv_max,
    QuadSphereMesh& out_mesh)
{
    // Compute patch center on sphere
    glm::vec2 center_uv = (uv_min + uv_max) * 0.5f;
    glm::vec2 face_center(center_uv.x * 2.0f - 1.0f, center_uv.y * 2.0f - 1.0f);
    glm::vec3 sphere_center = cube_to_sphere(face_center, face, proj);
    glm::vec3 world_center = center + sphere_center * radius;

    float dist = glm::length(camera_pos - world_center);
    float ang_size = patch_angular_size(uv_min, uv_max, face, proj);

    // Decide whether to subdivide
    bool should_split = depth < lod.min_depth;
    if (!should_split && depth < lod.max_depth) {
        float screen_pixels = (ang_size * radius / std::max(dist, 0.01f)) * lod.screen_height;
        should_split = screen_pixels > lod.split_factor * static_cast<float>(lod.grid_resolution);
    }

    if (should_split) {
        // Subdivide into 4 children
        glm::vec2 mid = (uv_min + uv_max) * 0.5f;
        subdivide_and_generate(center, radius, camera_pos, lod, proj,
                               height_fn, seed, face, depth + 1,
                               uv_min, mid, out_mesh);
        subdivide_and_generate(center, radius, camera_pos, lod, proj,
                               height_fn, seed, face, depth + 1,
                               {mid.x, uv_min.y}, {uv_max.x, mid.y}, out_mesh);
        subdivide_and_generate(center, radius, camera_pos, lod, proj,
                               height_fn, seed, face, depth + 1,
                               {uv_min.x, mid.y}, {mid.x, uv_max.y}, out_mesh);
        subdivide_and_generate(center, radius, camera_pos, lod, proj,
                               height_fn, seed, face, depth + 1,
                               mid, uv_max, out_mesh);
        return;
    }

    // Leaf node: generate grid mesh for this patch
    const uint32_t res = lod.grid_resolution;
    const uint32_t base_vertex = static_cast<uint32_t>(out_mesh.vertices.size());

    for (uint32_t iy = 0; iy < res; ++iy) {
        for (uint32_t ix = 0; ix < res; ++ix) {
            float u = static_cast<float>(ix) / static_cast<float>(res - 1);
            float v = static_cast<float>(iy) / static_cast<float>(res - 1);

            // Map to this patch's UV range
            float patch_u = uv_min.x + u * (uv_max.x - uv_min.x);
            float patch_v = uv_min.y + v * (uv_max.y - uv_min.y);

            glm::vec2 face_uv(patch_u * 2.0f - 1.0f, patch_v * 2.0f - 1.0f);
            glm::vec3 sphere_pos = cube_to_sphere(face_uv, face, proj);

            float r = radius;
            if (height_fn) {
                r += height_fn(sphere_pos, seed);
            }

            glm::vec3 origin = face_origin(face);
            glm::vec3 right, up_dir;
            face_axes(face, right, up_dir);
            glm::vec3 cube_coord = origin + face_uv.x * right + face_uv.y * up_dir;

            QuadSphereVertex vert;
            vert.position = center + sphere_pos * r;
            vert.normal = sphere_pos;
            vert.uv = {patch_u, patch_v};
            vert.cube_coord = cube_coord;
            vert.face_id = face;
            out_mesh.vertices.push_back(vert);
        }
    }

    // Indices for this patch
    uint32_t quads = res - 1;
    for (uint32_t iy = 0; iy < quads; ++iy) {
        for (uint32_t ix = 0; ix < quads; ++ix) {
            uint32_t i00 = base_vertex + iy * res + ix;
            uint32_t i10 = i00 + 1;
            uint32_t i01 = i00 + res;
            uint32_t i11 = i01 + 1;

            out_mesh.indices.push_back(i00);
            out_mesh.indices.push_back(i01);
            out_mesh.indices.push_back(i10);

            out_mesh.indices.push_back(i10);
            out_mesh.indices.push_back(i01);
            out_mesh.indices.push_back(i11);
        }
    }
}

QuadSphereMesh generate_lod_sphere(const glm::vec3& center, float radius,
                                    const glm::vec3& camera_pos,
                                    const QuadSphereLOD& lod, Projection proj,
                                    HeightFunc height_fn, uint32_t seed) {
    QuadSphereMesh mesh;

    for (uint32_t face = 0; face < 6; ++face) {
        subdivide_and_generate(center, radius, camera_pos, lod, proj,
                               height_fn, seed, face, 0,
                               {0.0f, 0.0f}, {1.0f, 1.0f}, mesh);
    }

    return mesh;
}

// ── Batch position extraction for noise sampling ───────────────────────────

void get_face_positions(uint32_t face, uint32_t resolution,
                        std::vector<float>& out_x,
                        std::vector<float>& out_y,
                        std::vector<float>& out_z,
                        Projection proj) {
    uint32_t total = resolution * resolution;
    out_x.resize(total);
    out_y.resize(total);
    out_z.resize(total);

    for (uint32_t iy = 0; iy < resolution; ++iy) {
        for (uint32_t ix = 0; ix < resolution; ++ix) {
            float u = static_cast<float>(ix) / static_cast<float>(resolution - 1);
            float v = static_cast<float>(iy) / static_cast<float>(resolution - 1);

            glm::vec2 face_uv(u * 2.0f - 1.0f, v * 2.0f - 1.0f);
            glm::vec3 sp = cube_to_sphere(face_uv, face, proj);

            uint32_t idx = iy * resolution + ix;
            out_x[idx] = sp.x;
            out_y[idx] = sp.y;
            out_z[idx] = sp.z;
        }
    }
}

} // namespace QuadSphere
