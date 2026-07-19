#include "TransformOperationFramework.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VoxelForge::Editor
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;
using Voxel = Asset::Voxel::Voxel;

[[nodiscard]] bool PositionLess(
    const Position left, const Position right) noexcept
{
    if (left.X != right.X) return left.X < right.X;
    if (left.Y != right.Y) return left.Y < right.Y;
    return left.Z < right.Z;
}

[[nodiscard]] bool InBounds(
    const Position position,
    const Asset::Voxel::VoxelDimensions dimensions) noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        position.X < static_cast<std::int32_t>(dimensions.X) &&
        position.Y < static_cast<std::int32_t>(dimensions.Y) &&
        position.Z < static_cast<std::int32_t>(dimensions.Z);
}

[[nodiscard]] SelectionBounds BoundsOf(
    const std::span<const Position> positions) noexcept
{
    if (positions.empty()) return {};
    Position minimum = positions.front();
    Position maximum = positions.front();
    for (const Position position : positions.subspan(1U))
    {
        minimum.X = std::min(minimum.X, position.X);
        minimum.Y = std::min(minimum.Y, position.Y);
        minimum.Z = std::min(minimum.Z, position.Z);
        maximum.X = std::max(maximum.X, position.X);
        maximum.Y = std::max(maximum.Y, position.Y);
        maximum.Z = std::max(maximum.Z, position.Z);
    }
    return SelectionBounds::FromCorners(minimum, maximum);
}

TransformOperationBuildResult Refused(
    const TransformOperationBuildCode code,
    std::string message)
{
    return {code, {}, std::move(message)};
}

[[nodiscard]] std::string Prefix(
    const std::string_view name, const std::string_view message)
{
    std::string result;
    result.reserve(name.size() + message.size());
    result.append(name);
    result.append(message);
    return result;
}
}

