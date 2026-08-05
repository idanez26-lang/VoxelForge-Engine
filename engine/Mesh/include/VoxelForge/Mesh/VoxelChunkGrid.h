#pragma once

#include "VoxelForge/Asset/Voxel/VoxelDocument.h"

#include <cstdint>
#include <tuple>

namespace VoxelForge::Mesh
{

// VF-0265 (lot 1) : découpage en chunks, mis en commun.
//
// Cette arithmétique était privée à VoxelDocumentMeshCache. Le compositeur de
// preview incrémental doit dériver exactement les mêmes chunks que le cache du
// document, sinon les deux divergeront un jour sur la frontière d'un chunk —
// et le symptôme serait une face manquante ou dédoublée, silencieuse.
// Une seule définition, partagée, ferme cette porte.

inline constexpr std::int32_t VoxelChunkEdgeLength = 32;

struct VoxelChunkKey final
{
    std::int32_t X = 0;
    std::int32_t Y = 0;
    std::int32_t Z = 0;

    [[nodiscard]] bool operator==(const VoxelChunkKey&) const noexcept = default;

    // Ordre total : les consommateurs indexent des std::map sur cette clé.
    [[nodiscard]] bool operator<(const VoxelChunkKey& other) const noexcept
    {
        return std::tie(X, Y, Z) < std::tie(other.X, other.Y, other.Z);
    }
};

// Division plancher : une position négative appartient au chunk inférieur, pas
// au chunk zéro. La troncature vers zéro de l'opérateur / ferait cohabiter
// -1 et 0 dans le même chunk.
[[nodiscard]] constexpr VoxelChunkKey VoxelChunkKeyForPosition(
    const Asset::Voxel::VoxelPosition& position) noexcept
{
    const auto floorDivide = [](const std::int32_t value) noexcept
    {
        const std::int32_t quotient = value / VoxelChunkEdgeLength;
        return (value % VoxelChunkEdgeLength < 0) ? quotient - 1 : quotient;
    };
    return {
        floorDivide(position.X),
        floorDivide(position.Y),
        floorDivide(position.Z)};
}

[[nodiscard]] constexpr Asset::Voxel::VoxelPosition VoxelChunkMinimum(
    const VoxelChunkKey key) noexcept
{
    return {
        key.X * VoxelChunkEdgeLength,
        key.Y * VoxelChunkEdgeLength,
        key.Z * VoxelChunkEdgeLength};
}

// Borne INCLUSIVE, comme les régions de VoxelMeshBuilder::Build.
[[nodiscard]] constexpr Asset::Voxel::VoxelPosition VoxelChunkMaximum(
    const VoxelChunkKey key) noexcept
{
    const Asset::Voxel::VoxelPosition minimum = VoxelChunkMinimum(key);
    return {
        minimum.X + VoxelChunkEdgeLength - 1,
        minimum.Y + VoxelChunkEdgeLength - 1,
        minimum.Z + VoxelChunkEdgeLength - 1};
}

} // namespace VoxelForge::Mesh
