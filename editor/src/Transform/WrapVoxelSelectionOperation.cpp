#include "WrapVoxelSelectionOperation.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace VoxelForge::Editor
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;
using Voxel = Asset::Voxel::Voxel;

// Application par axe : pour chaque coordonnee SOURCE c du motif, la liste
// croissante des coordonnees DESTINATION d de la cible telles que p(d) == c.
// Construite en deux passes sur l'etendue de la cible ; le nombre de
// destinations produites par un voxel source est le produit des tailles de
// ses trois listes — c'est ce qui rend le compte et l'enumeration
// proportionnels au RESULTAT, jamais au volume.
struct AxisMap final
{
    std::int32_t SourceMinimum = 0;
    std::vector<std::uint32_t> Offsets;      // etendue source + 1
    std::vector<std::int32_t> Coordinates;   // destinations groupees par source

    [[nodiscard]] std::span<const std::int32_t> For(
        const std::int32_t sourceCoordinate) const noexcept
    {
        const std::size_t index =
            static_cast<std::size_t>(sourceCoordinate - SourceMinimum);
        if (index + 1U >= Offsets.size()) return {};
        return std::span<const std::int32_t>(Coordinates).subspan(
            Offsets[index], Offsets[index + 1U] - Offsets[index]);
    }
};

AxisMap BuildAxisMap(
    const std::int32_t sourceMinimum, const std::int32_t sourceMaximum,
    const std::int32_t targetMinimum, const std::int32_t targetMaximum,
    const WrapAxisOptions& axis)
{
    AxisMap map;
    map.SourceMinimum = sourceMinimum;
    const std::size_t extent =
        static_cast<std::size_t>(sourceMaximum - sourceMinimum + 1);
    map.Offsets.assign(extent + 1U, 0U);
    for (std::int32_t d = targetMinimum; d <= targetMaximum; ++d)
    {
        const std::int32_t c = WrapVoxelSelectionOperation::LocalPatternCoordinate(
            d, sourceMinimum, sourceMaximum, axis);
        if (c >= 0) ++map.Offsets[static_cast<std::size_t>(c - sourceMinimum) + 1U];
    }
    for (std::size_t index = 1U; index < map.Offsets.size(); ++index)
        map.Offsets[index] += map.Offsets[index - 1U];
    map.Coordinates.assign(map.Offsets.back(), 0);
    std::vector<std::uint32_t> cursor(map.Offsets.begin(), map.Offsets.end() - 1);
    for (std::int32_t d = targetMinimum; d <= targetMaximum; ++d)
    {
        const std::int32_t c = WrapVoxelSelectionOperation::LocalPatternCoordinate(
            d, sourceMinimum, sourceMaximum, axis);
        if (c >= 0)
            map.Coordinates[cursor[static_cast<std::size_t>(c - sourceMinimum)]++] = d;
    }
    return map;
}

struct AxisMaps final
{
    AxisMap X, Y, Z;
};

AxisMaps BuildAxisMaps(
    const SelectionBounds& source, const SelectionBounds& target,
    const VoxelWrapOptions& options)
{
    return {
        BuildAxisMap(source.Minimum.X, source.Maximum.X,
            target.Minimum.X, target.Maximum.X, options.X),
        BuildAxisMap(source.Minimum.Y, source.Maximum.Y,
            target.Minimum.Y, target.Maximum.Y, options.Y),
        BuildAxisMap(source.Minimum.Z, source.Maximum.Z,
            target.Minimum.Z, target.Maximum.Z, options.Z)};
}

[[nodiscard]] std::string ValidateInputs(
    const WrapPatternIndex& pattern, const SelectionBounds newBounds,
    const VoxelWrapOptions& options)
{
    if (!pattern.Valid() || !newBounds.Valid)
        return "Wrap requires a captured selection with valid source and target bounds.";
    if (options.X.Spacing < 0 || options.Y.Spacing < 0 || options.Z.Spacing < 0)
        return "Wrap spacing must be zero or positive.";
    return {};
}

