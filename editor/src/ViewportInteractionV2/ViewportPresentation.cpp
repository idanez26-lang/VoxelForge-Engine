#include "ViewportPresentation.h"

#include <algorithm>

namespace VoxelForge::Editor::InteractionV2
{

bool ScreenRectangle::Contains(const Vec2 point) const noexcept
{
    return point.X >= MinimumX && point.X <= MaximumX &&
        point.Y >= MinimumY && point.Y <= MaximumY;
}

bool ScreenRectangle::Intersects(
    const ScreenRectangle& other) const noexcept
{
    return MaximumX >= other.MinimumX && MinimumX <= other.MaximumX &&
        MaximumY >= other.MinimumY && MinimumY <= other.MaximumY;
}

float ScreenRectangle::Width() const noexcept
{
    return std::max(0.0F, MaximumX - MinimumX);
}

float ScreenRectangle::Height() const noexcept
{
    return std::max(0.0F, MaximumY - MinimumY);
}

ScreenRectangle MakeScreenRectangle(
    const Vec2 first, const Vec2 second) noexcept
{
    return {
        std::min(first.X, second.X), std::min(first.Y, second.Y),
        std::max(first.X, second.X), std::max(first.Y, second.Y)};
}

} // namespace VoxelForge::Editor::InteractionV2
