#include "SmartBrushOptions.h"
#include "SmartToolPanel.h"

#include "Tools/SmartToolFamilies.h"
#include "SmartTools/BrushProfileService.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace VoxelForge::Editor
{
bool DrawSmartToolPanel(ToolContext& context)
{
    SmartTool& tool = context.Smart;
    bool changed = false;
    // VF-UX-TOOLS : la FAMILLE est desormais choisie dans la barre de gauche.
    // Ce panneau ne montre plus que les MODES et les OPTIONS de la famille
    // courante — c'est la hierarchie demandee : famille, puis modes, puis
    // seulement les options qui agissent.
    const SmartGeometry family = tool.Geometry();
    const bool geometryFamily =
        FamilyOf(family) == SmartToolFamily::Geometry;

    // Une option qui n'agit pas doit se voir inerte, jamais disparaitre en
    // silence. La regle vit desormais dans Tools/SmartToolFamilies.h, partagee
    // avec la barre : la dupliquer ici etait la cause du defaut « Cube ».
    // Pencil et Surface montrent toujours un Size actif : editer une taille
    // heritee de 1 (SingleVoxel interne) bascule en CubeBrush. Le predicat
    // canonique reste l'autorite pour les autres familles.
    const bool usesFootprint = ConsumesBrushSize(
        family, tool.Mode(), tool.Action()) ||
        ((FamilyOf(family) == SmartToolFamily::Pencil ||
          FamilyOf(family) == SmartToolFamily::Surface) &&
         tool.Mode() == SmartToolMode::SingleVoxel &&
         !(FamilyOf(family) == SmartToolFamily::Surface &&
           tool.Action() == SmartAction::Paint));

    ImGui::TextDisabled("FAMILY");
    ImGui::SameLine();
    ImGui::TextUnformatted(SmartToolFamilyName(FamilyOf(family)));
    ImGui::Separator();

    if (family == SmartGeometry::Face)
    {
        ImGui::TextDisabled("Acts on the whole connected exposed face");
        ImGui::TextDisabled("Brush size and shape are not used");
    }
    else if (family == SmartGeometry::Fill)
    {
        ImGui::TextDisabled("Mode");
        ImGui::PushID("FillMode");
        int fillMode = static_cast<int>(tool.FillMode());
        changed |= ImGui::RadioButton("Connected Region", &fillMode,
            static_cast<int>(SmartFillMode::Connected));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Face Plane", &fillMode,
            static_cast<int>(SmartFillMode::Plane));
        ImGui::PopID();
        const SmartFillMode fillModeBefore = tool.FillMode();
        tool.SetFillMode(static_cast<SmartFillMode>(fillMode));
        changed |= tool.FillMode() != fillModeBefore;
        ImGui::TextDisabled(tool.FillMode() == SmartFillMode::Connected
            ? "Click a connected color region"
            : "Click a visible face to fill its plane");
        ImGui::TextDisabled("Brush size and shape are not used");
    }
    else if (geometryFamily)
    {
        // Modes V1 de Geometry : Line, Cube, Sphere. Line reste porte par
        // SmartGeometry::Line cote moteur — seule la PRESENTATION change, aucun
        // planner n'est touche. Cylinder existe toujours et n'est pas casse :
        // il n'est simplement pas promu en V1 (decision produit). S'il est
        // actif, on l'affiche pour ne jamais alterer l'etat en silence.
        ImGui::TextDisabled("Shape");
        ImGui::PushID("GeometryShape");
        const GeometryShape currentShape =
            GeometryShapeOf(family, tool.Mode());
        // Cylinder n'est pas un mode V1 : il n'apparait que s'il est deja
        // actif, pour ne jamais alterer l'etat en silence.
        const bool cylinderActive = currentShape == GeometryShape::Cylinder;
        int shape = static_cast<int>(currentShape);
        const int shapeBefore = shape;
        changed |= ImGui::RadioButton("Line", &shape,
            static_cast<int>(GeometryShape::Line));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Cube", &shape,
            static_cast<int>(GeometryShape::Cube));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Sphere", &shape,
            static_cast<int>(GeometryShape::Sphere));
        if (cylinderActive)
        {
            ImGui::SameLine();
            changed |= ImGui::RadioButton("Cylinder", &shape,
                static_cast<int>(GeometryShape::Cylinder));
        }
        ImGui::PopID();
        if (shape != shapeBefore)
        {
            // Une seule regle, celle de SmartToolFamilies.h : « Cube » ne peut
            // plus designer deux etats internes distincts.
            const SmartToolSelection selection = ApplyGeometryShape(
                static_cast<GeometryShape>(shape), tool.Mode());
            tool.SetGeometry(selection.Geometry);
            tool.SetMode(selection.Mode);
            changed = true;
        }

        if (tool.Geometry() == SmartGeometry::Line)
        {
            if (const auto axis = tool.LineConstraintAxis())
                ImGui::TextDisabled("Axis %s", SmartToolLineAxisLabel(*axis));
            else
                ImGui::TextDisabled("Drag from A to B; hold Shift to constrain");
            // L'epaisseur du trait etait reglable avant l'harmonisation, via la
            // rangee Mode et la taille de brosse. La retirer aurait supprime une
            // capacite existante : on la reexpose sous un seul controle, qui dit
            // ce qu'il fait. Les formes Cube et Sphere, elles, restent a 1 par
            // contrat produit et n'ont pas ce reglage.
            int thickness = LineThicknessOf(tool.Mode(), tool.Brush().Size);
            ImGui::SetNextItemWidth(90.0F);
            if (ImGui::InputInt("Thickness", &thickness))
            {
                const LineThickness applied = ApplyLineThickness(std::clamp(
                    thickness, 1, MaximumSmartToolBrushSize));
                tool.SetMode(applied.Mode);
                tool.Brush().Size = applied.Size;
                changed = true;
            }
            ImGui::TextDisabled("1-64 voxels");
        }
        else
        {
            const bool cylinderHeightPhase =
                context.IsGeometryCylinderHeightPhase &&
                context.IsGeometryCylinderHeightPhase();
            ImGui::TextDisabled(
                tool.Mode() == SmartToolMode::SphereBrush
                    ? "Drag from center to set radius"
                : tool.Mode() == SmartToolMode::CylinderBrush
                    ? (cylinderHeightPhase
                        ? "Move along normal to set height, then click"
                        : "Drag from center to set radius")
                : "Drag from first corner to opposite corner");
            ImGui::TextDisabled("2D shape, always 1 voxel thick");
        }
    }
    else
    {
        // Pencil et Surface partagent l'axe Brush. « Single Voxel » n'est
        // PLUS expose : Pencil est deja l'outil voxel — une brosse de taille 1
        // EST un voxel, et le planner normalise CubeBrush taille 1 exactement
        // comme SingleVoxel (Shape=Cube, Size=1 : SmartToolPlanner.cpp:1356).
        // SingleVoxel reste une representation interne (Geometry Cube).
        // L'interface n'ecrit donc jamais SingleVoxel ici ; un etat herite en
        // SingleVoxel s'affiche comme Cube taille 1 et se re-ecrit des le
        // premier reglage. Surface garde strictement son fonctionnement.
        ImGui::TextDisabled("Brush");
        ImGui::PushID("BrushShape");
        const bool cylinderBrushActive =
            tool.Mode() == SmartToolMode::CylinderBrush;
        int shape = tool.Mode() == SmartToolMode::SphereBrush ? 1
            : cylinderBrushActive ? 2 : 0;
        const int shapeBefore = shape;
        changed |= ImGui::RadioButton("Cube", &shape, 0);
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Sphere", &shape, 1);
        if (cylinderBrushActive)
        {
            // Compatibilite : jamais propose, jamais detruit en silence.
            ImGui::SameLine();
            changed |= ImGui::RadioButton("Cylinder", &shape, 2);
        }
        ImGui::PopID();
        if (shape != shapeBefore)
        {
            tool.SetMode(shape == 1 ? SmartToolMode::SphereBrush
                : shape == 2 ? SmartToolMode::CylinderBrush
                : SmartToolMode::CubeBrush);
            changed = true;
        }

        if (family == SmartGeometry::Pencil)
        {
            ImGui::TextDisabled("Brush Mode");
            ImGui::PushID("PencilBrushMode");
            int dimension = static_cast<int>(tool.Brush().Dimension);
            changed |= ImGui::RadioButton("3D", &dimension,
                static_cast<int>(SmartBrushDimension::Volume3D));
            ImGui::SameLine();
            changed |= ImGui::RadioButton("2D", &dimension,
                static_cast<int>(SmartBrushDimension::Surface2D));
            tool.Brush().Dimension = static_cast<SmartBrushDimension>(dimension);
            ImGui::PopID();
        }
        else
        {
            ImGui::TextDisabled("Drag to extend the locked surface");
        }
    }

    // Taille. Elle n'est montree active que si la representation canonique la
    // consomme reellement ; sinon elle est grisee, avec la raison. Un Size
    // actif que le planner ecrase serait pire qu'un Size absent.
    if (family != SmartGeometry::Face && family != SmartGeometry::Fill &&
        family != SmartGeometry::Line)
    {
        if (!usesFootprint)
        {
            ImGui::BeginDisabled(true);
            // Line est deja exclu par la garde ci-dessus et possede son propre
            // controle Thickness : ici, « famille Geometry » veut donc dire
            // Cube, Sphere ou Cylinder, tous d'epaisseur 1.
            int frozen = geometryFamily ? 1 : tool.Brush().Size;
            ImGui::SetNextItemWidth(90.0F);
            ImGui::InputInt("Size", &frozen);
            ImGui::EndDisabled();
            ImGui::TextDisabled(geometryFamily
                ? "2D shapes are always 1 voxel thick"
                : "This mode does not use the brush size");
        }
        else
        {
            // Un etat herite en SingleVoxel s'affiche taille 1 ; le premier
            // reglage l'ecrit en CubeBrush. L'interface ne produit jamais
            // SingleVoxel.
            int size = tool.Mode() == SmartToolMode::SingleVoxel
                ? 1 : tool.Brush().Size;
            ImGui::SetNextItemWidth(90.0F);
            if (ImGui::InputInt("Size", &size))
            {
                if (tool.Mode() == SmartToolMode::SingleVoxel)
                    tool.SetMode(SmartToolMode::CubeBrush);
                tool.Brush().Size = std::clamp(size, 1,
                    MaximumSmartToolBrushSize);
                changed = true;
            }
            ImGui::TextDisabled("1-64 voxels; size 1 places a single voxel");
        }
    }

    // Une action invalide pour la famille (etat herite) est normalisee AVANT
    // affichage : l'interface ne montre jamais un etat qu'elle ne propose pas.
    if (!ActionIsValidFor(FamilyOf(family), tool.Action()))
    {
        tool.SetAction(NormalizeActionFor(FamilyOf(family), tool.Action()));
        changed = true;
    }
    ImGui::TextDisabled("Action");
    ImGui::PushID("Action");
    int action = static_cast<int>(tool.Action());
    // Fill ne propose pas Add — ni actif ni grise : la region de Fill est
    // definie par des voxels existants, Add n'y a aucune cible.
    if (ActionIsValidFor(FamilyOf(family), SmartAction::Add))
    {
        changed |= ImGui::RadioButton(
            "Add", &action, static_cast<int>(SmartAction::Add));
        ImGui::SameLine();
    }
    changed |= ImGui::RadioButton(
        "Erase", &action, static_cast<int>(SmartAction::Erase));
    ImGui::SameLine();
    changed |= ImGui::RadioButton(
        "Paint", &action, static_cast<int>(SmartAction::Paint));
    ImGui::PopID();
    const SmartAction actionBefore = tool.Action();
    tool.SetAction(static_cast<SmartAction>(action));
    changed |= tool.Action() != actionBefore;

    float previewAlpha = tool.PreviewAlpha();
    if (ImGui::SliderFloat("Preview Alpha", &previewAlpha, 0.0F, 1.0F))
    {
        tool.SetPreviewAlpha(previewAlpha);
        changed = true;
    }
    if (context.BrushProfiles != nullptr)
    {
        BrushProfileService& profiles = *context.BrushProfiles;
        static char newName[96] = "Brush Profile";
        static char rename[96] = {};
        static std::string renameUuid;
        static std::string profileFeedback;
        if (context.ActivePaletteIndex)
        {
            if (const auto activePalette = context.ActivePaletteIndex())
                tool.Brush().PaletteIndex = *activePalette;
        }
        const auto applySelected = [&](const BrushProfileResult& result) {
            if (!result.Succeeded() || !result.Profile)
            {
                profileFeedback = result.Message;
                return;
            }
            changed |= BrushProfileService::Apply(*result.Profile, tool);
            if (context.SelectPaletteIndex && !context.SelectPaletteIndex(result.Profile->PaletteIndex))
                profileFeedback = "Profile selected; its palette color is unavailable.";
            else
                profileFeedback = result.Message.empty() ? "Profile selected." : result.Message;
        };
        ImGui::Separator();
        ImGui::TextDisabled("Brush Profile");
        if (const BrushProfile* active = profiles.ActiveProfile(); active != nullptr)
        {
            ImGui::TextUnformatted(active->Name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(active->Favorite ? "Favorite" : "Add Favorite"))
            {
                const bool wasFavorite = active->Favorite;
                const BrushProfileResult result = profiles.SetFavorite(active->Uuid, !wasFavorite);
                profileFeedback = result.Succeeded()
                    ? (wasFavorite ? "Profile unfavorited." : "Profile favorited.")
                    : result.Message;
            }
        }
        if (ImGui::CollapsingHeader("Manage Profiles"))
        {
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputText("Name##BrushProfile", newName, sizeof(newName));
            if (ImGui::Button("Save New"))
            {
                const BrushProfileResult result = profiles.SaveNew(newName, tool);
                applySelected(result);
            }
            if (const BrushProfile* active = profiles.ActiveProfile(); active != nullptr &&
                ImGui::Button("Duplicate Active"))
            {
                applySelected(profiles.Duplicate(active->Uuid, active->Name + " Copy"));
            }
            ImGui::TextDisabled("Saved Profiles");
            for (const BrushProfile* profile : profiles.SortedProfiles())
            {
                ImGui::PushID(profile->Uuid.c_str());
                if (ImGui::Selectable(profile->Name.c_str(), profile->Uuid == profiles.ActiveUuid()))
                    applySelected(profiles.SelectProfile(profile->Uuid));
                ImGui::PopID();
            }
            if (const BrushProfile* active = profiles.ActiveProfile(); active != nullptr &&
                active->Uuid != BrushProfileService::DefaultProfileUuid)
            {
                if (renameUuid != active->Uuid)
                {
                    renameUuid = active->Uuid;
                    std::snprintf(rename, sizeof(rename), "%s", active->Name.c_str());
                }
                ImGui::SetNextItemWidth(-1.0F);
                ImGui::InputText("Rename##BrushProfile", rename, sizeof(rename));
                if (ImGui::Button("Rename"))
                {
                    const BrushProfileResult result = profiles.Rename(active->Uuid, rename);
                    profileFeedback = result.Succeeded() ? "Profile renamed." : result.Message;
                }
                ImGui::SameLine();
                if (ImGui::Button("Overwrite"))
                {
                    const BrushProfileResult result = profiles.Overwrite(active->Uuid, tool);
                    profileFeedback = result.Succeeded() ? "Profile overwritten." : result.Message;
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete")) applySelected(profiles.Delete(active->Uuid));
            }
        }
        if (!profileFeedback.empty()) ImGui::TextDisabled("%s", profileFeedback.c_str());
    }
    ImGui::TextDisabled("Advanced");
    ImGui::BeginDisabled();
    ImGui::TextUnformatted("More controls coming soon");
    ImGui::EndDisabled();

    ImGui::TextDisabled("Statistics");
    if (tool.Statistics().Available)
    {
        const auto& stats = tool.Statistics();
        const bool erasing = tool.Action() == SmartAction::Erase;
        const bool painting = tool.Action() == SmartAction::Paint;
        DrawSmartBrushStatistics(
            erasing ? "Erased" : painting ? "Painted" : "New",
            stats.Changed,
            erasing || painting ? "Ignored" : "Existing",
            stats.Unchanged,
            stats.Total,
            stats.Clipped);
    }
    return changed;
}
} // namespace VoxelForge::Editor
