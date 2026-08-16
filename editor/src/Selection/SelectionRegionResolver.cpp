#include "SelectionRegionResolver.h"

#include <algorithm>
#include <array>
#include <deque>
#include <unordered_set>

namespace VoxelForge::Editor
{
namespace
{
using Position = Asset::Voxel::VoxelPosition;

struct PositionHash final
{
    std::size_t operator()(const Position p) const noexcept
    {
        return static_cast<std::size_t>(static_cast<std::uint32_t>(p.X)) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(p.Y)) << 21U) ^
            (static_cast<std::size_t>(static_cast<std::uint32_t>(p.Z)) << 42U);
    }
};

// Voisinages volumiques : 6 = faces, l'analogue exact du 4 planaire ;
// 26 = faces + aretes + coins, l'analogue du 8. Tables construites une fois.
[[nodiscard]] std::vector<Position> VolumeOffsets(
    const SelectionVolumeConnectivity connectivity)
{
    std::vector<Position> offsets;
    if (connectivity == SelectionVolumeConnectivity::Six)
    {
        offsets = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                   {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        return offsets;
    }
    offsets.reserve(26U);
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            for (int z = -1; z <= 1; ++z)
                if (x != 0 || y != 0 || z != 0)
                    offsets.push_back({x, y, z});
    return offsets;
}

// Voisinages planaires dans le plan perpendiculaire a `axis`.
[[nodiscard]] std::vector<Position> PlanarOffsets(
    const int axis, const SelectionPlanarConnectivity connectivity)
{
    const auto inPlane = [axis](const int u, const int v) -> Position
    {
        switch (axis)
        {
        case 0: return {0, u, v};
        case 2: return {u, v, 0};
        default: return {u, 0, v};
        }
    };
    std::vector<Position> offsets{
        inPlane(1, 0), inPlane(-1, 0), inPlane(0, 1), inPlane(0, -1)};
    if (connectivity == SelectionPlanarConnectivity::Eight)
    {
        offsets.push_back(inPlane(1, 1));
        offsets.push_back(inPlane(1, -1));
        offsets.push_back(inPlane(-1, 1));
        offsets.push_back(inPlane(-1, -1));
    }
    return offsets;
}

[[nodiscard]] Position Offset(const Position base, const Position delta) noexcept
{
    return {base.X + delta.X, base.Y + delta.Y, base.Z + delta.Z};
}
} // namespace

SelectionRegionResult ResolveSelectionRegion(
    const SelectionRegionRequest& request)
{
    SelectionRegionResult result;
    if (!request.ReadVoxel) return result;

    const std::optional<std::uint8_t> seedColor =
        request.ReadVoxel(request.Seed);
    if (!seedColor) return result;   // graine hors matiere : region vide

    // --- Cible Color : globale, sans connectivite ------------------------
    if (request.Target == SelectionRegionTarget::Color)
    {
        if (!request.ForEachVoxel) return result;
        request.ForEachVoxel(
            [&result, &request, seed = *seedColor](
                const Position position, const std::uint8_t color)
            {
                if (color != seed) return;
                if (result.Positions.size() >= request.MaximumCells)
                {
                    result.TruncatedByLimit = true;
                    return;
                }
                result.Positions.push_back(position);
            });
        std::sort(result.Positions.begin(), result.Positions.end(),
            [](const Position a, const Position b) noexcept
            {
                return a.X != b.X ? a.X < b.X
                     : a.Y != b.Y ? a.Y < b.Y : a.Z < b.Z;
            });
        return result;
    }

    // --- Cibles Volume et Face : propagation par graine -------------------
    const bool planar = request.Target == SelectionRegionTarget::Face;
    const int axis = DominantAxisOf(
        request.NormalX, request.NormalY, request.NormalZ);
    // La face visee regarde vers +normale ou -normale : « exposee » signifie
    // qu'aucun voxel n'occupe la cellule immediatement du cote de la normale.
    const int sign = (axis == 0 ? request.NormalX
        : axis == 1 ? request.NormalY : request.NormalZ) < 0.0F ? -1 : 1;
    const Position outward = axis == 0 ? Position{sign, 0, 0}
        : axis == 1 ? Position{0, sign, 0} : Position{0, 0, sign};

    const std::vector<Position> offsets = planar
        ? PlanarOffsets(axis, request.Planar)
        : VolumeOffsets(request.Volume);

    const auto accepts = [&request, seed = *seedColor, planar, outward](
        const Position position) -> bool
    {
        const std::optional<std::uint8_t> color = request.ReadVoxel(position);
        if (!color) return false;
        if (request.Criterion == SelectionRegionCriterion::Color &&
            *color != seed)
            return false;
        // Cible Face : la cellule doit APPARTENIR a la face visible — donc
        // etre exposee du cote de la normale. Sans ce test, la propagation
        // planaire traverserait l'interieur du modele.
        if (planar && request.ReadVoxel(Offset(position, outward)).has_value())
            return false;
        return true;
    };

    if (!accepts(request.Seed)) return result;

    std::unordered_set<Position, PositionHash> visited;
    std::deque<Position> pending;
    visited.insert(request.Seed);
    pending.push_back(request.Seed);
    result.Positions.push_back(request.Seed);

    while (!pending.empty())
    {
        const Position current = pending.front();
        pending.pop_front();
        for (const Position delta : offsets)
        {
            const Position next = Offset(current, delta);
            if (visited.contains(next)) continue;
            if (!accepts(next)) continue;
            if (result.Positions.size() >= request.MaximumCells)
            {
                result.TruncatedByLimit = true;
                pending.clear();
                break;
            }
            visited.insert(next);
            pending.push_back(next);
            result.Positions.push_back(next);
        }
    }

    std::sort(result.Positions.begin(), result.Positions.end(),
        [](const Position a, const Position b) noexcept
        {
            return a.X != b.X ? a.X < b.X
                 : a.Y != b.Y ? a.Y < b.Y : a.Z < b.Z;
        });
    return result;
}

} // namespace VoxelForge::Editor
