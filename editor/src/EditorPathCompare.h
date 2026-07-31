#pragma once

#include <filesystem>
#include <system_error>

namespace VoxelForge::Editor
{

// Compare un répertoire, quelle que soit sa graphie (noms courts Windows
// type RUNNER~1, liens symboliques, ../ résiduels), à un répertoire de
// référence déjà canonique (produit par std::filesystem::weakly_canonical).
//
// Motivation : les services stockent leurs racines sous forme canonique,
// mais les chemins entrants peuvent arriver avec une autre graphie du même
// répertoire. Une égalité textuelle échoue alors à tort (constaté sur les
// runners CI où le répertoire temporaire s'épelle différemment).
[[nodiscard]] inline bool IsSameDirectoryAsCanonical(
    const std::filesystem::path& directory,
    const std::filesystem::path& canonicalReference)
{
    if (directory == canonicalReference) return true;

    std::error_code error;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(directory, error);
    return !error && canonical == canonicalReference;
}

} // namespace VoxelForge::Editor
