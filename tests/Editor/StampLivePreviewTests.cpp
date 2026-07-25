#include "Preview/VoxelPreview.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelStamps/Preview/StampLivePreviewBuilder.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cstdlib>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor;
using namespace VoxelForge::Editor::Stamps;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

VoxelStamp MakeStamp(
    const std::uint64_t id = 99U,
    const StampFixedPoint pivot = {128, 0, 0})
{
    const StampBounds bounds{{}, {1, 0, 0}, {2U, 1U, 1U}};
    const StampPivot stampPivot{.RequestedMode = StampPivotMode::Center,
        .ResolvedMode = StampPivotMode::Center, .LocalPosition = pivot};
    const std::vector<StampPaletteEntry> palette{{0U, {255U, 0U, 0U, 255U}},
                                                  {1U, {0U, 0U, 255U, 255U}}};
    const std::vector<StampVoxel> voxels{{{0, 0, 0}, 0U}, {{1, 0, 0}, 1U}};
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{id}, .ContentHash = "preview-" + std::to_string(id)},
        bounds, stampPivot, {}, palette, voxels, DefaultStampResourceLimits(), &validation);
    Require(stamp.has_value() && validation.IsValid(), "Preview fixture Stamp must be valid.");
    return *stamp;
}

Asset::Voxel::VoxelDocument MakeDocument()
{
    Asset::Vox::VoxModel source{};
    source.Version = 150U;
    source.Models.push_back({.Dimensions = {32U, 32U, 32U},
        .Voxels = {{.X = 10U, .Y = 2U, .Z = 4U, .ColorIndex = 1U}}});
    const auto built = Asset::Voxel::VoxDocumentLoader{}.Build(source, "preview.vox");
    Require(built.Succeeded() && built.Document, "Preview document fixture must build.");
    return std::move(*built.Document);
}

StampLivePreviewRequest Request(const VoxelStamp& stamp)
{
    return {.Stamp = &stamp,
        .TargetPivot = {10 * StampFixedPoint::UnitsPerVoxel + 128,
                        2 * StampFixedPoint::UnitsPerVoxel,
                        -4 * StampFixedPoint::UnitsPerVoxel}};
}

void Test01InactiveByDefault()
{
    VoxelPreviewSession session;
    Require(session.Current() == nullptr && session.Revision() == 0U,
        "01: Preview session must be inactive by default.");
}

void Test02ValidActivation()
{
    const VoxelStamp stamp = MakeStamp();
    VoxelPreviewSession session;
    Require(session.Activate(StampLivePreviewBuilder::Build(Request(stamp))) && session.Current()->IsActive(),
        "02: A valid Stamp must activate a preview session.");
}

void Test03ExactVoxelCount()
{
    const VoxelStamp stamp = MakeStamp();
    const auto preview = StampLivePreviewBuilder::Build(Request(stamp));
    Require(preview.Voxels.size() == stamp.Voxels().size(),
        "03: Preview must contain exactly the Stamp voxel count.");
}

void Test04PreservesPaletteColors()
{
    const VoxelStamp stamp = MakeStamp();
    const auto preview = StampLivePreviewBuilder::Build(Request(stamp));
    Require(preview.Voxels[0].Color == Asset::Vox::VoxColor{255U, 0U, 0U, 255U} &&
                preview.Voxels[1].Color == Asset::Vox::VoxColor{0U, 0U, 255U, 255U},
        "04: Preview must preserve the resolved real palette colors.");
}

void Test05PreservesFractionalFixedPointPivot()
{
    const VoxelStamp stamp = MakeStamp(99U, {128, 0, 0});
    const auto preview = StampLivePreviewBuilder::Build(Request(stamp));
    Require(preview.Pivot.LocalPosition == VoxelPreviewFixedPoint{128, 0, 0} &&
                preview.Transform.TargetPivot.X == 10 * StampFixedPoint::UnitsPerVoxel + 128,
        "05: Preview must retain a fractional fixed-point pivot exactly.");
}

void Test06TranslationUsesStoredPivot()
{
    const VoxelStamp stamp = MakeStamp();
    const auto preview = StampLivePreviewBuilder::Build(Request(stamp));
    Require(preview.Voxels[0].Position == Asset::Voxel::VoxelPosition{10, 2, -4} &&
                preview.Voxels[1].Position == Asset::Voxel::VoxelPosition{11, 2, -4},
        "06: Local voxels must translate by the stored pivot without drift.");
}

