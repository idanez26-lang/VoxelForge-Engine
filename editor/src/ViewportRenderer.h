#pragma once

#include "EditorCamera.h"

#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Voxel/VoxelPalette.h"

#include <cstdint>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

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
    void ConfigureGuides(float width, float height, float depth) noexcept;
    void ClearModel() noexcept;
    [[nodiscard]] bool Render(
        std::uint32_t width,
        std::uint32_t height,
        const EditorCamera& camera,
        bool showGrid,
        bool showAxes,
        const std::array<float, 4>& backgroundColor);
    void Shutdown() noexcept;

    [[nodiscard]] SDL_GPUTexture* Texture() const noexcept;
    [[nodiscard]] const std::string& LastError() const noexcept;

private:
    [[nodiscard]] bool EnsurePipeline();
    [[nodiscard]] bool EnsureGuides();
    [[nodiscard]] bool EnsureTargets(std::uint32_t width, std::uint32_t height);
    [[nodiscard]] bool UploadBufferPair(
        const void* vertexData,
        std::size_t vertexBytes,
        const std::uint32_t* indexData,
        std::size_t indexBytes,
        SDL_GPUBuffer*& vertexBuffer,
        SDL_GPUBuffer*& indexBuffer,
        std::string_view label);
    void ReleaseGuides() noexcept;
    void ReleaseTargets() noexcept;
    void SetError(std::string message);

    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUBuffer* vertexBuffer_ = nullptr;
    SDL_GPUBuffer* indexBuffer_ = nullptr;
    SDL_GPUBuffer* guideVertexBuffer_ = nullptr;
    SDL_GPUBuffer* guideIndexBuffer_ = nullptr;
    SDL_GPUTexture* colorTarget_ = nullptr;
    SDL_GPUTexture* depthTarget_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t indexCount_ = 0;
    std::uint32_t gridIndexCount_ = 0;
    std::uint32_t axesIndexCount_ = 0;
    float guideWidth_ = 0.0F;
    float guideHeight_ = 0.0F;
    float guideDepth_ = 0.0F;
    bool guidesDirty_ = true;
    std::string lastError_;
};

} // namespace VoxelForge::Editor
