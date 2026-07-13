#pragma once
#include "VoxelForge/Core/UUID.h"
#include <filesystem>
#include <string>

namespace VoxelForge::Project
{
    class Project final
    {
    public:
        Project(std::string name, std::filesystem::path rootPath);

        [[nodiscard]] const Core::UUID& Id() const noexcept;
        [[nodiscard]] const std::string& Name() const noexcept;
        [[nodiscard]] const std::filesystem::path& RootPath() const noexcept;

        void Rename(std::string newName);

    private:
        Core::UUID id_;
        std::string name_;
        std::filesystem::path rootPath_;
    };
}
