#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace VoxelForge::Editor
{

enum class SelectionMode : std::uint8_t
{
    Replace,
    Add,
    Subtract,
    Intersect
};

struct SelectionCenter;

struct SelectionBounds final
{
    Asset::Voxel::VoxelPosition Minimum{};
    Asset::Voxel::VoxelPosition Maximum{};
    bool Valid = false;

    [[nodiscard]] static SelectionBounds FromCorners(
        Asset::Voxel::VoxelPosition first,
        Asset::Voxel::VoxelPosition second) noexcept;
    [[nodiscard]] Asset::Voxel::VoxelDimensions Dimensions() const noexcept;
    [[nodiscard]] SelectionCenter Center() const noexcept;
    [[nodiscard]] bool Contains(
        Asset::Voxel::VoxelPosition position) const noexcept;
    [[nodiscard]] SelectionBounds ClampedTo(
        Asset::Voxel::VoxelDimensions dimensions) const noexcept;
    [[nodiscard]] bool operator==(const SelectionBounds&) const noexcept = default;
};

struct SelectionCenter final
{
    float X = 0.0F;
    float Y = 0.0F;
    float Z = 0.0F;

    [[nodiscard]] bool operator==(const SelectionCenter&) const noexcept = default;
};

class SelectionService final
{
public:
    [[nodiscard]] bool Apply(
        std::span<const Asset::Voxel::VoxelPosition> positions,
        SelectionMode mode);
    [[nodiscard]] bool Select(
        Asset::Voxel::VoxelPosition position,
        SelectionMode mode = SelectionMode::Replace);
    [[nodiscard]] bool SelectVolume(
        std::span<const Asset::Voxel::VoxelPosition> existingVoxels,
        SelectionBounds bounds,
        SelectionMode mode = SelectionMode::Replace);
    [[nodiscard]] bool Clear() noexcept;
    void SetDocumentGeneration(std::uint64_t generation) noexcept;
    void ClearDocument() noexcept;

    [[nodiscard]] bool Contains(
        Asset::Voxel::VoxelPosition position) const noexcept;
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;
    [[nodiscard]] std::span<const Asset::Voxel::VoxelPosition>
        Voxels() const noexcept;
    [[nodiscard]] const SelectionBounds& Bounds() const noexcept;
    [[nodiscard]] const SelectionBounds& EditableBounds() const noexcept;
    [[nodiscard]] std::optional<SelectionCenter> Center() const noexcept;
    [[nodiscard]] std::uint64_t DocumentGeneration() const noexcept;

private:
    static void Normalize(
        std::vector<Asset::Voxel::VoxelPosition>& positions);
    void RecalculateBounds() noexcept;

    std::vector<Asset::Voxel::VoxelPosition> positions_;
    SelectionBounds bounds_{};
    SelectionBounds editableBounds_{};
    std::uint64_t documentGeneration_ = 0U;
};

} // namespace VoxelForge::Editor
