#include "AssetBrowser/AssetBrowser.h"
#include "AssetBrowser/AssetDirectory.h"
#include "AssetBrowser/AssetBrowserViewModel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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

VoxelForge::Editor::AssetEntry MakeViewEntry(
    const std::string& name,
    const VoxelForge::Editor::AssetEntryType type,
    const std::optional<std::uintmax_t> size = std::nullopt,
    const int modifiedSeconds = 0,
    const fs::path& relativePath = {})
{
    const fs::path effectiveRelativePath = relativePath.empty()
        ? fs::path(name)
        : relativePath;
    const std::string extension = type ==
        VoxelForge::Editor::AssetEntryType::Directory
        ? std::string{}
        : fs::path(name).extension().string();
    const std::optional<fs::file_time_type> modified =
        fs::file_time_type{} + std::chrono::seconds(modifiedSeconds);
    return {
        name,
        fs::path("Assets") / effectiveRelativePath,
        effectiveRelativePath,
        type,
        extension,
        type == VoxelForge::Editor::AssetEntryType::Directory
            ? std::nullopt
            : size,
        modified};
}

std::vector<std::string> ViewNames(
    const std::vector<const VoxelForge::Editor::AssetEntry*>& entries)
{
    std::vector<std::string> names;
    names.reserve(entries.size());

    for (const VoxelForge::Editor::AssetEntry* entry : entries)
    {
        names.push_back(entry->Name());
    }

    return names;
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
    const VoxelForge::Editor::AssetOperationResult renameResult =
        directory.SetAssetsRoot(tree.Assets())
        ? directory.RenameEntry(externalLink, "RenamedLink")
        : VoxelForge::Editor::AssetOperationResult{};
    const VoxelForge::Editor::AssetOperationResult deleteResult =
        directory.DeleteEntry(externalLink);

    if (!directory.HasAssetsRoot() ||
        std::ranges::any_of(
            directory.Entries(),
            [](const VoxelForge::Editor::AssetEntry& entry)
            {
                return entry.Name() == "OutsideLink";
            }) ||
        directory.EnterDirectory(externalLink) ||
        !directory.CurrentRelativePath().empty() ||
        renameResult.Succeeded || deleteResult.Succeeded ||
        !fs::exists(outsideDirectory) || !fs::is_symlink(externalLink))
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

int TestRenameOperations()
{
    TemporaryAssetTree tree("rename-operations");
    WriteFile(tree.Assets() / "crate.txt", "crate");
    fs::create_directories(tree.Assets() / "Props" / "Nested");
    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(tree.Assets()))
    {
        return 18;
    }

    const VoxelForge::Editor::AssetOperationResult fileRename =
        directory.RenameEntry("crate.txt", "barrel");

    if (!fileRename.Succeeded || !fileRename.ResultingRelativePath ||
        *fileRename.ResultingRelativePath != fs::path("barrel.txt") ||
        fs::exists(tree.Assets() / "crate.txt") ||
        !fs::is_regular_file(tree.Assets() / "barrel.txt"))
    {
        std::cerr << "File rename failed: " << fileRename.Message;

        if (fileRename.ResultingRelativePath)
        {
            std::cerr << " (result: "
                      << fileRename.ResultingRelativePath->string()
                      << ')';
        }

        std::cerr << '\n';
        return 19;
    }

    const VoxelForge::Editor::AssetOperationResult extensionRename =
        directory.RenameEntry("barrel.txt", "barrel.md");

    if (!extensionRename.Succeeded ||
        !extensionRename.ResultingRelativePath ||
        *extensionRename.ResultingRelativePath != fs::path("barrel.md") ||
        fs::exists(tree.Assets() / "barrel.txt") ||
        !fs::is_regular_file(tree.Assets() / "barrel.md"))
    {
        return 37;
    }

    if (!directory.EnterDirectory("Props") ||
        !directory.EnterDirectory("Nested"))
    {
        return 20;
    }

    const VoxelForge::Editor::AssetOperationResult folderRename =
        directory.RenameEntry(
            tree.Assets() / "Props",
            "Environment");

    if (!folderRename.Succeeded ||
        !folderRename.ResultingRelativePath ||
        *folderRename.ResultingRelativePath != fs::path("Environment") ||
        directory.CurrentRelativePath() !=
            fs::path("Environment") / "Nested" ||
        fs::exists(tree.Assets() / "Props") ||
        !fs::is_directory(
            tree.Assets() / "Environment" / "Nested"))
    {
        return 21;
    }

    return 0;
}

