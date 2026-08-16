#include "Commands/CommandHistory.h"
#include "Commands/Voxel/PaintPaletteSelection.h"
#include "Commands/Voxel/PaintVoxelCommand.h"

#include "VoxelForge/Mesh/VoxelMeshBuilder.h"
#include "VoxelForge/Voxel/VoxelGrid.h"
#include "VoxelForge/Voxel/VoxelModel.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace
{
using VoxelForge::Editor::CommandResult;
using VoxelForge::Editor::PaintVoxelCommand;
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
    const std::uint8_t color,
    const std::uint8_t reservedFlags = 0U)
{
    return {color, static_cast<std::uint8_t>(
        Voxel::OccupiedFlag | reservedFlags)};
}

TestSession MakeSession(
    const std::uint32_t width = 1U,
    const std::uint32_t height = 1U,
    const std::uint32_t depth = 1U)
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

bool MeshUsesColor(const TestSession& session, const std::uint8_t color)
{
    return session.DisplayedMesh &&
        std::all_of(
            session.DisplayedMesh->Vertices().begin(),
            session.DisplayedMesh->Vertices().end(),
            [color](const auto& vertex)
            {
                return vertex.ColorIndex == color;
            });
}

bool MeshMatches(const TestSession& session, const MeshData& expected)
{
    return session.DisplayedMesh &&
        session.DisplayedMesh->Vertices() == expected.Vertices() &&
        session.DisplayedMesh->Indices() == expected.Indices();
}
}

