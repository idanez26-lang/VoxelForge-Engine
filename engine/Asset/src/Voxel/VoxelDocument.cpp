#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"

#include <algorithm>
#include <array>
#include <functional>
#include <unordered_set>

namespace VoxelForge::Asset::Voxel
{
namespace
{
VoxelDocumentOperationResult Success(
    const bool changed,
    std::string message)
{
    return {true, changed, VoxelDocumentError::None, std::move(message)};
}

VoxelDocumentOperationResult Failure(
    const VoxelDocumentError error,
    std::string message)
{
    return {false, false, error, std::move(message)};
}
}

std::size_t VoxelPositionHash::operator()(
    const VoxelPosition& position) const noexcept
{
    const auto combine = [](std::size_t seed, const std::size_t value)
    {
        return seed ^ (value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U));
    };
    std::size_t result = std::hash<std::int32_t>{}(position.X);
    result = combine(result, std::hash<std::int32_t>{}(position.Y));
    return combine(result, std::hash<std::int32_t>{}(position.Z));
}

const VoxelDimensions& VoxelSubModel::Dimensions() const noexcept
{
    return dimensions_;
}

std::size_t VoxelSubModel::VoxelCount() const noexcept
{
    return voxels_.size();
}

const VoxelBounds& VoxelSubModel::Bounds() const noexcept
{
    return bounds_;
}

bool VoxelSubModel::HasVoxel(const VoxelPosition& position) const noexcept
{
    return Contains(position) && voxels_.contains(position);
}

std::optional<Voxel> VoxelSubModel::GetVoxel(
    const VoxelPosition& position) const noexcept
{
    if (!Contains(position)) return std::nullopt;
    const auto found = voxels_.find(position);
    return found == voxels_.end()
        ? std::nullopt
        : std::optional<Voxel>(found->second);
}

bool VoxelSubModel::Contains(const VoxelPosition& position) const noexcept
{
    return position.X >= 0 && position.Y >= 0 && position.Z >= 0 &&
        static_cast<std::uint32_t>(position.X) < dimensions_.X &&
        static_cast<std::uint32_t>(position.Y) < dimensions_.Y &&
        static_cast<std::uint32_t>(position.Z) < dimensions_.Z;
}

void VoxelSubModel::ExtendBounds(const VoxelPosition& position) noexcept
{
    if (!bounds_.HasValue)
    {
        bounds_ = {true, position, position};
        return;
    }
    bounds_.Minimum.X = std::min(bounds_.Minimum.X, position.X);
    bounds_.Minimum.Y = std::min(bounds_.Minimum.Y, position.Y);
    bounds_.Minimum.Z = std::min(bounds_.Minimum.Z, position.Z);
    bounds_.Maximum.X = std::max(bounds_.Maximum.X, position.X);
    bounds_.Maximum.Y = std::max(bounds_.Maximum.Y, position.Y);
    bounds_.Maximum.Z = std::max(bounds_.Maximum.Z, position.Z);
}

void VoxelSubModel::RecalculateBounds() noexcept
{
    bounds_ = {};
    for (const auto& [position, voxel] : voxels_)
    {
        static_cast<void>(voxel);
        ExtendBounds(position);
    }
}

const std::filesystem::path& VoxelDocument::SourcePath() const noexcept
{
    return sourcePath_;
}

const std::optional<std::string>& VoxelDocument::AssetId() const noexcept
{
    return assetId_;
}

std::uint32_t VoxelDocument::VoxVersion() const noexcept
{
    return voxVersion_;
}

bool VoxelDocument::HasCustomPalette() const noexcept
{
    return hasCustomPalette_;
}

std::size_t VoxelDocument::GetModelCount() const noexcept
{
    return models_.size();
}

const VoxelSubModel* VoxelDocument::GetModel(
    const std::size_t modelIndex) const noexcept
{
    return modelIndex < models_.size() ? &models_[modelIndex] : nullptr;
}

std::optional<VoxelDimensions> VoxelDocument::GetDimensions(
    const std::size_t modelIndex) const noexcept
{
    const VoxelSubModel* model = GetModel(modelIndex);
    return model == nullptr
        ? std::nullopt
        : std::optional<VoxelDimensions>(model->Dimensions());
}

std::optional<Voxel> VoxelDocument::GetVoxel(
    const VoxelPosition& position,
    const std::size_t modelIndex) const noexcept
{
    const VoxelSubModel* model = GetModel(modelIndex);
    return model == nullptr ? std::nullopt : model->GetVoxel(position);
}

