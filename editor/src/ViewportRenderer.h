#pragma once

#include "EditorCamera.h"
#include "Selection/SelectionService.h"
#include "SmartTools/SmartBrushPreviewResolver.h"
#include "Transform/TransformPreviewModel.h"
#include "TransformGizmo/TransformGizmoModel.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelTools/VoxelBoxService.h"
#include "VoxelTools/VoxelSphereService.h"

#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"
#include "VoxelForge/Voxel/VoxelPalette.h"

#include <cstdint>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <optional>
#include <memory>
#include <span>
#include <vector>

struct SDL_GPUBuffer;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUTexture;

namespace VoxelForge::Editor
{

enum class VoxelPlacementPreviewStyle
{
    PencilValid,
    PencilInvalid,
    PencilOccupied,
    Eraser
};

enum class SelectionBoxVisualState : std::uint8_t
{
    Normal,
    Hovered,
    Moving
};

class ViewportRenderer final
{
public:
    static constexpr bool TransformGizmoVisibleDepthTestEnabled =
        TransformGizmoRenderPolicy::VisiblePassDepthTestEnabled;
    static constexpr bool TransformGizmoVisibleDepthWriteEnabled =
        TransformGizmoRenderPolicy::VisiblePassDepthWriteEnabled;
    static constexpr bool TransformGizmoOccludedDepthTestEnabled =
        TransformGizmoRenderPolicy::OccludedPassDepthTestEnabled;
    static constexpr bool TransformGizmoOccludedDepthWriteEnabled =
        TransformGizmoRenderPolicy::OccludedPassDepthWriteEnabled;

    ViewportRenderer();
    ~ViewportRenderer();

