#pragma once

#include "EditorCamera.h"
#include "VoxelSelection/VoxelRaycast.h"

#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Voxel/VoxelPalette.h"

#include <cstdint>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <optional>

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
        const Voxel::VoxelPalette& palette,
        Vec3 modelCenter);
    void ConfigureGuides(float width, float height, float depth) noexcept;
    void ConfigureHighlights(
        std::optional<VoxelCoordinates> hovered,
        std::optional<VoxelCoordinates> selected,
        std::optional<VoxelCoordinates> addPreview,
        Vec3 modelCenter) noexcept;
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
    [[nodiscard]] std::size_t HighlightUploadCount() const noexcept;
    [[nodiscard]] std::size_t HighlightRenderCount() const noexcept;
    [[nodiscard]] std::size_t ModelRenderCount() const noexcept;

private:
    [[nodiscard]] bool EnsurePipeline();
    [[nodiscard]] bool EnsureGuides();
    [[nodiscard]] bool EnsureHighlights();
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
    void ReleaseHighlights() noexcept;
    void ReleaseTargets() noexcept;
    void SetError(std::string message);

    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUBuffer* vertexBuffer_ = nullptr;
    SDL_GPUBuffer* indexBuffer_ = nullptr;
    SDL_GPUBuffer* guideVertexBuffer_ = nullptr;
    SDL_GPUBuffer* guideIndexBuffer_ = nullptr;
    SDL_GPUBuffer* highlightVertexBuffer_ = nullptr;
    SDL_GPUBuffer* highlightIndexBuffer_ = nullptr;
    SDL_GPUTexture* colorTarget_ = nullptr;
    SDL_GPUTexture* depthTarget_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t indexCount_ = 0;
    std::uint32_t gridIndexCount_ = 0;
    std::uint32_t axesIndexCount_ = 0;
    std::uint32_t highlightIndexCount_ = 0;
    float guideWidth_ = 0.0F;
    float guideHeight_ = 0.0F;
    float guideDepth_ = 0.0F;
    bool guidesDirty_ = true;
    bool highlightsDirty_ = false;
    std::optional<VoxelCoordinates> hoveredHighlight_;
    std::optional<VoxelCoordinates> selectedHighlight_;
    std::optional<VoxelCoordinates> addPreviewHighlight_;
    Vec3 modelCenter_{};
    std::size_t highlightUploadCount_ = 0U;
    std::size_t highlightRenderCount_ = 0U;
    std::size_t modelRenderCount_ = 0U;
    std::string lastError_;
};

} // namespace VoxelForge::Editor
