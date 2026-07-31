#include "ViewportInteractionV2/ViewportInteractionController.h"
#include "ViewportInteractionV2/PencilViewportInteractionController.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <iostream>
#include <utility>

int main()
{
    using namespace VoxelForge;
    namespace V2 = Editor::InteractionV2;

    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    Asset::Vox::VoxModelMetadata model;
    model.Dimensions = {64U, 64U, 64U};
    model.Voxels = {{0U, 0U, 0U, 1U}, {1U, 0U, 0U, 2U}};
    source.Models.push_back(std::move(model));
    source.DeclaredModelCount = 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "viewport-interaction-v2-smoke.vox");
    if (!loaded.Succeeded()) return 1;
    auto& document = *loaded.Document;

    Editor::SelectionService selection;
    selection.SetDocumentGeneration(91U);
    V2::ViewportInteractionController controller;
    const auto submit = [&controller, &document, &selection](
        V2::ViewportInputFrame input)
    {
        input.DocumentRevision = document.GetRevision();
        controller.SubmitInput(std::move(input));
        controller.Tick(&document, selection);
    };
    const auto input = [](const std::uint64_t frame, const Editor::Vec2 mouse)
    {
        V2::ViewportInputFrame value;
        value.Frame = frame;
        value.MouseScreen = mouse;
        value.ViewportHovered = true;
        value.ViewportFocused = true;
        value.SelectionToolActive = true;
        value.Viewport = {0.0F, 0.0F, 800.0F, 600.0F};
        value.ViewProjection = Editor::UniformScaleMatrix(0.02F);
        value.DocumentGeneration = 91U;
        return value;
    };

    auto down = input(1U, {390.0F, 285.0F});
    down.PrimaryPressed = true;
    down.PrimaryHeld = true;
    submit(down);
    auto drag = input(2U, {430.0F, 315.0F});
    drag.PrimaryHeld = true;
    submit(drag);
    auto up = input(3U, {430.0F, 315.0F});
    up.PrimaryReleased = true;
    submit(up);
    // Pencil V2 shares the Viewport Interaction V2 pointer model but keeps a
    // separate controller.  This smoke deliberately exercises it after the
    // Selection gesture above so a regression cannot silently steal input
    // from the established Selection/Move slice.
    V2::PencilViewportInteractionController pencil;
    Editor::PencilCompactRequest pencilRequest;
    pencilRequest.Dimensions = *document.GetDimensions(0U);
    pencilRequest.Brush.Shape = Editor::SmartBrushShape::Cube;
    pencilRequest.Brush.Dimension = Editor::SmartBrushDimension::Volume3D;
    pencilRequest.Brush.Size = 1;
    pencilRequest.Placement = {{4, 4, 4}, {0, 1, 0}};
    pencilRequest.Action = Editor::SmartAction::Add;
    pencilRequest.PaletteIndex = 7U;
    pencilRequest.DocumentGeneration = 91U;
    pencilRequest.DocumentRevision = document.GetRevision();
    const auto pencilInput = [&pencil, &document, &pencilRequest](
        const std::uint64_t frame, const bool pressed, const bool held,
        const bool released)
    {
        V2::PencilViewportInputFrame value;
        value.Frame = frame;
        value.PrimaryPressed = pressed;
        value.PrimaryHeld = held;
        value.PrimaryReleased = released;
        value.PencilToolActive = true;
        value.Interaction = {true, true, false, false, false};
        value.Request = pencilRequest;
        value.Target = pencilRequest.Placement.Target;
        pencil.SubmitInput(std::move(value));
        pencil.Tick(&document);
    };
    pencilInput(10U, false, false, false);
    pencilInput(11U, true, true, false);
    pencilInput(12U, false, false, true);
    const std::optional<Editor::VoxelEditOperation> pencilOperation =
        pencil.TakeCommit();
    const bool pencilPassed = pencilOperation &&
        pencilOperation->Changes.size() == 1U &&
        controller.Phase() == V2::InteractionPhase::SelectionReady &&
        controller.Metrics().MaximumResolvesPerFrame <= 1U;
    const bool passed = selection.Count() == 2U &&
        controller.Phase() == V2::InteractionPhase::SelectionReady &&
        controller.Presentation().SelectionBox.has_value() &&
        controller.Metrics().ProjectionBuilds == 1U &&
        controller.Metrics().MaximumResolvesPerFrame <= 1U && pencilPassed;
    std::cout << (passed
        ? "Viewport Interaction V2 smoke passed.\n"
        : "Viewport Interaction V2 smoke failed.\n");
    return passed ? 0 : 1;
}
