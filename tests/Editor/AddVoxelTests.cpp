#include "Commands/CommandHistory.h"
#include "Commands/Voxel/AddVoxelCommand.h"
#include "Commands/Voxel/AddVoxelTarget.h"

#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxelModelSerializer.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace
{
using namespace VoxelForge::Editor;
using VoxelForge::Mesh::MeshData;
using VoxelForge::Voxel::Voxel;
using VoxelForge::Voxel::VoxelGrid;
using VoxelForge::Voxel::VoxelModel;
using VoxelForge::Voxel::VoxelModelSerializer;

bool Check(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
    }
    return condition;
}

class TestSession final : public VoxelEditSession
{
public:
    std::uint64_t Generation = 1U;
    std::optional<VoxelModel> Model;
    std::optional<MeshData> DisplayedMesh;
    bool FailRebuild = false;
    bool FailUpload = false;
    bool ThrowRebuild = false;
    bool SelectionActive = true;
    bool Dirty = false;
    std::size_t RebuildCount = 0U;

    std::uint64_t VoxelModelGeneration() const noexcept override
    {
        return Generation;
    }

    VoxelModel* ActiveVoxelModel() noexcept override
    {
        return Model ? &*Model : nullptr;
    }

    CommandResult RebuildActiveVoxelMesh() override
    {
        ++RebuildCount;
        if (ThrowRebuild)
        {
            throw std::runtime_error("simulated rebuild exception");
        }
        if (FailRebuild)
        {
            return CommandResult::Failure("simulated rebuild failure");
        }
        VoxelGrid* grid = Model ? Model->GetGrid(0U) : nullptr;
        if (grid == nullptr)
        {
            return CommandResult::Failure("missing grid");
        }
        auto built = VoxelForge::Mesh::VoxelMeshBuilder::Build(*grid);
        if (!built.Succeeded || !built.Mesh)
        {
            return CommandResult::Failure(built.Message);
        }
        if (FailUpload)
        {
            return CommandResult::Failure("simulated upload failure");
        }
        DisplayedMesh = std::move(*built.Mesh);
        return CommandResult::Success();
    }

    void CompleteVoxelEdit() noexcept override
    {
        SelectionActive = false;
        Dirty = true;
    }
};

Voxel Occupied(const std::uint8_t color = 1U)
{
    return {color, Voxel::OccupiedFlag};
}

TestSession MakeSession(
    const std::uint32_t width = 3U,
    const std::uint32_t height = 3U,
    const std::uint32_t depth = 3U)
{
    TestSession session;
    session.Model.emplace();
    VoxelGrid grid;
    static_cast<void>(grid.Resize(width, height, depth));
    session.Model->AddGrid(std::move(grid));
    return session;
}

bool BuildInitialDisplay(TestSession& session)
{
    return session.RebuildActiveVoxelMesh().Succeeded;
}

std::size_t Faces(const TestSession& session)
{
    return session.DisplayedMesh ? session.DisplayedMesh->FaceCount() : 0U;
}

VoxelRaycastHit Hit(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    const VoxelHitFace face)
{
    return {{x, y, z}, face, 0.0F, {}, 1U};
}

