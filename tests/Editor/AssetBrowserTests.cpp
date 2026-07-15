#include "AssetBrowser/AssetBrowser.h"
#include "AssetBrowser/AssetDirectory.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
namespace fs = std::filesystem;

class TemporaryAssetTree final
{
public:
    explicit TemporaryAssetTree(const std::string& testName)
    {
        const auto uniqueValue =
            std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = fs::temp_directory_path() /
            ("VoxelForgeAssetBrowser-" + testName + "-" +
             std::to_string(uniqueValue));
        assets_ = root_ / "Assets";

        std::error_code error;
        fs::create_directories(assets_, error);

        if (error)
        {
            throw std::runtime_error(
                "Unable to create the temporary asset tree: " +
                error.message());
        }
    }

    ~TemporaryAssetTree()
    {
        std::error_code ignoredError;
        fs::remove_all(root_, ignoredError);
    }

    TemporaryAssetTree(const TemporaryAssetTree&) = delete;
    TemporaryAssetTree& operator=(const TemporaryAssetTree&) = delete;

    [[nodiscard]] const fs::path& Root() const noexcept
    {
        return root_;
    }

    [[nodiscard]] const fs::path& Assets() const noexcept
    {
        return assets_;
    }

private:
    fs::path root_;
    fs::path assets_;
};

void WriteFile(const fs::path& path, const std::string& contents)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);

    if (!stream)
    {
        throw std::runtime_error(
            "Unable to create a temporary test file: " + path.string());
    }

    stream << contents;

    if (!stream)
    {
        throw std::runtime_error(
            "Unable to write a temporary test file: " + path.string());
    }
}

int TestEmptyFolder()
{
    TemporaryAssetTree tree("empty");
    VoxelForge::Editor::AssetDirectory directory;
    const fs::path equivalentAssetsPath =
        tree.Assets() / ".." / "Assets";

    if (!directory.SetAssetsRoot(equivalentAssetsPath) ||
        !directory.Entries().empty() ||
        !directory.CurrentRelativePath().empty() ||
        directory.AssetsRoot() != fs::weakly_canonical(tree.Assets()))
    {
        return 1;
    }

    return 0;
}

int TestNavigationAndBack()
{
    TemporaryAssetTree tree("navigation");
    fs::create_directories(tree.Assets() / "Props" / "Barrels");
    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(tree.Assets()) ||
        !directory.EnterDirectory("Props") ||
        directory.CurrentRelativePath() != fs::path("Props") ||
        !directory.EnterDirectory("Barrels") ||
        directory.CurrentRelativePath() != fs::path("Props") / "Barrels")
    {
        return 2;
    }

    if (!directory.Back() ||
        directory.CurrentRelativePath() != fs::path("Props") ||
        !directory.Back() ||
        !directory.CurrentRelativePath().empty() ||
        directory.CanGoBack() ||
        !directory.Back() ||
        !directory.CurrentRelativePath().empty())
    {
        return 3;
    }

    return 0;
}

int TestSorting()
{
    TemporaryAssetTree tree("sorting");
    fs::create_directory(tree.Assets() / "zeta");
    fs::create_directory(tree.Assets() / "Alpha");
    WriteFile(tree.Assets() / "beta.txt", "beta");
    WriteFile(tree.Assets() / "alpha.md", "alpha");
    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(tree.Assets()) ||
        !directory.Refresh() || directory.Entries().size() != 4U)
    {
        return 4;
    }

    const auto& entries = directory.Entries();

    if (entries.size() != 4U ||
        !entries[0].IsDirectory() || entries[0].Name() != "Alpha" ||
        !entries[0].Extension().empty() || entries[0].FileSize() ||
        !entries[1].IsDirectory() || entries[1].Name() != "zeta" ||
        !entries[2].IsFile() || entries[2].Name() != "alpha.md" ||
        !entries[2].AbsolutePath().is_absolute() ||
        entries[2].RelativePath().is_absolute() ||
        entries[2].Extension() != ".md" ||
        !entries[2].FileSize() || *entries[2].FileSize() != 5U ||
        !entries[3].IsFile() || entries[3].Name() != "beta.txt")
    {
        return 5;
    }

    return 0;
}

int TestFolderCreation()
{
    TemporaryAssetTree tree("creation");
    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(tree.Assets()) ||
        !directory.CreateFolder("NewFolder") ||
        !fs::is_directory(tree.Assets() / "NewFolder") ||
        directory.Entries().size() != 1U ||
        directory.Entries().front().Name() != "NewFolder")
    {
        return 6;
    }

    if (directory.CreateFolder("") ||
        directory.CreateFolder(".") ||
        directory.CreateFolder("..") ||
        directory.CreateFolder("Bad/Name") ||
        directory.CreateFolder("Bad\\Name") ||
        directory.CreateFolder("Bad<Name") ||
        directory.CreateFolder("NewFolder") ||
        directory.CreateFolder("CON"))
    {
        return 7;
    }

    return 0;
}

