#include "Layout/InspectorLayoutModel.h"
#include "Layout/EditorLayoutPersistence.h"
#include "Layout/PalettePanelLayout.h"

#include <iostream>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
using namespace VoxelForge::Editor;
namespace Asset = VoxelForge::Asset;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

const std::string& Value(
    const InspectorLayoutModel& model,
    const std::string_view label)
{
    for (const InspectorDiagnosticRow& row : model.Rows())
        if (row.Label == label) return row.Value;
    throw std::runtime_error("Missing stable Inspector diagnostic row.");
}

void RequireSameStructure(
    const InspectorLayoutModel& left,
    const InspectorLayoutModel& right)
{
    Require(left.StableRowCount() == InspectorLayoutModel::RowCount &&
            right.StableRowCount() == InspectorLayoutModel::RowCount,
        "Inspector row count changed with diagnostic state.");
    for (std::size_t index = 0U;
         index < InspectorLayoutModel::RowCount; ++index)
        Require(left.Rows()[index].Label == right.Rows()[index].Label,
            "Inspector labels changed with diagnostic state.");
    Require(left.StableHeight(18.0F, 8.0F) ==
            right.StableHeight(18.0F, 8.0F),
        "Inspector diagnostic height changed with state.");
    Require(!left.RequestsViewportResize() &&
            !right.RequestsViewportResize(),
        "Inspector diagnostics requested a viewport resize.");
}

VoxelRaycastHit Hit(
    const VoxelCoordinates position,
    const VoxelHitFace face,
    const Asset::Voxel::VoxelPosition adjacent,
    const std::uint64_t revision)
{
    VoxelRaycastHit hit;
    hit.Coordinates = position;
    hit.Face = face;
    hit.Distance = 12.3456F;
    hit.ColorIndex = 7U;
    hit.SubModelIndex = 2U;
    hit.AdjacentPosition = adjacent;
    hit.AdjacentWithinBounds = true;
    hit.DocumentRevision = revision;
    return hit;
}
}