int TestRenameSelectionAndValidation()
{
    TemporaryAssetTree tree("rename-validation");
    WriteFile(tree.Assets() / "crate.txt", "crate");
    WriteFile(tree.Assets() / "duplicate.txt", "duplicate");
    VoxelForge::Editor::AssetBrowser browser;

    if (!browser.SetAssetsRoot(tree.Assets()) ||
        !browser.SelectEntry("crate.txt"))
    {
        return 22;
    }

    const VoxelForge::Editor::AssetOperationResult renameResult =
        browser.RenameSelectedEntry("renamed");

    if (!renameResult.Succeeded ||
        !browser.SelectedRelativePath() ||
        *browser.SelectedRelativePath() != fs::path("renamed.txt") ||
        !fs::is_regular_file(tree.Assets() / "renamed.txt"))
    {
        return 23;
    }

    if (browser.RenameSelectedEntry("duplicate.txt").Succeeded ||
        !browser.SelectedRelativePath() ||
        *browser.SelectedRelativePath() != fs::path("renamed.txt") ||
        !fs::exists(tree.Assets() / "renamed.txt") ||
        !fs::exists(tree.Assets() / "duplicate.txt"))
    {
        return 24;
    }

    constexpr std::array<std::string_view, 8> InvalidNames = {
        "",
        ".",
        "..",
        "Bad/Name",
        "Bad\\Name",
        "Bad<Name",
        "CON",
        "Trailing."};

    for (const std::string_view invalidName : InvalidNames)
    {
        if (browser.RenameSelectedEntry(invalidName).Succeeded ||
            !fs::exists(tree.Assets() / "renamed.txt"))
        {
            return 25;
        }
    }

    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(tree.Assets()) ||
        directory.RenameEntry(tree.Assets(), "RenamedAssets").Succeeded ||
        directory.DeleteEntry(tree.Assets()).Succeeded ||
        !fs::is_directory(tree.Assets()))
    {
        return 26;
    }

    return 0;
}

int TestDeleteOperations()
{
    TemporaryAssetTree tree("delete-operations");
    WriteFile(tree.Assets() / "temporary.txt", "temporary");
    fs::create_directory(tree.Assets() / "EmptyFolder");
    fs::create_directories(tree.Assets() / "NonEmpty" / "Nested");
    WriteFile(
        tree.Assets() / "NonEmpty" / "Nested" / "keep.txt",
        "keep");
    VoxelForge::Editor::AssetBrowser browser;

    if (!browser.SetAssetsRoot(tree.Assets()) ||
        !browser.SelectEntry("temporary.txt") ||
        !browser.CanDeleteSelectedEntry().CanDelete ||
        !browser.DeleteSelectedEntry().Succeeded ||
        browser.SelectedRelativePath() ||
        fs::exists(tree.Assets() / "temporary.txt"))
    {
        return 27;
    }

    if (!browser.SelectEntry("EmptyFolder"))
    {
        return 28;
    }

    const VoxelForge::Editor::AssetDeleteAssessment emptyAssessment =
        browser.CanDeleteSelectedEntry();

    if (!emptyAssessment.CanDelete || !emptyAssessment.IsDirectory ||
        !browser.DeleteSelectedEntry().Succeeded ||
        browser.SelectedRelativePath() ||
        fs::exists(tree.Assets() / "EmptyFolder"))
    {
        return 29;
    }

    if (!browser.SelectEntry("NonEmpty"))
    {
        return 30;
    }

    const VoxelForge::Editor::AssetDeleteAssessment nonEmptyAssessment =
        browser.CanDeleteSelectedEntry();
    const VoxelForge::Editor::AssetOperationResult refusedDelete =
        browser.DeleteSelectedEntry();

    if (nonEmptyAssessment.CanDelete ||
        !nonEmptyAssessment.IsNonEmptyDirectory ||
        refusedDelete.Succeeded || !browser.SelectedRelativePath() ||
        *browser.SelectedRelativePath() != fs::path("NonEmpty") ||
        !fs::is_regular_file(
            tree.Assets() / "NonEmpty" / "Nested" / "keep.txt"))
    {
        return 31;
    }

    return 0;
}