[[nodiscard]] bool Intersects(
    const SelectionBounds& a, const SelectionBounds& b) noexcept
{
    return a.Valid && b.Valid &&
        a.Minimum.X <= b.Maximum.X && b.Minimum.X <= a.Maximum.X &&
        a.Minimum.Y <= b.Maximum.Y && b.Minimum.Y <= a.Maximum.Y &&
        a.Minimum.Z <= b.Maximum.Z && b.Minimum.Z <= a.Maximum.Z;
}

// Decompose A \ B en au plus six boites disjointes (tranches X, puis Y
// restreintes a l'intersection en X, puis Z restreintes en X et Y).
template <typename Fn>
void ForEachBoxInDifference(
    const SelectionBounds& a, const SelectionBounds& b, Fn&& fn)
{
    if (!a.Valid) return;
    if (!Intersects(a, b)) { fn(a); return; }
    const auto box = [](const Position minimum, const Position maximum)
    {
        return SelectionBounds::FromCorners(minimum, maximum);
    };
    if (a.Minimum.X < b.Minimum.X)
        fn(box(a.Minimum, {b.Minimum.X - 1, a.Maximum.Y, a.Maximum.Z}));
    if (a.Maximum.X > b.Maximum.X)
        fn(box({b.Maximum.X + 1, a.Minimum.Y, a.Minimum.Z}, a.Maximum));
    const std::int32_t xLow = std::max(a.Minimum.X, b.Minimum.X);
    const std::int32_t xHigh = std::min(a.Maximum.X, b.Maximum.X);
    if (a.Minimum.Y < b.Minimum.Y)
        fn(box({xLow, a.Minimum.Y, a.Minimum.Z},
            {xHigh, b.Minimum.Y - 1, a.Maximum.Z}));
    if (a.Maximum.Y > b.Maximum.Y)
        fn(box({xLow, b.Maximum.Y + 1, a.Minimum.Z},
            {xHigh, a.Maximum.Y, a.Maximum.Z}));
    const std::int32_t yLow = std::max(a.Minimum.Y, b.Minimum.Y);
    const std::int32_t yHigh = std::min(a.Maximum.Y, b.Maximum.Y);
    if (a.Minimum.Z < b.Minimum.Z)
        fn(box({xLow, yLow, a.Minimum.Z}, {xHigh, yHigh, b.Minimum.Z - 1}));
    if (a.Maximum.Z > b.Maximum.Z)
        fn(box({xLow, yLow, b.Maximum.Z + 1}, {xHigh, yHigh, a.Maximum.Z}));
}

[[nodiscard]] SelectionBounds BoundsOf(
    const std::span<const Position> positions) noexcept
{
    if (positions.empty()) return {};
    Position minimum = positions.front();
    Position maximum = minimum;
    for (const Position p : positions.subspan(1U))
    {
        minimum.X = std::min(minimum.X, p.X);
        minimum.Y = std::min(minimum.Y, p.Y);
        minimum.Z = std::min(minimum.Z, p.Z);
        maximum.X = std::max(maximum.X, p.X);
        maximum.Y = std::max(maximum.Y, p.Y);
        maximum.Z = std::max(maximum.Z, p.Z);
    }
    return SelectionBounds::FromCorners(minimum, maximum);
}

// La destination `p` est-elle une cellule occupee du resultat ?
[[nodiscard]] bool IsOccupiedDestination(
    const WrapPatternIndex& pattern, const Position p,
    const VoxelWrapOptions& options) noexcept
{
    const SelectionBounds& source = pattern.SourceBounds();
    const std::int32_t x = WrapVoxelSelectionOperation::LocalPatternCoordinate(
        p.X, source.Minimum.X, source.Maximum.X, options.X);
    if (x < 0) return false;
    const std::int32_t y = WrapVoxelSelectionOperation::LocalPatternCoordinate(
        p.Y, source.Minimum.Y, source.Maximum.Y, options.Y);
    if (y < 0) return false;
    const std::int32_t z = WrapVoxelSelectionOperation::LocalPatternCoordinate(
        p.Z, source.Minimum.Z, source.Maximum.Z, options.Z);
    if (z < 0) return false;
    return pattern.Find({x, y, z}) != nullptr;
}
} // namespace

