#include "FirstCreationExperience.h"

namespace VoxelForge::Editor
{

void FirstCreationExperience::Start(const bool alreadyCompleted) noexcept
{
    stage_ = alreadyCompleted
        ? FirstCreationStage::Hidden : FirstCreationStage::Welcome;
}

void FirstCreationExperience::Acknowledge() noexcept
{
    if (stage_ == FirstCreationStage::Welcome)
        stage_ = FirstCreationStage::PlaceVoxel;
}

void FirstCreationExperience::OnFirstVoxelCreated() noexcept
{
    if (stage_ == FirstCreationStage::Welcome ||
        stage_ == FirstCreationStage::PlaceVoxel)
        stage_ = FirstCreationStage::Undo;
}

void FirstCreationExperience::OnUndo() noexcept
{
    if (stage_ == FirstCreationStage::Undo)
        stage_ = FirstCreationStage::Redo;
}

void FirstCreationExperience::OnRedo() noexcept
{
    if (stage_ == FirstCreationStage::Redo)
        stage_ = FirstCreationStage::Save;
}

bool FirstCreationExperience::OnSave() noexcept
{
    if (stage_ != FirstCreationStage::Save) return false;
    stage_ = FirstCreationStage::Completed;
    return true;
}

void FirstCreationExperience::Hide() noexcept
{
    stage_ = FirstCreationStage::Hidden;
}

FirstCreationStage FirstCreationExperience::Stage() const noexcept
{
    return stage_;
}

bool FirstCreationExperience::Visible() const noexcept
{
    return stage_ != FirstCreationStage::Hidden &&
        stage_ != FirstCreationStage::Completed;
}

const char* FirstCreationExperience::Message() const noexcept
{
    switch (stage_)
    {
    case FirstCreationStage::Welcome:
    case FirstCreationStage::PlaceVoxel:
        return "Bienvenue. Cliquez dans le volume pour creer votre premier voxel.";
    case FirstCreationStage::Undo: return "Bravo ! Essayez Ctrl+Z.";
    case FirstCreationStage::Redo: return "Essayez Ctrl+Y.";
    case FirstCreationStage::Save: return "Essayez Ctrl+S.";
    case FirstCreationStage::Hidden:
    case FirstCreationStage::Completed: return "";
    }
    return "";
}

} // namespace VoxelForge::Editor
