#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace VoxelForge::Editor
{

struct ProjectSessionVector3 final
{
    float X = 0.0F;
    float Y = 0.0F;
    float Z = 0.0F;

    [[nodiscard]] bool operator==(
        const ProjectSessionVector3&) const noexcept = default;
};

enum class ProjectSessionCameraView : std::uint8_t
{
    Perspective,
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom
};

struct ProjectSessionCamera final
{
    ProjectSessionVector3 Position{};
    ProjectSessionVector3 RotationDegrees{};
    float Distance = 0.0F;
    ProjectSessionVector3 Target{};
    ProjectSessionCameraView View = ProjectSessionCameraView::Perspective;

    [[nodiscard]] bool operator==(
        const ProjectSessionCamera&) const noexcept = default;
};

enum class ProjectSessionTool : std::uint8_t
{
    Pencil,
    Eraser
};

struct ProjectSessionData final
{
    static constexpr std::uint32_t FormatVersion = 1U;

    std::filesystem::path LastModel;
    ProjectSessionCamera Camera{};
    ProjectSessionTool ActiveTool = ProjectSessionTool::Pencil;
};

enum class ProjectSessionLoadStatus : std::uint8_t
{
    Loaded,
    NotFound,
    Corrupt,
    UnsupportedVersion,
    IoError
};

struct ProjectSessionLoadResult final
{
    ProjectSessionLoadStatus Status = ProjectSessionLoadStatus::NotFound;
    ProjectSessionData Session{};
    bool CameraValid = false;
    std::string Message;

    [[nodiscard]] bool Loaded() const noexcept
    {
        return Status == ProjectSessionLoadStatus::Loaded;
    }
};

class ProjectSessionService final
{
public:
    [[nodiscard]] bool SetProjectRoot(
        const std::filesystem::path& projectRoot);
    void ClearProject() noexcept;

    [[nodiscard]] ProjectSessionLoadResult Load() const;
    [[nodiscard]] bool Save(
        const ProjectSessionData& session,
        std::string& error) const;

    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept;
    [[nodiscard]] std::filesystem::path SessionPath() const;

    [[nodiscard]] static bool IsValidCamera(
        const ProjectSessionCamera& camera) noexcept;
    [[nodiscard]] static bool IsValidModelPath(
        const std::filesystem::path& relativePath) noexcept;

private:
    std::filesystem::path projectRoot_;
};

} // namespace VoxelForge::Editor
