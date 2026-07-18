#include "SelectionHighlightPolicy.h"

namespace VoxelForge::Editor
{

SelectionHighlightPlan SelectionHighlightPolicy::Build(
    const std::size_t selectedVoxelCount,
    const bool interactionActive) noexcept
{
    const std::size_t limit = interactionActive
        ? InteractiveIndividualLimit
        : PersistentIndividualLimit;
    const bool drawIndividuals = selectedVoxelCount <= limit;
    const std::size_t renderedCount = drawIndividuals
        ? selectedVoxelCount : 0U;
    return {
        drawIndividuals,
        renderedCount,
        renderedCount * VerticesPerVoxelOutline,
        renderedCount * IndicesPerVoxelOutline};
}

} // namespace VoxelForge::Editor
