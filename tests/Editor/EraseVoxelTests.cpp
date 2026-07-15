#include "Commands/CommandHistory.h"
#include "Commands/Voxel/EraseVoxelCommand.h"

#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace
{
using VoxelForge::Editor::CommandResult;
using VoxelForge::Editor::EraseVoxelCommand;
using VoxelForge::Editor::VoxelEditSession;
using VoxelForge::Mesh::MeshData;
using VoxelForge::Voxel::Voxel;
using VoxelForge::Voxel::VoxelGrid;
using VoxelForge::Voxel::VoxelModel;

bool Check(const bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
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
            throw std::runtime_error("simulated rebuild exception");
        if (FailRebuild)
            return CommandResult::Failure("simulated rebuild failure");
        VoxelGrid* grid = Model ? Model->GetGrid(0U) : nullptr;
        if (grid == nullptr)
            return CommandResult::Failure("missing grid");
        auto built = VoxelForge::Mesh::VoxelMeshBuilder::Build(*grid);
        if (!built.Succeeded || !built.Mesh)
            return CommandResult::Failure(built.Message);
        if (FailUpload)
            return CommandResult::Failure("simulated upload failure");
        DisplayedMesh = std::move(*built.Mesh);
        return CommandResult::Success();
    }

    void CompleteVoxelEdit() noexcept override
    {
        SelectionActive = false;
        Dirty = true;
    }
};

Voxel Occupied(
    const std::uint8_t color = 1U,
    const std::uint8_t reservedFlags = 0U)
{
    return {color, static_cast<std::uint8_t>(
        Voxel::OccupiedFlag | reservedFlags)};
}

TestSession MakeSession(
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t depth)
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
}