bool VoxelDocument::HasVoxel(
    const VoxelPosition& position,
    const std::size_t modelIndex) const noexcept
{
    const VoxelSubModel* model = GetModel(modelIndex);
    return model != nullptr && model->HasVoxel(position);
}

std::uint64_t VoxelDocument::GetVoxelCount() const noexcept
{
    return voxelCount_;
}

std::optional<std::size_t> VoxelDocument::GetVoxelCount(
    const std::size_t modelIndex) const noexcept
{
    const VoxelSubModel* model = GetModel(modelIndex);
    return model == nullptr
        ? std::nullopt
        : std::optional<std::size_t>(model->VoxelCount());
}

std::optional<VoxelBounds> VoxelDocument::GetBounds(
    const std::size_t modelIndex) const noexcept
{
    const VoxelSubModel* model = GetModel(modelIndex);
    return model == nullptr
        ? std::nullopt
        : std::optional<VoxelBounds>(model->Bounds());
}

VoxelBounds VoxelDocument::GetGlobalBounds() const noexcept
{
    VoxelBounds result;
    for (const VoxelSubModel& model : models_)
    {
        const VoxelBounds& bounds = model.Bounds();
        if (!bounds.HasValue) continue;
        if (!result.HasValue)
        {
            result = bounds;
            continue;
        }
        result.Minimum.X = std::min(result.Minimum.X, bounds.Minimum.X);
        result.Minimum.Y = std::min(result.Minimum.Y, bounds.Minimum.Y);
        result.Minimum.Z = std::min(result.Minimum.Z, bounds.Minimum.Z);
        result.Maximum.X = std::max(result.Maximum.X, bounds.Maximum.X);
        result.Maximum.Y = std::max(result.Maximum.Y, bounds.Maximum.Y);
        result.Maximum.Z = std::max(result.Maximum.Z, bounds.Maximum.Z);
    }
    return result;
}

const std::array<VoxelColor, 256U>& VoxelDocument::GetPalette() const noexcept
{
    return palette_;
}

std::optional<VoxelColor> VoxelDocument::GetPaletteColor(
    const std::size_t paletteIndex) const noexcept
{
    return paletteIndex < palette_.size()
        ? std::optional<VoxelColor>(palette_[paletteIndex])
        : std::nullopt;
}

VoxelDocumentPaletteSnapshot VoxelDocument::GetPaletteSnapshot() const noexcept
{
    return {palette_, hasCustomPalette_};
}

std::uint32_t VoxelDocument::UsedPaletteColorCount() const noexcept
{
    std::array<bool, 256U> used{};
    for (const VoxelSubModel& model : models_)
    {
        for (const auto& [position, voxel] : model.voxels_)
        {
            static_cast<void>(position);
            used[voxel.PaletteIndex] = true;
        }
    }
    return static_cast<std::uint32_t>(std::count(used.begin(), used.end(), true));
}

bool VoxelDocument::IsDirty() const noexcept
{
    return dirty_;
}

std::uint64_t VoxelDocument::GetRevision() const noexcept
{
    return revision_;
}

VoxelDocumentOperationResult VoxelDocument::SetVoxel(
    const VoxelPosition& position,
    const std::size_t paletteIndex,
    const std::size_t modelIndex)
{
    const VoxelDocumentOperationResult validation =
        ValidateMutation(position, paletteIndex, modelIndex);
    if (!validation) return validation;
    VoxelSubModel& model = models_[modelIndex];
    const auto found = model.voxels_.find(position);
    if (found != model.voxels_.end())
    {
        if (found->second.PaletteIndex == paletteIndex)
            return Success(false, "Voxel already has the requested color.");
        found->second.PaletteIndex = static_cast<std::uint8_t>(paletteIndex);
        RecordChange();
        JournalMutation({{position, false}}, false);
        return Success(true, "Voxel color replaced.");
    }
    model.voxels_.emplace(
        position, Voxel{static_cast<std::uint8_t>(paletteIndex)});
    model.ExtendBounds(position);
    ++voxelCount_;
    RecordChange();
    JournalMutation({{position, true}}, false);
    return Success(true, "Voxel added.");
}