int TestDisappearedEntryAndConfinement()
{
    TemporaryAssetTree tree("operation-confinement");
    WriteFile(tree.Assets() / "gone.txt", "gone");
    const fs::path outsideFile = tree.Root() / "outside.txt";
    WriteFile(outsideFile, "outside");
    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(tree.Assets()))
    {
        return 32;
    }

    std::error_code error;
    fs::remove(tree.Assets() / "gone.txt", error);

    if (error || directory.DeleteEntry("gone.txt").Succeeded ||
        std::ranges::any_of(
            directory.Entries(),
            [](const VoxelForge::Editor::AssetEntry& entry)
            {
                return entry.Name() == "gone.txt";
            }))
    {
        return 33;
    }

    if (directory.RenameEntry(outsideFile, "escaped.txt").Succeeded ||
        directory.DeleteEntry(outsideFile).Succeeded ||
        directory.RenameEntry("../outside.txt", "escaped.txt").Succeeded ||
        directory.DeleteEntry("../outside.txt").Succeeded ||
        !fs::is_regular_file(outsideFile) ||
        fs::exists(tree.Root() / "escaped.txt"))
    {
        return 34;
    }

    return 0;
}

int TestProjectSwitchRejectsPreparedOperation()
{
    TemporaryAssetTree firstTree("prepared-first");
    TemporaryAssetTree secondTree("prepared-second");
    const fs::path preparedPath = firstTree.Assets() / "prepared.txt";
    WriteFile(preparedPath, "prepared");
    WriteFile(secondTree.Assets() / "other.txt", "other");
    VoxelForge::Editor::AssetDirectory directory;

    if (!directory.SetAssetsRoot(firstTree.Assets()) ||
        !directory.SetAssetsRoot(secondTree.Assets()) ||
        directory.RenameEntry(preparedPath, "renamed.txt").Succeeded ||
        directory.DeleteEntry(preparedPath).Succeeded ||
        !fs::is_regular_file(preparedPath))
    {
        return 35;
    }

    VoxelForge::Editor::AssetBrowser browser;

    if (!browser.SetAssetsRoot(firstTree.Assets()) ||
        !browser.SelectEntry("prepared.txt") ||
        !browser.SetAssetsRoot(secondTree.Assets()) ||
        browser.SelectedRelativePath() ||
        browser.RenameSelectedEntry("renamed.txt").Succeeded ||
        browser.DeleteSelectedEntry().Succeeded ||
        !fs::is_regular_file(preparedPath))
    {
        return 36;
    }

    return 0;
}

