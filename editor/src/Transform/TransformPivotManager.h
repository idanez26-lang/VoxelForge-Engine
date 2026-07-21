#pragma once

#include "Selection/SelectionService.h"
#include "Transform/TransformPivot.h"

namespace VoxelForge::Editor
{

class TransformPivotManager final
{
public:
    [[nodiscard]] bool SetMode(TransformPivotMode mode) noexcept;
    [[nodiscard]] TransformPivotMode GetMode() const noexcept;

    [[nodiscard]] bool UpdateFromBounds(
        const SelectionBounds& bounds,
        Vec3 modelCenter = {}) noexcept;
    void Invalidate() noexcept;

    [[nodiscard]] bool HasValidPivot() const noexcept;
    [[nodiscard]] const TransformPivot& GetPivot() const noexcept;

private:
    [[nodiscard]] static bool BoundsAreValid(
        const SelectionBounds& bounds) noexcept;
    [[nodiscard]] static TransformPivot Calculate(
        TransformPivotMode mode,
        const SelectionBounds& bounds,
        Vec3 modelCenter) noexcept;

    TransformPivotMode mode_ = TransformPivotMode::Center;
    TransformPivot pivot_{};
    SelectionBounds cachedBounds_{};
    Vec3 cachedModelCenter_{};
    bool valid_ = false;
    bool inputsCached_ = false;
};

} // namespace VoxelForge::Editor
