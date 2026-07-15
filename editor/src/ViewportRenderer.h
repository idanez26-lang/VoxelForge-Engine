#pragma once

#include "EditorCamera.h"

#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Voxel/VoxelPalette.h"

#include <cstdint>
#include <string>

struct SDL_GPUBuffer;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUTexture;

namespace VoxelForge::Editor
{

class ViewportRenderer final
{
public:
    ~ViewportRenderer();

    [[nodiscard]] bool Upload(
        const Mesh::MeshData& mesh,
        const Voxel::VoxelPalette& palette);
    void ClearModel() noexcept;
    [[nodiscard]] bool Render(
        std::uint32_t width,
        std::uint32_t height,
        const EditorCamera& camera);
    void Shutdown() noexcept;

    [[nodiscard]] SDL_GPUTexture* Texture() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

private:
    [[nodiscard]] bool EnsurePipeline();
    [[nodiscard]] bool EnsureTargets(std::uint32_t width, std::uint32_t height);
    void ReleaseTargets() noexcept;
    void SetError(std::string message);

    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUBuffer* vertexBuffer_ = nullptr;
    SDL_GPUBuffer* indexBuffer_ = nullptr;
    SDL_GPUTexture* colorTarget_ = nullptr;
    SDL_GPUTexture* depthTarget_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t indexCount_ = 0;
    std::string lastError_;
};

} // namespace VoxelForge::Editor
