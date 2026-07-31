#include "ViewportInteractionV2/SelectionMoveInteractionHandler.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge;
using Position = Asset::Voxel::VoxelPosition;
namespace V2 = Editor::InteractionV2;

constexpr std::uint64_t Generation = 812U;

enum class Density
{
    Sparse,
    TenPercent,
    FiftyPercent,
    Full
};

std::string_view DensityName(const Density density)
{
    switch (density)
    {
    case Density::Sparse: return "sparse";
    case Density::TenPercent: return "10pct";
    case Density::FiftyPercent: return "50pct";
    case Density::Full: return "full";
    }
    return "unknown";
}

std::vector<Position> MakePositions(
    const std::uint32_t side,
    const Density density)
{
    const std::size_t cells =
        static_cast<std::size_t>(side) * side * side;
    std::vector<Position> result;
    if (density == Density::Sparse)
    {
        const std::size_t count = std::min<std::size_t>(64U, cells);
        result.reserve(count);
        if (count == 1U) return {{0, 0, 0}};
        for (std::size_t index = 0U; index < count; ++index)
        {
            const std::size_t linear =
                index * (cells - 1U) / (count - 1U);
            const std::size_t plane =
                static_cast<std::size_t>(side) * side;
            const std::size_t x = linear / plane;
            const std::size_t remainder = linear % plane;
            const std::size_t y = remainder / side;
            const std::size_t z = remainder % side;
            result.push_back({
                static_cast<std::int32_t>(x),
                static_cast<std::int32_t>(y),
                static_cast<std::int32_t>(z)});
        }
        return result;
    }

    const std::size_t numerator =
        density == Density::TenPercent ? 1U
        : density == Density::FiftyPercent ? 1U
        : 1U;
    const std::size_t denominator =
        density == Density::TenPercent ? 10U
        : density == Density::FiftyPercent ? 2U
        : 1U;
    result.reserve(cells * numerator / denominator + 2U);
    std::size_t linear = 0U;
    for (std::uint32_t x = 0U; x < side; ++x)
    {
        for (std::uint32_t y = 0U; y < side; ++y)
        {
            for (std::uint32_t z = 0U; z < side; ++z, ++linear)
            {
                if (linear % denominator < numerator ||
                    linear == 0U || linear + 1U == cells)
                {
                    result.push_back({
                        static_cast<std::int32_t>(x),
                        static_cast<std::int32_t>(y),
                        static_cast<std::int32_t>(z)});
                }
            }
        }
    }
    return result;
}

Asset::Voxel::VoxelDocument MakeDocument(
    const std::uint32_t side,
    const std::span<const Position> positions)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    const std::uint32_t width = side <= 128U ? side * 2U : side;
    model.Dimensions = {width, side, side};
    model.Voxels.reserve(positions.size());
    for (const Position position : positions)
    {
        model.Voxels.push_back({
            static_cast<std::uint8_t>(position.X),
            static_cast<std::uint8_t>(position.Y),
            static_cast<std::uint8_t>(position.Z),
            1U});
    }
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "viewport-interaction-v2-move-benchmark.vox");
    if (!loaded.Succeeded())
        throw std::runtime_error("Unable to build Move benchmark document.");
    return std::move(*loaded.Document);
}

