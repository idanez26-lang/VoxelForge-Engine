#include "VoxelStamps/Variants/StampVariantGroup.h"

#include <unordered_set>
#include <utility>

namespace
{
using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;

[[nodiscard]] bool IsSupported(
    const StampVariantSelectionMode mode) noexcept
{
    switch (mode)
    {
    case StampVariantSelectionMode::Fixed:
    case StampVariantSelectionMode::Sequential:
    case StampVariantSelectionMode::Random:
    case StampVariantSelectionMode::Weighted:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsSupported(const StampVariantSourceState state) noexcept
{
    switch (state)
    {
    case StampVariantSourceState::Available:
    case StampVariantSourceState::Missing:
    case StampVariantSourceState::ContentHashMismatch:
        return true;
    }
    return false;
}

[[nodiscard]] bool IsPortableReference(
    const StampAssetReference& reference)
{
    if (reference.Id.Value() == 0U || reference.ContentHash.empty() ||
        reference.RelativePath.empty() || reference.RelativePath.is_absolute() ||
        reference.RelativePath.has_root_name() ||
        reference.RelativePath.extension() != ".vfstamp")
    {
        return false;
    }

    for (const auto& component : reference.RelativePath)
    {
        if (component == "..") return false;
    }
    return true;
}

[[nodiscard]] StampVariantGroupValidation MakeError(
    const StampVariantGroupError error,
    const std::optional<Core::UUID> variantId = std::nullopt) noexcept
{
    return {
        .Error = error,
        .Message = StampVariantGroupErrorMessage(error),
        .VariantId = variantId};
}
}

namespace VoxelForge::Editor::Stamps
{

StampVariantGroupValidation StampVariantGroup::Validate(
    const StampVariantGroupDefinition& definition)
{
    if (definition.Id.Value() == 0U)
    {
        return MakeError(StampVariantGroupError::InvalidGroupIdentity);
    }
    if (!IsSupported(definition.SelectionMode))
    {
        return MakeError(StampVariantGroupError::InvalidSelectionMode);
    }
    if (definition.SeedPolicy != StampVariantSeedPolicy::PlacementSession)
    {
        return MakeError(StampVariantGroupError::UnsupportedSeedPolicy);
    }
    if (definition.PrimaryThumbnailStampId &&
        definition.PrimaryThumbnailStampId->Value() == 0U)
    {
        return MakeError(StampVariantGroupError::InvalidThumbnailIdentity);
    }
    if (definition.FixedVariantId &&
        definition.FixedVariantId->Value() == 0U)
    {
        return MakeError(StampVariantGroupError::InvalidFixedVariantIdentity);
    }

    std::unordered_set<std::uint64_t> variantIds;
    variantIds.reserve(definition.Variants.size());
    for (const StampVariant& variant : definition.Variants)
    {
        if (variant.Id.Value() == 0U)
        {
            return MakeError(
                StampVariantGroupError::InvalidVariantIdentity, variant.Id);
        }
        if (!variantIds.insert(variant.Id.Value()).second)
        {
            return MakeError(
                StampVariantGroupError::DuplicateVariantIdentity, variant.Id);
        }
        if (!IsPortableReference(variant.Stamp))
        {
            return MakeError(
                StampVariantGroupError::InvalidStampReference, variant.Id);
        }
        if (!IsSupported(variant.SourceState))
        {
            return MakeError(
                StampVariantGroupError::InvalidSourceState, variant.Id);
        }
    }
    return {};
}

std::optional<StampVariantGroup> StampVariantGroup::TryCreate(
    StampVariantGroupDefinition definition,
    StampVariantGroupValidation* validation)
{
    const StampVariantGroupValidation result = Validate(definition);
    if (validation != nullptr) *validation = result;
    if (!result.IsValid()) return std::nullopt;
    return StampVariantGroup{std::move(definition)};
}

} // namespace VoxelForge::Editor::Stamps