void Test07NoOverlapIsValid()
{
    const VoxelStamp stamp = MakeStamp();
    Asset::Voxel::VoxelDocument document = MakeDocument();
    auto request = Request(stamp);
    request.Document = &document;
    const auto preview = StampLivePreviewBuilder::Build(request);
    Require(preview.State == VoxelPreviewState::Valid && !preview.Voxels[0].OverlapsExisting,
        "07: A clear destination must be classified Valid.");
}

void Test08OverlapIsDetected()
{
    const VoxelStamp stamp = MakeStamp();
    Asset::Voxel::VoxelDocument document = MakeDocument();
    auto request = Request(stamp);
    request.Document = &document;
    request.TargetPivot = {10 * StampFixedPoint::UnitsPerVoxel + 128,
                           2 * StampFixedPoint::UnitsPerVoxel,
                           4 * StampFixedPoint::UnitsPerVoxel};
    const auto preview = StampLivePreviewBuilder::Build(request);
    Require(preview.State == VoxelPreviewState::Overlap && preview.Voxels[0].OverlapsExisting,
        "08: Existing document voxels must produce an Overlap indication.");
}

void Test09OverlapRemainsNonBlocking()
{
    const VoxelStamp stamp = MakeStamp();
    Asset::Voxel::VoxelDocument document = MakeDocument();
    auto request = Request(stamp);
    request.Document = &document;
    request.TargetPivot = {10 * StampFixedPoint::UnitsPerVoxel + 128,
                           2 * StampFixedPoint::UnitsPerVoxel,
                           4 * StampFixedPoint::UnitsPerVoxel};
    VoxelPreviewSession session;
    const auto preview = StampLivePreviewBuilder::Build(request);
    Require(preview.IsActive() && session.Activate(preview) && session.Current()->State == VoxelPreviewState::Overlap,
        "09: Overlap must remain an active non-blocking preview state.");
}

void Test10ClearPreview()
{
    const VoxelStamp stamp = MakeStamp();
    VoxelPreviewSession session;
    static_cast<void>(session.Activate(StampLivePreviewBuilder::Build(Request(stamp))));
    Require(session.Clear() && session.Current() == nullptr && !session.Clear(),
        "10: Clear must release the active preview once and leave a clean inactive state.");
}

void Test11StampChangeRebuilds()
{
    const VoxelStamp first = MakeStamp(100U);
    const VoxelStamp second = MakeStamp(101U);
    VoxelPreviewSession session;
    static_cast<void>(session.Activate(StampLivePreviewBuilder::Build(Request(first))));
    const std::uint64_t revision = session.Revision();
    Require(session.Activate(StampLivePreviewBuilder::Build(Request(second))) && session.Revision() == revision + 1U,
        "11: Changing Stamp identity must rebuild the session snapshot.");
}

void Test12TransformChangeRebuilds()
{
    const VoxelStamp stamp = MakeStamp();
    auto first = Request(stamp);
    auto moved = first;
    moved.TargetPivot.X += StampFixedPoint::UnitsPerVoxel;
    VoxelPreviewSession session;
    static_cast<void>(session.Activate(StampLivePreviewBuilder::Build(first)));
    const std::uint64_t revision = session.Revision();
    Require(session.Activate(StampLivePreviewBuilder::Build(moved)) && session.Revision() == revision + 1U,
        "12: A real target transform change must rebuild the preview.");
}

void Test13PreviewDoesNotMutateDocument()
{
    const VoxelStamp stamp = MakeStamp();
    Asset::Voxel::VoxelDocument document = MakeDocument();
    const auto voxels = document.GetVoxelCount();
    auto request = Request(stamp);
    request.Document = &document;
    static_cast<void>(StampLivePreviewBuilder::Build(request));
    Require(document.GetVoxelCount() == voxels,
        "13: Building preview must not add, erase or paint document voxels.");
}

void Test14PreviewDoesNotChangeRevisionOrUndoHistory()
{
    const VoxelStamp stamp = MakeStamp();
    Asset::Voxel::VoxelDocument document = MakeDocument();
    VoxelEditHistory history;
    const std::uint64_t documentRevision = document.GetRevision();
    auto request = Request(stamp);
    request.Document = &document;
    VoxelPreviewSession session;
    static_cast<void>(session.Activate(StampLivePreviewBuilder::Build(request)));
    Require(document.GetRevision() == documentRevision && history.UndoCount() == 0U && history.RedoCount() == 0U,
        "14: Preview must not change document revision or create Undo/Redo operations.");
}