VoxelDocumentOperationResult VoxelDocument::RemoveVoxel(
    const VoxelPosition& position,
    const std::size_t modelIndex)
{
    if (modelIndex >= models_.size())
        return Failure(VoxelDocumentError::InvalidModelIndex,
            "Voxel sub-model index is invalid.");
    VoxelSubModel& model = models_[modelIndex];
    if (!model.Contains(position))
        return Failure(VoxelDocumentError::OutOfBounds,
            "Voxel position lies outside the sub-model dimensions.");
    const auto found = model.voxels_.find(position);
    if (found == model.voxels_.end())
        return Success(false, "Voxel position is already empty.");
    const bool touchesBounds = model.bounds_.HasValue &&
        (position.X == model.bounds_.Minimum.X ||
         position.Y == model.bounds_.Minimum.Y ||
         position.Z == model.bounds_.Minimum.Z ||
         position.X == model.bounds_.Maximum.X ||
         position.Y == model.bounds_.Maximum.Y ||
         position.Z == model.bounds_.Maximum.Z);
    model.voxels_.erase(found);
    --voxelCount_;
    if (touchesBounds) model.RecalculateBounds();
    RecordChange();
    JournalMutation({{position, true}}, false);
    return Success(true, "Voxel removed.");
}

VoxelDocumentOperationResult VoxelDocument::ReplaceVoxelColor(
    const VoxelPosition& position,
    const std::size_t paletteIndex,
    const std::size_t modelIndex)
{
    const VoxelDocumentOperationResult validation =
        ValidateMutation(position, paletteIndex, modelIndex);
    if (!validation) return validation;
    VoxelSubModel& model = models_[modelIndex];
    const auto found = model.voxels_.find(position);
    if (found == model.voxels_.end())
        return Failure(VoxelDocumentError::VoxelNotFound,
            "Cannot replace the color of an empty voxel position.");
    if (found->second.PaletteIndex == paletteIndex)
        return Success(false, "Voxel already has the requested color.");
    found->second.PaletteIndex = static_cast<std::uint8_t>(paletteIndex);
    RecordChange();
    JournalMutation({{position, false}}, false);
    return Success(true, "Voxel color replaced.");
}

VoxelDocumentOperationResult VoxelDocument::SetPaletteColor(
    const std::size_t paletteIndex,
    const VoxelColor color)
{
    if (paletteIndex == 0U || paletteIndex >= palette_.size())
        return Failure(VoxelDocumentError::InvalidPaletteIndex,
            "Palette index 0 is reserved; valid editable indices are 1..255.");
    if (palette_[paletteIndex] == color)
        return Success(false, "Palette color is unchanged.");
    palette_[paletteIndex] = color;
    hasCustomPalette_ = true;
    RecordChange();
    JournalMutation({}, false);
    return Success(true, "Palette color changed.");
}

VoxelDocumentOperationResult VoxelDocument::ValidatePaletteSnapshot(
    const VoxelDocumentPaletteSnapshot& snapshot) const
{
    if (snapshot.Colors[0U] != palette_[0U])
    {
        return Failure(VoxelDocumentError::InvalidPalette,
            "Palette index 0 is reserved and cannot be changed.");
    }
    if (!snapshot.HasCustomPalette &&
        snapshot.Colors != Vox::DefaultVoxPalette())
    {
        return Failure(VoxelDocumentError::InvalidPalette,
            "A default palette snapshot must contain the default VOX palette.");
    }
    return Success(false, "Palette snapshot is valid.");
}

VoxelDocumentOperationResult VoxelDocument::ReplacePalette(
    const VoxelDocumentPaletteSnapshot& snapshot)
{
    const VoxelDocumentOperationResult validation =
        ValidatePaletteSnapshot(snapshot);
    if (!validation) return validation;
    if (GetPaletteSnapshot() == snapshot)
        return Success(false, "Palette is unchanged.");
    palette_ = snapshot.Colors;
    hasCustomPalette_ = snapshot.HasCustomPalette;
    RecordChange();
    JournalMutation({}, false);
    return Success(true, "Palette replaced.");
}

VoxelDocumentOperationResult VoxelDocument::ApplyVoxelChanges(
    const std::span<const VoxelDocumentChange> changes)
{
    return ApplyCompositeChanges(changes, nullptr);
}

