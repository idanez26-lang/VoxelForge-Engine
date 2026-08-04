#pragma once

#include "Console/EditorConsoleService.h"
#include "Commands/Voxel/VoxelEditSession.h"
#include "VoxelDocument/VoxelDocumentSession.h"
#include "VoxelHistory/VoxelEditHistory.h"
#include "VoxelStamps/Placement/StampPlacementSession.h"

#include <cstdint>
#include <functional>

namespace VoxelForge::Editor
{

// Orchestration de la preview de placement de Stamp (VF-0260, lot 3 —
// extrait d'EditorWorkspace). Fait le lien entre la session de placement,
// le document actif, l'historique d'édition et la console, sans UI.
// Le rafraîchissement visuel est délégué via un callback.
class StampPreviewController final
{
public:
    using HighlightsChangedCallback = std::function<void()>;

    StampPreviewController(
        Stamps::StampPlacementSession& placement,
        VoxelDocumentSession& documents,
        VoxelEditHistory& history,
        VoxelEditSession& editSession,
        EditorConsoleService& console,
        bool& sharedEditInProgressFlag,
        HighlightsChangedCallback onHighlightsChanged);

    void Move(std::int32_t x, std::int32_t y, std::int32_t z);
    void Rotate(bool clockwise);
    void Mirror(Stamps::StampPlacementMirrorMode mirror);
    void ToggleMirror(Stamps::StampPlacementMirrorMode axis);
    void CycleMirror();
    void ResetTransform();
    void Place();
    bool Refresh();
    void Clear() noexcept;

private:
    Stamps::StampPlacementSession& placement_;
    VoxelDocumentSession& documents_;
    VoxelEditHistory& history_;
    VoxelEditSession& editSession_;
    EditorConsoleService& console_;
    // Drapeau de réentrance PARTAGÉ avec les autres opérations d'édition
    // (l'ancien voxelEditInProgress_ d'EditorWorkspace).
    bool& editInProgress_;
    HighlightsChangedCallback onHighlightsChanged_;
};

} // namespace VoxelForge::Editor
