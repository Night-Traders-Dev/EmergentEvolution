#pragma once

#include "vulkan_context.h"
#include "types.h"
#include <string>
#include <array>

enum ParticleRenderMode : uint32_t {
    RENDER_PROCEDURAL = 0,  // current shader rendering (default)
    RENDER_TEXTURED   = 1,  // custom texture only
    RENDER_BLENDED    = 2,  // texture blended with procedural lighting
};

struct ParticleTextureManager {
    // 2D array image: [tex_size x tex_size x MAX_PARTICLE_TYPES layers]
    Image      array_image{};
    VkImageView array_view   = VK_NULL_HANDLE;
    VkSampler   tex_sampler  = VK_NULL_HANDLE;

    static constexpr uint32_t TEX_SIZE = 128;  // pixels per layer

    // Per-type render mode (CPU-authoritative, uploaded to GPU each frame)
    std::array<uint32_t, MAX_PARTICLE_TYPES> render_modes{};

    // Which types have a loaded custom texture
    std::array<bool, MAX_PARTICLE_TYPES> has_texture{};

    // Per-type blend factor for RENDER_BLENDED (0.0=all procedural, 1.0=all texture)
    std::array<float, MAX_PARTICLE_TYPES> blend_factors{};

    void init(VulkanContext& ctx);
    void destroy(VulkanContext& ctx);

    // Load textures from assets/particles/ (individual PNGs + optional atlas)
    void load_textures(VulkanContext& ctx);

    // Hot-reload a single type's texture at runtime
    void reload_texture(VulkanContext& ctx, uint32_t type_idx, const std::string& png_path);

    // Convert type name to expected PNG filename (lowercase, sanitized)
    static std::string type_to_filename(const char* name);

private:
    void upload_layer(VulkanContext& ctx, uint32_t layer,
                      const unsigned char* rgba_data, uint32_t w, uint32_t h);
};