bool TestTargets()
{
    bool passed = true;
    TestSession session = MakeSession();
    VoxelGrid* grid = session.Model->GetGrid(0U);
    static_cast<void>(grid->Set(1U, 1U, 1U, Occupied()));
    const std::array cases{
        std::pair{VoxelHitFace::NegativeX, VoxelCoordinates{0U, 1U, 1U}},
        std::pair{VoxelHitFace::PositiveX, VoxelCoordinates{2U, 1U, 1U}},
        std::pair{VoxelHitFace::NegativeY, VoxelCoordinates{1U, 0U, 1U}},
        std::pair{VoxelHitFace::PositiveY, VoxelCoordinates{1U, 2U, 1U}},
        std::pair{VoxelHitFace::NegativeZ, VoxelCoordinates{1U, 1U, 0U}},
        std::pair{VoxelHitFace::PositiveZ, VoxelCoordinates{1U, 1U, 2U}}};
    for (const auto& [face, expected] : cases)
    {
        const AddVoxelTarget target =
            FindAddVoxelTarget(grid, Hit(1U, 1U, 1U, face));
        passed &= Check(target && target.Coordinates == expected,
            "Adjacent target calculation failed for one face.");

        TestSession directional = MakeSession();
        VoxelGrid* directionalGrid = directional.Model->GetGrid(0U);
        static_cast<void>(
            directionalGrid->Set(1U, 1U, 1U, Occupied()));
        const AddVoxelTarget directionalTarget = FindAddVoxelTarget(
            directionalGrid, Hit(1U, 1U, 1U, face));
        passed &= Check(directionalTarget &&
            AddVoxelCommand(
                directional,
                directional.Generation,
                directionalTarget.Coordinates->X,
                directionalTarget.Coordinates->Y,
                directionalTarget.Coordinates->Z,
                5U).Execute() &&
            directionalGrid->Get(
                expected.X, expected.Y, expected.Z)->IsOccupied(),
            "Actual Add command failed for one of the six faces.");
    }
    passed &= Check(
        FindAddVoxelTarget(grid, Hit(1U, 1U, 1U, VoxelHitFace::None)).Status ==
            AddVoxelTargetStatus::InvalidFace,
        "An inside hit must be refused.");
    passed &= Check(
        FindAddVoxelTarget(grid, Hit(0U, 1U, 1U, VoxelHitFace::NegativeX)).Status ==
            AddVoxelTargetStatus::SelectedVoxelEmpty,
        "An empty selected cell must be refused before boundary evaluation.");
    static_cast<void>(grid->Set(0U, 1U, 1U, Occupied()));
    passed &= Check(
        FindAddVoxelTarget(grid, Hit(0U, 1U, 1U, VoxelHitFace::NegativeX)).Status ==
            AddVoxelTargetStatus::OutsideGrid,
        "Negative boundary addition must be refused without underflow.");
    static_cast<void>(grid->Set(2U, 1U, 1U, Occupied()));
    passed &= Check(
        FindAddVoxelTarget(grid, Hit(2U, 1U, 1U, VoxelHitFace::PositiveX)).Status ==
            AddVoxelTargetStatus::OutsideGrid,
        "Positive boundary addition must be refused past dimension minus one.");
    passed &= Check(
        FindAddVoxelTarget(grid, Hit(1U, 1U, 1U, VoxelHitFace::NegativeX)).Status ==
            AddVoxelTargetStatus::DestinationOccupied,
        "An occupied destination must be refused.");
    passed &= Check(
        FindAddVoxelTarget(nullptr, Hit(1U, 1U, 1U, VoxelHitFace::PositiveX)).Status ==
            AddVoxelTargetStatus::MissingGrid &&
        FindAddVoxelTarget(grid, std::nullopt).Status ==
            AddVoxelTargetStatus::NoSelection,
        "Missing grid or selection target validation failed.");
    passed &= Check(
        FindAddVoxelTarget(
            grid, Hit(99U, 1U, 1U, VoxelHitFace::PositiveX)).Status ==
            AddVoxelTargetStatus::SelectedOutsideGrid,
        "An out-of-grid stale selection must be refused.");
    return passed;
}

bool TestCommandAndHistory()
{
    bool passed = true;
    TestSession session = MakeSession();
    VoxelGrid* grid = session.Model->GetGrid(0U);
    static_cast<void>(grid->Set(1U, 1U, 1U, Occupied(9U)));
    const Voxel preservedEmpty{77U, 0x80U};
    static_cast<void>(grid->Set(2U, 1U, 1U, preservedEmpty));
    passed &= Check(BuildInitialDisplay(session) && Faces(session) == 6U,
        "Single voxel Add setup failed.");

    CommandHistory history;
    auto command = std::make_unique<AddVoxelCommand>(
        session, session.Generation, 2U, 1U, 1U, 0U);
    AddVoxelCommand* direct = command.get();
    passed &= Check(history.Execute(std::move(command)).Succeeded &&
        grid->OccupiedVoxelCount() == 2U && Faces(session) == 10U &&
        session.DisplayedMesh->TriangleCount() == 20U &&
        *grid->Get(2U, 1U, 1U) == Occupied(0U) &&
        direct->PreviousVoxel() == preservedEmpty &&
        direct->AddedVoxel() == Occupied(0U) &&
        direct->AddedVoxel().Flags == Voxel::OccupiedFlag &&
        session.Dirty && !session.SelectionActive &&
        history.UndoCount() == 1U,
        "Execute did not add palette zero with exact flags and geometry.");
    passed &= Check(history.Undo().Succeeded &&
        *grid->Get(2U, 1U, 1U) == preservedEmpty &&
        grid->OccupiedVoxelCount() == 1U && Faces(session) == 6U,
        "Undo did not restore the exact previous empty voxel.");
    passed &= Check(history.Redo().Succeeded &&
        *grid->Get(2U, 1U, 1U) == Occupied(0U) && Faces(session) == 10U &&
        history.Undo().Succeeded && history.Redo().Succeeded,
        "Redo or repeated Add cycles drifted.");

    TestSession palette255 = MakeSession();
    static_cast<void>(palette255.Model->GetGrid(0U)->Set(
        1U, 1U, 1U, Occupied()));
    passed &= Check(AddVoxelCommand(
        palette255, palette255.Generation, 1U, 2U, 1U, 255U).Execute() &&
        *palette255.Model->GetGrid(0U)->Get(1U, 2U, 1U) == Occupied(255U),
        "Palette index 255 addition failed.");
    passed &= Check(!AddVoxelCommand(
        palette255, palette255.Generation, 1U, 0U, 1U, 256U).Execute(),
        "Palette index 256 must be refused.");
    return passed;
}

