#include "Transform/TransformPreviewModel.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using VoxelForge::Asset::Voxel::VoxelDocument;
using VoxelForge::Asset::Voxel::VoxelPosition;
using VoxelForge::Asset::Voxel::VoxDocumentLoader;
using VoxelForge::Editor::SelectionBounds;
using VoxelForge::Editor::SelectionMode;
using VoxelForge::Editor::SelectionService;
using VoxelForge::Editor::TransformPreviewModel;
using VoxelForge::Editor::TransformPreviewRenderPolicy;
using VoxelForge::Editor::TransformPreviewVoxelState;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

void AppendU32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
    bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void AppendChunk(
    std::vector<std::uint8_t>& destination,
    const std::array<char, 4U>& id,
    const std::vector<std::uint8_t>& content,
    const std::vector<std::uint8_t>& children = {})
{
    destination.insert(destination.end(), id.begin(), id.end());
    AppendU32(destination, static_cast<std::uint32_t>(content.size()));
    AppendU32(destination, static_cast<std::uint32_t>(children.size()));
    destination.insert(destination.end(), content.begin(), content.end());
    destination.insert(destination.end(), children.begin(), children.end());
}

class DocumentFixture final
{
public:
    DocumentFixture(
        const std::array<std::uint32_t, 3U> dimensions,
        const std::vector<std::array<std::uint8_t, 4U>>& voxels)
    {
        const auto unique =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
            ("VoxelForgeTransformPreview-" + std::to_string(unique));
        std::filesystem::create_directories(root_);
        path_ = root_ / "preview.vox";

        std::vector<std::uint8_t> size;
        AppendU32(size, dimensions[0]);
        AppendU32(size, dimensions[1]);
        AppendU32(size, dimensions[2]);
        std::vector<std::uint8_t> xyzi;
        AppendU32(xyzi, static_cast<std::uint32_t>(voxels.size()));
        for (const auto& voxel : voxels)
            xyzi.insert(xyzi.end(), voxel.begin(), voxel.end());
        std::vector<std::uint8_t> children;
        AppendChunk(children, {'S', 'I', 'Z', 'E'}, size);
        AppendChunk(children, {'X', 'Y', 'Z', 'I'}, xyzi);
        std::vector<std::uint8_t> bytes{'V', 'O', 'X', ' '};
        AppendU32(bytes, 150U);
        AppendChunk(bytes, {'M', 'A', 'I', 'N'}, {}, children);
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        Require(static_cast<bool>(output), "Unable to write preview fixture.");
        output.close();

        auto loaded = VoxDocumentLoader{}.Load(path_, "preview-asset");
        Require(loaded.Succeeded(), loaded.Message);
        document_ = std::make_unique<VoxelDocument>(
            std::move(*loaded.Document));
    }

    ~DocumentFixture()
    {
        document_.reset();
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }

    [[nodiscard]] VoxelDocument& Document() noexcept { return *document_; }

private:
    std::filesystem::path root_;
    std::filesystem::path path_;
    std::unique_ptr<VoxelDocument> document_;
};

SelectionService MakeSelection(
    const std::uint64_t generation,
    const std::vector<VoxelPosition>& positions)
{
    SelectionService selection;
    selection.SetDocumentGeneration(generation);
    static_cast<void>(selection.Apply(positions, SelectionMode::Replace));
    return selection;
}

std::vector<std::pair<VoxelPosition, std::uint8_t>> Snapshot(
    const VoxelDocument& document)
{
    std::vector<std::pair<VoxelPosition, std::uint8_t>> result;
    const auto* model = document.GetModel(0U);
    if (model)
        model->ForEachVoxel([&result](const auto position, const auto voxel)
        {
            result.emplace_back(position, voxel.PaletteIndex);
        });
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
    {
        if (left.first.X != right.first.X) return left.first.X < right.first.X;
        if (left.first.Y != right.first.Y) return left.first.Y < right.first.Y;
        return left.first.Z < right.first.Z;
    });
    return result;
}

void TestInactiveEmptyAndCaptureValidation()
{
    DocumentFixture fixture({8U, 8U, 8U}, {{{1U, 1U, 1U, 7U}}});
    TransformPreviewModel preview;
    SelectionService empty;
    empty.SetDocumentGeneration(5U);
    Require(!preview.IsActive() && preview.VoxelCount() == 0U &&
            !preview.BeginPreview(fixture.Document(), empty, 5U),
        "An inactive or empty preview must be explicit and harmless.");

    SelectionService missing = MakeSelection(5U, {{1, 1, 1}, {2, 2, 2}});
    Require(!preview.BeginPreview(fixture.Document(), missing, 5U) &&
            !preview.IsActive(),
        "A selection containing a missing voxel must be rejected atomically.");
}

