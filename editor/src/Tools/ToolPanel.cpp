#include "ToolPanel.h"

#include "Panels/MovePanel.h"
#include "Panels/SmartToolPanel.h"
#include "Panels/RotatePanel.h"
#include "Panels/ScalePanel.h"

#include <imgui.h>

namespace
{
using namespace VoxelForge::Editor;

// VF-UX (validation Tony) : la famille Transform expose ses modes existants.
// Move, Rotate et Scale ont chacun un moteur et un panneau valides ; cette
// rangee ne fait que router entre eux, par le meme chemin que la barre.
// « Wrap » n'existe pas dans le moteur : il est signale dans le rapport, pas
// simule ici.
bool DrawTransformModeRow(const ToolManager& manager, ToolContext& context)
{
    const ActiveVoxelTool active = manager.ActiveTool();
    ImGui::TextDisabled("Transform");
    ImGui::PushID("TransformMode");
    int mode = active == ActiveVoxelTool::Scale ? 1
        : active == ActiveVoxelTool::Wrap ? 2
        : active == ActiveVoxelTool::Rotate ? 3 : 0;
    const int before = mode;
    bool changed = false;
    changed |= ImGui::RadioButton("Move", &mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Scale", &mode, 1);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Wrap", &mode, 2);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Rotate", &mode, 3);
    ImGui::PopID();
    if (mode != before && context.SelectTool)
    {
        context.SelectTool(mode == 1 ? ActiveVoxelTool::Scale
            : mode == 2 ? ActiveVoxelTool::Wrap
            : mode == 3 ? ActiveVoxelTool::Rotate : ActiveVoxelTool::Move);
        changed = true;
    }
    ImGui::Separator();
    return changed;
}

// VF-WRAP-V1 : options de Wrap, montrees UNIQUEMENT quand Wrap est actif. Le
// geste principal reste les poignees des bounds dans le viewport ; ces champs
// sont complementaires.
bool DrawWrapPanel(ToolContext& context)
{
    bool changed = false;
    ImGui::TextDisabled("Drag a bounds face to repeat or crop the pattern");
    ImGui::TextDisabled("Spacing");
    ImGui::PushID("WrapSpacing");
    const auto spacingField = [&changed](const char* label,
        std::int32_t& value)
    {
        int spacing = value;
        ImGui::SetNextItemWidth(72.0F);
        if (ImGui::InputInt(label, &spacing))
        {
            value = spacing < 0 ? 0 : spacing;
            changed = true;
        }
    };
    spacingField("X", context.Wrap.X.Spacing);
    ImGui::SameLine();
    spacingField("Y", context.Wrap.Y.Spacing);
    ImGui::SameLine();
    spacingField("Z", context.Wrap.Z.Spacing);
    ImGui::PopID();
    ImGui::TextDisabled("Mirror Repeat");
    ImGui::PushID("WrapMirror");
    changed |= ImGui::Checkbox("X##m", &context.Wrap.X.MirrorRepeat);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Y##m", &context.Wrap.Y.MirrorRepeat);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Z##m", &context.Wrap.Z.MirrorRepeat);
    ImGui::PopID();
    return changed;
}

// VF-UX-SELECTION-V1 : la famille Selection expose ses trois modes et son
// operation courante. Les options de Region suivent la regle de visibilite
// formalisee dans SelectionUxModes.h — une option affichee agit.
bool DrawSelectionPanel(ToolContext& context)
{
    SelectionToolOptions& options = context.Selection;
    bool changed = false;

    ImGui::TextDisabled("Mode");
    ImGui::PushID("SelectionMode");
    int mode = static_cast<int>(options.Mode);
    changed |= ImGui::RadioButton("Box", &mode,
        static_cast<int>(SelectionFamilyMode::Box));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Rect", &mode,
        static_cast<int>(SelectionFamilyMode::Rect));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Region", &mode,
        static_cast<int>(SelectionFamilyMode::Region));
    ImGui::PopID();
    options.Mode = static_cast<SelectionFamilyMode>(mode);

    ImGui::TextDisabled(options.Mode == SelectionFamilyMode::Box
        ? "Drag a 3D box in the viewport"
        : options.Mode == SelectionFamilyMode::Rect
        ? "Drag a flat rectangle: one layer on the aimed plane"
        : "Click a voxel to select its region");
    ImGui::Separator();

    // Operation de base, visible. Les modificateurs clavier priment pendant le
    // geste — l'interface et les raccourcis disent la meme verite.
    ImGui::TextDisabled("Operation");
    ImGui::PushID("SelectionOperation");
    int operation = static_cast<int>(options.Operation);
    changed |= ImGui::RadioButton("Replace", &operation,
        static_cast<int>(SelectionMode::Replace));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Add", &operation,
        static_cast<int>(SelectionMode::Add));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Subtract", &operation,
        static_cast<int>(SelectionMode::Subtract));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Intersect", &operation,
        static_cast<int>(SelectionMode::Intersect));
    ImGui::PopID();
    options.Operation = static_cast<SelectionMode>(operation);
    ImGui::TextDisabled("Shift = Add | Ctrl = Subtract | Shift+Ctrl = Intersect");

    if (options.Mode == SelectionFamilyMode::Region)
    {
        ImGui::Separator();
        ImGui::TextDisabled("Target");
        ImGui::PushID("RegionTarget");
        int target = static_cast<int>(options.RegionTarget);
        changed |= ImGui::RadioButton("Volume", &target,
            static_cast<int>(SelectionRegionTarget::Volume));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Face", &target,
            static_cast<int>(SelectionRegionTarget::Face));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Color", &target,
            static_cast<int>(SelectionRegionTarget::Color));
        ImGui::PopID();
        options.RegionTarget = static_cast<SelectionRegionTarget>(target);

        if (RegionShowsCriterion(options.RegionTarget))
        {
            ImGui::TextDisabled("Criterion");
            ImGui::PushID("RegionCriterion");
            int criterion = static_cast<int>(options.RegionCriterion);
            changed |= ImGui::RadioButton("Geometry", &criterion,
                static_cast<int>(SelectionRegionCriterion::Geometry));
            ImGui::SameLine();
            changed |= ImGui::RadioButton("Color##crit", &criterion,
                static_cast<int>(SelectionRegionCriterion::Color));
            ImGui::PopID();
            options.RegionCriterion =
                static_cast<SelectionRegionCriterion>(criterion);
        }
        if (RegionShowsPlanarConnectivity(options.RegionTarget))
        {
            ImGui::TextDisabled("Connectivity");
            ImGui::PushID("PlanarConnectivity");
            int connectivity = static_cast<int>(options.PlanarConnectivity);
            changed |= ImGui::RadioButton("4", &connectivity,
                static_cast<int>(SelectionPlanarConnectivity::Four));
            ImGui::SameLine();
            changed |= ImGui::RadioButton("8", &connectivity,
                static_cast<int>(SelectionPlanarConnectivity::Eight));
            ImGui::PopID();
            options.PlanarConnectivity =
                static_cast<SelectionPlanarConnectivity>(connectivity);
        }
        if (RegionShowsVolumeConnectivity(options.RegionTarget))
        {
            // 4/8 n'a aucun sens en volume : le voisinage volumique est nomme
            // pour ce qu'il est — 6 (faces) ou 26 (faces + aretes + coins).
            ImGui::TextDisabled("Connectivity");
            ImGui::PushID("VolumeConnectivity");
            int connectivity = static_cast<int>(options.VolumeConnectivity);
            changed |= ImGui::RadioButton("6 (faces)", &connectivity,
                static_cast<int>(SelectionVolumeConnectivity::Six));
            ImGui::SameLine();
            changed |= ImGui::RadioButton("26 (all)", &connectivity,
                static_cast<int>(SelectionVolumeConnectivity::TwentySix));
            ImGui::PopID();
            options.VolumeConnectivity =
                static_cast<SelectionVolumeConnectivity>(connectivity);
        }
        if (options.RegionTarget == SelectionRegionTarget::Color)
            ImGui::TextDisabled("Selects every voxel of the clicked color");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Selection persists across tool changes");
    ImGui::TextDisabled("Escape cancels the gesture, then clears");
    return changed;
}
} // namespace

