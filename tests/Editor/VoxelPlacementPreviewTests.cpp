#include "Preview/VoxelPreview.h"
#include "Transform/TransformPlacementPreviewAdapter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

[[nodiscard]] bool Near(const float left, const float right) noexcept
{
    return std::abs(left - right) < 0.0001F;
}

void TestImmutableInstancesStatisticsAndRevision()
{
    std::vector<VoxelPreviewInstance> instances{
        {{1, 2, 3},
         VoxelPreviewSemantic::Valid,
         {-1.0F, 0.25F, 2.0F, 1.0F},
         1.5F},
        {{4, 5, 6},
         VoxelPreviewSemantic::Overlap,
         {1.0F, 0.5F, 0.1F, 1.0F},
         0.5F},
        {{7, 8, 9},
         VoxelPreviewSemantic::Invalid,
         {1.0F, 0.0F, 0.0F, 1.0F},
         -1.0F}};
    const VoxelPlacementPreview preview =
        VoxelPlacementPreview::FromInstances(41U, std::move(instances));
    Require(
        preview.IsActive() && preview.Revision() == 41U &&
            preview.Instances().size() == 3U &&
            preview.Statistics().Total == 3U &&
            preview.Statistics().Valid == 1U &&
            preview.Statistics().Overlap == 1U &&
            preview.Statistics().Invalid == 1U &&
            preview.Statistics().IsConsistent() &&
            preview.RenderMode() == VoxelPreviewRenderMode::DetailedInstances &&
            preview.InstancesComplete(),
        "The common preview lost instances, semantics, statistics or mode.");
    Require(
        preview.Instances()[0].Color[0] == 0.0F &&
            preview.Instances()[0].Color[2] == 1.0F &&
            preview.Instances()[0].Alpha == 1.0F &&
            preview.Instances()[2].Alpha == 0.0F,
        "The renderer-neutral preview did not clamp presentation values.");

    const VoxelPlacementPreview advanced = preview.WithRevision(42U);
    Require(
        advanced.Revision() == 42U &&
            advanced.Instances().data() == preview.Instances().data() &&
            std::equal(
                advanced.Instances().begin(), advanced.Instances().end(),
                preview.Instances().begin()),
        "Advancing a preview revision copied or changed immutable instances.");
}

void TestEmptyAndLargePreviewPolicy()
{
    const VoxelPlacementPreview empty =
        VoxelPlacementPreview::FromInstances(1U, {});
    Require(
        !empty.IsActive() && empty.Instances().empty() &&
            empty.Statistics().IsConsistent(),
        "An empty common preview was not harmless.");

    std::vector<VoxelPreviewInstance> large;
    large.reserve(VoxelPlacementPreview::DetailedInstanceLimit + 1U);
    for (std::size_t index = 0U;
         index <= VoxelPlacementPreview::DetailedInstanceLimit; ++index)
    {
        large.push_back(
            {{static_cast<std::int32_t>(index), 0, 0},
             VoxelPreviewSemantic::Added,
             {0.2F, 0.9F, 0.4F, 1.0F},
             0.5F});
    }
    const VoxelPlacementPreview retained =
        VoxelPlacementPreview::FromInstances(9U, std::move(large));
    Require(
        retained.RenderMode() == VoxelPreviewRenderMode::AggregateBounds &&
            retained.InstancesComplete() &&
            retained.Instances().size() ==
                VoxelPlacementPreview::DetailedInstanceLimit + 1U,
        "A large exact preview did not select the aggregate render policy.");

    VoxelPreviewStats statistics;
    statistics.Total = 600U;
    statistics.Added = 500U;
    statistics.Ignored = 100U;
    const VoxelPlacementPreview aggregate =
        VoxelPlacementPreview::Aggregate(10U, statistics);
    Require(
        aggregate.IsActive() && aggregate.Instances().empty() &&
            !aggregate.InstancesComplete() &&
            aggregate.RenderMode() == VoxelPreviewRenderMode::AggregateBounds &&
            aggregate.Statistics().IsConsistent(),
        "An aggregate-only preview lost its exact statistics or policy.");
}