int main()
{
    using VoxelForge::Editor::CommandHistory;
    using VoxelForge::Editor::PaintPaletteSelection;
    bool passed = true;

    PaintPaletteSelection paletteSelection;
    // VF-STAB-01 bugs 6-7: palette index 0 means "no voxel", and VoxelDocument
    // rejects it for any present voxel. The selection therefore starts on the
    // first paintable colour and refuses 0 outright; it used to default to 0,
    // so an Add or Paint with the untouched default failed silently.
    passed &= Check(PaintPaletteSelection::MinimumIndex() == 1U,
        "The first paintable palette index must be one.");
    passed &= Check(paletteSelection.Index() ==
        PaintPaletteSelection::MinimumIndex(),
        "Paint palette selection must default to a paintable index.");
    passed &= Check(paletteSelection.SelectedColor(nullptr) == nullptr,
        "An absent palette must not expose a selected color.");
    passed &= Check(!paletteSelection.SetIndex(0U) &&
        paletteSelection.Index() == PaintPaletteSelection::MinimumIndex(),
        "Paint palette selection must refuse the empty-voxel index zero.");
    passed &= Check(paletteSelection.SetIndex(1U) &&
        paletteSelection.Index() == 1U,
        "Paint palette selection must accept the first paintable index.");
    passed &= Check(paletteSelection.SetIndex(255U) &&
        paletteSelection.Index() == 255U &&
        !paletteSelection.SetIndex(256U) &&
        paletteSelection.Index() == 255U,
        "Paint palette selection did not enforce the 256-color bounds.");
    paletteSelection.OnModelLoaded();
    passed &= Check(paletteSelection.Index() == 255U,
        "Loading a model must preserve the editor-session paint color.");
    VoxelForge::Voxel::VoxelPalette palette;
    passed &= Check(
        paletteSelection.SelectedColor(&palette) == palette.Get(255U),
        "The selected palette color did not follow the chosen index.");

    TestSession basic = MakeSession();
    VoxelGrid* basicGrid = basic.Model->GetGrid(0U);
    const Voxel exact = Occupied(0U, 0xA0U);
    passed &= Check(basicGrid->Set(0U, 0U, 0U, exact) &&
        BuildInitialDisplay(basic) &&
        basic.DisplayedMesh->FaceCount() == 6U &&
        basic.DisplayedMesh->TriangleCount() == 12U,
        "Single voxel paint setup failed.");
    PaintVoxelCommand direct(
        basic, basic.Generation, 0U, 0U, 0U, 255U);
    const Voxel painted = Occupied(255U, 0xA0U);
    passed &= Check(direct.Execute().Succeeded &&
        *basicGrid->Get(0U, 0U, 0U) == painted &&
        direct.PreviousVoxel() == exact && direct.PaintedVoxel() == painted &&
        basicGrid->OccupiedVoxelCount() == 1U &&
        basic.DisplayedMesh->FaceCount() == 6U &&
        basic.DisplayedMesh->TriangleCount() == 12U &&
        MeshUsesColor(basic, 255U) && !basic.SelectionActive && basic.Dirty,
        "Execute did not preserve flags, geometry, and derived mesh color.");
    passed &= Check(direct.Undo().Succeeded &&
        *basicGrid->Get(0U, 0U, 0U) == exact &&
        MeshUsesColor(basic, 0U),
        "Undo did not restore palette index zero exactly.");
    passed &= Check(direct.Redo().Succeeded &&
        *basicGrid->Get(0U, 0U, 0U) == painted &&
        MeshUsesColor(basic, 255U) && direct.Undo().Succeeded &&
        direct.Redo().Succeeded && MeshUsesColor(basic, 255U),
        "Repeated Paint Undo/Redo cycles drifted from the expected state.");

    TestSession ordered = MakeSession(2U, 1U, 1U);
    VoxelGrid* orderedGrid = ordered.Model->GetGrid(0U);
    static_cast<void>(orderedGrid->Set(0U, 0U, 0U, Occupied(5U)));
    static_cast<void>(orderedGrid->Set(1U, 0U, 0U, Occupied(6U)));
    passed &= Check(BuildInitialDisplay(ordered),
        "Ordered multi-voxel paint setup failed.");
    CommandHistory orderedHistory;
    passed &= Check(orderedHistory.Execute(
        std::make_unique<PaintVoxelCommand>(
            ordered, ordered.Generation, 0U, 0U, 0U, 0U)).Succeeded &&
        orderedHistory.Execute(
        std::make_unique<PaintVoxelCommand>(
            ordered, ordered.Generation, 1U, 0U, 0U, 255U)).Succeeded &&
        orderedGrid->Get(0U, 0U, 0U)->ColorIndex == 0U &&
        orderedGrid->Get(1U, 0U, 0U)->ColorIndex == 255U &&
        orderedGrid->OccupiedVoxelCount() == 2U &&
        orderedHistory.Undo().Succeeded &&
        orderedGrid->Get(1U, 0U, 0U)->ColorIndex == 6U &&
        orderedHistory.Undo().Succeeded &&
        orderedGrid->Get(0U, 0U, 0U)->ColorIndex == 5U &&
        orderedHistory.Redo().Succeeded &&
        orderedGrid->Get(0U, 0U, 0U)->ColorIndex == 0U &&
        orderedHistory.Redo().Succeeded &&
        orderedGrid->Get(1U, 0U, 0U)->ColorIndex == 255U,
        "Indices 0/255 or reverse-order multi-voxel Undo/Redo failed.");

    TestSession invalid = MakeSession(2U, 2U, 2U);
    VoxelGrid* invalidGrid = invalid.Model->GetGrid(0U);
    static_cast<void>(invalidGrid->Set(0U, 0U, 0U, Occupied(4U)));
    CommandHistory invalidHistory;
    passed &= Check(!invalidHistory.Execute(
        std::make_unique<PaintVoxelCommand>(
            invalid, invalid.Generation, 0U, 0U, 0U, 4U)) &&
        invalidHistory.UndoCount() == 0U && !invalid.Dirty,
        "Painting with the current color must be a history-free no-op.");
    passed &= Check(!PaintVoxelCommand(
        invalid, invalid.Generation, 0U, 0U, 0U, 256U).Execute(),
        "A palette index above 255 must fail.");
    passed &= Check(!PaintVoxelCommand(
        invalid, invalid.Generation, 1U, 1U, 1U, 5U).Execute(),
        "Painting an empty voxel must fail.");
    passed &= Check(!PaintVoxelCommand(
        invalid, invalid.Generation, 3U, 0U, 0U, 5U).Execute(),
        "Out-of-range paint coordinates must fail.");
    TestSession noModel;
    passed &= Check(!PaintVoxelCommand(
        noModel, noModel.Generation, 0U, 0U, 0U, 1U).Execute(),
        "A missing model must fail.");
    TestSession noGrid;
    noGrid.Model.emplace();
    passed &= Check(!PaintVoxelCommand(
        noGrid, noGrid.Generation, 0U, 0U, 0U, 1U).Execute(),
        "A missing grid must fail.");

    TestSession rollback = MakeSession();
    VoxelGrid* rollbackGrid = rollback.Model->GetGrid(0U);
    const Voxel rollbackVoxel = Occupied(7U, 0x40U);
    static_cast<void>(rollbackGrid->Set(0U, 0U, 0U, rollbackVoxel));
    passed &= Check(BuildInitialDisplay(rollback), "Rollback setup failed.");
    const MeshData oldMesh = *rollback.DisplayedMesh;
    rollback.FailRebuild = true;
    CommandHistory rebuildHistory;
    passed &= Check(!rebuildHistory.Execute(
        std::make_unique<PaintVoxelCommand>(
            rollback, rollback.Generation, 0U, 0U, 0U, 8U)) &&
        *rollbackGrid->Get(0U, 0U, 0U) == rollbackVoxel &&
        rebuildHistory.UndoCount() == 0U &&
        MeshMatches(rollback, oldMesh) &&
        !rollback.Dirty && rollback.SelectionActive,
        "A rebuild failure did not roll back CPU, mesh, selection, and history.");
    rollback.FailRebuild = false;
    rollback.ThrowRebuild = true;
    CommandHistory exceptionHistory;
    passed &= Check(!exceptionHistory.Execute(
        std::make_unique<PaintVoxelCommand>(
            rollback, rollback.Generation, 0U, 0U, 0U, 8U)) &&
        *rollbackGrid->Get(0U, 0U, 0U) == rollbackVoxel &&
        exceptionHistory.UndoCount() == 0U &&
        MeshMatches(rollback, oldMesh),
        "A rebuild exception escaped or bypassed paint rollback.");
    rollback.ThrowRebuild = false;
    rollback.FailUpload = true;
    CommandHistory uploadHistory;
    passed &= Check(!uploadHistory.Execute(
        std::make_unique<PaintVoxelCommand>(
            rollback, rollback.Generation, 0U, 0U, 0U, 8U)) &&
        *rollbackGrid->Get(0U, 0U, 0U) == rollbackVoxel &&
        uploadHistory.UndoCount() == 0U &&
        MeshMatches(rollback, oldMesh),
        "An upload failure did not preserve the source and displayed mesh.");

    TestSession transition = MakeSession();
    VoxelGrid* transitionGrid = transition.Model->GetGrid(0U);
    const Voxel transitionVoxel = Occupied(12U, 0x20U);
    static_cast<void>(transitionGrid->Set(0U, 0U, 0U, transitionVoxel));
    passed &= Check(BuildInitialDisplay(transition),
        "Undo/Redo paint rollback setup failed.");
    CommandHistory transitionHistory;
    passed &= Check(transitionHistory.Execute(
        std::make_unique<PaintVoxelCommand>(
            transition, transition.Generation, 0U, 0U, 0U, 13U)).Succeeded,
        "Undo/Redo paint command execution failed.");
    transition.FailUpload = true;
    passed &= Check(!transitionHistory.Undo() &&
        transitionGrid->Get(0U, 0U, 0U)->ColorIndex == 13U &&
        transitionHistory.UndoCount() == 1U &&
        transitionHistory.RedoCount() == 0U && MeshUsesColor(transition, 13U),
        "A failed Paint Undo did not preserve CPU, GPU, and history state.");
    transition.FailUpload = false;
    passed &= Check(transitionHistory.Undo().Succeeded &&
        *transitionGrid->Get(0U, 0U, 0U) == transitionVoxel,
        "Paint Undo did not recover after an upload failure.");
    transition.FailRebuild = true;
    passed &= Check(!transitionHistory.Redo() &&
        *transitionGrid->Get(0U, 0U, 0U) == transitionVoxel &&
        transitionHistory.UndoCount() == 0U &&
        transitionHistory.RedoCount() == 1U && MeshUsesColor(transition, 12U),
        "A failed Paint Redo did not preserve CPU, GPU, and history state.");
    transition.FailRebuild = false;
    passed &= Check(transitionHistory.Redo().Succeeded &&
        transitionGrid->Get(0U, 0U, 0U)->ColorIndex == 13U,
        "Paint Redo did not recover after a rebuild failure.");

    TestSession stale = MakeSession();
    static_cast<void>(stale.Model->GetGrid(0U)->Set(
        0U, 0U, 0U, Occupied(1U)));
    PaintVoxelCommand changedSession(
        stale, stale.Generation, 0U, 0U, 0U, 2U);
    ++stale.Generation;
    passed &= Check(!changedSession.Execute() &&
        stale.Model->GetGrid(0U)->Get(0U, 0U, 0U)->ColorIndex == 1U,
        "Paint must reject a changed model session.");
    PaintVoxelCommand closed(
        stale, stale.Generation, 0U, 0U, 0U, 2U);
    stale.Model.reset();
    passed &= Check(!closed.Execute(),
        "A project closed during Paint must fail safely.");

    return passed ? 0 : 1;
}
