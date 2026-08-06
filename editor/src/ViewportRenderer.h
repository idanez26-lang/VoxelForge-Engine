#pragma once

#include "EditorCamera.h"
#include "Preview/VoxelPreview.h"
#include "Selection/SelectionService.h"
#include "SmartTools/SmartBrushPreviewResolver.h"
#include "Transform/TransformPreviewModel.h"
#include "TransformGizmo/TransformGizmoModel.h"
#include "ViewportDepthFormatPolicy.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelTools/VoxelBoxService.h"
#include "VoxelTools/VoxelSphereService.h"

#include "VoxelForge/Mesh/MeshData.h"
#include "VoxelForge/Asset/Voxel/VoxelDocument.h"
#include "VoxelForge/Voxel/VoxelPalette.h"

#include <cstdint>
#include <array>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <optional>
#include <memory>
#include <span>
#include <tuple>
#include <vector>

struct SDL_GPUBuffer;
struct SDL_GPUDevice;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUTexture;
struct SDL_GPUTransferBuffer;

namespace VoxelForge::Editor
{

namespace InteractionV2
{
struct MovePreviewPresentation;
}

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
    Moving,
    MovingPending,
    MovingInvalid
};

enum class SmartBrushGhostGeometryStyle : std::uint8_t
{
    VoxelBoxes,
    ExposedFaceSurface
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

    // VF-0262 (lot 262-4): chunked model presentation. The renderer keeps one
    // GPU buffer pair per chunk and a batch call patches only the chunks the
    // mesh cache rebuilt. A null or empty mesh removes the chunk. The chunked
    // and whole-mesh model paths are mutually exclusive: each upload owns the
    // model view and releases the other's buffers.
    struct ModelChunkId final
    {
        std::int32_t X = 0;
        std::int32_t Y = 0;
        std::int32_t Z = 0;

        [[nodiscard]] bool operator==(
            const ModelChunkId&) const noexcept = default;

        [[nodiscard]] bool operator<(
            const ModelChunkId& other) const noexcept
        {
            return std::tie(X, Y, Z) <
                std::tie(other.X, other.Y, other.Z);
        }
    };
    struct ModelChunkUpdate final
    {
        ModelChunkId Id{};
        const Mesh::MeshData* Mesh = nullptr;
    };

    // VF-0265 (lot 3f): preview overrides drawn in SUPERIMPOSITION over the
    // chunked model. A null or empty mesh hides the model's chunk — the case of
    // erasing a chunk's last voxel. The model's own buffers are never released
    // nor rewritten, so leaving the preview costs nothing at all.
    struct ExactPreviewChunkUpdate final
    {
        ModelChunkId Id{};
        const Mesh::MeshData* Mesh = nullptr;
        // LOT 4c : revision fournie par le compositeur. Egale a celle deja
        // presente dans le slot, l'envoi GPU est saute — c'est tout l'objet du
        // lot. Zero force l'envoi : un appelant qui ne renseigne pas ce champ
        // garde donc l'ancien comportement.
        std::uint64_t Revision = 0U;
    };

