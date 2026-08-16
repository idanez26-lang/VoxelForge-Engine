#pragma once

// Feedback utilisateur commun des Transform a fusion (Move / Scale / Rotate /
// Wrap / Align, politique MergeOverlap) — autorite PURE de l'ordre de
// priorite des messages et du verdict de commit :
//
//   1. hors-limites document  -> BLOQUANT ("blocked: destination is outside
//      the model"), meme si des recouvrements existent ;
//   2. recouvrement de voxels existants -> INFORMATION ("overlaps existing
//      voxels - they will be replaced") : le commit fusionne ;
//   3. etat normal.
//
// Un recouvrement ne masque jamais une erreur bloquante : l'interface ne
// doit jamais annoncer une fusion quand le commit sera refuse.

#include "TransformPreviewModel.h"

#include <cstdint>

namespace VoxelForge::Editor
{

enum class TransformPreviewFeedback : std::uint8_t
{
    Normal,
    Overlap,        // information : fusion au commit
    OutOfBounds     // bloquant
};

[[nodiscard]] constexpr TransformPreviewFeedback ResolveTransformPreviewFeedback(
    const bool hasOutOfBounds, const bool hasCollisions) noexcept
{
    if (hasOutOfBounds) return TransformPreviewFeedback::OutOfBounds;
    if (hasCollisions) return TransformPreviewFeedback::Overlap;
    return TransformPreviewFeedback::Normal;
}

[[nodiscard]] inline TransformPreviewFeedback ResolveTransformPreviewFeedback(
    const TransformPreviewModel& preview) noexcept
{
    return ResolveTransformPreviewFeedback(
        preview.HasOutOfBounds(), preview.HasCollisions());
}

// Le seul feedback qui bloque le commit d'une Transform a fusion.
[[nodiscard]] constexpr bool TransformPreviewFeedbackBlocksCommit(
    const TransformPreviewFeedback feedback) noexcept
{
    return feedback == TransformPreviewFeedback::OutOfBounds;
}

// Une preview a fusion est committable ssi elle est active et sans feedback
// bloquant — la garde commune des boutons / menus / raccourci Apply.
[[nodiscard]] inline bool MergingTransformPreviewCanCommit(
    const TransformPreviewModel& preview) noexcept
{
    return preview.IsActive() &&
        !TransformPreviewFeedbackBlocksCommit(
            ResolveTransformPreviewFeedback(preview));
}

} // namespace VoxelForge::Editor
