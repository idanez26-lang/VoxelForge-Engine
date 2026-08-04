#include "VoxelStamps/Library/StampLibraryPaths.h"
#include "VoxelStamps/Library/StampProjectLibraryRepository.h"
#include "VoxelStamps/Library/StampUserLibraryRepository.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

using namespace VoxelForge;
using namespace VoxelForge::Editor::Stamps;
namespace fs = std::filesystem;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

class TemporaryProfile final
{
public:
    TemporaryProfile()
    {
        root_ = fs::temp_directory_path() /
            ("VoxelForgeStampUserLibrary-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        localAppData_ = root_ / "LocalAppData";
        project_ = root_ / "Project";
        Require(fs::create_directories(localAppData_),
            "Unable to create the injected local-data root.");
        Require(fs::create_directories(project_ / "Assets"),
            "Unable to create the isolated project root.");
    }

    ~TemporaryProfile()
    {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] const fs::path& LocalAppData() const noexcept
    {
        return localAppData_;
    }

    [[nodiscard]] const fs::path& Project() const noexcept
    {
        return project_;
    }

private:
    fs::path root_;
    fs::path localAppData_;
    fs::path project_;
};

VoxelStamp MakeStamp(const std::uint64_t id)
{
    StampValidationResult validation{};
    const auto stamp = VoxelStamp::TryCreate(
        {.Id = Core::UUID{id}, .ContentHash = {}},
        {.Minimum = {},
         .Maximum = {},
         .Dimensions = {.X = 1U, .Y = 1U, .Z = 1U}},
        {.RequestedMode = StampPivotMode::Auto,
         .ResolvedMode = StampPivotMode::Center,
         .LocalPosition = {.X = 128, .Y = 128, .Z = 128},
         .AutoPolicyVersion = 1U},
        {},
        {{.LocalColorId = 0U,
          .Color = {.Red = 10U, .Green = 20U, .Blue = 30U, .Alpha = 255U}}},
        {{.Position = {}, .LocalColorId = 0U}},
        DefaultStampResourceLimits(),
        &validation);
    Require(stamp.has_value(), validation.Message);
    return *stamp;
}

void TestInjectedProfileAndCrossProjectAvailability(TemporaryProfile& profile)
{
    const Core::UserDataPaths paths({}, profile.LocalAppData());
    StampUserLibraryRepository first(paths);
    const fs::path expectedRoot =
        profile.LocalAppData() / "VoxelForge Studio";
    Require(first.UserDataRoot() == expectedRoot,
        "My Library must use the injected VoxelForge Studio data root.");
    Require(!fs::exists(expectedRoot),
        "Configuring My Library must not create profile data eagerly.");

    const StampLibraryResult installed = first.Install(
        MakeStamp(901U), {.PreferredFileStem = "shared-rock"});
    Require(installed.Succeeded() &&
            installed.Reference.Scope == StampLibraryScope::User &&
            installed.Reference.RelativePath ==
                fs::path{"Library/Creations/shared-rock.vfstamp"} &&
            !installed.Reference.RelativePath.is_absolute() &&
            fs::is_regular_file(expectedRoot / installed.Reference.RelativePath),
        "My Library must publish a scoped portable reference under the injected profile.");

    StampUserLibraryRepository second(paths);
    const StampLibraryResult inventory = second.EnumerateSourceAssets();
    const StampLibraryResult rebuilt = second.RebuildSourceInventory();
    const StampLibraryResult read = second.Read(installed.Reference);
    Require(inventory.Succeeded() && inventory.Assets.size() == 1U &&
            inventory.Assets.front().Reference == installed.Reference &&
            rebuilt.Succeeded() && rebuilt.Assets == inventory.Assets &&
            read.Succeeded() && read.Stamp &&
            read.Stamp->Identity().Id == Core::UUID{901U},
        "A second project session must see the same profile-level My Library asset.");

    const StampAssetReference missing{
        .Id = Core::UUID{902U},
        .ContentHash = "missing",
        .RelativePath = "Library/Creations/missing.vfstamp",
        .Scope = StampLibraryScope::User};
    Require(second.Read(missing).Error == StampLibraryError::AssetNotFound,
        "A missing personal asset must be reported explicitly.");
    Require(!second.ResolvePortableReference("../outside.vfstamp").Succeeded() &&
            !second.ResolvePortableReference(
                "Assets/ForgeLibrary/Creations/project.vfstamp").Succeeded(),
        "My Library must reject traversal and Project Library paths.");
    Require(second.Remove(installed.Reference).Succeeded() &&
            !fs::exists(expectedRoot / installed.Reference.RelativePath),
        "My Library must remove only the validated scoped source asset.");
}

void TestScopeIsolation(TemporaryProfile& profile)
{
    const Core::UserDataPaths paths({}, profile.LocalAppData());
    StampUserLibraryRepository user(paths);
    const StampLibraryResult installed = user.Install(
        MakeStamp(903U), {.PreferredFileStem = "scope-check"});
    Require(installed.Succeeded(), "Unable to create the scope-isolation asset.");

    StampProjectLibraryRepository project;
    Require(project.SetProjectRoot(profile.Project()),
        "Unable to configure the isolated Project Library.");
    Require(project.Read(installed.Reference).Error ==
                StampLibraryError::InvalidReference,
        "Project Library must reject a User-scoped reference.");

    StampAssetReference projectReference = installed.Reference;
    projectReference.Scope = StampLibraryScope::Project;
    Require(user.Read(projectReference).Error ==
                StampLibraryError::InvalidReference,
        "My Library must reject a Project-scoped reference.");
}

void TestInvalidInjectionDoesNotTouchTheProfile(TemporaryProfile& profile)
{
    StampUserLibraryRepository unavailable(Core::UserDataPaths({}, {}));
    Require(unavailable.UserDataRoot().empty() &&
            unavailable.EnumerateSourceAssets().Error ==
                StampLibraryError::NotConfigured,
        "An unavailable local-data root must leave My Library unconfigured.");

    StampUserLibraryRepository relative(
        Core::UserDataPaths({}, fs::path{"relative-profile"}));
    Require(relative.UserDataRoot().empty(),
        "A relative injected profile root must be rejected.");

    const fs::path blockedRoot =
        profile.LocalAppData() / "VoxelForge Studio";
    {
        std::ofstream file(blockedRoot);
        Require(static_cast<bool>(file),
            "Unable to create the invalid profile-root fixture.");
    }
    StampUserLibraryRepository blocked(
        Core::UserDataPaths({}, profile.LocalAppData()));
    Require(blocked.UserDataRoot().empty(),
        "A profile root occupied by a regular file must be rejected.");
}

} // namespace

int main()
{
    try
    {
        TemporaryProfile sharedProfile;
        TestInjectedProfileAndCrossProjectAvailability(sharedProfile);
        TemporaryProfile scopedProfile;
        TestScopeIsolation(scopedProfile);
        TemporaryProfile invalidProfile;
        TestInvalidInjectionDoesNotTouchTheProfile(invalidProfile);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
