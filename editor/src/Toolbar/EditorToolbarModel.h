#pragma once

#include "Input/EditorInputService.h"
#include "VoxelTools/VoxelToolState.h"
#include "Tools/SmartToolFamilies.h"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace VoxelForge::Editor
{

enum class EditorToolbarAction : std::uint8_t
{
    Save,
    Pencil,
    Eraser,
    Paint,
    Selection,
    Move,
    Rotate,
    Scale,
    Box,
    Line,
    Sphere,
    Duplicate,
    Mirror,
    Align,
    Face,
    Transform,
    // VF-UX-TOOLS : familles exposees directement dans la barre.
    Geometry,
    Surface,
    Fill
};

enum class EditorToolbarGroup : std::uint8_t
{
    File,
    Sculpt,
    Selection,
    Transform,
    Construction,
    Manipulation
};

struct EditorToolbarButton final
{
    EditorToolbarAction Action = EditorToolbarAction::Save;
    EditorToolbarGroup Group = EditorToolbarGroup::File;
    std::string_view Name;
    std::string_view Description;
    EditorInputCommand Command = EditorInputCommand::None;
    ActiveVoxelTool Tool = ActiveVoxelTool::None;
    // VF-UX-TOOLS : famille Smart representee par ce bouton. `HasFamily` evite
    // de faire porter ce sens a une valeur sentinelle de SmartGeometry.
    bool HasFamily = false;
    SmartGeometry Family = SmartGeometry::Pencil;
};

struct EditorToolbarState final
{
    bool HasDocument = false;
    bool CanSave = false;
    ActiveVoxelTool ActiveTool = ActiveVoxelTool::None;
    bool CanMoveSelection = false;
    bool CanDuplicateSelection = false;
    bool CanRotateSelection = false;
    bool CanMirrorSelection = false;
    bool CanScaleSelection = false;
    bool CanAlignSelection = false;
    // VF-UX-TOOLS : sans elle, les cinq boutons de famille s'allumeraient
    // ensemble des que l'outil Smart est actif. Placee EN FIN de structure :
    // les initialisations positionnelles existantes restent valides.
    SmartGeometry ActiveGeometry = SmartGeometry::Pencil;
};

struct EditorToolbarLayout final
{
    float ButtonSize = 34.0F;
    float ItemSpacing = 5.0F;
    float GroupSpacing = 12.0F;
    bool WrapGroups = false;
};

class EditorToolbarModel final
{
public:
    static constexpr std::size_t ButtonCount = 7U;
    static constexpr std::size_t PrimaryButtonCount = 7U;

    [[nodiscard]] static const std::array<EditorToolbarButton, ButtonCount>&
        Buttons() noexcept;
    [[nodiscard]] static std::span<const EditorToolbarButton,
        PrimaryButtonCount> PrimaryButtons() noexcept;
    [[nodiscard]] static bool IsEnabled(
        const EditorToolbarButton& button,
        const EditorToolbarState& state) noexcept;
    [[nodiscard]] static bool IsActive(
        const EditorToolbarButton& button,
        const EditorToolbarState& state) noexcept;
    [[nodiscard]] static EditorToolbarLayout CalculateLayout(
        float availableWidth,
        float fontSize) noexcept;
};

} // namespace VoxelForge::Editor
