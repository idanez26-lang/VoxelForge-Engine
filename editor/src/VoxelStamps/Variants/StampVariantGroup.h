#pragma once

#include "VoxelStamps/Library/IStampLibraryRepository.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class StampVariantSelectionMode : std::uint8_t
{
    Fixed,
    Sequential,
    Random,
    Weighted
};

// V1 derives every non-fixed choice from one placement-session seed.  The
// explicit policy keeps the serialized domain open to future seed policies
// without making the resolver depend on placement state.
enum class StampVariantSeedPolicy : std::uint8_t
{
    PlacementSession
};

enum class StampVariantSourceState : std::uint8_t
{
    Available,
    Missing,
    ContentHashMismatch
};

struct StampVariant final
{
    Core::UUID Id{0U};
    // The portable reference carries StampUUID and ExpectedContentHash.
    StampAssetReference Stamp;
    double Weight = 1.0;
    bool Enabled = true;
    std::uint32_t DisplayOrder = 0U;
    StampVariantSourceState SourceState = StampVariantSourceState::Available;

    [[nodiscard]] bool operator==(const StampVariant&) const noexcept = default;
};

struct StampVariantGroupDefinition final
{
    Core::UUID Id{0U};
    std::string Name;
    std::string Category;
    std::vector<std::string> Tags;
    std::optional<Core::UUID> PrimaryThumbnailStampId;
    StampVariantSelectionMode SelectionMode =
        StampVariantSelectionMode::Sequential;
    StampVariantSeedPolicy SeedPolicy =
        StampVariantSeedPolicy::PlacementSession;
    std::uint64_t Revision = 0U;
    std::optional<Core::UUID> FixedVariantId;
    std::vector<StampVariant> Variants;
};

enum class StampVariantGroupError : std::uint8_t
{
    None,
    InvalidGroupIdentity,
    InvalidSelectionMode,
    UnsupportedSeedPolicy,
    InvalidThumbnailIdentity,
    InvalidFixedVariantIdentity,
    InvalidVariantIdentity,
    DuplicateVariantIdentity,
    InvalidStampReference,
    InvalidSourceState
};

[[nodiscard]] constexpr std::string_view StampVariantGroupErrorMessage(
    const StampVariantGroupError error) noexcept
{
    switch (error)
    {
    case StampVariantGroupError::None:
        return "Stamp Variant group data is valid.";
    case StampVariantGroupError::InvalidGroupIdentity:
        return "Stamp Variant groups require a non-zero group UUID.";
    case StampVariantGroupError::InvalidSelectionMode:
        return "Stamp Variant group selection mode is not supported.";
    case StampVariantGroupError::UnsupportedSeedPolicy:
        return "Stamp Variant group seed policy is not supported in V1.";
    case StampVariantGroupError::InvalidThumbnailIdentity:
        return "A configured primary thumbnail requires a non-zero Stamp UUID.";
    case StampVariantGroupError::InvalidFixedVariantIdentity:
        return "A configured fixed variant requires a non-zero variant UUID.";
    case StampVariantGroupError::InvalidVariantIdentity:
        return "Every Stamp Variant requires a non-zero variant UUID.";
    case StampVariantGroupError::DuplicateVariantIdentity:
        return "Variant UUIDs must be unique inside one group.";
    case StampVariantGroupError::InvalidStampReference:
        return "Every Stamp Variant requires a portable library reference with UUID, hash, and .vfstamp path.";
    case StampVariantGroupError::InvalidSourceState:
        return "Stamp Variant source state is not supported.";
    }
    return "Unknown Stamp Variant group error.";
}

struct StampVariantGroupValidation final
{
    StampVariantGroupError Error = StampVariantGroupError::None;
    std::string_view Message{
        StampVariantGroupErrorMessage(StampVariantGroupError::None)};
    std::optional<Core::UUID> VariantId;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return Error == StampVariantGroupError::None;
    }
};

class StampVariantGroup final
{
public:
    [[nodiscard]] static StampVariantGroupValidation Validate(
        const StampVariantGroupDefinition& definition);

    [[nodiscard]] static std::optional<StampVariantGroup> TryCreate(
        StampVariantGroupDefinition definition,
        StampVariantGroupValidation* validation = nullptr);

    [[nodiscard]] const Core::UUID& Id() const noexcept { return definition_.Id; }
    [[nodiscard]] const std::string& Name() const noexcept { return definition_.Name; }
    [[nodiscard]] const std::string& Category() const noexcept
    {
        return definition_.Category;
    }
    [[nodiscard]] const std::vector<std::string>& Tags() const noexcept
    {
        return definition_.Tags;
    }
    [[nodiscard]] const std::optional<Core::UUID>& PrimaryThumbnailStampId()
        const noexcept
    {
        return definition_.PrimaryThumbnailStampId;
    }
    [[nodiscard]] StampVariantSelectionMode SelectionMode() const noexcept
    {
        return definition_.SelectionMode;
    }
    [[nodiscard]] StampVariantSeedPolicy SeedPolicy() const noexcept
    {
        return definition_.SeedPolicy;
    }
    [[nodiscard]] std::uint64_t Revision() const noexcept
    {
        return definition_.Revision;
    }
    [[nodiscard]] const std::optional<Core::UUID>& FixedVariantId() const noexcept
    {
        return definition_.FixedVariantId;
    }
    [[nodiscard]] const std::vector<StampVariant>& Variants() const noexcept
    {
        return definition_.Variants;
    }

private:
    explicit StampVariantGroup(StampVariantGroupDefinition definition)
        : definition_(std::move(definition))
    {
    }

    StampVariantGroupDefinition definition_;
};

} // namespace VoxelForge::Editor::Stamps