// --- WrapPatternIndex ------------------------------------------------------

std::size_t WrapPatternIndex::PositionHash::operator()(
    const Position p) const noexcept
{
    return static_cast<std::size_t>(static_cast<std::uint32_t>(p.X)) ^
        (static_cast<std::size_t>(static_cast<std::uint32_t>(p.Y)) << 21U) ^
        (static_cast<std::size_t>(static_cast<std::uint32_t>(p.Z)) << 42U);
}

WrapPatternIndex::WrapPatternIndex(
    const std::span<const TransformPreviewVoxel> sourceVoxels,
    const SelectionBounds sourceBounds)
    : sources_(sourceVoxels.begin(), sourceVoxels.end()),
      sourceBounds_(sourceBounds)
{
    lookup_.reserve(sources_.size());
    for (std::size_t index = 0U; index < sources_.size(); ++index)
        lookup_.emplace(sources_[index].SourcePosition, index);
}

bool WrapPatternIndex::Valid() const noexcept
{
    return sourceBounds_.Valid && !sources_.empty();
}

const SelectionBounds& WrapPatternIndex::SourceBounds() const noexcept
{
    return sourceBounds_;
}

std::span<const TransformPreviewVoxel> WrapPatternIndex::Sources() const noexcept
{
    return sources_;
}

const TransformPreviewVoxel* WrapPatternIndex::Find(
    const Position position) const noexcept
{
    const auto found = lookup_.find(position);
    return found == lookup_.end() ? nullptr : &sources_[found->second];
}

void WrapPatternIndex::Reset() noexcept
{
    sources_.clear();
    lookup_.clear();
    sourceBounds_ = {};
}

// --- Geometrie -------------------------------------------------------------

std::int32_t WrapVoxelSelectionOperation::LocalPatternCoordinate(
    const std::int32_t destination,
    const std::int32_t sourceMinimum,
    const std::int32_t sourceMaximum,
    const WrapAxisOptions& axis) noexcept
{
    const std::int32_t size = sourceMaximum - sourceMinimum + 1;
    const std::int32_t period = size + axis.Spacing;
    // Distance a l'ancre, toujours >= 0 : la face opposee a l'ancre est la
    // seule qui bouge, donc la destination reste du cote de l'ancre.
    const std::int32_t fromAnchor = axis.Anchor == WrapAxisAnchor::Minimum
        ? destination - sourceMinimum
        : sourceMaximum - destination;
    if (fromAnchor < 0) return -1;   // hors du domaine ancre : rien
    const std::int32_t repetition = fromAnchor / period;
    const std::int32_t cell = fromAnchor % period;
    if (cell >= size) return -1;     // cellule de spacing : vide
    const bool mirrored = axis.MirrorRepeat && (repetition % 2) == 1;
    const std::int32_t local = mirrored ? size - 1 - cell : cell;
    // Retour en coordonnee ABSOLUE du motif source. Depuis l'ancre maximum,
    // la cellule 0 est la colonne maximum du motif.
    return axis.Anchor == WrapAxisAnchor::Minimum
        ? sourceMinimum + local
        : sourceMaximum - local;
}

