#include "ThumbnailTextureCache.h"

#include "ThumbnailImage.h"

#include "VoxelForge/Renderer/Renderer.h"

#include <SDL3/SDL_gpu.h>

#include <cstring>
#include <system_error>

namespace VoxelForge::Editor
{

ThumbnailTextureCache::~ThumbnailTextureCache()
{
    Clear();
}

SDL_GPUTexture* ThumbnailTextureCache::Get(
    const std::filesystem::path& thumbnailPath)
{
    if (thumbnailPath.empty()) return nullptr;
    std::error_code error;
    const std::filesystem::path absolute =
        std::filesystem::absolute(thumbnailPath, error).lexically_normal();
    if (error || !std::filesystem::is_regular_file(absolute, error) || error)
    {
        Invalidate(thumbnailPath);
        lastError_ = "Thumbnail cache file is unavailable.";
        return nullptr;
    }
    const std::uintmax_t size = std::filesystem::file_size(absolute, error);
    const auto modified = std::filesystem::last_write_time(absolute, error);
    if (error)
    {
        lastError_ = "Thumbnail cache timestamp is unavailable.";
        return nullptr;
    }
    const std::string key = absolute.generic_string();
    auto existing = textures_.find(key);
    if (existing != textures_.end() && existing->second.FileSize == size &&
        existing->second.ModifiedTime == modified)
        return existing->second.Texture;
    if (existing != textures_.end())
    {
        Release(existing->second);
        textures_.erase(existing);
    }
    SDL_GPUTexture* texture = Upload(absolute);
    if (texture != nullptr)
        textures_.emplace(key, CachedTexture{texture, size, modified});
    return texture;
}

void ThumbnailTextureCache::Invalidate(
    const std::filesystem::path& thumbnailPath) noexcept
{
    std::error_code error;
    const std::string key = std::filesystem::absolute(
        thumbnailPath, error).lexically_normal().generic_string();
    if (error) return;
    const auto entry = textures_.find(key);
    if (entry == textures_.end()) return;
    Release(entry->second);
    textures_.erase(entry);
}

void ThumbnailTextureCache::Clear() noexcept
{
    if (!textures_.empty() && Renderer::Renderer::HasGPUDevice())
        SDL_WaitForGPUIdle(Renderer::Renderer::GetGPUDevice());
    for (auto& [key, texture] : textures_)
    {
        static_cast<void>(key);
        Release(texture);
    }
    textures_.clear();
    lastError_.clear();
}

const std::string& ThumbnailTextureCache::LastError() const noexcept
{
    return lastError_;
}

SDL_GPUTexture* ThumbnailTextureCache::Upload(
    const std::filesystem::path& path)
{
    ThumbnailImage image;
    if (!ReadThumbnailImage(path, image, lastError_)) return nullptr;
    SDL_GPUDevice* device = Renderer::Renderer::GetGPUDevice();
    if (device == nullptr)
    {
        lastError_ = "GPU device is unavailable for thumbnail upload.";
        return nullptr;
    }
    const SDL_GPUTextureCreateInfo textureInfo{
        SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_SAMPLER,
        image.Width, image.Height, 1U, 1U,
        SDL_GPU_SAMPLECOUNT_1, 0U};
    SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &textureInfo);
    const SDL_GPUTransferBufferCreateInfo transferInfo{
        SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        static_cast<std::uint32_t>(image.Pixels.size()), 0U};
    SDL_GPUTransferBuffer* transfer =
        SDL_CreateGPUTransferBuffer(device, &transferInfo);
    if (texture == nullptr || transfer == nullptr)
    {
        if (texture != nullptr) SDL_ReleaseGPUTexture(device, texture);
        if (transfer != nullptr)
            SDL_ReleaseGPUTransferBuffer(device, transfer);
        lastError_ = std::string("Unable to create thumbnail GPU resources: ") +
            SDL_GetError();
        return nullptr;
    }
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (mapped == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTexture(device, texture);
        lastError_ = std::string("Unable to map thumbnail upload: ") +
            SDL_GetError();
        return nullptr;
    }
    std::memcpy(mapped, image.Pixels.data(), image.Pixels.size());
    SDL_UnmapGPUTransferBuffer(device, transfer);
    SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device);
    SDL_GPUCopyPass* copy = command ? SDL_BeginGPUCopyPass(command) : nullptr;
    if (copy == nullptr)
    {
        if (command != nullptr) SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTexture(device, texture);
        lastError_ = std::string("Unable to begin thumbnail upload: ") +
            SDL_GetError();
        return nullptr;
    }
    const SDL_GPUTextureTransferInfo source{
        transfer, 0U, image.Width, image.Height};
    const SDL_GPUTextureRegion destination{
        texture, 0U, 0U, 0U, 0U, 0U,
        image.Width, image.Height, 1U};
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    if (!SDL_SubmitGPUCommandBuffer(command))
    {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTexture(device, texture);
        lastError_ = std::string("Unable to submit thumbnail upload: ") +
            SDL_GetError();
        return nullptr;
    }
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    lastError_.clear();
    return texture;
}

void ThumbnailTextureCache::Release(CachedTexture& texture) noexcept
{
    if (texture.Texture != nullptr && Renderer::Renderer::HasGPUDevice())
        SDL_ReleaseGPUTexture(
            Renderer::Renderer::GetGPUDevice(), texture.Texture);
    texture.Texture = nullptr;
}

} // namespace VoxelForge::Editor