    [[nodiscard]] bool Upload(
        const Mesh::MeshData& mesh,
        const Voxel::VoxelPalette& palette,
        Vec3 modelCenter);
    void ConfigureGuides(float width, float height, float depth) noexcept;
    void ConfigureHighlights(
        std::optional<VoxelCoordinates> hovered,
        std::span<const Asset::Voxel::VoxelPosition> selected,
        std::optional<VoxelBoxBounds> selectionBounds,
        std::optional<SelectionBounds> editableSelectionBounds,
        bool selectionToolStyle,
        SelectionBoxVisualState selectionBoxVisualState,
        std::optional<Asset::Voxel::VoxelPosition> placementPreview,
        VoxelPlacementPreviewStyle placementPreviewStyle,
        std::span<const Asset::Voxel::VoxelPosition> brushPreview,
        std::span<const Asset::Voxel::VoxelPosition> brushOccupiedPreview,
        std::optional<VoxelBoxBounds> brushAggregatePreview,
        std::optional<VoxelSpherePreview> brushAggregateSpherePreview,
        std::optional<VoxelBoxBounds> boxPreview,
        std::span<const Asset::Voxel::VoxelPosition> linePreview,
        std::optional<VoxelSpherePreview> spherePreview,
        std::span<const GhostVoxel> smartBrushGhostPreview,
        Vec3 modelCenter) noexcept;
    void ConfigureTransformPreview(
        const TransformPreviewRenderData* preview) noexcept;
    void ConfigureTransformGizmo(
        const TransformGizmoView* gizmo) noexcept;
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
    [[nodiscard]] std::size_t ModelUploadCount() const noexcept;
    [[nodiscard]] bool HasModelMesh() const noexcept;
    [[nodiscard]] bool HasHighlightMesh() const noexcept;
    [[nodiscard]] bool HasTransformPreview() const noexcept;
    [[nodiscard]] std::size_t TransformPreviewSourcePrimitiveCount()
        const noexcept;
    [[nodiscard]] std::size_t TransformPreviewDestinationPrimitiveCount()
        const noexcept;
    [[nodiscard]] std::size_t TransformPreviewCollisionPrimitiveCount()
        const noexcept;
    [[nodiscard]] bool HasTransformGizmo() const noexcept;
    [[nodiscard]] std::size_t TransformGizmoAxisPrimitiveCount()
        const noexcept;

private:
    struct HighlightGeometryCache;
    struct TransformPreviewSnapshot;

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
    SDL_GPUGraphicsPipeline* smartBrushGhostPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* transformGizmoVisiblePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* transformGizmoOccludedPipeline_ = nullptr;
    SDL_GPUBuffer* vertexBuffer_ = nullptr;
    SDL_GPUBuffer* indexBuffer_ = nullptr;
    SDL_GPUBuffer* guideVertexBuffer_ = nullptr;
    SDL_GPUBuffer* guideIndexBuffer_ = nullptr;
    SDL_GPUBuffer* highlightVertexBuffer_ = nullptr;
    SDL_GPUBuffer* highlightIndexBuffer_ = nullptr;
    SDL_GPUBuffer* smartBrushGhostVertexBuffer_ = nullptr;
    SDL_GPUBuffer* smartBrushGhostIndexBuffer_ = nullptr;
    SDL_GPUBuffer* transformGizmoVisibleVertexBuffer_ = nullptr;
    SDL_GPUBuffer* transformGizmoVisibleIndexBuffer_ = nullptr;
    SDL_GPUBuffer* transformGizmoOccludedVertexBuffer_ = nullptr;
    SDL_GPUBuffer* transformGizmoOccludedIndexBuffer_ = nullptr;
    SDL_GPUTexture* colorTarget_ = nullptr;
    SDL_GPUTexture* depthTarget_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t indexCount_ = 0;
    std::uint32_t gridIndexCount_ = 0;
    std::uint32_t axesIndexCount_ = 0;
    std::uint32_t highlightIndexCount_ = 0;
    std::uint32_t smartBrushGhostIndexCount_ = 0;
    std::uint32_t transformGizmoVisibleIndexCount_ = 0;
    std::uint32_t transformGizmoOccludedIndexCount_ = 0;
    float guideWidth_ = 0.0F;
    float guideHeight_ = 0.0F;
    float guideDepth_ = 0.0F;
    bool guidesDirty_ = true;
    bool highlightsDirty_ = false;
    std::optional<VoxelCoordinates> hoveredHighlight_;
    std::vector<Asset::Voxel::VoxelPosition> selectedHighlights_;
    std::optional<VoxelBoxBounds> selectionBoundsHighlight_;
    std::optional<SelectionBounds> editableSelectionBoundsHighlight_;
    bool selectionToolStyle_ = false;
    SelectionBoxVisualState selectionBoxVisualState_ =
        SelectionBoxVisualState::Normal;
    std::optional<Asset::Voxel::VoxelPosition> placementPreviewHighlight_;
    VoxelPlacementPreviewStyle placementPreviewStyle_ =
        VoxelPlacementPreviewStyle::PencilInvalid;
    std::vector<Asset::Voxel::VoxelPosition> brushPreviewHighlights_;
    std::vector<Asset::Voxel::VoxelPosition> brushOccupiedPreviewHighlights_;
    std::optional<VoxelBoxBounds> brushAggregatePreviewHighlight_;
    std::optional<VoxelSpherePreview> brushAggregateSpherePreviewHighlight_;
    std::optional<VoxelBoxBounds> boxPreviewHighlight_;
    std::vector<Asset::Voxel::VoxelPosition> linePreviewHighlights_;
    std::vector<GhostVoxel> smartBrushGhostPreview_;
    std::unique_ptr<HighlightGeometryCache> highlightGeometry_;
    std::unique_ptr<HighlightGeometryCache> smartBrushGhostGeometry_;
    std::unique_ptr<TransformPreviewSnapshot> transformPreview_;
    std::optional<TransformGizmoView> transformGizmo_;
    std::optional<VoxelSpherePreview> spherePreviewHighlight_;
    Vec3 modelCenter_{};
    std::size_t highlightUploadCount_ = 0U;
    std::size_t highlightRenderCount_ = 0U;
    std::size_t modelRenderCount_ = 0U;
    std::size_t modelUploadCount_ = 0U;
    std::string lastError_;
};

} // namespace VoxelForge::Editor