bool TestInvalidAndRollback()
{
    bool passed = true;
    TestSession invalid = MakeSession();
    VoxelGrid* grid = invalid.Model->GetGrid(0U);
    static_cast<void>(grid->Set(1U, 1U, 1U, Occupied()));
    CommandHistory occupiedHistory;
    passed &= Check(!occupiedHistory.Execute(std::make_unique<AddVoxelCommand>(
        invalid, invalid.Generation, 1U, 1U, 1U, 3U)) &&
        occupiedHistory.UndoCount() == 0U && !invalid.Dirty,
        "Occupied Add failure changed history or dirty state.");
    passed &= Check(!AddVoxelCommand(
        invalid, invalid.Generation, 3U, 1U, 1U, 3U).Execute(),
        "Out-of-range Add coordinates must fail.");
    TestSession noModel;
    passed &= Check(!AddVoxelCommand(
        noModel, noModel.Generation, 0U, 0U, 0U, 1U).Execute(),
        "Missing model must fail.");
    TestSession noGrid;
    noGrid.Model.emplace();
    passed &= Check(!AddVoxelCommand(
        noGrid, noGrid.Generation, 0U, 0U, 0U, 1U).Execute(),
        "Missing grid must fail.");

    TestSession rollback = MakeSession();
    VoxelGrid* rollbackGrid = rollback.Model->GetGrid(0U);
    static_cast<void>(rollbackGrid->Set(1U, 1U, 1U, Occupied()));
    passed &= Check(BuildInitialDisplay(rollback), "Rollback setup failed.");
    const MeshData oldMesh = *rollback.DisplayedMesh;
    rollback.FailRebuild = true;
    CommandHistory rebuildHistory;
    passed &= Check(!rebuildHistory.Execute(std::make_unique<AddVoxelCommand>(
        rollback, rollback.Generation, 2U, 1U, 1U, 2U)) &&
        !rollbackGrid->Get(2U, 1U, 1U)->IsOccupied() &&
        rebuildHistory.UndoCount() == 0U && !rollback.Dirty &&
        rollback.DisplayedMesh->Indices() == oldMesh.Indices(),
        "Rebuild failure did not roll back CPU, display, and history.");
    rollback.FailRebuild = false;
    rollback.ThrowRebuild = true;
    CommandHistory exceptionHistory;
    passed &= Check(!exceptionHistory.Execute(std::make_unique<AddVoxelCommand>(
        rollback, rollback.Generation, 2U, 1U, 1U, 2U)) &&
        !rollbackGrid->Get(2U, 1U, 1U)->IsOccupied() &&
        exceptionHistory.UndoCount() == 0U,
        "Rebuild exception bypassed Add rollback.");
    rollback.ThrowRebuild = false;
    rollback.FailUpload = true;
    CommandHistory uploadHistory;
    passed &= Check(!uploadHistory.Execute(std::make_unique<AddVoxelCommand>(
        rollback, rollback.Generation, 2U, 1U, 1U, 2U)) &&
        !rollbackGrid->Get(2U, 1U, 1U)->IsOccupied() &&
        uploadHistory.UndoCount() == 0U &&
        rollback.DisplayedMesh->Indices() == oldMesh.Indices(),
        "Upload failure did not roll back Add.");

    TestSession transition = MakeSession();
    VoxelGrid* transitionGrid = transition.Model->GetGrid(0U);
    static_cast<void>(transitionGrid->Set(1U, 1U, 1U, Occupied()));
    passed &= Check(BuildInitialDisplay(transition),
        "Undo/Redo Add rollback setup failed.");
    CommandHistory transitionHistory;
    passed &= Check(transitionHistory.Execute(
        std::make_unique<AddVoxelCommand>(
            transition, transition.Generation, 2U, 1U, 1U, 6U)).Succeeded,
        "Undo/Redo Add rollback command execution failed.");
    transition.FailUpload = true;
    passed &= Check(!transitionHistory.Undo() &&
        transitionGrid->Get(2U, 1U, 1U)->IsOccupied() &&
        transitionHistory.UndoCount() == 1U &&
        transitionHistory.RedoCount() == 0U && Faces(transition) == 10U,
        "Failed Add Undo did not preserve CPU, mesh, and history.");
    transition.FailUpload = false;
    passed &= Check(transitionHistory.Undo().Succeeded &&
        !transitionGrid->Get(2U, 1U, 1U)->IsOccupied(),
        "Add Undo did not recover after upload failure.");
    transition.FailRebuild = true;
    passed &= Check(!transitionHistory.Redo() &&
        !transitionGrid->Get(2U, 1U, 1U)->IsOccupied() &&
        transitionHistory.UndoCount() == 0U &&
        transitionHistory.RedoCount() == 1U && Faces(transition) == 6U,
        "Failed Add Redo did not preserve CPU, mesh, and history.");
    transition.FailRebuild = false;
    passed &= Check(transitionHistory.Redo().Succeeded &&
        transitionGrid->Get(2U, 1U, 1U)->IsOccupied(),
        "Add Redo did not recover after rebuild failure.");

    TestSession stale = MakeSession();
    AddVoxelCommand staleCommand(
        stale, stale.Generation, 1U, 1U, 1U, 4U);
    ++stale.Generation;
    passed &= Check(!staleCommand.Execute(),
        "A stale model generation must be refused.");
    return passed;
}