int main()
{
    try
    {
        const std::filesystem::path testRoot =
            std::filesystem::temp_directory_path() /
            "VoxelForgeLayoutPersistenceTests";
        const std::filesystem::path localAppData = testRoot / "LocalAppData";
        const std::filesystem::path canonical =
            EditorLayoutPersistence::CanonicalPathFromLocalAppData(localAppData);
        Require(canonical.is_absolute() &&
                canonical == localAppData / "VoxelForge Studio" / "imgui.ini",
            "The canonical ImGui layout path is not absolute or stable.");
        const std::filesystem::path isolated =
            testRoot / "Smoke" / "layout" / "imgui.ini";
        EditorLayoutPersistence persistence(isolated);
        Require(persistence.Initialize() && persistence.Path() == isolated &&
                std::filesystem::is_directory(isolated.parent_path()),
            "The isolated layout directory was not created.");
        const char* const stablePointer = persistence.IniFilename();
        Require(stablePointer != nullptr &&
                std::filesystem::path(stablePointer) == isolated &&
                stablePointer == persistence.IniFilename(),
            "IniFilename does not have stable storage.");
        EditorLayoutPersistence relative("imgui.ini");
        Require(!relative.Initialize(),
            "A relative ImGui layout path should be rejected.");
        std::error_code cleanupError;
        std::filesystem::remove_all(testRoot, cleanupError);
        Require(!cleanupError,
            "The layout persistence test fixture could not be cleaned.");

        const PaletteGridLayout narrow = CalculatePaletteGridLayout(
            18.0F, 8.0F, 20.0F, 256U);
        const PaletteGridLayout wide = CalculatePaletteGridLayout(
            640.0F, 8.0F, 20.0F, 256U);
        const PaletteGridLayout emptyWidth = CalculatePaletteGridLayout(
            0.0F, 8.0F, 20.0F, 256U);
        const PaletteGridLayout emptyItems = CalculatePaletteGridLayout(
            320.0F, 8.0F, 20.0F, 0U);
        Require(narrow.IsValid() && narrow.ColumnCount == 1U &&
                narrow.SwatchSize == 18.0F,
            "A narrow Palette panel produced an invalid grid.");
        Require(wide.IsValid() && wide.ColumnCount > narrow.ColumnCount &&
                wide.SwatchSize >= 20.0F,
            "A wide Palette panel does not use its available width.");
        Require(emptyWidth.IsValid() && emptyWidth.ColumnCount == 1U &&
                emptyItems.IsValid() && emptyItems.ColumnCount == 1U,
            "Palette grid fallback allowed zero columns or invalid sizes.");

        const InspectorLayoutModel closed =
            InspectorLayoutModel::Build({});
        Require(Value(closed, "Document") == "--" &&
                Value(closed, "Hovered Voxel") == "--" &&
                Value(closed, "Status") == "Unavailable",
            "Closed-document diagnostics are not explicit placeholders.");

        InspectorLayoutInput noHit;
        noHit.HasDocument = true;
        noHit.DocumentName = std::string(200U, 'L') + ".vox";
        noHit.Revision = 41U;
        noHit.InteractionState = VoxelPickingInteractionState::NoHit;
        noHit.Tool = ActiveVoxelTool::Pencil;
        noHit.PaletteIndex = 9U;
        noHit.Placement = {
            VoxelPlacementPreviewStatus::TargetMissing,
            std::nullopt,
            VoxelPreviewTool::Pencil};
        const InspectorLayoutModel noHitModel =
            InspectorLayoutModel::Build(noHit);
        Require(Value(noHitModel, "Document").size() ==
                InspectorLayoutModel::MaximumValueCharacters &&
                Value(noHitModel, "Document").ends_with("...") &&
                Value(noHitModel, "Revision") == "41" &&
                Value(noHitModel, "Palette Index") == "9" &&
                Value(noHitModel, "Tool Target") == "--",
            "No-hit, long-text, or Pencil diagnostics are incorrect.");
        RequireSameStructure(closed, noHitModel);

        InspectorLayoutInput hitInput = noHit;
        hitInput.DocumentName = "Maison.vox";
        hitInput.Revision = 42U;
        hitInput.InteractionState = VoxelPickingInteractionState::Hit;
        hitInput.Hovered = Hit(
            {10U, 11U, 12U}, VoxelHitFace::PositiveY,
            {10, 12, 12}, hitInput.Revision);
        hitInput.Placement = {
            VoxelPlacementPreviewStatus::Valid,
            Asset::Voxel::VoxelPosition{10, 12, 12},
            VoxelPreviewTool::Pencil};
        hitInput.LastPencilResult = {
            VoxelToolResultCode::Applied, true, {10, 12, 12},
            41U, 42U, {}};
        const InspectorLayoutModel validHit =
            InspectorLayoutModel::Build(hitInput);
        Require(Value(validHit, "Hovered Voxel") == "Hit" &&
                Value(validHit, "Position") == "10, 11, 12" &&
                Value(validHit, "Face") == "+Y" &&
                Value(validHit, "Adjacent") == "10, 12, 12" &&
                Value(validHit, "Tool Target") == "10, 12, 12" &&
                Value(validHit, "Status") == "Hit",
            "Valid-hit or construction-plane target diagnostics are incorrect.");
        RequireSameStructure(noHitModel, validHit);

        InspectorLayoutInput changedFace = hitInput;
        changedFace.Hovered = Hit(
            {20U, 21U, 22U}, VoxelHitFace::NegativeX,
            {19, 21, 22}, 43U);
        changedFace.Revision = 43U;
        const InspectorLayoutModel changedFaceModel =
            InspectorLayoutModel::Build(changedFace);
        Require(Value(changedFaceModel, "Position") == "20, 21, 22" &&
                Value(changedFaceModel, "Face") == "-X" &&
                Value(changedFaceModel, "Revision") == "43",
            "Face, position, or revision changes were not represented.");
        RequireSameStructure(validHit, changedFaceModel);

        InspectorLayoutInput invalidTarget = hitInput;
        invalidTarget.Placement = {
            VoxelPlacementPreviewStatus::OutOfBounds,
            Asset::Voxel::VoxelPosition{-1, 0, 0},
            VoxelPreviewTool::Pencil};
        const InspectorLayoutModel invalidTargetModel =
            InspectorLayoutModel::Build(invalidTarget);
        Require(Value(invalidTargetModel, "Placement") == "Out of bounds" &&
                Value(invalidTargetModel, "Tool Target") == "-1, 0, 0",
            "Invalid Pencil target diagnostics are incorrect.");
        RequireSameStructure(validHit, invalidTargetModel);

        InspectorLayoutInput eraser = hitInput;
        eraser.Tool = ActiveVoxelTool::Eraser;
        eraser.Placement = {
            VoxelPlacementPreviewStatus::Valid,
            Asset::Voxel::VoxelPosition{10, 11, 12},
            VoxelPreviewTool::Eraser};
        eraser.LastEraserResult = {
            VoxelEraserResultCode::Applied, true, {10, 11, 12},
            7U, 2U, 42U, 43U, {}};
        const InspectorLayoutModel eraserModel =
            InspectorLayoutModel::Build(eraser);
        Require(Value(eraserModel, "Active Tool") == "Eraser" &&
                Value(eraserModel, "Palette Index") == "--" &&
                Value(eraserModel, "Removed Palette") == "7",
            "Eraser diagnostics are incorrect.");
        RequireSameStructure(validHit, eraserModel);

        InspectorLayoutInput missAfterHit = eraser;
        missAfterHit.Hovered.reset();
        missAfterHit.InteractionState = VoxelPickingInteractionState::NoHit;
        const InspectorLayoutModel missModel =
            InspectorLayoutModel::Build(missAfterHit);
        Require(Value(missModel, "Hovered Voxel") == "--" &&
                Value(missModel, "Status") == "No hit",
            "Miss-after-hit diagnostics are incorrect.");
        RequireSameStructure(eraserModel, missModel);

        std::cout << "Layout stability tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "Layout stability tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
