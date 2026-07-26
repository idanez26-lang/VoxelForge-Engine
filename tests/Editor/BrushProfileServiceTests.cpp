#include "SmartTools/BrushProfileService.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace VoxelForge::Editor;

void Require(const bool condition, const std::string& message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

class TemporaryProject final
{
public:
    TemporaryProject()
    {
        root_ = std::filesystem::temp_directory_path() /
                ("VoxelForgeBrushProfiles-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_);
    }

    ~TemporaryProject()
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    const std::filesystem::path& Root() const noexcept
    {
        return root_;
    }

    void Write(const std::string& text) const
    {
        std::ofstream output(root_ / "BrushProfiles.json", std::ios::binary | std::ios::trunc);
        output << text;
        Require(static_cast<bool>(output), "Unable to write fixture JSON.");
    }

private:
    std::filesystem::path root_;
};

SmartTool Tool()
{
    SmartTool tool;
    tool.SetGeometry(SmartGeometry::Pencil);
    tool.SetAction(SmartAction::Paint);
    tool.Brush().Shape = SmartBrushShape::Sphere;
    tool.Brush().Size = 4;
    tool.Brush().Dimension = SmartBrushDimension::Surface2D;
    tool.Brush().Orientation = SmartBrushOrientation::X;
    tool.SetPreviewAlpha(0.35F);
    return tool;
}

void RequireApplied(const BrushProfile& profile, const int expectedSize, const float expectedAlpha)
{
    SmartTool restored;
    Require(BrushProfileService::Apply(profile, restored), "Saved profile could not be applied.");
    Require(
        restored.Geometry() == SmartGeometry::Pencil && restored.Action() == SmartAction::Paint &&
            restored.Brush().Shape == SmartBrushShape::Sphere && restored.Brush().Size == expectedSize &&
            restored.Brush().Dimension == SmartBrushDimension::Surface2D &&
            restored.Brush().Orientation == SmartBrushOrientation::X && restored.PreviewAlpha() == expectedAlpha,
        "Load did not restore every profile field.");
}

std::string ValidProfileJson(const std::string& suffix = {})
{
    return "{\"uuid\":\"12345678-1234-4234-8234-123456789abc\",\"name\":\"Profile\","
           "\"favorite\":false,\"geometry\":\"Pencil\",\"shape\":\"Cube\","
           "\"action\":\"Add\",\"brushSize\":1,\"dimension\":\"Volume3D\","
           "\"orientation\":\"Auto\",\"previewAlpha\":0.5,\"timestamp\":1" + suffix + "}";
}

void TestCrudReopenAndSorting()
{
    TemporaryProject project;
    BrushProfileService service;
    Require(service.SetProjectRoot(project.Root()), "Project root was refused.");

    SmartTool tool = Tool();
    const auto zulu = service.SaveNew("Zulu", tool);
    const auto alpha = service.SaveNew("Alpha", tool);
    const auto favorite = service.SaveNew("Favorite", tool);
    Require(
        zulu.Succeeded() && alpha.Succeeded() && favorite.Succeeded() && zulu.Profile && alpha.Profile &&
            favorite.Profile && zulu.Profile->Uuid != alpha.Profile->Uuid,
        "Save New did not create distinct stable UUIDs.");
    Require(
        std::filesystem::is_regular_file(project.Root() / "BrushProfiles.json"),
        "Profiles were not saved at project root.");

    Require(service.SetFavorite(favorite.Profile->Uuid, true).Succeeded(), "Favorite failed.");
    const auto sorted = service.SortedProfiles();
    Require(
        sorted.size() == 4U && sorted[0]->Uuid == favorite.Profile->Uuid && sorted[1]->Name == "Alpha" &&
            sorted[2]->Name == "Default" && sorted[3]->Name == "Zulu",
        "Profiles are not sorted by favorite then name.");

    tool.Brush().Size = 2;
    tool.SetPreviewAlpha(0.9F);
    Require(service.Overwrite(favorite.Profile->Uuid, tool).Succeeded(), "Explicit overwrite failed.");
    const auto loaded = service.SelectProfile(favorite.Profile->Uuid);
    Require(loaded.Succeeded() && loaded.Profile, "Load Profile failed.");
    RequireApplied(*loaded.Profile, 2, 0.9F);
    const std::vector<std::string> expectedRecent = service.Recent();

    BrushProfileService reopened;
    Require(
        reopened.SetProjectRoot(project.Root()) && reopened.Load().Succeeded() &&
            reopened.Recent() == expectedRecent,
        "Saved JSON did not round-trip.");
    const auto reopenedProfile = reopened.SelectProfile(favorite.Profile->Uuid);
    Require(reopenedProfile.Succeeded() && reopenedProfile.Profile, "Reopened profile was not found.");
    RequireApplied(*reopenedProfile.Profile, 2, 0.9F);
    Require(!reopened.Recent().empty() && reopened.Recent().front() == favorite.Profile->Uuid,
        "Selected profile was not first in the persisted Recent list.");
}

void TestRecentOrderAndPersistence()
{
    TemporaryProject project;
    BrushProfileService service;
    Require(service.SetProjectRoot(project.Root()), "Project root failed.");

    std::vector<std::string> uuids;
    for (int index = 0; index < 12; ++index)
    {
        const auto saved = service.SaveNew("P" + std::to_string(index), Tool());
        Require(saved.Succeeded() && saved.Profile, "Recent fixture save failed.");
        uuids.push_back(saved.Profile->Uuid);
        Require(service.SelectProfile(saved.Profile->Uuid).Succeeded(), "Recent fixture load failed.");
    }

    Require(
        service.Recent().size() == BrushProfileService::MaximumRecent &&
            service.Recent().front() == uuids.back() && service.Recent().back() == uuids[2],
        "Recent list did not keep the expected order and limit.");

    Require(service.SelectProfile(uuids[5]).Succeeded(), "Recent reorder load failed.");
    Require(
        service.Recent().front() == uuids[5] && service.Recent()[1] == uuids[11],
        "Recent profile was not moved to the front.");

    BrushProfileService reopened;
    Require(
        reopened.SetProjectRoot(project.Root()) && reopened.Load().Succeeded() &&
            reopened.Recent() == service.Recent(),
        "Recent list did not persist across reopening.");
}

void TestValidationAndV1Compatibility()
{
    TemporaryProject project;
    BrushProfileService service;
    Require(service.SetProjectRoot(project.Root()), "Project root failed.");

    const std::string profile = ValidProfileJson();
    project.Write("{\"version\":1,\"profiles\":[" + profile + "]}");
    const BrushProfileResult loadedV1 = service.Load();
    std::string loadedV1Summary = " status=" +
        std::to_string(static_cast<unsigned>(loadedV1.Status)) + " message=" + loadedV1.Message +
        " count=" + std::to_string(service.Profiles().size()) + " profiles=";
    for (const BrushProfile& item : service.Profiles())
        loadedV1Summary += "[" + item.Uuid + ":" + item.Name + "]";
    Require(
        loadedV1.Succeeded() && service.Profiles().size() == 2U && service.Recent().empty() &&
            std::any_of(service.Profiles().begin(), service.Profiles().end(), [](const BrushProfile& item) {
                return item.Uuid == "12345678-1234-4234-8234-123456789abc" && item.Name == "Profile";
            }) &&
            std::any_of(service.Profiles().begin(), service.Profiles().end(), [](const BrushProfile& item) {
                return item.Uuid == BrushProfileService::DefaultProfileUuid;
            }),
        "Version 1 file did not preserve its profile and add Default." + loadedV1Summary);

    const std::vector<std::string> invalidNumbers = {
        "+1", ".5", "01", "1.", "1e", "1e+", "-01",
    };
    for (const std::string& invalidNumber : invalidNumbers)
    {
        project.Write("{\"version\":" + invalidNumber + ",\"profiles\":[]}");
        Require(service.Load().Status == BrushProfileStatus::Invalid, "Invalid JSON number was accepted.");
    }
    project.Write("{\"version\":1,\v\"profiles\":[]}");
    Require(service.Load().Status == BrushProfileStatus::Invalid, "Invalid JSON whitespace was accepted.");

    project.Write("{\"version\":1,\"profiles\":[" + ValidProfileJson(
        ",\"unknown\":true") + "]}");
    Require(service.Load().Succeeded(), "Unknown profile fields were not ignored.");

    std::string extremeTimestamp = ValidProfileJson();
    const std::string timestampNeedle = "\"timestamp\":1";
    extremeTimestamp.replace(
        extremeTimestamp.find(timestampNeedle),
        timestampNeedle.size(),
        "\"timestamp\":18446744073709551616");
    project.Write("{\"version\":1,\"profiles\":[" + extremeTimestamp + "]}");
    const BrushProfileResult invalidTimestamp = service.Load();
    Require(invalidTimestamp.Succeeded() && invalidTimestamp.Message.find("Ignored") != std::string::npos &&
                service.Profiles().size() == 1U &&
                service.ActiveUuid() == BrushProfileService::DefaultProfileUuid,
        "Out-of-range timestamp did not reject only the invalid profile.");

    std::string invalidEnum = ValidProfileJson();
    const std::string needle = "\"shape\":\"Cube\"";
    invalidEnum.replace(invalidEnum.find(needle), needle.size(), "\"shape\":\"Invalid\"");
    project.Write("{\"version\":1,\"profiles\":[" + invalidEnum + "]}");
    const BrushProfileResult invalidShape = service.Load();
    Require(invalidShape.Succeeded() && invalidShape.Message.find("Ignored") != std::string::npos &&
                service.Profiles().size() == 1U &&
                service.ActiveProfile() != nullptr &&
                service.ActiveProfile()->Uuid == BrushProfileService::DefaultProfileUuid,
        "Invalid shape did not reject only the invalid profile.");
}

void TestUnicodeControlsAndAlphaClamp()
{
    TemporaryProject project;
    BrushProfileService service;
    Require(service.SetProjectRoot(project.Root()), "Project root failed.");

    SmartTool tool = Tool();
    tool.SetPreviewAlpha(-4.0F);
    Require(tool.PreviewAlpha() == 0.0F, "Preview alpha did not clamp low.");

    const std::string name = std::string("\xC3\x89") + "t" + "\xC3\xA9" + "\nBrush";
    const auto saved = service.SaveNew(name, tool);
    Require(saved.Succeeded() && saved.Profile, "Unicode profile save failed.");

    BrushProfileService reopened;
    Require(
        reopened.SetProjectRoot(project.Root()) && reopened.Load().Succeeded() && reopened.Profiles().size() == 2U &&
            std::any_of(reopened.Profiles().begin(), reopened.Profiles().end(), [&name](const BrushProfile& profile) {
                return profile.Name == name;
            }),
        "UTF-8 or control characters did not round-trip.");
}

void TestDefaultActiveAndDeletionFallback()
{
    TemporaryProject project;
    BrushProfileService service;
    Require(service.SetProjectRoot(project.Root()), "Project root failed.");
    const BrushProfile* fallback = service.ActiveProfile();
    Require(fallback != nullptr && fallback->Uuid == BrushProfileService::DefaultProfileUuid &&
                fallback->Name == "Default",
        "A deterministic Default profile was not created and selected.");
    Require(service.Delete(fallback->Uuid).Status == BrushProfileStatus::Protected,
        "Default profile must not be deletable.");
    Require(service.Rename(fallback->Uuid, "Changed").Status == BrushProfileStatus::Protected,
        "Default profile must not be renameable.");

    project.Write("{\"version\":1,\"profiles\":[{\"uuid\":\"" +
        std::string(BrushProfileService::DefaultProfileUuid) +
        "\",\"name\":\"Forged Default\",\"favorite\":true,\"geometry\":\"Pencil\",\"shape\":\"Sphere\",\"action\":\"Paint\",\"brushSize\":4,\"dimension\":\"Surface2D\",\"orientation\":\"X\",\"paletteIndex\":42,\"previewAlpha\":0.2,\"timestamp\":99}]}");
    Require(service.Load().Succeeded() && service.ActiveProfile() != nullptr &&
                service.ActiveProfile()->Name == "Default" &&
                service.ActiveProfile()->Shape == SmartBrushShape::Cube &&
                service.ActiveProfile()->Action == SmartAction::Add &&
                service.ActiveProfile()->PaletteIndex == 1U &&
                service.ActiveProfile()->PreviewAlpha == 0.5F &&
                service.ActiveProfile()->Timestamp == 0U,
        "Catalog data was allowed to redefine the deterministic Default profile.");

    const auto created = service.SaveNew("Temporary", Tool());
    Require(created.Succeeded() && created.Profile && service.ActiveUuid() == created.Profile->Uuid,
        "New profile was not selected.");
    const auto retained = service.SaveNew("Retained", Tool());
    Require(retained.Succeeded() && retained.Profile, "Second fixture profile creation failed.");
    Require(service.Delete(created.Profile->Uuid).Succeeded() &&
                service.ActiveUuid() == retained.Profile->Uuid,
        "Deleting a non-active profile changed the active selection.");
    const auto deleted = service.Delete(created.Profile->Uuid);
    Require(deleted.Status == BrushProfileStatus::NotFound,
        "Deleted profile remained addressable.");
    const auto activeDeleted = service.Delete(retained.Profile->Uuid);
    Require(activeDeleted.Succeeded() && activeDeleted.Profile &&
                service.ActiveUuid() == BrushProfileService::DefaultProfileUuid,
        "Deleting the active profile did not fall back to Default.");
    BrushProfileService reopened;
    Require(reopened.SetProjectRoot(project.Root()) && reopened.Load().Succeeded() &&
                reopened.ActiveUuid() == BrushProfileService::DefaultProfileUuid,
        "Active profile fallback was not persisted.");
}

void TestDuplicatePaletteAndLegacyShapes()
{
    TemporaryProject project;
    BrushProfileService service;
    Require(service.SetProjectRoot(project.Root()), "Project root failed.");
    SmartTool tool = Tool();
    tool.Brush().PaletteIndex = 42U;
    const auto created = service.SaveNew("Source", tool);
    Require(created.Succeeded() && created.Profile, "Source profile creation failed.");
    const auto copy = service.Duplicate(created.Profile->Uuid, "Copy");
    Require(copy.Succeeded() && copy.Profile && copy.Profile->Uuid != created.Profile->Uuid &&
                copy.Profile->PaletteIndex == 42U,
        "Duplicate did not receive a distinct identity or preserve palette.");
    const auto renamed = service.Rename(copy.Profile->Uuid, "Renamed Copy");
    Require(renamed.Succeeded() && renamed.Profile && renamed.Profile->Name == "Renamed Copy",
        "Profile rename did not persist a valid name.");
    SmartTool restored;
    Require(BrushProfileService::Apply(*copy.Profile, restored) && restored.Brush().PaletteIndex == 42U,
        "Palette index did not round-trip through a profile.");

    BrushProfile cube = *copy.Profile;
    cube.Shape = SmartBrushShape::Cube;
    BrushProfile sphere = cube;
    sphere.Uuid = "12345678-1234-4234-8234-123456789abd";
    sphere.Shape = SmartBrushShape::Sphere;
    Require(BrushProfileService::IsValid(cube) && BrushProfileService::IsValid(sphere),
        "Legacy Cube/Sphere profiles are no longer accepted.");
}

void TestValidationToleranceAndFormatStrictness()
{
    TemporaryProject project;
    BrushProfileService service;
    Require(service.SetProjectRoot(project.Root()), "Project root failed.");
    const std::string valid = ValidProfileJson();
    const std::string invalid = "{\"uuid\":\"bad\",\"name\":\"Broken\"}";
    project.Write("{\"version\":1,\"active\":\"12345678-1234-4234-8234-123456789abc\",\"profiles\":[" +
        valid + "," + invalid + "]}");
    const auto loaded = service.Load();
    Require(loaded.Succeeded() && service.Profiles().size() == 2U &&
                service.ActiveUuid() == "12345678-1234-4234-8234-123456789abc",
        "An isolated corrupt profile invalidated the entire catalog.");
    std::ifstream preserved(project.Root() / "BrushProfiles.json", std::ios::binary);
    const std::string preservedBytes((std::istreambuf_iterator<char>(preserved)), {});
    Require(preservedBytes.find("\"uuid\":\"bad\"") != std::string::npos,
        "A partial load silently rewrote the corrupt catalog.");

    project.Write("{\"version\":1.0,\"profiles\":[]}");
    Require(service.Load().Status == BrushProfileStatus::UnsupportedVersion,
        "Fractional format version was accepted.");
    BrushProfile invalidSize = *service.ActiveProfile();
    invalidSize.BrushSize = 0;
    Require(!BrushProfileService::IsValid(invalidSize), "Zero brush size was accepted.");
    invalidSize = *service.ActiveProfile();
    invalidSize.BrushSize = -1;
    Require(!BrushProfileService::IsValid(invalidSize), "Negative brush size was accepted.");
    invalidSize = *service.ActiveProfile();
    invalidSize.Shape = SmartBrushShape::Cylinder;
    Require(!BrushProfileService::IsValid(invalidSize), "Unsupported shape was accepted.");
}

void TestDeterministicWriteAndUnicodeSurrogates()
{
    TemporaryProject project;
    BrushProfileService service;
    Require(service.SetProjectRoot(project.Root()), "Project root failed.");
    const auto second = service.SaveNew("Second", Tool());
    const auto first = service.SaveNew("First", Tool());
    Require(second.Succeeded() && second.Profile && first.Succeeded() && first.Profile, "Fixture save failed.");
    Require(service.SelectProfile(second.Profile->Uuid).Succeeded(), "Initial selection persistence failed.");
    std::ifstream before(project.Root() / "BrushProfiles.json", std::ios::binary);
    const std::string firstBytes((std::istreambuf_iterator<char>(before)), {});
    before.close();
    Require(static_cast<bool>(before), "Unable to finish reading the initial catalog.");
    Require(service.SelectProfile(first.Profile->Uuid).Succeeded(), "Selection persistence failed.");
    std::ifstream after(project.Root() / "BrushProfiles.json", std::ios::binary);
    const std::string secondBytes((std::istreambuf_iterator<char>(after)), {});
    after.close();
    Require(static_cast<bool>(after), "Unable to finish reading the selected catalog.");
    Require(firstBytes != secondBytes, "Active profile was not persisted.");
    Require(secondBytes.find("\"profiles\":[{\"uuid\":\"00000000") != std::string::npos,
        "Profile serialization order is not deterministic by UUID.");

    project.Write("{\"version\":1,\"profiles\":[{\"uuid\":\"12345678-1234-4234-8234-123456789abc\",\"name\":\"\\uD83D\\uDE00\",\"favorite\":false,\"geometry\":\"Pencil\",\"shape\":\"Cube\",\"action\":\"Add\",\"brushSize\":1,\"dimension\":\"Volume3D\",\"orientation\":\"Auto\",\"previewAlpha\":0.5,\"timestamp\":1}]}");
    Require(service.Load().Succeeded() && service.Profiles()[0].Name == "\xF0\x9F\x98\x80",
        "Unicode surrogate pair was not decoded as UTF-8.");
}

void TestInterruptedTransactionRecovery()
{
    TemporaryProject project;
    BrushProfileService service;
    Require(service.SetProjectRoot(project.Root()), "Project root failed.");
    const auto saved = service.SaveNew("Recovered", Tool());
    Require(saved.Succeeded(), "Recovery fixture save failed.");
    const auto destination = project.Root() / "BrushProfiles.json";
    const auto backup = std::filesystem::path(destination.string() + ".bak");
    const auto temporary = std::filesystem::path(destination.string() + ".tmp");
    std::filesystem::rename(destination, backup);
    { std::ofstream output(temporary, std::ios::binary | std::ios::trunc); output << "incomplete"; }
    BrushProfileService recovered;
    Require(recovered.SetProjectRoot(project.Root()) && recovered.Load().Succeeded() &&
                recovered.ActiveProfile() != nullptr && recovered.Profiles().size() == 2U &&
                !std::filesystem::exists(temporary) && !std::filesystem::exists(backup),
        "Interrupted profile transaction was not safely recovered.");
}
} // namespace

int main()
{
    try
    {
        TestCrudReopenAndSorting();
        TestRecentOrderAndPersistence();
        TestValidationAndV1Compatibility();
        TestUnicodeControlsAndAlphaClamp();
        TestDefaultActiveAndDeletionFallback();
        TestDuplicatePaletteAndLegacyShapes();
        TestValidationToleranceAndFormatStrictness();
        TestDeterministicWriteAndUnicodeSurrogates();
        TestInterruptedTransactionRecovery();
        std::cout << "Brush Profile tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
