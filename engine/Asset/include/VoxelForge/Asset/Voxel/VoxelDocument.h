#pragma once

#include "VoxelForge/Asset/Vox/VoxModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VoxelForge::Asset::Voxel
{

struct VoxelPosition final
{
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::int32_t Z = 0;

    [[nodiscard]] bool operator==(
        const VoxelPosition&) const noexcept = default;
};

struct VoxelDimensions final
{
    std::uint32_t X = 0U;
    std::uint32_t Y = 0U;
    std::uint32_t Z = 0U;

    [[nodiscard]] bool operator==(
        const VoxelDimensions&) const noexcept = default;
};

struct Voxel final
{
    std::uint8_t PaletteIndex = 0U;

    [[nodiscard]] bool operator==(const Voxel&) const noexcept = default;
};

struct VoxelDocumentChange final
{
    std::size_t SubModelIndex = 0U;
    VoxelPosition Position{};
    bool ExistedBefore = false;
    std::uint8_t PaletteIndexBefore = 0U;
    bool ExistsAfter = false;
    std::uint8_t PaletteIndexAfter = 0U;

    [[nodiscard]] bool operator==(
        const VoxelDocumentChange&) const noexcept = default;
};

using VoxelColor = Vox::VoxColor;

struct VoxelDocumentPaletteSnapshot final
{
    std::array<VoxelColor, 256U> Colors{};
    bool HasCustomPalette = false;

    [[nodiscard]] bool operator==(
        const VoxelDocumentPaletteSnapshot&) const noexcept = default;
};

struct VoxelDocumentPaletteChange final
{
    VoxelDocumentPaletteSnapshot Before;
    VoxelDocumentPaletteSnapshot After;

    [[nodiscard]] bool operator==(
        const VoxelDocumentPaletteChange&) const noexcept = default;
};

enum class VoxelDocumentCompositeOrder
{
    PaletteThenVoxels,
    VoxelsThenPalette
};

struct VoxelBounds final
{
    bool HasValue = false;
    VoxelPosition Minimum{};
    VoxelPosition Maximum{};

    [[nodiscard]] bool operator==(
        const VoxelBounds&) const noexcept = default;
};

enum class VoxelDocumentError
{
    None,
    FileNotFound,
    ReadFailure,
    InvalidVox,
    UnsupportedVersion,
    MalformedChunk,
    InvalidDimensions,
    InvalidPaletteIndex,
    InvalidModelIndex,
    OutOfBounds,
    VoxelNotFound,
    DuplicateVoxel,
    TooManyModels,
    TooManyVoxels,
    InvalidPalette,
    StateMismatch,
    InvalidTransaction
};

struct VoxelDocumentOperationResult final
{
    bool Succeeded = false;
    bool Changed = false;
    VoxelDocumentError Error = VoxelDocumentError::None;
    std::string Message;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Succeeded;
    }
};

struct VoxelPositionHash final
{
    [[nodiscard]] std::size_t operator()(
        const VoxelPosition& position) const noexcept;
};

class VoxelSubModel final
{
public:
    [[nodiscard]] const VoxelDimensions& Dimensions() const noexcept;
    [[nodiscard]] std::size_t VoxelCount() const noexcept;
    [[nodiscard]] const VoxelBounds& Bounds() const noexcept;
    [[nodiscard]] bool HasVoxel(const VoxelPosition& position) const noexcept;
    [[nodiscard]] std::optional<Voxel> GetVoxel(
        const VoxelPosition& position) const noexcept;

    template<typename Visitor>
    void ForEachVoxel(Visitor&& visitor) const
    {
        for (const auto& [position, voxel] : voxels_)
            std::invoke(visitor, position, voxel);
    }

private:
    friend class VoxelDocument;
    friend class VoxDocumentLoader;

    using Storage = std::unordered_map<
        VoxelPosition,
        Voxel,
        VoxelPositionHash>;

    [[nodiscard]] bool Contains(const VoxelPosition& position) const noexcept;
    void ExtendBounds(const VoxelPosition& position) noexcept;
    void RecalculateBounds() noexcept;

    VoxelDimensions dimensions_{};
    Storage voxels_;
    VoxelBounds bounds_{};
};

class VoxelDocument final
{
public:
    VoxelDocument() = default;