VoxelWrapSummary WrapVoxelSelectionOperation::Summarize(
    const WrapPatternIndex& pattern,
    const SelectionBounds newBounds,
    const VoxelWrapOptions& options)
{
    VoxelWrapSummary summary;
    summary.RequestedBounds = newBounds;
    summary.Message = ValidateInputs(pattern, newBounds, options);
    if (!summary.Message.empty()) return summary;

    const AxisMaps maps = BuildAxisMaps(pattern.SourceBounds(), newBounds, options);
    Position minimum{}, maximum{};
    bool any = false;
    for (const TransformPreviewVoxel& source : pattern.Sources())
    {
        const auto xs = maps.X.For(source.SourcePosition.X);
        const auto ys = maps.Y.For(source.SourcePosition.Y);
        const auto zs = maps.Z.For(source.SourcePosition.Z);
        if (xs.empty() || ys.empty() || zs.empty()) continue;
        summary.DestinationCount +=
            static_cast<std::uint64_t>(xs.size()) * ys.size() * zs.size();
        const Position low{xs.front(), ys.front(), zs.front()};
        const Position high{xs.back(), ys.back(), zs.back()};
        if (!any) { minimum = low; maximum = high; any = true; continue; }
        minimum.X = std::min(minimum.X, low.X);
        minimum.Y = std::min(minimum.Y, low.Y);
        minimum.Z = std::min(minimum.Z, low.Z);
        maximum.X = std::max(maximum.X, high.X);
        maximum.Y = std::max(maximum.Y, high.Y);
        maximum.Z = std::max(maximum.Z, high.Z);
    }
    if (!any)
    {
        summary.Message =
            "Wrap result is empty: the new bounds contain no pattern voxel.";
        return summary;
    }
    summary.Bounds = SelectionBounds::FromCorners(minimum, maximum);
    return summary;
}

void WrapVoxelSelectionOperation::ForEachDestination(
    const WrapPatternIndex& pattern,
    const SelectionBounds newBounds,
    const VoxelWrapOptions& options,
    const std::function<void(const TransformPreviewDestinationVoxel&)>& sink)
{
    if (!ValidateInputs(pattern, newBounds, options).empty() || !sink) return;
    const AxisMaps maps = BuildAxisMaps(pattern.SourceBounds(), newBounds, options);
    for (const TransformPreviewVoxel& source : pattern.Sources())
    {
        const auto xs = maps.X.For(source.SourcePosition.X);
        const auto ys = maps.Y.For(source.SourcePosition.Y);
        const auto zs = maps.Z.For(source.SourcePosition.Z);
        if (xs.empty() || ys.empty() || zs.empty()) continue;
        for (const std::int32_t x : xs)
            for (const std::int32_t y : ys)
                for (const std::int32_t z : zs)
                    sink({source.SourcePosition, Position{x, y, z},
                        source.Value});
    }
}

VoxelWrapGeometry WrapVoxelSelectionOperation::BuildGeometry(
    const WrapPatternIndex& pattern,
    const SelectionBounds newBounds,
    const VoxelWrapOptions& options)
{
    VoxelWrapGeometry geometry;
    const VoxelWrapSummary summary = Summarize(pattern, newBounds, options);
    geometry.RequestedBounds = summary.RequestedBounds;
    if (!summary.Valid())
    {
        geometry.Message = summary.Message;
        return geometry;
    }
    // Garde d'overflow (pas un plafond produit) : un compte qui ne tient pas
    // dans un vecteur adressable est refuse au lieu de lancer une allocation
    // absurde. Les budgets reels sont ceux de la preview et de l'historique.
    if (summary.DestinationCount >
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        geometry.Message = "Wrap result is too large to materialise.";
        return geometry;
    }
    geometry.Destinations.reserve(
        static_cast<std::size_t>(summary.DestinationCount));
    ForEachDestination(pattern, newBounds, options,
        [&geometry](const TransformPreviewDestinationVoxel& destination)
        {
            geometry.Destinations.push_back(destination);
        });
    geometry.Bounds = summary.Bounds;
    return geometry;
}

VoxelWrapGeometry WrapVoxelSelectionOperation::BuildGeometry(
    const std::span<const TransformPreviewVoxel> sourceVoxels,
    const SelectionBounds sourceBounds,
    const SelectionBounds newBounds,
    const VoxelWrapOptions& options)
{
    return BuildGeometry(
        WrapPatternIndex(sourceVoxels, sourceBounds), newBounds, options);
}

