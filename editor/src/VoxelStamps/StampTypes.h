#pragma once

#include "VoxelForge/Asset/Vox/VoxModel.h"
#include "VoxelForge/Core/UUID.h"

#include <cstdint>
#include <string>

namespace VoxelForge::Editor::Stamps
{

using StampColor = Asset::Vox::VoxColor;

struct StampLocalPosition final
{
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::int32_t Z = 0;

    [[nodiscard]] bool operator==(const StampLocalPosition&) const noexcept = default;
};

struct StampDimensions final
{
    std::uint32_t X = 1U;
    std::uint32_t Y = 1U;
    std::uint32_t Z = 1U;

    [[nodiscard]] bool operator==(const StampDimensions&) const noexcept = default;
};

struct StampBounds final
{
    // Stamp-local content is canonical: its minimum is always the origin.
    StampLocalPosition Minimum{};
    StampLocalPosition Maximum{};
    StampDimensions Dimensions{};

    [[nodiscard]] bool operator==(const StampBounds&) const noexcept = default;
};

struct StampVoxel final
{
    StampLocalPosition Position{};
    std::uint8_t LocalColorId = 0U;

    [[nodiscard]] bool operator==(const StampVoxel&) const noexcept = default;
};

struct StampPaletteEntry final
{
    std::uint8_t LocalColorId = 0U;
    StampColor Color{};
    bool HasSourcePaletteIndex = false;
    std::uint8_t SourcePaletteIndex = 0U;

    [[nodiscard]] bool operator==(const StampPaletteEntry&) const noexcept = default;
};

struct StampIdentity final
{
    Core::UUID Id{0U};
    // The canonical serializer fills this with its stable content digest in STAMP-03.
    std::string ContentHash;

    [[nodiscard]] bool operator==(const StampIdentity&) const noexcept = default;
};

enum class StampPivotMode
{
    Auto,
    Center,
    BottomCenter,
    Surface,
    Corner
};

struct StampFixedPoint final
{
    static constexpr std::int32_t UnitsPerVoxel = 256;

    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::int32_t Z = 0;

    [[nodiscard]] bool operator==(const StampFixedPoint&) const noexcept = default;
};

struct StampNormal final
{
    std::int8_t X = 0;
    std::int8_t Y = 0;
    std::int8_t Z = 0;

    [[nodiscard]] bool operator==(const StampNormal&) const noexcept = default;
};

struct StampPivot final
{
    StampPivotMode RequestedMode = StampPivotMode::Auto;
    StampPivotMode ResolvedMode = StampPivotMode::Center;
    StampFixedPoint LocalPosition{};
    StampNormal LocalNormal{};
    std::uint32_t AutoPolicyVersion = 0U;

    [[nodiscard]] bool operator==(const StampPivot&) const noexcept = default;
};

// Portable scale reservation stored with a VoxelStamp. V1 accepts only an exact
// 1:1 scale on every axis; rotation and mirror remain placement-session state.
struct StampTransform final
{
    double ScaleX = 1.0;
    double ScaleY = 1.0;
    double ScaleZ = 1.0;

    [[nodiscard]] bool IsUnitScale() const noexcept
    {
        return ScaleX == 1.0 && ScaleY == 1.0 && ScaleZ == 1.0;
    }

    [[nodiscard]] bool operator==(const StampTransform&) const noexcept = default;
};

} // namespace VoxelForge::Editor::Stamps
