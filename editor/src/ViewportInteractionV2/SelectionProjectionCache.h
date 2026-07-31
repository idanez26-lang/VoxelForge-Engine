#pragma once

#include "ViewportInputFrame.h"
#include "ViewportPresentation.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstdint>
#include <span>
#include <vector>

namespace VoxelForge::Editor::InteractionV2
{

class SelectionProjectionCache final
{
public:
    [[nodiscard]] bool Ensure(
        const Asset::Voxel::VoxelDocument& document,
        const ViewportInputFrame& input,
        ViewportInteractionMetrics& metrics);
    [[nodiscard]] std::span<const Asset::Voxel::VoxelPosition> Query(
        const ScreenRectangle& rectangle,
        ViewportInteractionMetrics& metrics);
    void Reset() noexcept;

private:
    struct ProjectedVoxel final
    {
        Asset::Voxel::VoxelPosition Position{};
        ScreenRectangle Footprint{};
    };

    [[nodiscard]] bool Matches(
        const Asset::Voxel::VoxelDocument& document,
        const ViewportInputFrame& input) const noexcept;

    std::vector<ProjectedVoxel> projected_;
    std::vector<Asset::Voxel::VoxelPosition> queryResult_;
    Matrix4 viewProjection_ = IdentityMatrix();
    ViewportRectangle viewport_{};
    Vec3 modelCenter_{};
    std::uint64_t documentGeneration_ = 0U;
    std::uint64_t documentRevision_ = 0U;
    float framebufferScale_ = 0.0F;
    bool valid_ = false;
};

} // namespace VoxelForge::Editor::InteractionV2