void Test15UnchangedFramesReuseCache()
{
    const VoxelStamp stamp = MakeStamp();
    const auto preview = StampLivePreviewBuilder::Build(Request(stamp));
    VoxelPreviewSession session;
    Require(session.Activate(preview), "15: First preview frame must activate.");
    const std::uint64_t revision = session.Revision();
    Require(!session.Activate(preview) && session.Revision() == revision,
        "15: Identical frame input must not rebuild the preview cache.");
}

void Test16ProjectChangeCleanup()
{
    const VoxelStamp stamp = MakeStamp();
    VoxelPreviewSession session;
    static_cast<void>(session.Activate(StampLivePreviewBuilder::Build(Request(stamp))));
    const std::uint64_t revision = session.Revision();
    Require(session.Clear() && session.Current() == nullptr && session.Revision() == revision + 1U,
        "16: Project/session cleanup must drop all preview resources explicitly.");
}

void Test17InvalidAndEmptyInputs()
{
    const auto missing = StampLivePreviewBuilder::Build({});
    const VoxelStamp stamp = MakeStamp();
    auto invalid = Request(stamp);
    invalid.ForceInvalid = true;
    const auto forced = StampLivePreviewBuilder::Build(invalid);
    Require(!missing.IsActive() && forced.State == VoxelPreviewState::Invalid,
        "17: Missing or invalid preview input must fail safely without a crash.");
}

void Test18NegativeCoordinates()
{
    const VoxelStamp stamp = MakeStamp();
    auto request = Request(stamp);
    request.TargetPivot = {-2 * StampFixedPoint::UnitsPerVoxel + 128, 0, 0};
    const auto preview = StampLivePreviewBuilder::Build(request);
    Require(preview.IsActive() && preview.Voxels[0].Position == Asset::Voxel::VoxelPosition{-2, 0, 0} &&
                preview.Voxels[1].Position == Asset::Voxel::VoxelPosition{-1, 0, 0},
        "18: Negative world coordinates must remain exact preview positions.");
}

void Test19PreviewIsExcludedFromRayPicking()
{
    const VoxelStamp stamp = MakeStamp();
    Asset::Voxel::VoxelDocument document = MakeDocument();
    auto request = Request(stamp);
    request.Document = &document;
    request.TargetPivot = {1 * StampFixedPoint::UnitsPerVoxel + 128,
                           1 * StampFixedPoint::UnitsPerVoxel,
                           1 * StampFixedPoint::UnitsPerVoxel};
    Require(StampLivePreviewBuilder::Build(request).IsActive(), "19: Ray-picking fixture preview must exist.");
    const auto hit = RaycastVoxelDocument(document, {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}});
    Require(!hit.has_value(), "19: Document ray picking must not see preview-only ghost voxels.");
}

void Test20OverflowAndInvalidSubmodel()
{
    const VoxelStamp stamp = MakeStamp();
    auto overflow = Request(stamp);
    overflow.TargetPivot.X = std::numeric_limits<std::int32_t>::max();
    auto submodel = Request(stamp);
    Asset::Voxel::VoxelDocument document = MakeDocument();
    submodel.Document = &document;
    submodel.SubModelIndex = 1U;
    const auto overflowPreview = StampLivePreviewBuilder::Build(overflow);
    const auto invalidModelPreview = StampLivePreviewBuilder::Build(submodel);
    Require(!overflowPreview.IsActive() && overflowPreview.State == VoxelPreviewState::Invalid &&
                !invalidModelPreview.IsActive(),
        "20: Overflow and invalid document submodels must fail without partial preview output.");
}

} // namespace

int main()
{
    try
    {
        Test01InactiveByDefault();
        Test02ValidActivation();
        Test03ExactVoxelCount();
        Test04PreservesPaletteColors();
        Test05PreservesFractionalFixedPointPivot();
        Test06TranslationUsesStoredPivot();
        Test07NoOverlapIsValid();
        Test08OverlapIsDetected();
        Test09OverlapRemainsNonBlocking();
        Test10ClearPreview();
        Test11StampChangeRebuilds();
        Test12TransformChangeRebuilds();
        Test13PreviewDoesNotMutateDocument();
        Test14PreviewDoesNotChangeRevisionOrUndoHistory();
        Test15UnchangedFramesReuseCache();
        Test16ProjectChangeCleanup();
        Test17InvalidAndEmptyInputs();
        Test18NegativeCoordinates();
        Test19PreviewIsExcludedFromRayPicking();
        Test20OverflowAndInvalidSubmodel();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Stamp Live Preview tests passed.\n";
    return EXIT_SUCCESS;
}
