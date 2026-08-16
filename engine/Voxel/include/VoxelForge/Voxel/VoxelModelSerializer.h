#pragma once

#include "VoxelModel.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>

namespace VoxelForge::Voxel
{

struct VoxelSerializationResult final
{
    bool Succeeded = false;
    std::string Message;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Succeeded;
    }
};

struct VoxelDeserializationResult final
{
    std::optional<VoxelModel> Model;
    std::string Message;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return Model.has_value();
    }
};

class VoxelModelSerializer final
{
public:
    // VF-STAB-01B : seam de test minimal, strictement limite a ce serializer.
    //
    // Motif : la branche « le rename final a reussi mais la suppression du
    // backup echoue » porte le coeur du contrat utilisateur, et aucun obstacle
    // externe ne peut la declencher de facon deterministe sous Windows — tout
    // mode de partage qui autorise le rename autorise la suppression, et
    // std::filesystem::remove efface l'attribut lecture seule puis reessaie.
    // Ces trois operations sont donc injectables. La production passe les
    // vraies fonctions std::filesystem (DefaultFileOperations) ; seul un test
    // fournit un substitut, et la totalite de la logique de Save reste
    // executee : rien n'est contourne.
    struct FileOperations final
    {
        std::function<bool(const std::filesystem::path&, std::error_code&)>
            Exists;
        std::function<void(const std::filesystem::path&,
            const std::filesystem::path&, std::error_code&)> Rename;
        std::function<bool(const std::filesystem::path&, std::error_code&)>
            Remove;
    };

    [[nodiscard]] static FileOperations DefaultFileOperations();

    [[nodiscard]] static VoxelSerializationResult Save(
        const std::filesystem::path& destination,
        const VoxelModel& model);

    [[nodiscard]] static VoxelSerializationResult Save(
        const std::filesystem::path& destination,
        const VoxelModel& model,
        const FileOperations& operations);

    [[nodiscard]] static VoxelDeserializationResult Load(
        const std::filesystem::path& source);
};

} // namespace VoxelForge::Voxel