    [[nodiscard]] bool Upload(
        const Mesh::MeshData& mesh,
        const Voxel::VoxelPalette& palette,
        Vec3 modelCenter);
    // One call = one model upload operation (ModelUploadCount increments
    // once, like Upload). `clearExisting` refreshes the whole chunk set.
    [[nodiscard]] bool UploadModelChunks(
        std::span<const ModelChunkUpdate> updates,
        const Voxel::VoxelPalette& palette,
        Vec3 modelCenter,
        bool clearExisting);
    // Accepts prepared mesh data only.  The renderer never receives a document
    // or a SmartToolPlan and therefore cannot recalculate placement logic.
    [[nodiscard]] bool ConfigureExactPreviewMesh(
        const Mesh::MeshData* mesh,
        const Voxel::VoxelPalette* palette,
        Vec3 modelCenter,
        bool active,
        std::uint64_t documentIdentity,
        std::uint64_t documentRevision,
        std::uint64_t planId,
        std::uint64_t planRevision);
    // VF-0265 (lot 3f): chunked preview. Refuses when the model is not chunked
    // — there would be nothing to superimpose on, and a preview over a stale
    // monolithic model would be visibly wrong. `compositionId` is a monotonic
    // ordinal of real recompositions: identical id means identical geometry, so
    // nothing is re-uploaded. Exclusive with ConfigureExactPreviewMesh.
    [[nodiscard]] bool ConfigureExactPreviewChunks(
        std::span<const ExactPreviewChunkUpdate> overrides,
        const Voxel::VoxelPalette& palette,
        Vec3 modelCenter,
        bool active,
        std::uint64_t documentIdentity,
        std::uint64_t documentRevision,
        std::uint64_t compositionId);
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
        const VoxelPlacementPreview* smartBrushPreview,
        SmartBrushGhostGeometryStyle smartBrushGhostGeometryStyle,
        Vec3 modelCenter) noexcept;
    void ConfigureTransformPreview(
        const TransformPreviewRenderData* preview) noexcept;
    /// Dedicated presentation entry point for the isolated Selection + Move
    /// V2 vertical slice. The renderer consumes prepared data only.
    void ConfigureInteractionV2(
        std::span<const Asset::Voxel::VoxelPosition> selectedDetail,
        std::optional<SelectionBounds> selectionBounds,
        const InteractionV2::MovePreviewPresentation* movePreview,
        Vec3 modelCenter,
        std::uint64_t presentationRevision,
        bool active) noexcept;
    /// Consumes a prepared generic snapshot. It never receives a document or
    /// participates in picking, history, or asset mutation.
    void ConfigureVoxelPreview(const VoxelPreviewData* preview) noexcept;
    void ConfigureVoxelPlacementPreview(
        const VoxelPlacementPreview* preview) noexcept;
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
    [[nodiscard]] std::size_t InteractionV2UploadCount() const noexcept;
    [[nodiscard]] std::size_t InteractionV2BufferRecreationCount()
        const noexcept;
    [[nodiscard]] std::size_t InteractionV2UploadedBytes() const noexcept;
    [[nodiscard]] std::size_t InteractionV2MoveSourceUploadCount()
        const noexcept;
    [[nodiscard]] std::size_t InteractionV2MoveSourceUploadedBytes()
        const noexcept;
    [[nodiscard]] std::size_t InteractionV2MoveDeltaUpdateCount()
        const noexcept;
    [[nodiscard]] std::size_t InteractionV2MoveDrawCount() const noexcept;
    [[nodiscard]] std::size_t HighlightRenderCount() const noexcept;
    [[nodiscard]] std::size_t ModelRenderCount() const noexcept;
    [[nodiscard]] std::size_t ModelUploadCount() const noexcept;
    [[nodiscard]] bool HasModelMesh() const noexcept;
    [[nodiscard]] std::size_t ModelChunkCount() const noexcept;
    /// True while a Smart Tool exact final-state mesh overrides the document
    /// mesh. This remains true for a valid empty final state.
    [[nodiscard]] bool HasExactPreviewMesh() const noexcept;
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
    [[nodiscard]] bool UploadInteractionV2Highlights(
        const void* vertexData,
        std::size_t vertexBytes,
        const std::uint32_t* indexData,
        std::size_t indexBytes);
    [[nodiscard]] bool UploadInteractionV2MoveSource(
        const void* vertexData,
        std::size_t vertexBytes,
        const std::uint32_t* indexData,
        std::size_t indexBytes);
    [[nodiscard]] bool UploadMesh(
        const Mesh::MeshData& mesh,
        const Voxel::VoxelPalette& palette,
        Vec3 modelCenter,
        SDL_GPUBuffer*& vertexBuffer,
        SDL_GPUBuffer*& indexBuffer,
        std::uint32_t& indexCount,
        std::string_view label);
    void ClearExactPreviewMesh() noexcept;
    void ReleaseExactPreviewChunks() noexcept;
    // LOT 4c : relache les tampons de la preview MONOLITHIQUE seulement. La
    // preview chunkee doit pouvoir prendre la main sans detruire ses propres
    // overrides, ce que ClearExactPreviewMesh faisait indistinctement.
    void ReleaseExactPreviewMonolithic() noexcept;
    void ReleaseModelChunks() noexcept;
    void ReleaseWholeModelBuffers() noexcept;
    void ReleaseInteractionV2MoveSource() noexcept;
    void ReleaseGuides() noexcept;
    void ReleaseHighlights() noexcept;
    void ReleaseTargets() noexcept;
    void SetError(std::string message);

    SDL_GPUDevice* device_ = nullptr;
    ViewportDepthFormat depthFormat_ = ViewportDepthFormat::Unavailable;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* smartBrushGhostPipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* transformGizmoVisiblePipeline_ = nullptr;
    SDL_GPUGraphicsPipeline* transformGizmoOccludedPipeline_ = nullptr;
    struct ModelChunkBuffers final
    {
        SDL_GPUBuffer* VertexBuffer = nullptr;
        SDL_GPUBuffer* IndexBuffer = nullptr;
        std::uint32_t IndexCount = 0U;
        // LOT 4c : revision du CONTENU envoye dans ce slot. Zero signifie
        // « jamais envoye ». Sert uniquement aux overrides de preview.
        std::uint64_t Revision = 0U;
    };

    SDL_GPUBuffer* vertexBuffer_ = nullptr;
    SDL_GPUBuffer* indexBuffer_ = nullptr;
    std::map<ModelChunkId, ModelChunkBuffers> modelChunks_;
    SDL_GPUBuffer* exactPreviewVertexBuffer_ = nullptr;
    SDL_GPUBuffer* exactPreviewIndexBuffer_ = nullptr;
    SDL_GPUBuffer* guideVertexBuffer_ = nullptr;
    SDL_GPUBuffer* guideIndexBuffer_ = nullptr;
    SDL_GPUBuffer* highlightVertexBuffer_ = nullptr;
    SDL_GPUBuffer* highlightIndexBuffer_ = nullptr;
    SDL_GPUTransferBuffer* interactionV2HighlightTransferBuffer_ = nullptr;
    SDL_GPUBuffer* interactionV2MoveVertexBuffer_ = nullptr;
    SDL_GPUBuffer* interactionV2MoveIndexBuffer_ = nullptr;
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
    std::uint32_t exactPreviewIndexCount_ = 0;
    std::uint32_t gridIndexCount_ = 0;
    std::uint32_t axesIndexCount_ = 0;
    std::uint32_t highlightIndexCount_ = 0;
    std::uint32_t interactionV2MoveIndexCount_ = 0U;
    std::size_t interactionV2HighlightVertexCapacity_ = 0U;
    std::size_t interactionV2HighlightIndexCapacity_ = 0U;
    std::size_t interactionV2HighlightTransferCapacity_ = 0U;
    std::uint32_t smartBrushGhostIndexCount_ = 0;
    std::uint32_t transformGizmoVisibleIndexCount_ = 0;
    std::uint32_t transformGizmoOccludedIndexCount_ = 0;
    float guideWidth_ = 0.0F;
    float guideHeight_ = 0.0F;
    float guideDepth_ = 0.0F;
    bool guidesDirty_ = true;
    bool exactPreviewActive_ = false;
    std::uint64_t exactPreviewDocumentIdentity_ = 0U;
    std::uint64_t exactPreviewDocumentRevision_ = 0U;
    std::uint64_t exactPreviewPlanId_ = 0U;
    std::uint64_t exactPreviewPlanRevision_ = 0U;
    // VF-0265 (lot 3f) : overrides de preview par chunk. Une entrée à
    // IndexCount nul est délibérée : elle MASQUE le chunk du modèle, elle ne
    // l'oublie pas.
    std::map<ModelChunkId, ModelChunkBuffers> exactPreviewChunks_;
    bool exactPreviewChunksActive_ = false;
    std::uint64_t exactPreviewChunksCompositionId_ = 0U;
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
    SmartBrushGhostGeometryStyle smartBrushGhostGeometryStyle_ =
        SmartBrushGhostGeometryStyle::VoxelBoxes;
    std::vector<GhostVoxel> voxelPreviewGhosts_;
    std::uint64_t voxelPreviewRevision_ = 0U;
    std::uint64_t interactionV2Revision_ = 0U;
    std::uint64_t interactionV2MoveSourceIdentity_ = 0U;
    Asset::Voxel::VoxelPosition interactionV2MoveDelta_{};
    bool interactionV2MoveSourceDirty_ = false;
    bool interactionV2MoveActive_ = false;
    std::unique_ptr<HighlightGeometryCache> highlightGeometry_;
    std::unique_ptr<HighlightGeometryCache> interactionV2MoveGeometry_;
    std::unique_ptr<HighlightGeometryCache> smartBrushGhostGeometry_;
    std::unique_ptr<TransformPreviewSnapshot> transformPreview_;
    std::optional<TransformGizmoView> transformGizmo_;
    std::optional<VoxelSpherePreview> spherePreviewHighlight_;
    Vec3 modelCenter_{};
    std::size_t highlightUploadCount_ = 0U;
    std::size_t interactionV2UploadCount_ = 0U;
    std::size_t interactionV2BufferRecreationCount_ = 0U;
    std::size_t interactionV2UploadedBytes_ = 0U;
    std::size_t interactionV2MoveSourceUploadCount_ = 0U;
    std::size_t interactionV2MoveSourceUploadedBytes_ = 0U;
    std::size_t interactionV2MoveDeltaUpdateCount_ = 0U;
    std::size_t interactionV2MoveDrawCount_ = 0U;
    std::size_t highlightRenderCount_ = 0U;
    std::size_t modelRenderCount_ = 0U;
    std::size_t modelUploadCount_ = 0U;
    std::string lastError_;
};

} // namespace VoxelForge::Editor