std::size_t WrapVoxelSelectionOperation::EstimateOperationMemory(
    const std::uint64_t destinationCount, const std::size_t sourceCount) noexcept
{
    constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();
    // Meme forme que EstimateVoxelEditOperationMemory : chaque source liberee
    // et chaque destination est au plus un changement ; la transition porte
    // les positions source (Before) et destination (After).
    const std::uint64_t entries = destinationCount + sourceCount;
    const std::uint64_t perEntry =
        sizeof(VoxelChange) + sizeof(Asset::Voxel::VoxelPosition);
    if (entries > (maximum - 256U) / perEntry) return maximum;
    return static_cast<std::size_t>(entries * perEntry) +
        sizeof(VoxelEditOperation) + sizeof(VoxelEditSelectionTransition) + 32U;
}

// --- Suivi des collisions (preview compacte) --------------------------------

void WrapCollisionTracker::Reset() noexcept
{
    signature_ = {};
    previousTarget_ = {};
    positions_.clear();
    state_ = {};
    lastVisited_ = 0U;
    primed_ = false;
}

const WrapCollisionState& WrapCollisionTracker::State() const noexcept
{
    return state_;
}

std::uint64_t WrapCollisionTracker::LastVisited() const noexcept
{
    return lastVisited_;
}

void WrapCollisionTracker::Scan(
    const Asset::Voxel::VoxelDocument& document, const std::size_t modelIndex,
    const WrapPatternIndex& pattern, const SelectionBounds box,
    const VoxelWrapOptions& options, const bool add)
{
    WrapVoxelSelectionOperation::ForEachDestination(pattern, box, options,
        [&](const TransformPreviewDestinationVoxel& destination)
        {
            ++lastVisited_;
            const Position p = destination.DestinationPosition;
            // Meme regle que TransformPreviewModel (IgnoreSource) : une source
            // recouverte n'est pas une collision ; tout autre voxel du
            // document sous une destination en est une. Le test de bornes
            // evite une recherche dans l'index pour tout ce qui est hors du
            // motif source (la quasi-totalite d'un grand resultat).
            if ((pattern.SourceBounds().Contains(p) && pattern.Find(p) != nullptr) ||
                !document.HasVoxel(p, modelIndex))
                return;
            if (add)
            {
                ++state_.Count;
                if (state_.PositionsTracked)
                {
                    if (positions_.size() < PositionLimit)
                        positions_.push_back(p);
                    else
                    {
                        positions_.clear();
                        positions_.shrink_to_fit();
                        state_.PositionsTracked = false;
                    }
                }
            }
            else if (state_.Count > 0U)
            {
                --state_.Count;
            }
        });
}

void WrapCollisionTracker::RefreshBounds(
    const Asset::Voxel::VoxelDocument& document, const std::size_t modelIndex,
    const SelectionBounds target) noexcept
{
    if (state_.Count == 0U) { state_.Bounds = {}; return; }
    if (state_.PositionsTracked)
    {
        state_.Bounds = BoundsOf(positions_);
        return;
    }
    // Trop de collisions pour les suivre une a une : bornes conservatrices,
    // la cible restreinte aux bornes occupees du document. Le compte, lui,
    // reste exact.
    state_.Bounds = target;
    if (const auto documentBounds = document.GetBounds(modelIndex);
        documentBounds && documentBounds->HasValue)
    {
        const SelectionBounds occupied = SelectionBounds::FromCorners(
            documentBounds->Minimum, documentBounds->Maximum);
        if (Intersects(target, occupied))
            state_.Bounds = SelectionBounds::FromCorners(
                {std::max(target.Minimum.X, occupied.Minimum.X),
                 std::max(target.Minimum.Y, occupied.Minimum.Y),
                 std::max(target.Minimum.Z, occupied.Minimum.Z)},
                {std::min(target.Maximum.X, occupied.Maximum.X),
                 std::min(target.Maximum.Y, occupied.Maximum.Y),
                 std::min(target.Maximum.Z, occupied.Maximum.Z)});
    }
}