namespace VoxelForge::Editor
{

bool ToolPanel::Draw(const ToolManager& manager, ToolContext& context)
{
    bool changed = false;
    const ToolDescriptor& descriptor = manager.ActiveDescriptor();
    ImGui::TextDisabled("Tool");
    ImGui::SameLine();
    ImGui::TextUnformatted(descriptor.Name.data(),
        descriptor.Name.data() + descriptor.Name.size());
    ImGui::Separator();

    ImGui::BeginDisabled(!context.HasDocument);
    switch (descriptor.Panel)
    {
    case ToolPanelKind::Smart: changed = DrawSmartToolPanel(context); break;
    case ToolPanelKind::Move:
        changed = DrawTransformModeRow(manager, context);
        DrawMovePanel(context);
        break;
    case ToolPanelKind::Rotate:
        changed = DrawTransformModeRow(manager, context);
        DrawRotatePanel(context);
        break;
    case ToolPanelKind::Scale:
        changed = DrawTransformModeRow(manager, context);
        DrawScalePanel(context);
        break;
    case ToolPanelKind::Wrap:
        changed = DrawTransformModeRow(manager, context);
        changed |= DrawWrapPanel(context);
        break;
    case ToolPanelKind::Selection:
        changed = DrawSelectionPanel(context);
        break;
    case ToolPanelKind::Generic:
    default:
        ImGui::TextDisabled("This tool uses its existing viewport controls.");
        break;
    }
    ImGui::EndDisabled();
    if (!context.HasDocument)
        ImGui::TextDisabled("Open a voxel model to use tool options.");
    return changed;
}

float ToolPanel::PreferredHeight(const ToolPanelKind panel) noexcept
{
    switch (panel)
    {
    case ToolPanelKind::Smart: return 330.0F;
    case ToolPanelKind::Move:
    case ToolPanelKind::Rotate: return 168.0F;
    case ToolPanelKind::Scale: return 152.0F;
    case ToolPanelKind::Wrap: return 192.0F;
    case ToolPanelKind::Selection: return 248.0F;
    default: return 82.0F;
    }
}

} // namespace VoxelForge::Editor