bool TestGeometryAndSave()
{
    bool passed = true;
    TestSession surrounded = MakeSession();
    VoxelGrid* grid = surrounded.Model->GetGrid(0U);
    for (const VoxelCoordinates coordinate : std::array{
        VoxelCoordinates{0U, 1U, 1U}, VoxelCoordinates{2U, 1U, 1U},
        VoxelCoordinates{1U, 0U, 1U}, VoxelCoordinates{1U, 2U, 1U},
        VoxelCoordinates{1U, 1U, 0U}, VoxelCoordinates{1U, 1U, 2U}})
    {
        static_cast<void>(grid->Set(
            coordinate.X, coordinate.Y, coordinate.Z, Occupied()));
    }
    passed &= Check(BuildInitialDisplay(surrounded) && Faces(surrounded) == 36U,
        "Surrounded-cell geometry setup failed.");
    passed &= Check(AddVoxelCommand(
        surrounded, surrounded.Generation, 1U, 1U, 1U, 255U).Execute() &&
        grid->OccupiedVoxelCount() == 7U && Faces(surrounded) == 30U,
        "Filling a six-neighbor cavity did not reduce exposed faces correctly.");

    const auto unique = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("VoxelForgeAddVoxel-" + std::to_string(unique));
    const auto path = directory / "added.vfvoxel";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    passed &= Check(!error &&
        VoxelModelSerializer::Save(path, *surrounded.Model).Succeeded,
        "Add plus Save failed.");
    const auto loaded = VoxelModelSerializer::Load(path);
    passed &= Check(loaded.Model &&
        loaded.Model->GetGrid(0U)->Get(1U, 1U, 1U)->IsOccupied() &&
        loaded.Model->GetGrid(0U)->Get(1U, 1U, 1U)->ColorIndex == 255U &&
        loaded.Model->GetGrid(0U)->OccupiedVoxelCount() == 7U,
        "Added voxel did not survive Save and Reload.");
    std::filesystem::remove_all(directory, error);
    return passed;
}

} // namespace

int main()
{
    const bool passed = TestTargets() && TestCommandAndHistory() &&
        TestInvalidAndRollback() && TestGeometryAndSave();
    return passed ? 0 : 1;
}