void TestDeterministicCaptureColorsBoundsAndNoDuplicates()
{
    DocumentFixture fixture({8U, 8U, 8U},
        {{{4U, 3U, 2U, 11U}}, {{1U, 2U, 3U, 5U}}});
    SelectionService selection = MakeSelection(
        8U, {{4, 3, 2}, {1, 2, 3}, {4, 3, 2}});
    const auto selectionBefore = std::vector<VoxelPosition>(
        selection.Voxels().begin(), selection.Voxels().end());
    const auto documentBefore = Snapshot(fixture.Document());
    const std::uint64_t revision = fixture.Document().GetRevision();
    const bool dirty = fixture.Document().IsDirty();

    TransformPreviewModel preview;
    Require(preview.BeginPreview(fixture.Document(), selection, 8U) &&
            preview.VoxelCount() == 2U &&
            preview.SourcePositions()[0] == VoxelPosition{1, 2, 3} &&
            preview.SourcePositions()[1] == VoxelPosition{4, 3, 2} &&
            preview.Voxels()[0].Value.PaletteIndex == 5U &&
            preview.Voxels()[1].Value.PaletteIndex == 11U &&
            preview.SourceBounds() ==
                SelectionBounds::FromCorners({1, 2, 2}, {4, 3, 3}) &&
            preview.PreviewBounds() == preview.SourceBounds(),
        "Capture must be sorted, unique, colored and bounded deterministically.");
    Require(preview.IsValidFor(fixture.Document(), selection, 8U) &&
            Snapshot(fixture.Document()) == documentBefore &&
            fixture.Document().GetRevision() == revision &&
            fixture.Document().IsDirty() == dirty &&
            std::equal(selection.Voxels().begin(), selection.Voxels().end(),
                selectionBefore.begin()),
        "Capture must not modify the document or selection.");
}

void TestDeltasBoundsAndBufferReuse()
{
    DocumentFixture fixture({16U, 16U, 16U},
        {{{2U, 3U, 4U, 1U}}, {{5U, 7U, 9U, 2U}}});
    SelectionService selection = MakeSelection(10U, {{2, 3, 4}, {5, 7, 9}});
    TransformPreviewModel preview;
    Require(preview.BeginPreview(fixture.Document(), selection, 10U),
        "Delta fixture capture failed.");
    const auto initial = preview.Metrics();
    Require(preview.Delta() == VoxelPosition{} &&
            preview.SetDelta(fixture.Document(), selection, 10U, {3, 0, 0}) &&
            preview.PreviewBounds() ==
                SelectionBounds::FromCorners({5, 3, 4}, {8, 7, 9}),
        "Positive X delta or destination bounds failed.");
    Require(preview.SetDelta(fixture.Document(), selection, 10U, {-1, -2, -3}) &&
            preview.PreviewBounds() ==
                SelectionBounds::FromCorners({1, 1, 1}, {4, 5, 6}),
        "Negative combined delta failed.");
    const auto operation = preview.OperationData();
    Require(operation.DocumentGeneration == 10U &&
            operation.DocumentRevision == fixture.Document().GetRevision() &&
            operation.ModelIndex == 0U &&
            operation.Delta == VoxelPosition{-1, -2, -3} &&
            operation.Voxels.size() == 2U &&
            operation.SourcePositions.size() == 2U,
        "The future operation view must expose complete immutable input data.");
    const auto rebuilt = preview.Metrics();
    Require(!preview.SetDelta(
                fixture.Document(), selection, 10U, {-1, -2, -3}) &&
            preview.Metrics().RebuildCount == rebuilt.RebuildCount &&
            preview.Metrics().CapturedCapacity == initial.CapturedCapacity &&
            preview.Metrics().SourcePositionCapacity ==
                initial.SourcePositionCapacity,
        "An unchanged delta must not rebuild or reallocate capture buffers.");
}

void TestCollisionsInternalOverlapAndOutOfBounds()
{
    DocumentFixture fixture({8U, 8U, 8U},
        {{{0U, 0U, 0U, 1U}}, {{2U, 2U, 2U, 2U}},
         {{3U, 2U, 2U, 3U}}, {{4U, 2U, 2U, 4U}},
         {{5U, 2U, 2U, 5U}}, {{6U, 2U, 2U, 6U}}});
    SelectionService selection = MakeSelection(
        12U, {{2, 2, 2}, {3, 2, 2}, {4, 2, 2}});
    TransformPreviewModel preview;
    Require(preview.BeginPreview(fixture.Document(), selection, 12U) &&
            preview.SetDelta(fixture.Document(), selection, 12U, {1, 0, 0}) &&
            preview.HasCollisions() && preview.CollisionCount() == 1U &&
            preview.CollisionPositions()[0] == VoxelPosition{5, 2, 2},
        "Internal source overlap must be ignored while external collision remains.");
    Require(preview.SetDelta(fixture.Document(), selection, 12U, {2, 0, 0}) &&
            preview.CollisionCount() == 2U,
        "Multiple external collisions were not reported exactly.");

    SelectionService edge = MakeSelection(12U, {{0, 0, 0}});
    Require(preview.BeginPreview(fixture.Document(), edge, 12U) &&
            preview.SetDelta(fixture.Document(), edge, 12U, {-1, -2, -3}) &&
            preview.HasOutOfBounds() && preview.OutOfBoundsCount() == 1U &&
            preview.Voxels()[0].State ==
                TransformPreviewVoxelState::OutOfBounds,
        "Minimum out-of-bounds positions must remain explicit.");
    Require(preview.SetDelta(fixture.Document(), edge, 12U, {8, 8, 8}) &&
            preview.HasOutOfBounds(),
        "Maximum out-of-bounds positions must remain explicit.");
}