void WrapCollisionTracker::Recompute(
    const Asset::Voxel::VoxelDocument& document, const std::size_t modelIndex,
    const WrapPatternIndex& pattern, const SelectionBounds target,
    const VoxelWrapOptions& options, const std::uint64_t destinationCount)
{
    positions_.clear();
    state_ = {};
    const std::uint64_t documentCount =
        document.GetVoxelCount(modelIndex).value_or(0U);
    const auto* model = document.GetModel(modelIndex);
    if (model != nullptr && documentCount <= destinationCount)
    {
        // Moins de voxels dans le document que de destinations : visiter le
        // document (borne par son occupation, pas par le volume).
        model->ForEachVoxel([&](const Position p, const Voxel&)
        {
            ++lastVisited_;
            if (!target.Contains(p) ||
                (pattern.SourceBounds().Contains(p) &&
                 pattern.Find(p) != nullptr) ||
                !IsOccupiedDestination(pattern, p, options))
                return;
            ++state_.Count;
            if (state_.PositionsTracked)
            {
                if (positions_.size() < PositionLimit)
                    positions_.push_back(p);
                else
                {
                    positions_.clear();
                    positions_.shrink_to_fit();
                    state_.PositionsTracked = false;
                }
            }
        });
    }
    else
    {
        Scan(document, modelIndex, pattern, target, options, true);
    }
}

WrapCollisionState WrapCollisionTracker::Update(
    const Asset::Voxel::VoxelDocument& document,
    const std::size_t modelIndex,
    const WrapPatternIndex& pattern,
    const SelectionBounds target,
    const VoxelWrapOptions& options,
    const std::uint64_t destinationCount)
{
    lastVisited_ = 0U;
    const Signature signature{
        pattern.SourceBounds(), pattern.Sources().size(), options,
        document.GetRevision(), modelIndex};
    if (!pattern.Valid() || !target.Valid)
    {
        Reset();
        return state_;
    }
    if (!primed_ || signature != signature_ || !previousTarget_.Valid)
    {
        Recompute(document, modelIndex, pattern, target, options,
            destinationCount);
    }
    else if (target != previousTarget_)
    {
        // Cout de la difference contre cout d'un recalcul complet
        // (min(destinations, document)) : le moins cher gagne.
        std::uint64_t differenceCount = 0U;
        ForEachBoxInDifference(target, previousTarget_,
            [&](const SelectionBounds& box)
            {
                differenceCount += WrapVoxelSelectionOperation::Summarize(
                    pattern, box, options).DestinationCount;
            });
        if (!state_.PositionsTracked)
            ForEachBoxInDifference(previousTarget_, target,
                [&](const SelectionBounds& box)
                {
                    differenceCount += WrapVoxelSelectionOperation::Summarize(
                        pattern, box, options).DestinationCount;
                });
        const std::uint64_t documentCount =
            document.GetVoxelCount(modelIndex).value_or(0U);
        if (differenceCount > std::min(destinationCount, documentCount))
        {
            Recompute(document, modelIndex, pattern, target, options,
                destinationCount);
        }
        else
        {
            if (state_.PositionsTracked)
            {
                // Les positions suivies donnent le compte exact des
                // collisions restantes sans visiter la zone retiree.
                std::erase_if(positions_, [&target](const Position p)
                {
                    return !target.Contains(p);
                });
                state_.Count = positions_.size();
            }
            else
            {
                ForEachBoxInDifference(previousTarget_, target,
                    [&](const SelectionBounds& box)
                    {
                        Scan(document, modelIndex, pattern, box, options,
                            false);
                    });
            }
            ForEachBoxInDifference(target, previousTarget_,
                [&](const SelectionBounds& box)
                {
                    Scan(document, modelIndex, pattern, box, options, true);
                });
        }
    }
    signature_ = signature;
    previousTarget_ = target;
    primed_ = true;
    RefreshBounds(document, modelIndex, target);
    return state_;
}