void TestVoxelPreviewAdapterAndStableSession()
{
    VoxelPreviewData source;
    source.SourceId = Core::UUID{7U};
    source.SourceRevision = "common-preview";
    source.State = VoxelPreviewState::Overlap;
    source.Voxels = {
        {{2, 3, 4}, {40U, 80U, 120U, 255U}, false},
        {{3, 3, 4}, {120U, 80U, 40U, 255U}, true}};
    source.Placement = BuildVoxelPlacementPreview(source);

    const auto instances = source.Placement.Instances();
    Require(
        instances.size() == source.Voxels.size() &&
            instances[0].Position == source.Voxels[0].Position &&
            instances[0].Semantic == VoxelPreviewSemantic::Valid &&
            instances[1].Semantic == VoxelPreviewSemantic::Overlap &&
            source.Placement.Statistics().Valid == 1U &&
            source.Placement.Statistics().Overlap == 1U &&
            Near(instances[0].Alpha, 0.52F) && Near(instances[1].Alpha, 0.52F),
        "The voxel preview adapter changed cells or overlap semantics.");

    VoxelPreviewSession session;
    Require(
        session.Activate(source) && session.Revision() == 1U &&
            session.Current() != nullptr &&
            session.Current()->Placement.Revision() == 1U &&
            !session.Activate(source) && session.Revision() == 1U,
        "An unchanged immutable preview advanced its session revision.");

    source.State = VoxelPreviewState::Invalid;
    source.Placement = BuildVoxelPlacementPreview(source);
    Require(
        session.Activate(source) && session.Revision() == 2U &&
            session.Current()->Placement.Statistics().Invalid == 2U,
        "A changed invalid preview did not advance or classify every cell.");
}

void TestTransformAdapterEquivalence()
{
    std::array<Asset::Voxel::VoxelColor, 256U> palette{};
    palette[7U] = {64U, 128U, 255U, 255U};
    const std::array<TransformPreviewVoxel, 3U> voxels{
        {{{1, 1, 1},
          {2, 1, 1},
          Asset::Voxel::Voxel{7U},
          TransformPreviewVoxelState::Valid},
         {{2, 1, 1},
          {3, 1, 1},
          Asset::Voxel::Voxel{7U},
          TransformPreviewVoxelState::Collision},
         {{3, 1, 1},
          {-1, 1, 1},
          Asset::Voxel::Voxel{7U},
          TransformPreviewVoxelState::OutOfBounds}}};
    const TransformPreviewRenderData source{
        77U,     false,
        voxels,  {},
        palette, {},
        {},      {},
        {},      TransformPreviewRenderPolicy::Build(3U, 3U, 1U, 1U)};

    const VoxelPlacementPreview preview =
        BuildTransformPlacementPreview(source);
    const auto instances = preview.Instances();
    Require(
        preview.Revision() == source.Revision && instances.size() == 3U &&
            instances[0].Position == voxels[0].PreviewPosition &&
            instances[1].Position == voxels[1].PreviewPosition &&
            instances[2].Position == voxels[2].PreviewPosition &&
            instances[0].Semantic == VoxelPreviewSemantic::Valid &&
            instances[1].Semantic == VoxelPreviewSemantic::Overlap &&
            instances[2].Semantic == VoxelPreviewSemantic::Invalid &&
            preview.Statistics().Valid == 1U &&
            preview.Statistics().Overlap == 1U &&
            preview.Statistics().Invalid == 1U,
        "The transform adapter changed prepared positions or states.");
    Require(
        Near(instances[0].Color[0], 64.0F / 255.0F) &&
            Near(instances[0].Color[1], 128.0F / 255.0F) &&
            Near(instances[0].Color[2], 1.0F) &&
            instances[1].Color ==
                std::array<float, 4U>{1.0F, 0.16F, 0.10F, 1.0F} &&
            instances[2].Color ==
                std::array<float, 4U>{1.0F, 0.56F, 0.08F, 1.0F},
        "The transform adapter changed existing semantic colors.");
}
}

int main()
{
    try
    {
        TestImmutableInstancesStatisticsAndRevision();
        TestEmptyAndLargePreviewPolicy();
        TestVoxelPreviewAdapterAndStableSession();
        TestTransformAdapterEquivalence();
        std::cout << "Voxel placement preview tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Voxel placement preview tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
