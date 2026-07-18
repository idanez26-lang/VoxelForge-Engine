#pragma once

#include <cstddef>

namespace VoxelForge::Editor
{

struct SelectionHighlightPlan final
{
    bool DrawIndividualVoxels = false;
    std::size_t IndividualVoxelCount = 0U;
    std::size_t EstimatedVertexCount = 0U;
    std::size_t EstimatedIndexCount = 0U;
};

class SelectionHighlightPolicy final
{
public:
    static constexpr std::size_t InteractiveIndividualLimit = 256U;
    static constexpr std::size_t PersistentIndividualLimit = 2048U;
    static constexpr std::size_t VerticesPerVoxelOutline = 288U;
    static constexpr std::size_t IndicesPerVoxelOutline = 432U;

    [[nodiscard]] static SelectionHighlightPlan Build(
        std::size_t selectedVoxelCount,
        bool interactionActive) noexcept;
};

} // namespace VoxelForge::Editor