// --- Commit ----------------------------------------------------------------

WrapVoxelSelectionResult WrapVoxelSelectionOperation::Build(
    const Asset::Voxel::VoxelDocument& document,
    const SelectionService& selection,
    const std::uint64_t documentGeneration,
    TransformPreviewModel& preview,
    const SelectionBounds sourceBounds,
    const SelectionBounds newBounds,
    const VoxelWrapOptions& options)
{
    if (!preview.IsActive())
        return {WrapVoxelSelectionResultCode::InvalidPreview, {},
            "Wrap preview is inactive."};

    // LES MEMES bornes source que la preview : les EditableBounds du debut du
    // geste, jamais les bornes serrees des voxels occupes.
    const WrapPatternIndex pattern(preview.SourceVoxels(), sourceBounds);
    const VoxelWrapSummary summary = Summarize(pattern, newBounds, options);
    if (!summary.Valid())
        return {WrapVoxelSelectionResultCode::InvalidGeometry, {},
            summary.Message};
    if (summary.DestinationCount >
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
        return {WrapVoxelSelectionResultCode::InvalidGeometry, {},
            "Wrap result is too large to materialise."};

    // Materialisation exacte, une seule fois, directement dans le tampon du
    // modele — que la preview ait ete exacte ou compacte.
    if (!preview.SetGeneratedVoxelDestinations(
            document, selection, documentGeneration,
            static_cast<std::size_t>(summary.DestinationCount),
            [&](const TransformPreviewDestinationSink& sink)
            {
                ForEachDestination(pattern, newBounds, options, sink);
            }))
        return {WrapVoxelSelectionResultCode::InvalidPreview, {},
            "Wrap could not materialise its result from the preview."};

    // Politique de collision COMMUNE aux Transform (Move/Scale/Rotate/Wrap) :
    // fusion — le resultat transforme est prioritaire sur l'existant.
    TransformOperationRequest request;
    request.Name = "Wrap";
    request.Label = "Wrap selection";
    request.Policy = {TransformSourcePolicy::RemoveSource,
        TransformCollisionPolicy::MergeOverlap};
    request.DestinationBounds = summary.Bounds;
    // Bornes semantiques de l'historique : Undo restaure les EditableBounds
    // source exactes, Redo les bornes demandees exactes.
    request.SourceEditableBounds = sourceBounds;
    request.DestinationEditableBounds = newBounds;

    TransformOperationBuildResult common = TransformOperationBuilder::Build(
        document, selection, documentGeneration, preview, std::move(request));

    WrapVoxelSelectionResult result;
    result.Message = std::move(common.Message);
    result.Operation = std::move(common.Operation);
    switch (common.Code)
    {
    case TransformOperationBuildCode::Ready:
        result.Code = WrapVoxelSelectionResultCode::Ready; break;
    case TransformOperationBuildCode::NoChange:
        result.Code = WrapVoxelSelectionResultCode::NoChange; break;
    case TransformOperationBuildCode::InvalidDestinations:
        result.Code = WrapVoxelSelectionResultCode::InvalidGeometry; break;
    case TransformOperationBuildCode::InvalidPreview:
        result.Code = WrapVoxelSelectionResultCode::InvalidPreview; break;
    case TransformOperationBuildCode::ModelChanged:
        result.Code = WrapVoxelSelectionResultCode::ModelChanged; break;
    case TransformOperationBuildCode::SelectionChanged:
        result.Code = WrapVoxelSelectionResultCode::SelectionChanged; break;
    case TransformOperationBuildCode::Collision:
        result.Code = WrapVoxelSelectionResultCode::Collision; break;
    case TransformOperationBuildCode::OutOfBounds:
        result.Code = WrapVoxelSelectionResultCode::OutOfBounds; break;
    case TransformOperationBuildCode::Failed:
    default:
        result.Code = WrapVoxelSelectionResultCode::Failed; break;
    }
    return result;
}

} // namespace VoxelForge::Editor