void TestValidityCancelResetAndNoMutation()
{
    DocumentFixture fixture({8U, 8U, 8U}, {{{1U, 1U, 1U, 7U}}});
    SelectionService selection = MakeSelection(20U, {{1, 1, 1}});
    TransformPreviewModel preview;
    const auto before = Snapshot(fixture.Document());
    const std::uint64_t revision = fixture.Document().GetRevision();
    Require(preview.BeginPreview(fixture.Document(), selection, 20U) &&
            !preview.IsValidFor(fixture.Document(), selection, 21U),
        "A generation change must invalidate the preview.");
    SelectionService changedSelection = MakeSelection(20U, {{1, 1, 1}});
    static_cast<void>(changedSelection.Clear());
    Require(!preview.IsValidFor(
                fixture.Document(), changedSelection, 20U),
        "A changed selection must invalidate the captured preview.");
    Require(fixture.Document().SetVoxel({2, 2, 2}, 8U).Changed &&
            !preview.IsValidFor(fixture.Document(), selection, 20U),
        "An incompatible document revision must invalidate the preview.");
    Require(preview.CancelPreview() && !preview.IsActive() &&
            preview.VoxelCount() == 0U,
        "CancelPreview must clear all temporary state.");
    static_cast<void>(fixture.Document().RemoveVoxel({2, 2, 2}));
    Require(Snapshot(fixture.Document()) == before &&
            fixture.Document().GetRevision() > revision,
        "Only explicit test mutations may affect the document.");
    preview.Reset();
    Require(!preview.IsActive() && preview.CollisionCount() == 0U &&
            preview.OutOfBoundsCount() == 0U,
        "Reset must be idempotent and empty.");
}

void TestLargeSelectionAndAdaptiveRenderPolicy()
{
    std::vector<std::array<std::uint8_t, 4U>> voxels;
    std::vector<VoxelPosition> selected;
    voxels.reserve(20U * 20U * 20U);
    selected.reserve(600U);
    for (std::uint8_t z = 0U; z < 20U; ++z)
        for (std::uint8_t y = 0U; y < 20U; ++y)
            for (std::uint8_t x = 0U; x < 20U; ++x)
            {
                voxels.push_back({x, y, z,
                    static_cast<std::uint8_t>((x + y + z) % 254U + 1U)});
                if (selected.size() < 600U)
                    selected.push_back({x, y, z});
            }
    DocumentFixture fixture({20U, 20U, 20U}, voxels);
    SelectionService selection = MakeSelection(30U, selected);
    TransformPreviewModel preview;
    Require(preview.BeginPreview(fixture.Document(), selection, 30U) &&
            preview.VoxelCount() == 600U,
        "Large deterministic capture failed.");
    const auto plan = preview.RenderData().Plan;
    Require(!plan.DrawIndividualVoxels &&
            plan.SourceVoxelCount == 600U &&
            TransformPreviewRenderPolicy::IndividualVoxelLimit == 512U,
        "Large previews must switch to the explicit bounds policy.");
    const auto capacity = preview.Metrics();
    Require(preview.SetDelta(fixture.Document(), selection, 30U, {1, 0, 0}),
        "Large preview delta failed.");
    const auto rebuilt = preview.Metrics();
    Require(rebuilt.CapturedCapacity == capacity.CapturedCapacity &&
            rebuilt.SourcePositionCapacity == capacity.SourcePositionCapacity &&
            rebuilt.CollisionCapacity == capacity.CollisionCapacity &&
            rebuilt.OutOfBoundsCapacity == capacity.OutOfBoundsCapacity,
        "Large delta must reuse every prepared buffer.");
}
}

int main()
{
    try
    {
        TestInactiveEmptyAndCaptureValidation();
        TestDeterministicCaptureColorsBoundsAndNoDuplicates();
        TestDeltasBoundsAndBufferReuse();
        TestCollisionsInternalOverlapAndOutOfBounds();
        TestValidityCancelResetAndNoMutation();
        TestLargeSelectionAndAdaptiveRenderPolicy();
        std::cout << "TransformPreview tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "TransformPreview tests failed: " << exception.what()
                  << '\n';
        return 1;
    }
}