int TestViewSearchFiltersAndMarkers()
{
    using namespace VoxelForge::Editor;
    const std::vector<AssetEntry> entries = {
        MakeViewEntry("Props", AssetEntryType::Directory),
        MakeViewEntry("castle.vox", AssetEntryType::File, 10U),
        MakeViewEntry("model.qb", AssetEntryType::File, 20U),
        MakeViewEntry("mesh.obj", AssetEntryType::File, 30U),
        MakeViewEntry("preview.PNG", AssetEntryType::File, 40U),
        MakeViewEntry("notes.txt", AssetEntryType::File, 50U),
        MakeViewEntry("README.MD", AssetEntryType::File, 60U),
        MakeViewEntry("unknown.bin", AssetEntryType::File, 70U)};
    AssetBrowserViewModel viewModel;

    viewModel.SetSearchText("CAST");

    if (ViewNames(viewModel.VisibleEntries(entries)) !=
        std::vector<std::string>{"castle.vox"})
    {
        return 38;
    }

    viewModel.SetSearchText("missing");

    if (!viewModel.VisibleEntries(entries).empty())
    {
        return 39;
    }

    viewModel.ClearSearch();
    const std::array filterExpectations = {
        std::pair{AssetBrowserFilter::Folders,
                  std::vector<std::string>{"Props"}},
        std::pair{AssetBrowserFilter::Voxel,
                  std::vector<std::string>{"castle.vox", "model.qb"}},
        std::pair{AssetBrowserFilter::Models,
                  std::vector<std::string>{"mesh.obj"}},
        std::pair{AssetBrowserFilter::Images,
                  std::vector<std::string>{"preview.PNG"}},
        std::pair{AssetBrowserFilter::Text,
                  std::vector<std::string>{"notes.txt", "README.MD"}},
        std::pair{AssetBrowserFilter::Other,
                  std::vector<std::string>{"unknown.bin"}}};

    for (const auto& [filter, expectedNames] : filterExpectations)
    {
        viewModel.Settings().Filter = filter;

        if (ViewNames(viewModel.VisibleEntries(entries)) != expectedNames)
        {
            return 40;
        }
    }

    viewModel.Settings().Filter = AssetBrowserFilter::Voxel;
    viewModel.SetSearchText("MODEL");

    if (ViewNames(viewModel.VisibleEntries(entries)) !=
        std::vector<std::string>{"model.qb"})
    {
        return 41;
    }

    const std::array expectedMarkers = {
        std::string_view{"[DIR]"},
        std::string_view{"[VOX]"},
        std::string_view{"[QB]"},
        std::string_view{"[OBJ]"},
        std::string_view{"[IMG]"},
        std::string_view{"[TXT]"},
        std::string_view{"[TXT]"},
        std::string_view{"[FILE]"}};

    for (std::size_t index = 0; index < entries.size(); ++index)
    {
        if (AssetEntryMarker(entries[index]) != expectedMarkers[index])
        {
            return 42;
        }
    }

    return 0;
}

int TestViewSorting()
{
    using namespace VoxelForge::Editor;
    const std::vector<AssetEntry> entries = {
        MakeViewEntry("ZetaFolder", AssetEntryType::Directory),
        MakeViewEntry("AlphaFolder", AssetEntryType::Directory),
        MakeViewEntry("large.txt", AssetEntryType::File, 90U, 40),
        MakeViewEntry("small.obj", AssetEntryType::File, 10U, 30),
        MakeViewEntry("same-b.vox", AssetEntryType::File, 20U, 20),
        MakeViewEntry("same-a.vox", AssetEntryType::File, 20U, 20),
        MakeViewEntry("image.png", AssetEntryType::File, 40U, 10),
        MakeViewEntry(
            "equal.bin",
            AssetEntryType::File,
            50U,
            50,
            fs::path("B") / "equal.bin"),
        MakeViewEntry(
            "equal.bin",
            AssetEntryType::File,
            50U,
            50,
            fs::path("A") / "equal.bin")};
    AssetBrowserViewModel viewModel;
    AssetBrowserViewSettings& settings = viewModel.Settings();

    settings.SortMode = AssetBrowserSortMode::Name;
    const std::vector<std::string> nameOrder =
        ViewNames(viewModel.VisibleEntries(entries));

    if (nameOrder[0] != "AlphaFolder" ||
        nameOrder[1] != "ZetaFolder" ||
        nameOrder[2] != "equal.bin" ||
        nameOrder[3] != "equal.bin" ||
        nameOrder.back() != "small.obj")
    {
        return 43;
    }

    settings.SortMode = AssetBrowserSortMode::Type;
    const std::vector<std::string> typeOrder =
        ViewNames(viewModel.VisibleEntries(entries));

    if (typeOrder[0] != "AlphaFolder" ||
        typeOrder[1] != "ZetaFolder" ||
        typeOrder[2] != "same-a.vox" ||
        typeOrder[3] != "same-b.vox" ||
        typeOrder[4] != "small.obj" ||
        typeOrder[5] != "image.png")
    {
        return 44;
    }

    settings.SortMode = AssetBrowserSortMode::Size;
    const std::vector<std::string> sizeOrder =
        ViewNames(viewModel.VisibleEntries(entries));

    if (sizeOrder[0] != "AlphaFolder" ||
        sizeOrder[1] != "ZetaFolder" ||
        sizeOrder[2] != "small.obj" ||
        sizeOrder[3] != "same-a.vox" ||
        sizeOrder[4] != "same-b.vox" ||
        sizeOrder.back() != "large.txt")
    {
        return 45;
    }

    settings.SortMode = AssetBrowserSortMode::Modified;
    const std::vector<std::string> modifiedOrder =
        ViewNames(viewModel.VisibleEntries(entries));

    if (modifiedOrder[2] != "image.png" ||
        modifiedOrder[3] != "same-a.vox" ||
        modifiedOrder[4] != "same-b.vox" ||
        modifiedOrder.back() != "equal.bin")
    {
        return 46;
    }

    settings.SortAscending = false;
    const std::vector<const AssetEntry*> descending =
        viewModel.VisibleEntries(entries);

    if (!descending[0]->IsDirectory() || !descending[1]->IsDirectory() ||
        descending[0]->Name() != "ZetaFolder" ||
        descending[2]->Name() != "equal.bin" ||
        descending.back()->Name() != "image.png")
    {
        return 47;
    }

    const std::vector<const AssetEntry*> repeated =
        viewModel.VisibleEntries(entries);

    for (std::size_t index = 0; index < descending.size(); ++index)
    {
        if (descending[index]->RelativePath() !=
            repeated[index]->RelativePath())
        {
            return 48;
        }
    }

    return 0;
}