TransformOperationBuildResult TransformOperationBuilder::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    const TransformPreviewModel& preview,
    TransformOperationRequest request)
{
    const std::string_view name = request.Name.empty()
        ? std::string_view{"Transform"} : request.Name;
    if (!preview.IsActive())
        return Refused(TransformOperationBuildCode::InvalidPreview,
            Prefix(name, " preview is inactive."));
    if (preview.DocumentGeneration() != documentGeneration ||
        preview.DocumentRevision() != document.GetRevision())
        return Refused(TransformOperationBuildCode::ModelChanged,
            Prefix(name, " cancelled: model changed"));

    const TransformPreviewOperationData data = preview.OperationData();
    const auto selected = selection.Voxels();
    if (selection.DocumentGeneration() != documentGeneration ||
        selected.size() != data.SourcePositions.size() ||
        !std::equal(selected.begin(), selected.end(),
            data.SourcePositions.begin()))
        return Refused(TransformOperationBuildCode::SelectionChanged,
            Prefix(name, " cancelled: selection changed"));
    if (!preview.IsValidFor(document, selection, documentGeneration))
        return Refused(TransformOperationBuildCode::ModelChanged,
            Prefix(name, " cancelled: model changed"));
    const auto dimensions = document.GetDimensions(data.ModelIndex);
    if (!dimensions || request.Label.empty() ||
        data.SourceVoxels.empty() || data.SourcePositions.empty() ||
        data.SourceVoxels.size() != data.SourcePositions.size() ||
        data.Voxels.empty() || !data.SourceBounds.Valid ||
        !request.DestinationBounds.Valid)
        return Refused(TransformOperationBuildCode::InvalidPreview,
            Prefix(name, " preview data is incomplete."));

    // Duplicate's established zero-delta behavior is a no-op even though its
    // normal collision policy rejects occupied source cells.
    if (request.Policy.Source == TransformSourcePolicy::PreserveSource &&
        data.Voxels.size() == data.SourceVoxels.size() &&
        std::equal(data.Voxels.begin(), data.Voxels.end(),
            data.SourceVoxels.begin(),
            [](const TransformPreviewVoxel& destination,
               const TransformPreviewVoxel& source)
            {
                return destination.SourcePosition == source.SourcePosition &&
                    destination.PreviewPosition == source.SourcePosition &&
                    destination.Value == source.Value;
            }))
        return Refused(TransformOperationBuildCode::NoChange,
            Prefix(name, " does not change any voxel."));
    if (preview.HasOutOfBounds())
        return Refused(TransformOperationBuildCode::OutOfBounds,
            Prefix(name, " blocked: destination is outside the model"));

    try
    {
        std::unordered_map<
            Position, std::size_t, Asset::Voxel::VoxelPositionHash>
            sourceIndices;
        sourceIndices.reserve(data.SourcePositions.size());
        for (std::size_t index = 0U;
             index < data.SourcePositions.size(); ++index)
        {
            const TransformPreviewVoxel& captured = data.SourceVoxels[index];
            if (captured.SourcePosition != data.SourcePositions[index])
                return Refused(TransformOperationBuildCode::InvalidPreview,
                    Prefix(name,
                        " preview does not match its source snapshot."));
            const std::optional<Voxel> source = document.GetVoxel(
                captured.SourcePosition, data.ModelIndex);
            if (!source || *source != captured.Value)
                return Refused(TransformOperationBuildCode::ModelChanged,
                    Prefix(name, " cancelled: model changed"));
            if (!sourceIndices.emplace(captured.SourcePosition, index).second)
                return Refused(TransformOperationBuildCode::InvalidPreview,
                    Prefix(name,
                        " source positions are not unique."));
        }

        std::vector<std::pair<Position, Voxel>> destinations;
        destinations.reserve(data.Voxels.size());
        for (const TransformPreviewVoxel& voxel : data.Voxels)
        {
            const auto source = sourceIndices.find(voxel.SourcePosition);
            if (source == sourceIndices.end())
                return Refused(TransformOperationBuildCode::InvalidPreview,
                    Prefix(name,
                        " preview references an unknown source voxel."));
            if (data.SourceVoxels[source->second].Value != voxel.Value)
                return Refused(TransformOperationBuildCode::InvalidPreview,
                    Prefix(name,
                        " preview changed a captured voxel value."));
            if (!InBounds(voxel.PreviewPosition, *dimensions))
                return Refused(TransformOperationBuildCode::OutOfBounds,
                    Prefix(name,
                        " blocked: destination is outside the model"));

            const bool sourceOverlap =
                sourceIndices.contains(voxel.PreviewPosition);
            const bool occupied = document.HasVoxel(
                voxel.PreviewPosition, data.ModelIndex);
            if (occupied &&
                (request.Policy.Collision ==
                    TransformCollisionPolicy::RejectAnyOccupiedDestination ||
                 !sourceOverlap))
                return Refused(TransformOperationBuildCode::Collision,
                    Prefix(name, " blocked: destination is occupied"));
            destinations.emplace_back(voxel.PreviewPosition, voxel.Value);
        }

        std::sort(destinations.begin(), destinations.end(),
            [](const auto& left, const auto& right)
            {
                return PositionLess(left.first, right.first);
            });
        if (std::adjacent_find(destinations.begin(), destinations.end(),
                [](const auto& left, const auto& right)
                {
                    return left.first == right.first;
                }) != destinations.end())
            return Refused(TransformOperationBuildCode::InvalidDestinations,
                Prefix(name, " destinations are not unique."));

        std::vector<Position> destinationPositions;
        destinationPositions.reserve(destinations.size());
        for (const auto& destination : destinations)
            destinationPositions.push_back(destination.first);
        if (BoundsOf(destinationPositions) != request.DestinationBounds)
            return Refused(TransformOperationBuildCode::InvalidDestinations,
                Prefix(name,
                    " destination bounds do not match the preview."));

        std::vector<Position> affected;
        affected.reserve(destinations.size() +
            (request.Policy.Source == TransformSourcePolicy::RemoveSource
                ? data.SourcePositions.size() : 0U));
        if (request.Policy.Source == TransformSourcePolicy::RemoveSource)
            affected.insert(affected.end(),
                data.SourcePositions.begin(), data.SourcePositions.end());
        affected.insert(
            affected.end(), destinationPositions.begin(),
            destinationPositions.end());
        std::sort(affected.begin(), affected.end(), PositionLess);
        affected.erase(
            std::unique(affected.begin(), affected.end()), affected.end());

        VoxelEditOperation operation;
        operation.Label = std::move(request.Label);
        operation.Changes.reserve(affected.size());
        for (const Position position : affected)
        {
            const std::optional<Voxel> before =
                document.GetVoxel(position, data.ModelIndex);
            const auto destination = std::lower_bound(
                destinations.begin(), destinations.end(), position,
                [](const auto& item, const Position candidate)
                {
                    return PositionLess(item.first, candidate);
                });
            const bool hasAfter = destination != destinations.end() &&
                destination->first == position;
            const Voxel after = hasAfter ? destination->second : Voxel{};
            if (before.has_value() == hasAfter &&
                (!before || *before == after))
                continue;
            operation.Changes.push_back({
                data.ModelIndex, position,
                before.has_value(), before ? before->PaletteIndex : 0U,
                hasAfter, hasAfter ? after.PaletteIndex : 0U});
        }
        if (operation.Changes.empty())
            return Refused(TransformOperationBuildCode::NoChange,
                Prefix(name, " does not change any voxel."));

        auto transition = std::make_shared<VoxelEditSelectionTransition>();
        transition->Before = {
            documentGeneration,
            std::vector<Position>(data.SourcePositions.begin(),
                data.SourcePositions.end()),
            data.SourceBounds};
        transition->After = {
            documentGeneration, std::move(destinationPositions),
            request.DestinationBounds};
        operation.SelectionTransition = std::move(transition);
        return {TransformOperationBuildCode::Ready,
            std::move(operation), {}};
    }
    catch (const std::exception& exception)
    {
        return Refused(TransformOperationBuildCode::Failed,
            exception.what());
    }
    catch (...)
    {
        return Refused(TransformOperationBuildCode::Failed,
            {});
    }
}

const char* TransformOperationBuildCodeName(
    const TransformOperationBuildCode code) noexcept
{
    switch (code)
    {
    case TransformOperationBuildCode::Ready: return "Ready";
    case TransformOperationBuildCode::NoChange: return "No change";
    case TransformOperationBuildCode::InvalidPreview: return "Invalid preview";
    case TransformOperationBuildCode::InvalidDestinations:
        return "Invalid destinations";
    case TransformOperationBuildCode::ModelChanged: return "Model changed";
    case TransformOperationBuildCode::SelectionChanged:
        return "Selection changed";
    case TransformOperationBuildCode::Collision: return "Collision";
    case TransformOperationBuildCode::OutOfBounds: return "Out of bounds";
    case TransformOperationBuildCode::Failed: return "Failed";
    }
    return "Failed";
}

} // namespace VoxelForge::Editor