// VF-0265 (lot 1) : extrait verbatim d'ApplyCompositeChanges. Constant, sans
// mutation ni journalisation : la preview peut donc l'appeler sans toucher au
// document. Il n'existe plus qu'une seule implémentation des règles de rejet.
VoxelDocumentOperationResult VoxelDocument::ValidateVoxelChanges(
    const std::span<const VoxelDocumentChange> changes) const
{
    struct ChangeKey final
    {
        std::size_t ModelIndex = 0U;
        VoxelPosition Position{};

        [[nodiscard]] bool operator==(
            const ChangeKey&) const noexcept = default;
    };
    struct ChangeKeyHash final
    {
        [[nodiscard]] std::size_t operator()(
            const ChangeKey& key) const noexcept
        {
            const std::size_t positionHash = VoxelPositionHash{}(key.Position);
            return positionHash ^
                (key.ModelIndex + 0x9e3779b9U +
                 (positionHash << 6U) + (positionHash >> 2U));
        }
    };

    std::unordered_set<ChangeKey, ChangeKeyHash> uniqueChanges;
    uniqueChanges.reserve(changes.size());
    for (const VoxelDocumentChange& change : changes)
    {
        if (change.SubModelIndex >= models_.size())
            return Failure(VoxelDocumentError::InvalidModelIndex,
                "Voxel change sub-model index is invalid.");
        const VoxelSubModel& model = models_[change.SubModelIndex];
        if (!model.Contains(change.Position))
            return Failure(VoxelDocumentError::OutOfBounds,
                "Voxel change position lies outside its sub-model dimensions.");
        if ((change.ExistedBefore && change.PaletteIndexBefore == 0U) ||
            (change.ExistsAfter && change.PaletteIndexAfter == 0U))
        {
            return Failure(VoxelDocumentError::InvalidPaletteIndex,
                "Voxel changes require palette indices between 1 and 255.");
        }
        if (change.ExistedBefore == change.ExistsAfter &&
            (!change.ExistedBefore ||
             change.PaletteIndexBefore == change.PaletteIndexAfter))
        {
            return Failure(VoxelDocumentError::DuplicateVoxel,
                "Voxel change has identical before and after states.");
        }
        if (!uniqueChanges.emplace(ChangeKey{
                change.SubModelIndex, change.Position}).second)
        {
            return Failure(VoxelDocumentError::DuplicateVoxel,
                "Voxel change set contains the same position more than once.");
        }
        const std::optional<Voxel> current = model.GetVoxel(change.Position);
        const bool matchesBefore = change.ExistedBefore
            ? current && current->PaletteIndex == change.PaletteIndexBefore
            : !current;
        if (!matchesBefore)
            return Failure(VoxelDocumentError::DuplicateVoxel,
                "Voxel document no longer matches the expected before state.");
    }
    return Success(false, "Voxel changes are applicable.");
}