int TestViewStateAndHiddenSelection()
{
    using namespace VoxelForge::Editor;
    TemporaryAssetTree firstTree("view-state-first");
    TemporaryAssetTree secondTree("view-state-second");
    WriteFile(firstTree.Assets() / "castle.vox", "castle");
    WriteFile(secondTree.Assets() / "preview.png", "preview");
    AssetBrowser browser;

    if (!browser.SetAssetsRoot(firstTree.Assets()) ||
        !browser.SelectEntry("castle.vox"))
    {
        return 49;
    }

    AssetBrowserViewSettings& settings = browser.ViewSettings();
    settings.DisplayMode = AssetBrowserDisplayMode::List;
    settings.Filter = AssetBrowserFilter::Images;
    settings.SortMode = AssetBrowserSortMode::Size;
    settings.SortAscending = false;
    browser.SetSearchText("CASTLE");

    if (!browser.VisibleEntries().empty() ||
        !browser.SelectedRelativePath() ||
        *browser.SelectedRelativePath() != fs::path("castle.vox"))
    {
        return 50;
    }

    const AssetBrowserViewModel navigationViewModel = []
    {
        AssetBrowserViewModel model;
        model.SetSearchText("Props");
        return model;
    }();
    static_cast<void>(navigationViewModel.VisibleEntries({}));

    if (navigationViewModel.SearchText() != "Props")
    {
        return 51;
    }

    if (!browser.SetAssetsRoot(secondTree.Assets()) ||
        browser.SelectedRelativePath() ||
        browser.ViewSettings().SearchText[0] != '\0')
    {
        return 52;
    }

    if (browser.ViewSettings().SearchText[0] != '\0' ||
        browser.ViewSettings().DisplayMode != AssetBrowserDisplayMode::List ||
        browser.ViewSettings().Filter != AssetBrowserFilter::Images ||
        browser.ViewSettings().SortMode != AssetBrowserSortMode::Size ||
        browser.ViewSettings().SortAscending)
    {
        return 53;
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

        if (const int result = TestSelectionSurvivesRefresh(); result != 0)
        {
            return result;
        }

        if (const int result = TestRenameOperations(); result != 0)
        {
            return result;
        }

        if (const int result = TestRenameSelectionAndValidation(); result != 0)
        {
            return result;
        }

        if (const int result = TestDeleteOperations(); result != 0)
        {
            return result;
        }

        if (const int result = TestDisappearedEntryAndConfinement();
            result != 0)
        {
            return result;
        }

        if (const int result = TestProjectSwitchRejectsPreparedOperation();
            result != 0)
        {
            return result;
        }

        if (const int result = TestViewSearchFiltersAndMarkers(); result != 0)
        {
            return result;
        }

        if (const int result = TestViewSorting(); result != 0)
        {
            return result;
        }

        return TestViewStateAndHiddenSelection();
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 100;
    }
}
