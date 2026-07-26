#pragma once

#include "SmartTools/SmartTool.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoxelForge::Editor
{
struct BrushProfile final
{
    std::string Uuid;
    std::string Name;
    bool Favorite = false;
    SmartGeometry Geometry = SmartGeometry::Pencil;
    SmartBrushShape Shape = SmartBrushShape::Cube;
    SmartAction Action = SmartAction::Add;
    int BrushSize = 1;
    SmartBrushDimension Dimension = SmartBrushDimension::Volume3D;
    SmartBrushOrientation Orientation = SmartBrushOrientation::Auto;
    std::size_t PaletteIndex = 1U;
    float PreviewAlpha = 0.5F;
    std::uint64_t Timestamp = 0U;
};

enum class BrushProfileStatus : std::uint8_t
{
    Success, NotFound, Invalid, UnsupportedVersion, IoError, AlreadyExists, Protected
};

struct BrushProfileResult final
{
    BrushProfileStatus Status = BrushProfileStatus::Success;
    std::string Message;
    std::optional<BrushProfile> Profile;

    [[nodiscard]] bool Succeeded() const noexcept
    { return Status == BrushProfileStatus::Success; }
};

class BrushProfileService final
{
public:
    static constexpr std::uint32_t FormatVersion = 1U;
    static constexpr std::size_t MaximumRecent = 10U;
    static constexpr std::size_t MaximumProfiles = 256U;
    static constexpr std::size_t MaximumNameBytes = 96U;
    static constexpr std::uintmax_t MaximumFileBytes = 1024U * 1024U;
    static constexpr std::string_view DefaultProfileUuid =
        "00000000-0000-4000-8000-000000000001";

    BrushProfileService();

    [[nodiscard]] bool SetProjectRoot(const std::filesystem::path& projectRoot);
    void ClearProject() noexcept;
    [[nodiscard]] const std::filesystem::path& ProjectRoot() const noexcept;
    [[nodiscard]] std::filesystem::path ProfilePath() const;

    [[nodiscard]] BrushProfileResult Load();
    [[nodiscard]] BrushProfileResult SaveNew(
        std::string name, const SmartTool& tool);
    [[nodiscard]] BrushProfileResult Duplicate(
        const std::string& uuid, std::string name);
    [[nodiscard]] BrushProfileResult Overwrite(
        const std::string& uuid, const SmartTool& tool);
    [[nodiscard]] BrushProfileResult Rename(
        const std::string& uuid, std::string name);
    [[nodiscard]] BrushProfileResult Delete(const std::string& uuid);
    [[nodiscard]] BrushProfileResult SetFavorite(const std::string& uuid, bool favorite);
    // SelectProfile records the active profile and returns it. Applying the
    // profile remains explicit so callers can coordinate the palette service.
    [[nodiscard]] BrushProfileResult SelectProfile(const std::string& uuid);

    [[nodiscard]] const std::vector<BrushProfile>& Profiles() const noexcept;
    [[nodiscard]] const std::vector<std::string>& Recent() const noexcept;
    [[nodiscard]] std::string_view ActiveUuid() const noexcept;
    [[nodiscard]] const BrushProfile* ActiveProfile() const noexcept;
    [[nodiscard]] std::vector<const BrushProfile*> SortedProfiles(
        std::string_view filter = {}) const;

    [[nodiscard]] static BrushProfile Capture(
        std::string name, const SmartTool& tool);
    [[nodiscard]] static bool Apply(const BrushProfile& profile, SmartTool& tool);
    [[nodiscard]] static bool IsValid(const BrushProfile& profile) noexcept;

private:
    [[nodiscard]] BrushProfileResult EnsureDefaultProfile();
    [[nodiscard]] BrushProfileResult Persist();
    [[nodiscard]] bool RecoverTransaction(std::string& diagnostic);
    [[nodiscard]] BrushProfile* Find(const std::string& uuid) noexcept;
    [[nodiscard]] const BrushProfile* Find(const std::string& uuid) const noexcept;
    void TouchRecent(const std::string& uuid);

    std::filesystem::path projectRoot_;
    std::vector<BrushProfile> profiles_;
    std::vector<std::string> recent_;
    std::string activeUuid_;
};
} // namespace VoxelForge::Editor