    [[nodiscard]] const std::filesystem::path& SourcePath() const noexcept;
    [[nodiscard]] const std::optional<std::string>& AssetId() const noexcept;
    [[nodiscard]] std::uint32_t VoxVersion() const noexcept;
    [[nodiscard]] bool HasCustomPalette() const noexcept;
    [[nodiscard]] std::size_t GetModelCount() const noexcept;
    [[nodiscard]] const VoxelSubModel* GetModel(
        std::size_t modelIndex) const noexcept;
    [[nodiscard]] std::optional<VoxelDimensions> GetDimensions(
        std::size_t modelIndex = 0U) const noexcept;
    [[nodiscard]] std::optional<Voxel> GetVoxel(
        const VoxelPosition& position,
        std::size_t modelIndex = 0U) const noexcept;
    [[nodiscard]] bool HasVoxel(
        const VoxelPosition& position,
        std::size_t modelIndex = 0U) const noexcept;
    [[nodiscard]] std::uint64_t GetVoxelCount() const noexcept;
    [[nodiscard]] std::optional<std::size_t> GetVoxelCount(
        std::size_t modelIndex) const noexcept;
    [[nodiscard]] std::optional<VoxelBounds> GetBounds(
        std::size_t modelIndex = 0U) const noexcept;
    [[nodiscard]] VoxelBounds GetGlobalBounds() const noexcept;
    [[nodiscard]] const std::array<VoxelColor, 256U>& GetPalette() const noexcept;
    [[nodiscard]] std::optional<VoxelColor> GetPaletteColor(
        std::size_t paletteIndex) const noexcept;
    [[nodiscard]] VoxelDocumentPaletteSnapshot GetPaletteSnapshot()
        const noexcept;
    [[nodiscard]] std::uint32_t UsedPaletteColorCount() const noexcept;
    [[nodiscard]] bool IsDirty() const noexcept;
    [[nodiscard]] std::uint64_t GetRevision() const noexcept;

    [[nodiscard]] VoxelDocumentOperationResult SetVoxel(
        const VoxelPosition& position,
        std::size_t paletteIndex,
        std::size_t modelIndex = 0U);
    [[nodiscard]] VoxelDocumentOperationResult RemoveVoxel(
        const VoxelPosition& position,
        std::size_t modelIndex = 0U);
    [[nodiscard]] VoxelDocumentOperationResult ReplaceVoxelColor(
        const VoxelPosition& position,
        std::size_t paletteIndex,
        std::size_t modelIndex = 0U);
    [[nodiscard]] VoxelDocumentOperationResult SetPaletteColor(
        std::size_t paletteIndex,
        VoxelColor color);
    [[nodiscard]] VoxelDocumentOperationResult ValidatePaletteSnapshot(
        const VoxelDocumentPaletteSnapshot& snapshot) const;
    [[nodiscard]] VoxelDocumentOperationResult ReplacePalette(
        const VoxelDocumentPaletteSnapshot& snapshot);
    [[nodiscard]] VoxelDocumentOperationResult ApplyVoxelChanges(
        std::span<const VoxelDocumentChange> changes);
    [[nodiscard]] VoxelDocumentOperationResult ApplyCompositeChanges(
        std::span<const VoxelDocumentChange> voxelChanges,
        const VoxelDocumentPaletteChange* paletteChange,
        VoxelDocumentCompositeOrder order =
            VoxelDocumentCompositeOrder::PaletteThenVoxels);
    void MarkSaved() noexcept;
    void UpdateDirtyFromHistory(bool isAtSavedState) noexcept;

private:
    friend class VoxDocumentLoader;

    [[nodiscard]] VoxelDocumentOperationResult ValidateMutation(
        const VoxelPosition& position,
        std::size_t paletteIndex,
        std::size_t modelIndex) const;
    void RecordChange() noexcept;

    std::filesystem::path sourcePath_;
    std::optional<std::string> assetId_;
    std::uint32_t voxVersion_ = 0U;
    std::array<VoxelColor, 256U> palette_{};
    std::vector<VoxelSubModel> models_;
    std::uint64_t voxelCount_ = 0U;
    std::uint64_t revision_ = 0U;
    bool hasCustomPalette_ = false;
    bool dirty_ = false;
};

} // namespace VoxelForge::Asset::Voxel
