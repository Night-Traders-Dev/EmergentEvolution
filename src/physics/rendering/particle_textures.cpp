#include "particle_textures.h"
#include "physics/ui_data.h"
#include "stb_image.h"
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

// ── Name → filename conversion ───────────────────────────────────────────────

std::string ParticleTextureManager::type_to_filename(const char* name) {
    std::string s(name);
    // Lowercase
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    // Replace special characters
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == ' ')      out += '_';
        else if (c == '+')  out += "_plus";
        else if (c == '-')  out += "_minus";
        else if (c == '.')  { /* skip dots */ }
        else if (c == '\'') out += "_prime";
        else                out += c;
    }
    return out + ".png";
}

// ── Init ─────────────────────────────────────────────────────────────────────

void ParticleTextureManager::init(VulkanContext& ctx) {
    // Create 2D array image: TEX_SIZE x TEX_SIZE x MAX_PARTICLE_TYPES layers
    array_image = ctx.create_image_array(
        TEX_SIZE, TEX_SIZE, MAX_PARTICLE_TYPES,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    array_view = ctx.create_image_view_array(
        array_image.handle,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT,
        MAX_PARTICLE_TYPES);

    tex_sampler = ctx.create_sampler_linear();

    // Transition all layers to TRANSFER_DST so we can clear/upload
    ctx.transition_image_layout(array_image.handle,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, MAX_PARTICLE_TYPES);

    // Clear entire image to transparent (via a vkCmdClearColorImage)
    {
        VkCommandBuffer cmd = ctx.begin_single_command();
        VkClearColorValue clear_color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
        VkImageSubresourceRange range{};
        range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel   = 0;
        range.levelCount     = 1;
        range.baseArrayLayer = 0;
        range.layerCount     = MAX_PARTICLE_TYPES;
        vkCmdClearColorImage(cmd, array_image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);
        ctx.end_single_command(cmd);
    }

    // Transition to SHADER_READ_ONLY for sampling
    ctx.transition_image_layout(array_image.handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        0, MAX_PARTICLE_TYPES);

    // Defaults
    render_modes.fill(RENDER_PROCEDURAL);
    has_texture.fill(false);
    blend_factors.fill(0.5f);
}

// ── Destroy ──────────────────────────────────────────────────────────────────

void ParticleTextureManager::destroy(VulkanContext& ctx) {
    if (tex_sampler != VK_NULL_HANDLE)
        vkDestroySampler(ctx.device, tex_sampler, nullptr);
    if (array_view != VK_NULL_HANDLE)
        vkDestroyImageView(ctx.device, array_view, nullptr);
    // array_image.view is managed separately (array_view), clear it to avoid double-free
    array_image.view = VK_NULL_HANDLE;
    ctx.destroy_image(array_image);
    tex_sampler = VK_NULL_HANDLE;
    array_view  = VK_NULL_HANDLE;
}

// ── Upload a single layer ────────────────────────────────────────────────────

void ParticleTextureManager::upload_layer(VulkanContext& ctx, uint32_t layer,
                                           const unsigned char* rgba_data,
                                           uint32_t w, uint32_t h)
{
    // If source is not TEX_SIZE x TEX_SIZE, we need to resize.
    // For simplicity, use nearest-neighbor resize into a temp buffer.
    std::vector<unsigned char> resized;
    const unsigned char* src = rgba_data;

    if (w != TEX_SIZE || h != TEX_SIZE) {
        resized.resize(TEX_SIZE * TEX_SIZE * 4);
        for (uint32_t dy = 0; dy < TEX_SIZE; ++dy) {
            uint32_t sy = dy * h / TEX_SIZE;
            for (uint32_t dx = 0; dx < TEX_SIZE; ++dx) {
                uint32_t sx = dx * w / TEX_SIZE;
                uint32_t si = (sy * w + sx) * 4;
                uint32_t di = (dy * TEX_SIZE + dx) * 4;
                resized[di + 0] = rgba_data[si + 0];
                resized[di + 1] = rgba_data[si + 1];
                resized[di + 2] = rgba_data[si + 2];
                resized[di + 3] = rgba_data[si + 3];
            }
        }
        src = resized.data();
    }

    VkDeviceSize img_size = TEX_SIZE * TEX_SIZE * 4;

    // Create staging buffer
    const VkMemoryPropertyFlags HOST_PROPS =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    Buffer staging = ctx.create_buffer(img_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, HOST_PROPS);

    // Upload pixel data to staging
    void* mapped = nullptr;
    vkMapMemory(ctx.device, staging.memory, 0, img_size, 0, &mapped);
    std::memcpy(mapped, src, static_cast<size_t>(img_size));
    vkUnmapMemory(ctx.device, staging.memory);

    // Transition this layer to TRANSFER_DST
    ctx.transition_image_layout(array_image.handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        layer, 1);

    // Copy staging buffer → image layer
    {
        VkCommandBuffer cmd = ctx.begin_single_command();

        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel       = 0;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount     = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { TEX_SIZE, TEX_SIZE, 1 };

        vkCmdCopyBufferToImage(cmd, staging.handle, array_image.handle,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        ctx.end_single_command(cmd);
    }

    // Transition back to SHADER_READ_ONLY
    ctx.transition_image_layout(array_image.handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        layer, 1);

    ctx.destroy_buffer(staging);
}

// ── Load textures from assets/particles/ ─────────────────────────────────────

void ParticleTextureManager::load_textures(VulkanContext& ctx, const std::string& prefix) {
    const std::string base_dir = prefix + "assets/particles/";

    if (!fs::exists(base_dir)) {
        std::cerr << "[textures] assets/particles/ not found — all types procedural\n";
        return;
    }

    int loaded = 0;
    for (uint32_t t = 0; t < PHYS_PARTICLE_TYPES; ++t) {
        std::string filename = type_to_filename(PHYS_TYPE_NAMES[t]);
        std::string path = base_dir + filename;

        if (!fs::exists(path)) continue;

        int w, h, c;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 4);
        if (!data) {
            std::cerr << "[textures] Failed to load " << path << "\n";
            continue;
        }

        upload_layer(ctx, t, data, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        stbi_image_free(data);

        has_texture[t] = true;
        ++loaded;
    }

    // Default to textured rendering for types that have a custom texture
    for (uint32_t t = 0; t < MAX_PARTICLE_TYPES; ++t) {
        if (has_texture[t]) render_modes[t] = RENDER_TEXTURED;
    }

    if (loaded > 0)
        std::cerr << "[textures] Loaded " << loaded << " custom particle textures (set as default)\n";
}

// ── Hot-reload single texture ────────────────────────────────────────────────

void ParticleTextureManager::reload_texture(VulkanContext& ctx, uint32_t type_idx,
                                             const std::string& png_path)
{
    if (type_idx >= MAX_PARTICLE_TYPES) return;

    int w, h, c;
    unsigned char* data = stbi_load(png_path.c_str(), &w, &h, &c, 4);
    if (!data) {
        std::cerr << "[textures] Failed to load " << png_path << "\n";
        return;
    }

    vkQueueWaitIdle(ctx.queue);
    upload_layer(ctx, type_idx, data, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    stbi_image_free(data);

    has_texture[type_idx] = true;
    std::cerr << "[textures] Reloaded type " << type_idx << " from " << png_path << "\n";
}