double Milliseconds(
    const std::chrono::steady_clock::time_point begin,
    const std::chrono::steady_clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

void RunCase(const std::uint32_t side, const Density density)
{
    std::vector<Position> positions = MakePositions(side, density);
    auto document = MakeDocument(side, positions);
    Editor::SelectionService selection;
    selection.SetDocumentGeneration(Generation);
    const Editor::SelectionBounds bounds =
        Editor::SelectionBounds::FromCorners(
            {0, 0, 0},
            {static_cast<std::int32_t>(side - 1U),
             static_cast<std::int32_t>(side - 1U),
             static_cast<std::int32_t>(side - 1U)});
    static_cast<void>(selection.ApplySortedVolume(
        positions, bounds, Editor::SelectionMode::Replace));

    V2::SelectionMoveInteractionHandler handler;
    V2::ViewportInteractionMetrics metrics;
    auto selectionPlan = std::make_shared<V2::ScreenSelectionPlan>();
    selectionPlan->PlanId =
        static_cast<std::uint64_t>(side) * 10U +
        static_cast<std::uint64_t>(density);
    selectionPlan->DocumentGeneration = Generation;
    selectionPlan->DocumentRevision = document.GetRevision();
    selectionPlan->Voxels = positions;
    selectionPlan->Bounds = bounds;

    const auto activationBegin = std::chrono::steady_clock::now();
    const auto source = handler.CaptureMoveSource(
        document, selection, Generation, selectionPlan, metrics);
    const auto activationEnd = std::chrono::steady_clock::now();
    if (!source) throw std::runtime_error("Move source capture failed.");

    const Position delta = side <= 128U
        ? Position{static_cast<std::int32_t>(side), 0, 0}
        : Position{};
    const auto deltaBegin = std::chrono::steady_clock::now();
    const auto resolved = handler.ResolveMove(
        document, source, delta, 9001U, metrics);
    const auto deltaEnd = std::chrono::steady_clock::now();
    if (!resolved.Plan) throw std::runtime_error("Move resolve failed.");

    const auto repeatedBegin = std::chrono::steady_clock::now();
    const auto repeated = handler.ResolveMove(
        document, source, delta, 9002U, metrics);
    const auto repeatedEnd = std::chrono::steady_clock::now();
    if (!repeated.Plan || repeated.Plan != resolved.Plan)
        throw std::runtime_error("Move cache did not reuse the plan.");

    double commitMilliseconds = 0.0;
    bool committed = false;
    if (delta != Position{} && positions.size() <= 1'000'000U)
    {
        const auto commitBegin = std::chrono::steady_clock::now();
        const auto operation = handler.BuildCommit(
            document, selection, *resolved.Plan, metrics);
        const auto commitEnd = std::chrono::steady_clock::now();
        commitMilliseconds = Milliseconds(commitBegin, commitEnd);
        committed = operation.has_value();
    }

    const std::size_t cells =
        static_cast<std::size_t>(side) * side * side;
    std::cout
        << "side=" << side
        << " density=" << DensityName(density)
        << " box_cells=" << cells
        << " selected_voxels=" << positions.size()
        << " activation_ms=" << Milliseconds(
            activationBegin, activationEnd)
        << " delta_ms=" << Milliseconds(deltaBegin, deltaEnd)
        << " same_delta_ms=" << Milliseconds(
            repeatedBegin, repeatedEnd)
        << " commit_ms=" << commitMilliseconds
        << " commit_built=" << committed
        << " visited_drag=" << metrics.MoveSelectedCoordinatesVisited -
            metrics.MoveCommitCoordinatesMaterialized
        << " materialized_drag="
        << metrics.MoveDestinationCoordinatesMaterialized
        << " materialized_commit="
        << metrics.MoveCommitCoordinatesMaterialized
        << " collision_passes=" << metrics.MoveCollisionPasses
        << " collision_deferred=" << metrics.MoveCollisionDeferred
        << " cache_hits=" << metrics.MoveCollisionCacheHits
        << " plan_bytes=" << sizeof(V2::MovePreviewState)
        << '\n';
}
}

int main()
{
    try
    {
        for (const std::uint32_t side : {1U, 8U, 32U, 64U, 128U, 256U})
            RunCase(side, Density::Sparse);
        for (const std::uint32_t side : {8U, 32U, 64U, 128U, 256U})
            RunCase(side, Density::TenPercent);
        for (const std::uint32_t side : {8U, 32U, 64U})
            RunCase(side, Density::FiftyPercent);
        for (const std::uint32_t side : {1U, 8U, 32U, 64U})
            RunCase(side, Density::Full);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