int TestMissingFolderAndConfinement()
{
    TemporaryAssetTree tree("missing");
    VoxelForge::Editor::AssetDirectory missingDirectory;

    if (missingDirectory.SetAssetsRoot(tree.Root() / "MissingAssets"))
    {
        return 8;
    }

    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(tree.Assets()) ||
        directory.EnterDirectory("MissingFolder") ||
        !directory.CurrentRelativePath().empty() ||
        directory.EnterDirectory("..") ||
        !directory.CurrentRelativePath().empty() ||
        directory.EnterDirectory(tree.Root()))
    {
        return 9;
    }

    return 0;
}

int TestExternalCurrentFolderRemoval()
{
    TemporaryAssetTree tree("removed-current");
    fs::create_directories(tree.Assets() / "Props" / "Environment");
    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(tree.Assets()) ||
        !directory.EnterDirectory("Props"))
    {
        return 10;
    }

    std::error_code error;
    fs::remove_all(tree.Assets() / "Props", error);

    if (error || !directory.Refresh() ||
        !directory.CurrentRelativePath().empty() ||
        !directory.Entries().empty())
    {
        return 11;
    }

    return 0;
}

int TestExternalSymlinkConfinement()
{
    TemporaryAssetTree tree("symlink");
    const fs::path outsideDirectory = tree.Root() / "Outside";
    const fs::path externalLink = tree.Assets() / "OutsideLink";
    fs::create_directory(outsideDirectory);
    std::error_code error;
    fs::create_directory_symlink(outsideDirectory, externalLink, error);

    if (error)
    {
        std::cout << "Symlink confinement test skipped: "
                  << error.message() << '\n';
        return 0;
    }

    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(tree.Assets()) ||
        std::ranges::any_of(
            directory.Entries(),
            [](const VoxelForge::Editor::AssetEntry& entry)
            {
                return entry.Name() == "OutsideLink";
            }) ||
        directory.EnterDirectory(externalLink) ||
        !directory.CurrentRelativePath().empty())
    {
        return 12;
    }

    return 0;
}

int TestSelectionSurvivesRefresh()
{
    TemporaryAssetTree tree("selection");
    TemporaryAssetTree otherTree("selection-other-project");
    WriteFile(tree.Assets() / "crate.txt", "crate");
    WriteFile(otherTree.Assets() / "other.txt", "other");
    VoxelForge::Editor::AssetBrowser browser;

    if (!browser.SetAssetsRoot(tree.Assets()) ||
        !browser.SelectEntry("crate.txt") ||
        !browser.SelectedRelativePath() ||
        *browser.SelectedRelativePath() != fs::path("crate.txt"))
    {
        return 13;
    }

    WriteFile(tree.Assets() / "readme.md", "readme");

    if (!browser.Refresh() || !browser.SelectedRelativePath() ||
        *browser.SelectedRelativePath() != fs::path("crate.txt"))
    {
        return 14;
    }

    if (!browser.SetAssetsRoot(otherTree.Assets()) ||
        browser.SelectedRelativePath() ||
        browser.Directory().Entries().size() != 1U ||
        browser.Directory().Entries().front().Name() != "other.txt")
    {
        return 15;
    }

    if (!browser.SetAssetsRoot(tree.Assets()) ||
        !browser.SelectEntry("crate.txt"))
    {
        return 16;
    }

    std::error_code error;
    fs::remove(tree.Assets() / "crate.txt", error);

    if (error || !browser.Refresh() || browser.SelectedRelativePath())
    {
        return 17;
    }

    return 0;
}
}

int main()
{
    try
    {
        if (const int result = TestEmptyFolder(); result != 0)
        {
            return result;
        }

        if (const int result = TestNavigationAndBack(); result != 0)
        {
            return result;
        }

        if (const int result = TestSorting(); result != 0)
        {
            return result;
        }

        if (const int result = TestFolderCreation(); result != 0)
        {
            return result;
        }

        if (const int result = TestMissingFolderAndConfinement(); result != 0)
        {
            return result;
        }

        if (const int result = TestExternalCurrentFolderRemoval(); result != 0)
        {
            return result;
        }

        if (const int result = TestExternalSymlinkConfinement(); result != 0)
        {
            return result;
        }

        return TestSelectionSurvivesRefresh();
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 100;
    }
}
