#include "SmartTools/LegacySmartBrushPreview.h"

#include "SmartTools/SmartToolController.h"

#include <algorithm>

namespace VoxelForge::Editor
{
namespace
{
float ResolveAlpha(const float alpha) noexcept
{
    return std::clamp(alpha, 0.0F, 1.0F);
}

void AppendGhost(SmartBrushPreviewResult& result,
    const Asset::Voxel::VoxelPosition position, const GhostVoxelState state,
    const std::array<float, 4>& color, const float alpha)
{
    result.GhostVoxels.push_back({position, state, color, alpha});
}
}

bool LegacySmartBrushPreviewCacheKey::operator==(
    const LegacySmartBrushPreviewCacheKey& other) const noexcept
{
    return Document == other.Document && SubModelIndex == other.SubModelIndex &&
        DocumentRevision == other.DocumentRevision &&
        DocumentGeneration == other.DocumentGeneration && State == other.State &&
        GeometryKey == other.GeometryKey &&
        Placement.Target == other.Placement.Target &&
        Placement.Normal == other.Placement.Normal &&
        ActivePaletteColor == other.ActivePaletteColor && Alpha == other.Alpha;
}

SmartBrushPreviewResult LegacySmartBrushPreviewResolver::Resolve(
    const LegacySmartBrushPreviewRequest& request)
{
    const float alpha = ResolveAlpha(request.Alpha);
    if (request.State.Mode != SmartBrushMode::Paint)
    {
        SmartBrushPreviewResult result;
        result.Code = SmartBrushResultCode::InvalidRequest;
        result.Error = "The legacy Smart Brush preview is Paint-only.";
        AppendGhost(result, request.Placement.Target, GhostVoxelState::Invalid,
            GhostPreviewStyle::Invalid, alpha);
        return result;
    }
    if (request.Document == nullptr ||
        request.Document->GetModel(request.SubModelIndex) == nullptr)
    {
        SmartBrushPreviewResult result;
        result.Code = SmartBrushResultCode::InvalidRequest;
        result.Error = "The legacy Smart Brush preview has no editable document.";
        AppendGhost(result, request.Placement.Target, GhostVoxelState::Invalid,
            GhostPreviewStyle::Invalid, alpha);
        return result;
    }
    const auto dimensions = request.Document->GetDimensions(request.SubModelIndex);
    if (!dimensions)
    {
        SmartBrushPreviewResult result;
        result.Code = SmartBrushResultCode::InvalidRequest;
        result.Error = "The legacy Smart Brush preview has no valid dimensions.";
        AppendGhost(result, request.Placement.Target, GhostVoxelState::Invalid,
            GhostPreviewStyle::Invalid, alpha);
        return result;
    }

    SmartToolRequest planRequest;
    planRequest.Geometry = SmartGeometry::Pencil;
    planRequest.Action = SmartAction::Paint;
    planRequest.BrushRequest = {
        *dimensions, request.State, request.Placement, {}};
    planRequest.ReadVoxel =
        [document = request.Document, subModelIndex = request.SubModelIndex](
            const Asset::Voxel::VoxelPosition position)
    {
        const auto voxel = document->GetVoxel(position, subModelIndex);
        return SmartToolVoxelState{
            voxel.has_value(), voxel ? voxel->PaletteIndex : 0U};
    };
    planRequest.SourceIdentity =
        reinterpret_cast<std::uintptr_t>(request.Document);
    planRequest.SourceRevision = request.Document->GetRevision();
    planRequest.SourceSubModelIndex = request.SubModelIndex;

    SmartToolController controller;
    SmartToolSession session;
    const SmartToolResult planned =
        controller.ResolvePreview(session, planRequest);
    if (!planned.HasPlan())
    {
        SmartBrushPreviewResult result;
        result.Code = planned.Code;
        result.Error = planned.Error;
        AppendGhost(result, request.Placement.Target,
            GhostVoxelState::Invalid, GhostPreviewStyle::Invalid, alpha);
        return result;
    }
    return SmartBrushPreviewResolver::Resolve(
        *planned.Plan, request.ActivePaletteColor, alpha);
}

const SmartBrushPreviewResult& LegacySmartBrushPreviewCache::Resolve(
    const LegacySmartBrushPreviewCacheKey& key,
    const LegacySmartBrushPreviewRequest& request)
{
    if (!key_ || *key_ != key)
    {
        result_ = LegacySmartBrushPreviewResolver::Resolve(request);
        key_ = key;
        ++resolutionCount_;
    }
    return result_;
}

void LegacySmartBrushPreviewCache::Clear() noexcept
{
    key_.reset();
    result_ = {};
}

std::size_t LegacySmartBrushPreviewCache::ResolutionCount() const noexcept
{
    return resolutionCount_;
}
} // namespace VoxelForge::Editor