VoxelDocumentOperationResult VoxelDocument::ApplyCompositeChanges(
    const std::span<const VoxelDocumentChange> voxelChanges,
    const VoxelDocumentPaletteChange* paletteChange,
    const VoxelDocumentCompositeOrder order)
{
    if (voxelChanges.empty() && paletteChange == nullptr)
        return Success(false, "Composite change set is empty.");

    if (paletteChange != nullptr)
    {
        if (paletteChange->Before == paletteChange->After)
        {
            return Failure(VoxelDocumentError::InvalidTransaction,
                "Palette change has identical before and after states.");
        }
        const VoxelDocumentOperationResult beforeValidation =
            ValidatePaletteSnapshot(paletteChange->Before);
        if (!beforeValidation) return beforeValidation;
        const VoxelDocumentOperationResult afterValidation =
            ValidatePaletteSnapshot(paletteChange->After);
        if (!afterValidation) return afterValidation;
        if (GetPaletteSnapshot() != paletteChange->Before)
        {
            return Failure(VoxelDocumentError::StateMismatch,
                "Voxel document palette no longer matches the expected before state.");
        }
    }

    // VF-0265 (lot 1) : les règles de rejet vivent désormais dans
    // ValidateVoxelChanges, seule implémentation. La preview incrémentale
    // l'appellera aussi, ce qui étend « Preview == Commit » jusqu'aux cas
    // d'échec : un refus en preview est un refus au commit, même message.
    if (const VoxelDocumentOperationResult validation =
            ValidateVoxelChanges(voxelChanges);
        !validation.Succeeded)
    {
        return validation;
    }

    std::vector<bool> recalculateBounds(models_.size(), false);
    const auto applyPalette = [&]() noexcept
    {
        if (paletteChange != nullptr)
        {
            palette_ = paletteChange->After.Colors;
            hasCustomPalette_ = paletteChange->After.HasCustomPalette;
        }
    };
    const auto applyVoxels = [&]()
    {
        for (const VoxelDocumentChange& change : voxelChanges)
        {
            VoxelSubModel& model = models_[change.SubModelIndex];
            if (change.ExistedBefore && !change.ExistsAfter)
            {
                model.voxels_.erase(change.Position);
                --voxelCount_;
                recalculateBounds[change.SubModelIndex] = true;
            }
            else if (!change.ExistedBefore && change.ExistsAfter)
            {
                model.voxels_.emplace(
                    change.Position, Voxel{change.PaletteIndexAfter});
                ++voxelCount_;
                model.ExtendBounds(change.Position);
            }
            else
            {
                model.voxels_.at(change.Position).PaletteIndex =
                    change.PaletteIndexAfter;
            }
        }
    };
    if (order == VoxelDocumentCompositeOrder::PaletteThenVoxels)
    {
        applyPalette();
        applyVoxels();
    }
    else
    {
        applyVoxels();
        applyPalette();
    }
    for (std::size_t index = 0U; index < models_.size(); ++index)
    {
        if (recalculateBounds[index]) models_[index].RecalculateBounds();
    }
    RecordChange();
    std::vector<TouchedPosition> journaledPositions;
    const bool journalOverflowed =
        voxelChanges.size() > MaximumJournaledPositionsPerRevision;
    if (!journalOverflowed)
    {
        journaledPositions.reserve(voxelChanges.size());
        for (const VoxelDocumentChange& change : voxelChanges)
        {
            journaledPositions.push_back({
                change.Position,
                change.ExistedBefore != change.ExistsAfter});
        }
    }
    JournalMutation(std::move(journaledPositions), journalOverflowed);
    return Success(true, "Composite voxel document changes applied atomically.");
}

void VoxelDocument::MarkSaved() noexcept
{
    dirty_ = false;
}

void VoxelDocument::UpdateDirtyFromHistory(
    const bool isAtSavedState) noexcept
{
    dirty_ = !isAtSavedState;
}

VoxelDocumentOperationResult VoxelDocument::ValidateMutation(
    const VoxelPosition& position,
    const std::size_t paletteIndex,
    const std::size_t modelIndex) const
{
    if (modelIndex >= models_.size())
        return Failure(VoxelDocumentError::InvalidModelIndex,
            "Voxel sub-model index is invalid.");
    if (!models_[modelIndex].Contains(position))
        return Failure(VoxelDocumentError::OutOfBounds,
            "Voxel position lies outside the sub-model dimensions.");
    if (paletteIndex == 0U || paletteIndex >= palette_.size())
        return Failure(VoxelDocumentError::InvalidPaletteIndex,
            "Voxel palette index must be between 1 and 255.");
    return Success(false, "Voxel mutation is valid.");
}

void VoxelDocument::RecordChange() noexcept
{
    dirty_ = true;
    ++revision_;
}

void VoxelDocument::JournalMutation(
    std::vector<TouchedPosition> positions,
    const bool overflowed)
{
    revisionJournal_.push_back(
        RevisionDelta{revision_, std::move(positions), overflowed});
    while (revisionJournal_.size() > MaximumJournaledRevisions)
        revisionJournal_.pop_front();
}

std::optional<std::vector<VoxelDocument::TouchedPosition>>
VoxelDocument::ChangesSince(const std::uint64_t sinceRevision) const
{
    if (sinceRevision > revision_) return std::nullopt;
    if (sinceRevision == revision_) return std::vector<TouchedPosition>{};
    // Every RecordChange() site journals exactly one delta, so journal
    // revisions are consecutive. The ring must therefore still contain the
    // delta for `sinceRevision + 1`; otherwise history was evicted.
    if (revisionJournal_.empty() ||
        revisionJournal_.front().Revision > sinceRevision + 1U)
        return std::nullopt;
    std::vector<TouchedPosition> aggregated;
    for (const RevisionDelta& delta : revisionJournal_)
    {
        if (delta.Revision <= sinceRevision) continue;
        if (delta.Overflowed) return std::nullopt;
        aggregated.insert(aggregated.end(),
            delta.Positions.begin(), delta.Positions.end());
    }
    return aggregated;
}

} // namespace VoxelForge::Asset::Voxel
