#pragma once

#include "VoxelTools/VoxelBrush.h"

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

enum class SmartBrushShape : std::uint8_t
{
    Cube,
    Sphere,
    Cylinder,
    Diamond,
    Line,
    Custom
};

enum class SmartBrushDimension : std::uint8_t { Volume3D, Surface2D };
enum class SmartBrushOrientation : std::uint8_t { Auto, X, Y, Z };
enum class SmartBrushMode : std::uint8_t { Add, Erase, Paint, Replace };
enum class SmartBrushPreviewMode : std::uint8_t { Adaptive };

struct SmartBrushState final
{
    SmartBrushShape Shape = SmartBrushShape::Cube;
    SmartBrushDimension Dimension = SmartBrushDimension::Volume3D;
    // Orientation affects Surface2D and future anisotropic shapes. Cube and
    // Sphere Volume3D are isotropic, so their generated geometry ignores it.
    SmartBrushOrientation Orientation = SmartBrushOrientation::Auto;
    int Size = 1;
    std::size_t PaletteIndex = 1U;
    SmartBrushMode Mode = SmartBrushMode::Add;
    SmartBrushPreviewMode PreviewMode = SmartBrushPreviewMode::Adaptive;

    [[nodiscard]] bool operator==(const SmartBrushState&) const noexcept = default;
};

struct SmartBrushPlacement final
{
    Asset::Voxel::VoxelPosition Target{};
    Asset::Voxel::VoxelPosition Normal{0, 1, 0};
};

struct SmartBrushBounds final
{
    Asset::Voxel::VoxelPosition Minimum{};
    Asset::Voxel::VoxelPosition Maximum{};
};

enum class SmartBrushRenderMode : std::uint8_t
{
    DetailedCells,
    AggregateBox,
    AggregateSphere
};

struct SmartBrushRenderPlan final
{
    SmartBrushRenderMode Mode = SmartBrushRenderMode::DetailedCells;
    SmartBrushBounds Bounds{};
    Asset::Voxel::VoxelPosition SphereCenter{};
    std::uint32_t SphereRadius = 0U;
};

struct SmartBrushStatistics final
{
    std::size_t Total = 0U;
    std::size_t New = 0U;
    std::size_t Existing = 0U;
    std::size_t Clipped = 0U;
};

enum class SmartBrushResultCode : std::uint8_t
{
    Valid,
    OutOfBounds,
    Unsupported,
    InvalidRequest,
    TechnicalFailure
};

struct SmartBrushRequest final
{
    Asset::Voxel::VoxelDimensions Dimensions{};
    SmartBrushState State{};
    SmartBrushPlacement Placement{};
    std::function<bool(Asset::Voxel::VoxelPosition)> IsOccupied;
};

struct SmartBrushResult final
{
    SmartBrushResultCode Code = SmartBrushResultCode::InvalidRequest;
    SmartBrushStatistics Statistics{};
    std::vector<Asset::Voxel::VoxelPosition> Positions;
    std::vector<Asset::Voxel::VoxelPosition> ClippedPositions;
    std::vector<Asset::Voxel::VoxelPosition> AddablePositions;
    std::vector<Asset::Voxel::VoxelPosition> ExistingPositions;
    SmartBrushRenderPlan RenderPlan{};
    std::string Error;

    [[nodiscard]] bool IsWithinBounds() const noexcept;
    [[nodiscard]] bool HasAddablePositions() const noexcept;
};

class SmartBrushEngine final
{
public:
    [[nodiscard]] static int MaximumSize() noexcept;
    [[nodiscard]] static std::size_t EstimateTotal(
        const SmartBrushState& state) noexcept;
    [[nodiscard]] static SmartBrushResult Resolve(
        const SmartBrushRequest& request);
};

} // namespace VoxelForge::Editor
