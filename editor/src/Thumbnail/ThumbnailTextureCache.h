#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

struct SDL_GPUTexture;

namespace VoxelForge::Editor
{

class ThumbnailTextureCache final
{
public:
    ~ThumbnailTextureCache();

    [[nodiscard]] SDL_GPUTexture* Get(
        const std::filesystem::path& thumbnailPath);
    void Invalidate(const std::filesystem::path& thumbnailPath) noexcept;
    void Clear() noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

private:
    struct CachedTexture final
    {
        SDL_GPUTexture* Texture = nullptr;
        std::uintmax_t FileSize = 0U;
        std::filesystem::file_time_type ModifiedTime{};
    };

    [[nodiscard]] SDL_GPUTexture* Upload(
        const std::filesystem::path& path);
    void Release(CachedTexture& texture) noexcept;

    std::unordered_map<std::string, CachedTexture> textures_;
    std::string lastError_;
};

} // namespace VoxelForge::Editor
