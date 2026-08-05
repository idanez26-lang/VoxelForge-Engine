#include "VoxelForge/Mesh/VoxelChangeOverlay.h"

#include <algorithm>
#include <new>

namespace VoxelForge::Mesh
{
namespace
{
using Asset::Voxel::VoxelBounds;
using Asset::Voxel::VoxelPosition;

void ExtendBounds(VoxelBounds& bounds, const VoxelPosition& position) noexcept
{
    if (!bounds.HasValue)
    {
        bounds.HasValue = true;
        bounds.Minimum = position;
        bounds.Maximum = position;
        return;
    }
    bounds.Minimum.X = std::min(bounds.Minimum.X, position.X);
    bounds.Minimum.Y = std::min(bounds.Minimum.Y, position.Y);
    bounds.Minimum.Z = std::min(bounds.Minimum.Z, position.Z);
    bounds.Maximum.X = std::max(bounds.Maximum.X, position.X);
    bounds.Maximum.Y = std::max(bounds.Maximum.Y, position.Y);
    bounds.Maximum.Z = std::max(bounds.Maximum.Z, position.Z);
}

void DilateByOne(VoxelBounds& bounds) noexcept
{
    if (!bounds.HasValue) return;
    --bounds.Minimum.X;
    --bounds.Minimum.Y;
    --bounds.Minimum.Z;
    ++bounds.Maximum.X;
    ++bounds.Maximum.Y;
    ++bounds.Maximum.Z;
}

} // namespace

std::optional<VoxelChangeOverlay> VoxelChangeOverlay::TryCreate(
    const Asset::Voxel::VoxelDocument& document,
    const std::span<const Asset::Voxel::VoxelDocumentChange> changes,
    const std::size_t modelIndex,
    Asset::Voxel::VoxelDocumentOperationResult& validation)
{
    const Asset::Voxel::VoxelSubModel* const model =
        document.GetModel(modelIndex);
    if (model == nullptr)
    {
        validation = {false, false,
            Asset::Voxel::VoxelDocumentError::InvalidModelIndex,
            "Voxel change sub-model index is invalid."};
        return std::nullopt;
    }

    // Mêmes règles de rejet que le commit, même message : une seule
    // implémentation, donc aucune divergence possible entre preview et commit.
    validation = document.ValidateVoxelChanges(changes);
    if (!validation.Succeeded)
    {
        return std::nullopt;
    }

    // La vue ne couvre qu'un sous-modèle : un changement destiné à un autre
    // n'aurait aucun effet sur le maillage produit, ce serait un mensonge
    // silencieux. On le refuse.
    for (const Asset::Voxel::VoxelDocumentChange& change : changes)
    {
        if (change.SubModelIndex != modelIndex)
        {
            validation = {false, false,
                Asset::Voxel::VoxelDocumentError::InvalidModelIndex,
                "Voxel change targets another sub-model than the overlay."};
            return std::nullopt;
        }
    }

    VoxelChangeOverlay overlay;
    overlay.model_ = model;
    overlay.bounds_ = model->Bounds();
    overlay.voxelCount_ = model->VoxelCount();
    try
    {
        overlay.overrides_.reserve(changes.size());
        for (const Asset::Voxel::VoxelDocumentChange& change : changes)
        {
            if (change.ExistsAfter)
            {
                overlay.overrides_.insert_or_assign(change.Position,
                    Asset::Voxel::Voxel{change.PaletteIndexAfter});
                // Un ajout peut sortir des bornes courantes ; le clipping
                // régional du constructeur de mesh l'effacerait sinon.
                if (!change.ExistedBefore)
                {
                    ExtendBounds(overlay.bounds_, change.Position);
                    ++overlay.voxelCount_;
                }
            }
            else
            {
                overlay.overrides_.insert_or_assign(
                    change.Position, std::nullopt);
                if (change.ExistedBefore && overlay.voxelCount_ > 0U)
                {
                    // Les bornes ne sont PAS rétrécies : un sur-ensemble reste
                    // correct, et le recalcul coûterait un parcours complet.
                    --overlay.voxelCount_;
                }
            }
            ExtendBounds(overlay.dirtyBounds_, change.Position);
        }
    }
    catch (const std::bad_alloc&)
    {
        validation = {false, false,
            Asset::Voxel::VoxelDocumentError::AllocationFailure,
            "Unable to allocate the voxel change overlay."};
        return std::nullopt;
    }
    DilateByOne(overlay.dirtyBounds_);
    return overlay;
}

bool VoxelChangeOverlay::HasVoxel(
    const Asset::Voxel::VoxelPosition& position) const noexcept
{
    const auto found = overrides_.find(position);
    if (found != overrides_.end())
    {
        return found->second.has_value();
    }
    return model_ != nullptr && model_->HasVoxel(position);
}

std::optional<Asset::Voxel::Voxel> VoxelChangeOverlay::GetVoxel(
    const Asset::Voxel::VoxelPosition& position) const noexcept
{
    const auto found = overrides_.find(position);
    if (found != overrides_.end())
    {
        return found->second;
    }
    return model_ != nullptr ? model_->GetVoxel(position) : std::nullopt;
}

const Asset::Voxel::VoxelBounds& VoxelChangeOverlay::Bounds() const noexcept
{
    return bounds_;
}

const Asset::Voxel::VoxelBounds&
VoxelChangeOverlay::DirtyBounds() const noexcept
{
    return dirtyBounds_;
}

std::uint64_t VoxelChangeOverlay::VoxelCount() const noexcept
{
    return voxelCount_;
}

bool VoxelChangeOverlay::Empty() const noexcept
{
    return overrides_.empty();
}

void VoxelChangeOverlay::ForEachVoxel(
    const std::function<void(
        const Asset::Voxel::VoxelPosition&,
        const Asset::Voxel::Voxel&)>& visitor) const
{
    if (model_ != nullptr)
    {
        model_->ForEachVoxel(
            [this, &visitor](
                const Asset::Voxel::VoxelPosition& position,
                const Asset::Voxel::Voxel& voxel)
            {
                // Une position couverte par un override est servie plus bas,
                // dans son état final : on l'ignore ici pour ne pas la
                // visiter deux fois.
                if (overrides_.find(position) != overrides_.end()) return;
                visitor(position, voxel);
            });
    }
    for (const auto& [position, voxel] : overrides_)
    {
        if (voxel) visitor(position, *voxel);
    }
}

} // namespace VoxelForge::Mesh