int main()
{
    using VoxelForge::Editor::CommandHistory;
    bool passed = true;

    TestSession basic = MakeSession(1U, 1U, 1U);
    VoxelGrid* basicGrid = basic.Model->GetGrid(0U);
    const Voxel exact{0U, static_cast<std::uint8_t>(Voxel::OccupiedFlag | 0x80U)};
    passed &= Check(basicGrid->Set(0U, 0U, 0U, exact) &&
        BuildInitialDisplay(basic) && Faces(basic) == 6U,
        "Single voxel setup failed.");
    EraseVoxelCommand direct(basic, basic.Generation, 0U, 0U, 0U);
    passed &= Check(direct.Execute().Succeeded &&
        basicGrid->OccupiedVoxelCount() == 0U &&
        *basicGrid->Get(0U, 0U, 0U) == Voxel{} &&
        direct.PreviousVoxel() == exact && Faces(basic) == 0U &&
        !basic.SelectionActive && basic.Dirty,
        "Execute did not erase palette zero exactly or update derived state.");
    passed &= Check(direct.Undo().Succeeded &&
        *basicGrid->Get(0U, 0U, 0U) == exact &&
        basicGrid->OccupiedVoxelCount() == 1U && Faces(basic) == 6U,
        "Undo did not restore the exact voxel and its reserved bits.");
    passed &= Check(direct.Redo().Succeeded &&
        *basicGrid->Get(0U, 0U, 0U) == Voxel{} && Faces(basic) == 0U,
        "Redo did not erase the voxel again.");
    passed &= Check(direct.Undo().Succeeded &&
        *basicGrid->Get(0U, 0U, 0U) == exact && Faces(basic) == 6U &&
        direct.Redo().Succeeded &&
        *basicGrid->Get(0U, 0U, 0U) == Voxel{} && Faces(basic) == 0U,
        "Repeated Undo/Redo cycles drifted from the expected voxel state.");

    TestSession invalid = MakeSession(2U, 2U, 2U);
    passed &= Check(!EraseVoxelCommand(
        invalid, invalid.Generation, 3U, 0U, 0U).Execute(),
        "Out-of-range coordinates must fail.");
    passed &= Check(!EraseVoxelCommand(
        invalid, invalid.Generation, 0U, 0U, 0U).Execute(),
        "An already empty voxel must fail.");
    TestSession noModel;
    passed &= Check(!EraseVoxelCommand(
        noModel, noModel.Generation, 0U, 0U, 0U).Execute(),
        "A missing model must fail.");
    TestSession noGrid;
    noGrid.Model.emplace();
    passed &= Check(!EraseVoxelCommand(
        noGrid, noGrid.Generation, 0U, 0U, 0U).Execute(),
        "A missing grid must fail.");

    TestSession rollback = MakeSession(1U, 1U, 1U);
    VoxelGrid* rollbackGrid = rollback.Model->GetGrid(0U);
    const Voxel rollbackVoxel = Occupied(7U, 0x40U);
    static_cast<void>(rollbackGrid->Set(0U, 0U, 0U, rollbackVoxel));
    passed &= Check(BuildInitialDisplay(rollback), "Rollback setup failed.");
    const MeshData oldMesh = *rollback.DisplayedMesh;
    rollback.FailRebuild = true;
    CommandHistory rebuildHistory;
    passed &= Check(!rebuildHistory.Execute(
        std::make_unique<EraseVoxelCommand>(
            rollback, rollback.Generation, 0U, 0U, 0U)) &&
        *rollbackGrid->Get(0U, 0U, 0U) == rollbackVoxel &&
        rebuildHistory.UndoCount() == 0U &&
        rollback.DisplayedMesh->FaceCount() == oldMesh.FaceCount() &&
        !rollback.Dirty && rollback.SelectionActive,
        "A rebuild failure did not roll back CPU, mesh, selection, and history.");
    rollback.FailRebuild = false;
    rollback.ThrowRebuild = true;
    CommandHistory exceptionHistory;
    passed &= Check(!exceptionHistory.Execute(
        std::make_unique<EraseVoxelCommand>(
            rollback, rollback.Generation, 0U, 0U, 0U)) &&
        *rollbackGrid->Get(0U, 0U, 0U) == rollbackVoxel &&
        exceptionHistory.UndoCount() == 0U &&
        rollback.DisplayedMesh->FaceCount() == oldMesh.FaceCount(),
        "A rebuild exception escaped or bypassed transactional rollback.");
    rollback.ThrowRebuild = false;
    rollback.FailUpload = true;
    CommandHistory uploadHistory;
    passed &= Check(!uploadHistory.Execute(
        std::make_unique<EraseVoxelCommand>(
            rollback, rollback.Generation, 0U, 0U, 0U)) &&
        *rollbackGrid->Get(0U, 0U, 0U) == rollbackVoxel &&
        uploadHistory.UndoCount() == 0U &&
        rollback.DisplayedMesh->FaceCount() == oldMesh.FaceCount(),
        "An upload failure did not preserve source and displayed mesh.");

    TestSession transition = MakeSession(1U, 1U, 1U);
    VoxelGrid* transitionGrid = transition.Model->GetGrid(0U);
    const Voxel transitionVoxel = Occupied(8U, 0x20U);
    static_cast<void>(transitionGrid->Set(0U, 0U, 0U, transitionVoxel));
    passed &= Check(BuildInitialDisplay(transition),
        "Undo/Redo rollback setup failed.");
    CommandHistory transitionHistory;
    passed &= Check(transitionHistory.Execute(
        std::make_unique<EraseVoxelCommand>(
            transition, transition.Generation, 0U, 0U, 0U)).Succeeded,
        "Undo/Redo rollback command execution failed.");
    transition.FailUpload = true;
    passed &= Check(!transitionHistory.Undo() &&
        *transitionGrid->Get(0U, 0U, 0U) == Voxel{} &&
        transitionHistory.UndoCount() == 1U &&
        transitionHistory.RedoCount() == 0U && Faces(transition) == 0U,
        "A failed Undo did not restore the erased CPU and history state.");
    transition.FailUpload = false;
    passed &= Check(transitionHistory.Undo().Succeeded &&
        *transitionGrid->Get(0U, 0U, 0U) == transitionVoxel,
        "Undo did not recover after an upload failure.");
    transition.FailRebuild = true;
    passed &= Check(!transitionHistory.Redo() &&
        *transitionGrid->Get(0U, 0U, 0U) == transitionVoxel &&
        transitionHistory.UndoCount() == 0U &&
        transitionHistory.RedoCount() == 1U && Faces(transition) == 6U,
        "A failed Redo did not restore the occupied CPU and history state.");
    transition.FailRebuild = false;
    passed &= Check(transitionHistory.Redo().Succeeded &&
        *transitionGrid->Get(0U, 0U, 0U) == Voxel{},
        "Redo did not recover after a rebuild failure.");

    TestSession adjacent = MakeSession(2U, 1U, 1U);
    VoxelGrid* adjacentGrid = adjacent.Model->GetGrid(0U);
    static_cast<void>(adjacentGrid->Set(0U, 0U, 0U, Occupied()));
    static_cast<void>(adjacentGrid->Set(1U, 0U, 0U, Occupied(2U)));
    passed &= Check(BuildInitialDisplay(adjacent) && Faces(adjacent) == 10U,
        "Adjacent voxel mesh must start with ten faces.");
    EraseVoxelCommand adjacentErase(
        adjacent, adjacent.Generation, 0U, 0U, 0U);
    passed &= Check(adjacentErase.Execute().Succeeded && Faces(adjacent) == 6U &&
        adjacentGrid->OccupiedVoxelCount() == 1U,
        "Erasing one of two adjacent voxels must leave six faces.");

    TestSession cube = MakeSession(2U, 2U, 2U);
    VoxelGrid* cubeGrid = cube.Model->GetGrid(0U);
    cubeGrid->Fill(Occupied(3U));
    passed &= Check(BuildInitialDisplay(cube) && Faces(cube) == 24U,
        "The 2x2x2 block must start with 24 faces.");
    EraseVoxelCommand corner(cube, cube.Generation, 0U, 0U, 0U);
    passed &= Check(corner.Execute().Succeeded && Faces(cube) == 24U &&
        cubeGrid->OccupiedVoxelCount() == 7U,
        "Removing a 2x2x2 corner must retain 24 exposed faces.");

    TestSession changed = MakeSession(1U, 1U, 1U);
    static_cast<void>(changed.Model->GetGrid(0U)->Set(
        0U, 0U, 0U, Occupied()));
    EraseVoxelCommand stale(changed, changed.Generation, 0U, 0U, 0U);
    ++changed.Generation;
    passed &= Check(!stale.Execute() &&
        changed.Model->GetGrid(0U)->OccupiedVoxelCount() == 1U,
        "A command must reject a changed model session.");
    EraseVoxelCommand closed(changed, changed.Generation, 0U, 0U, 0U);
    changed.Model.reset();
    passed &= Check(!closed.Execute(), "A project closed during use must fail safely.");

    return passed ? 0 : 1;
}
