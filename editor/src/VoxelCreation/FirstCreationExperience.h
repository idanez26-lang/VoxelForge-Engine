#pragma once

namespace VoxelForge::Editor
{

enum class FirstCreationStage
{
    Hidden,
    Welcome,
    PlaceVoxel,
    Undo,
    Redo,
    Save,
    Completed
};

class FirstCreationExperience final
{
public:
    void Start(bool alreadyCompleted) noexcept;
    void Acknowledge() noexcept;
    void OnFirstVoxelCreated() noexcept;
    void OnUndo() noexcept;
    void OnRedo() noexcept;
    [[nodiscard]] bool OnSave() noexcept;
    void Hide() noexcept;

    [[nodiscard]] FirstCreationStage Stage() const noexcept;
    [[nodiscard]] bool Visible() const noexcept;
    [[nodiscard]] const char* Message() const noexcept;

private:
    FirstCreationStage stage_ = FirstCreationStage::Hidden;
};

} // namespace VoxelForge::Editor
