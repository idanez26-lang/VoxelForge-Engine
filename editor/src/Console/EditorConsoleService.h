#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

// Journal de la console de l'éditeur : file bornée de messages + visibilité
// du panneau. Service pur, sans dépendance UI — le panneau ImGui le consomme.
// (VF-0260, lot 1 : extrait d'EditorWorkspace.)
class EditorConsoleService final
{
public:
    static constexpr std::size_t DefaultMaximumMessageCount = 200;

    explicit EditorConsoleService(
        std::size_t maximumMessageCount = DefaultMaximumMessageCount);

    // Ajoute un message (en évinçant le plus ancien si la file est pleine)
    // et rend la console visible, comme le faisait EditorWorkspace.
    void AddMessage(std::string message);

    [[nodiscard]] const std::vector<std::string>& Messages() const noexcept;
    void Clear() noexcept;

    [[nodiscard]] bool IsVisible() const noexcept;
    void SetVisible(bool visible) noexcept;
    // Pointeur stable vers le drapeau de visibilité, pour
    // ImGui::Begin / ImGui::MenuItem.
    [[nodiscard]] bool* VisibilityFlag() noexcept;

    [[nodiscard]] std::size_t MaximumMessageCount() const noexcept;

private:
    std::size_t maximumMessageCount_;
    std::vector<std::string> messages_;
    bool visible_ = true;
};

} // namespace VoxelForge::Editor
