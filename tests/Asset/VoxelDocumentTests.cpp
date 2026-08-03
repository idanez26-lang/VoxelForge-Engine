#include "VoxelDocument/VoxelDocumentSession.h"

#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Vox/VoxModelAnalyzer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <tuple>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
using VoxelForge::Asset::Voxel::VoxelBounds;
using VoxelForge::Asset::Voxel::VoxelColor;
using VoxelForge::Asset::Voxel::VoxelDimensions;
using VoxelForge::Asset::Voxel::VoxelDocument;
using VoxelForge::Asset::Voxel::VoxelDocumentChange;
using VoxelForge::Asset::Voxel::VoxelDocumentError;
using VoxelForge::Asset::Voxel::VoxelPosition;
using VoxelForge::Asset::Voxel::VoxDocumentLoader;
using VoxelForge::Asset::Vox::VoxModelAnalyzer;
using VoxelForge::Editor::VoxelDocumentSession;
using VoxelForge::Editor::VoxelDocumentSessionError;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

class TemporaryProject final
{
public:
    TemporaryProject()
    {
        Root = fs::temp_directory_path() /
            ("VoxelForgeVoxelDocument-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        Models = Root / "Project" / "Assets" / "Models";
        External = Root / "External";
        fs::create_directories(Models);
        fs::create_directories(External);
    }

    ~TemporaryProject()
    {
        std::error_code ignored;
        fs::remove_all(Root, ignored);
    }

    fs::path Root;
    fs::path Models;
    fs::path External;
};

void AppendU32(Bytes& bytes, const std::uint32_t value)
{
    for (unsigned int shift = 0U; shift < 32U; shift += 8U)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void Append(Bytes& destination, const Bytes& source)
{
    destination.insert(destination.end(), source.begin(), source.end());
}

Bytes Chunk(
    const std::string_view id,
    const Bytes& content = {},
    const Bytes& children = {})
{
    Require(id.size() == 4U, "VOX chunk id must contain four bytes.");
    Bytes result(id.begin(), id.end());
    AppendU32(result, static_cast<std::uint32_t>(content.size()));
    AppendU32(result, static_cast<std::uint32_t>(children.size()));
    Append(result, content);
    Append(result, children);
    return result;
}

Bytes Size(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z)
{
    Bytes content;
    AppendU32(content, x);
    AppendU32(content, y);
    AppendU32(content, z);
    return Chunk("SIZE", content);
}

Bytes Xyzi(const std::vector<std::array<std::uint8_t, 4U>>& voxels)
{
    Bytes content;
    AppendU32(content, static_cast<std::uint32_t>(voxels.size()));
    for (const auto& voxel : voxels)
        content.insert(content.end(), voxel.begin(), voxel.end());
    return Chunk("XYZI", content);
}

Bytes Model(
    const std::uint32_t x,
    const std::uint32_t y,
    const std::uint32_t z,
    const std::vector<std::array<std::uint8_t, 4U>>& voxels)
{
    Bytes result = Size(x, y, z);
    Append(result, Xyzi(voxels));
    return result;
}

Bytes Pack(const std::uint32_t count)
{
    Bytes content;
    AppendU32(content, count);
    return Chunk("PACK", content);
}

Bytes Palette(const VoxelColor first, const VoxelColor last)
{
    Bytes content(1024U, 0U);
    content[0] = first.Red;
    content[1] = first.Green;
    content[2] = first.Blue;
    content[3] = first.Alpha;
    content[1016] = last.Red;
    content[1017] = last.Green;
    content[1018] = last.Blue;
    content[1019] = last.Alpha;
    return Chunk("RGBA", content);
}

Bytes Vox(const Bytes& children, const std::uint32_t version = 150U)
{
    Bytes result{'V', 'O', 'X', ' '};
    AppendU32(result, version);
    Append(result, Chunk("MAIN", {}, children));
    return result;
}

void Write(const fs::path& path, const Bytes& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(output), "Unable to write VOX test fixture.");
}

VoxelDocument Load(const fs::path& path)
{
    auto loaded = VoxDocumentLoader{}.Load(path, "asset-id");
    Require(loaded.Succeeded(), loaded.Message);
    return std::move(*loaded.Document);
}

void TestEmptyAndInvalidDocuments(TemporaryProject& temporary)
{
    VoxelDocument empty;
    Require(empty.GetModelCount() == 0U && empty.GetVoxelCount() == 0U,
        "Controlled empty document must contain no models or voxels.");
    Require(!empty.GetGlobalBounds().HasValue && !empty.GetBounds(),
        "Controlled empty document must expose explicit empty bounds.");
    Require(!empty.GetVoxel({0, 0, 0}) && !empty.HasVoxel({0, 0, 0}),
        "Controlled empty document reads must be safe.");
    Require(empty.GetRevision() == 0U && !empty.IsDirty(),
        "Controlled empty document state must be deterministic.");

    const auto missing = VoxDocumentLoader{}.Load(
        temporary.Models / "missing.vox");
    Require(missing.Error == VoxelDocumentError::FileNotFound,
        "Missing file must return FileNotFound.");

    const fs::path invalid = temporary.Models / "invalid.vox";
    Write(invalid, {'N', 'O', 'P', 'E'});
    Require(VoxDocumentLoader{}.Load(invalid).Error ==
        VoxelDocumentError::MalformedChunk,
        "Truncated VOX must return a deterministic error.");

    const fs::path unsupported = temporary.Models / "unsupported.vox";
    Write(unsupported, Vox(Model(1U, 1U, 1U, {{0U, 0U, 0U, 1U}}), 200U));
    Require(VoxDocumentLoader{}.Load(unsupported).Error ==
        VoxelDocumentError::UnsupportedVersion,
        "Unsupported VOX version must be refused explicitly.");
}

void TestLoadingReadingAndAnalyzerCoherence(TemporaryProject& temporary)
{
    Bytes children;
    Append(children, Pack(2U));
    Append(children, Model(5U, 7U, 9U,
        {{1U, 2U, 3U, 5U}, {4U, 5U, 6U, 7U}}));
    Append(children, Model(2U, 3U, 4U,
        {{0U, 0U, 0U, 255U}}));
    Append(children, Palette(
        {10U, 20U, 30U, 40U},
        {50U, 60U, 70U, 80U}));
    const fs::path path = temporary.Models / "multi.vox";
    Write(path, Vox(children));

    VoxelDocument document = Load(path);
    Require(document.SourcePath() == fs::absolute(path).lexically_normal(),
        "Document source path was not retained.");
    Require(document.AssetId() && *document.AssetId() == "asset-id",
        "Optional asset id was not retained.");
    Require(document.VoxVersion() == 150U && document.HasCustomPalette(),
        "VOX version or custom palette flag is incorrect.");
    Require(document.GetModelCount() == 2U && document.GetVoxelCount() == 3U,
        "Multi-model/global voxel count is incorrect.");
    Require(document.GetVoxelCount(0U) == 2U &&
        document.GetVoxelCount(1U) == 1U,
        "Per-model voxel counts are incorrect.");
    Require(document.GetDimensions(0U) == VoxelDimensions{5U, 7U, 9U} &&
        document.GetDimensions(1U) == VoxelDimensions{2U, 3U, 4U},
        "Per-model dimensions are incorrect.");
    Require(document.HasVoxel({1, 2, 3}, 0U) &&
        !document.HasVoxel({0, 0, 0}, 0U) &&
        document.GetVoxel({1, 2, 3}, 0U)->PaletteIndex == 5U &&
        document.GetVoxel({0, 0, 0}, 1U)->PaletteIndex == 255U,
        "Sparse voxel reads or palette indices are incorrect.");
    Require(!document.GetVoxel({-1, 0, 0}, 0U) &&
        !document.GetVoxel({5, 0, 0}, 0U) &&
        !document.GetVoxel({0, 0, 0}, 2U),
        "Out-of-bounds/model reads must be refused safely.");

    const auto bounds = document.GetBounds(0U);
    Require(bounds && bounds->HasValue &&
        bounds->Minimum == VoxelPosition{1, 2, 3} &&
        bounds->Maximum == VoxelPosition{4, 5, 6},
        "Non-cubic model bounds are incorrect.");
    Require(document.GetGlobalBounds() == VoxelBounds{
        true, {0, 0, 0}, {4, 5, 6}},
        "Global multi-model bounds are incorrect.");
    Require(document.GetPaletteColor(0U) == VoxelColor{0U, 0U, 0U, 0U} &&
        document.GetPaletteColor(1U) == VoxelColor{10U, 20U, 30U, 40U} &&
        document.GetPaletteColor(255U) == VoxelColor{50U, 60U, 70U, 80U} &&
        !document.GetPaletteColor(256U),
        "Custom palette mapping or bounds are incorrect.");
    Require(!document.IsDirty() && document.GetRevision() == 0U,
        "Loaded document must start clean at revision zero.");

    const auto analysis = VoxModelAnalyzer{}.Analyze(path);
    Require(analysis.Valid &&
        analysis.FormatVersion == document.VoxVersion() &&
        analysis.ModelCount == document.GetModelCount() &&
        analysis.VoxelCount == document.GetVoxelCount() &&
        analysis.SizeX == document.GetDimensions(0U)->X &&
        analysis.SizeY == document.GetDimensions(0U)->Y &&
        analysis.SizeZ == document.GetDimensions(0U)->Z &&
        analysis.UsedPaletteColorCount == document.UsedPaletteColorCount() &&
        analysis.HasCustomPalette == document.HasCustomPalette(),
        "VoxelDocument diverges from VoxModelAnalyzer.");
}

void TestDefaultPaletteAndEmptyBounds(TemporaryProject& temporary)
{
    const fs::path emptyPath = temporary.Models / "empty.vox";
    Write(emptyPath, Vox(Model(4U, 5U, 6U, {})));
    VoxelDocument empty = Load(emptyPath);
    Require(empty.GetModelCount() == 1U && empty.GetVoxelCount() == 0U,
        "Empty VOX model must load as an empty sub-model.");
    Require(empty.GetBounds(0U) && !empty.GetBounds(0U)->HasValue,
        "Empty VOX model bounds must be explicit.");
    Require(!empty.HasCustomPalette() &&
        empty.GetPaletteColor(1U) == VoxelColor{255U, 255U, 255U, 255U},
        "Canonical default MagicaVoxel palette was not retained.");
}

void TestMutationsDirtyRevisionAndBounds(TemporaryProject& temporary)
{
    const fs::path path = temporary.Models / "edit.vox";
    Write(path, Vox(Model(8U, 8U, 8U,
        {{2U, 2U, 2U, 3U}, {4U, 4U, 4U, 4U}})));
    VoxelDocument document = Load(path);

    auto result = document.SetVoxel({6, 1, 7}, 5U);
    Require(result.Succeeded && result.Changed && document.IsDirty() &&
        document.GetRevision() == 1U && document.GetVoxelCount() == 3U,
        "SetVoxel must add, dirty and increment revision once.");
    Require(document.GetBounds(0U) == VoxelBounds{
        true, {2, 1, 2}, {6, 4, 7}},
        "SetVoxel did not extend bounds.");

    result = document.SetVoxel({6, 1, 7}, 5U);
    Require(result.Succeeded && !result.Changed &&
        document.GetRevision() == 1U && document.GetVoxelCount() == 3U,
        "Identical SetVoxel must be a no-op.");
    result = document.SetVoxel({6, 1, 7}, 6U);
    Require(result.Changed && document.GetRevision() == 2U &&
        document.GetVoxelCount() == 3U,
        "SetVoxel on an occupied position must replace only the color.");

    result = document.ReplaceVoxelColor({2, 2, 2}, 8U);
    Require(result.Changed && document.GetRevision() == 3U &&
        document.GetVoxel({2, 2, 2})->PaletteIndex == 8U,
        "ReplaceVoxelColor failed.");
    result = document.ReplaceVoxelColor({2, 2, 2}, 8U);
    Require(!result.Changed && document.GetRevision() == 3U,
        "Identical color replacement must be a no-op.");
    Require(document.ReplaceVoxelColor({0, 0, 0}, 8U).Error ==
        VoxelDocumentError::VoxelNotFound,
        "Color replacement on empty space must fail explicitly.");

    Require(document.SetVoxel({-1, 0, 0}, 1U).Error ==
        VoxelDocumentError::OutOfBounds &&
        document.SetVoxel({8, 0, 0}, 1U).Error ==
            VoxelDocumentError::OutOfBounds &&
        document.SetVoxel({0, 0, 0}, 1U, 5U).Error ==
            VoxelDocumentError::InvalidModelIndex &&
        document.SetVoxel({0, 0, 0}, 0U).Error ==
            VoxelDocumentError::InvalidPaletteIndex &&
        document.SetVoxel({0, 0, 0}, 256U).Error ==
            VoxelDocumentError::InvalidPaletteIndex,
        "Mutation validation errors are not deterministic.");
    Require(document.GetRevision() == 3U,
        "Rejected mutations must not increment revision.");

    result = document.RemoveVoxel({6, 1, 7});
    Require(result.Changed && document.GetRevision() == 4U &&
        document.GetVoxelCount() == 2U &&
        document.GetBounds(0U) == VoxelBounds{
            true, {2, 2, 2}, {4, 4, 4}},
        "Boundary removal must shrink bounds correctly.");
    result = document.RemoveVoxel({6, 1, 7});
    Require(!result.Changed && document.GetRevision() == 4U,
        "Removing empty space must be a no-op.");
    Require(document.RemoveVoxel({0, 0, 0}, 9U).Error ==
        VoxelDocumentError::InvalidModelIndex,
        "Invalid model removal must be refused.");

    Require(document.RemoveVoxel({2, 2, 2}).Changed &&
        document.RemoveVoxel({4, 4, 4}).Changed &&
        document.GetVoxelCount() == 0U &&
        document.GetBounds(0U) && !document.GetBounds(0U)->HasValue,
        "Removing the last voxel must clear bounds.");

    const VoxelColor replacement{1U, 2U, 3U, 4U};
    const std::uint64_t beforePalette = document.GetRevision();
    result = document.SetPaletteColor(9U, replacement);
    Require(result.Changed &&
        document.GetPaletteColor(9U) == replacement &&
        document.GetRevision() == beforePalette + 1U,
        "Palette mutation must dirty and increment revision.");
    result = document.SetPaletteColor(9U, replacement);
    Require(!result.Changed &&
        document.GetRevision() == beforePalette + 1U,
        "Identical palette mutation must be a no-op.");
    Require(document.SetPaletteColor(0U, replacement).Error ==
        VoxelDocumentError::InvalidPaletteIndex,
        "Reserved palette index zero must not be editable.");

    const std::uint64_t savedRevision = document.GetRevision();
    document.MarkSaved();
    Require(!document.IsDirty() && document.GetRevision() == savedRevision,
        "MarkSaved must clear dirty without resetting revision.");
}

[[nodiscard]] bool SamePositions(
    std::vector<VoxelPosition> actual,
    std::vector<VoxelPosition> expected)
{
    const auto lessThan = [](const VoxelPosition& a, const VoxelPosition& b)
    {
        return std::tie(a.X, a.Y, a.Z) < std::tie(b.X, b.Y, b.Z);
    };
    std::sort(actual.begin(), actual.end(), lessThan);
    std::sort(expected.begin(), expected.end(), lessThan);
    return actual == expected;
}

void TestRevisionJournalChangesSince(TemporaryProject& temporary)
{
    const fs::path path = temporary.Models / "journal.vox";
    Write(path, Vox(Model(8U, 8U, 8U, {})));
    VoxelDocument document = Load(path);

    Require(document.ChangesSince(0U) && document.ChangesSince(0U)->empty(),
        "Fresh document must report an empty change set at revision zero.");
    Require(!document.ChangesSince(5U),
        "A future revision must be refused with nullopt.");

    Require(document.SetVoxel({1, 1, 1}, 3U).Changed &&
        document.SetVoxel({2, 2, 2}, 4U).Changed,
        "Journal test setup mutations failed.");
    auto changes = document.ChangesSince(0U);
    Require(changes && SamePositions(*changes, {{1, 1, 1}, {2, 2, 2}}),
        "ChangesSince(0) must aggregate both mutations.");
    changes = document.ChangesSince(1U);
    Require(changes && SamePositions(*changes, {{2, 2, 2}}),
        "ChangesSince must exclude already-seen revisions.");
    Require(document.ChangesSince(2U) && document.ChangesSince(2U)->empty(),
        "ChangesSince at the current revision must be empty.");

    Require(!document.SetVoxel({1, 1, 1}, 3U).Changed &&
        document.SetVoxel({0, 0, 0}, 0U).Error ==
            VoxelDocumentError::InvalidPaletteIndex &&
        document.GetRevision() == 2U,
        "No-ops and rejected mutations must not touch the journal.");

    Require(document.SetPaletteColor(9U, VoxelColor{1U, 2U, 3U, 4U}).Changed,
        "Palette mutation failed.");
    changes = document.ChangesSince(2U);
    Require(changes && changes->empty(),
        "Palette-only mutations must journal an empty voxel set.");
    changes = document.ChangesSince(0U);
    Require(changes && SamePositions(*changes, {{1, 1, 1}, {2, 2, 2}}),
        "Palette mutations must not add voxel positions to the journal.");

    Require(document.RemoveVoxel({1, 1, 1}).Changed,
        "Journal removal mutation failed.");
    changes = document.ChangesSince(3U);
    Require(changes && SamePositions(*changes, {{1, 1, 1}}),
        "Removals must be journaled like additions.");

    const std::vector<VoxelDocumentChange> batch{
        {0U, {5, 5, 5}, false, 0U, true, 2U},
        {0U, {6, 6, 6}, false, 0U, true, 2U},
        {0U, {7, 7, 7}, false, 0U, true, 2U}};
    Require(document.ApplyVoxelChanges(batch).Changed,
        "Composite change batch failed.");
    changes = document.ChangesSince(4U);
    Require(changes &&
        SamePositions(*changes, {{5, 5, 5}, {6, 6, 6}, {7, 7, 7}}),
        "Composite changes must journal every touched position.");

    // Eviction: churn more revisions than the bounded ring keeps.
    const std::uint64_t beforeChurn = document.GetRevision();
    for (std::size_t index = 0U;
         index <= VoxelDocument::MaximumJournaledRevisions; ++index)
    {
        Require(document.ReplaceVoxelColor(
            {5, 5, 5}, (index % 2U == 0U) ? 3U : 2U).Changed,
            "Journal churn mutation failed.");
    }
    Require(!document.ChangesSince(beforeChurn),
        "Evicted revisions must force a nullopt (full rebuild).");
    changes = document.ChangesSince(document.GetRevision() - 1U);
    Require(changes && SamePositions(*changes, {{5, 5, 5}}),
        "Recent revisions must survive the ring eviction.");

    // Overflow: one mutation touching more positions than the per-revision cap.
    const fs::path overflowPath = temporary.Models / "journal-overflow.vox";
    Write(overflowPath, Vox(Model(17U, 17U, 17U, {})));
    VoxelDocument big = Load(overflowPath);
    std::vector<VoxelDocumentChange> huge;
    huge.reserve(VoxelDocument::MaximumJournaledPositionsPerRevision + 1U);
    for (std::size_t linear = 0U;
         linear <= VoxelDocument::MaximumJournaledPositionsPerRevision;
         ++linear)
    {
        huge.push_back({0U,
            {static_cast<std::int32_t>(linear % 17U),
             static_cast<std::int32_t>((linear / 17U) % 17U),
             static_cast<std::int32_t>(linear / (17U * 17U))},
            false, 0U, true, 1U});
    }
    Require(big.ApplyVoxelChanges(huge).Changed,
        "Oversized composite batch failed.");
    Require(!big.ChangesSince(0U),
        "An overflowed delta must force a nullopt (full rebuild).");
    Require(big.ChangesSince(big.GetRevision()) &&
        big.ChangesSince(big.GetRevision())->empty(),
        "The current revision must stay answerable after an overflow.");
    Require(big.RemoveVoxel({0, 0, 0}).Changed,
        "Post-overflow removal failed.");
    changes = big.ChangesSince(big.GetRevision() - 1U);
    Require(changes && SamePositions(*changes, {{0, 0, 0}}),
        "Deltas recorded after an overflow must remain answerable.");
    Require(!big.ChangesSince(big.GetRevision() - 2U),
        "Ranges crossing an overflowed delta must force a nullopt.");
}

void TestIndependentSubModelsAndDuplicateRefusal(TemporaryProject& temporary)
{
    Bytes children;
    Append(children, Pack(2U));
    Append(children, Model(3U, 3U, 3U, {{0U, 0U, 0U, 1U}}));
    Append(children, Model(3U, 3U, 3U, {{2U, 2U, 2U, 2U}}));
    const fs::path path = temporary.Models / "independent.vox";
    Write(path, Vox(children));
    VoxelDocument document = Load(path);
    Require(document.SetVoxel({1, 1, 1}, 3U, 1U).Changed &&
        !document.HasVoxel({1, 1, 1}, 0U) &&
        document.HasVoxel({1, 1, 1}, 1U) &&
        document.GetVoxelCount(0U) == 1U &&
        document.GetVoxelCount(1U) == 2U,
        "Sub-model mutations must remain independent.");

    const fs::path duplicate = temporary.Models / "duplicate.vox";
    Write(duplicate, Vox(Model(2U, 2U, 2U,
        {{0U, 0U, 0U, 1U}, {0U, 0U, 0U, 2U}})));
    Require(VoxDocumentLoader{}.Load(duplicate).Error ==
        VoxelDocumentError::DuplicateVoxel,
        "Duplicate coordinates must be refused by the document loader.");
}

void TestSparseReasonableLargeDocument(TemporaryProject& temporary)
{
    std::vector<std::array<std::uint8_t, 4U>> voxels;
    voxels.reserve(128U * 128U);
    for (std::uint32_t z = 0U; z < 128U; ++z)
    {
        for (std::uint32_t y = 0U; y < 128U; ++y)
        {
            voxels.push_back({
                static_cast<std::uint8_t>((y + z) % 128U),
                static_cast<std::uint8_t>(y),
                static_cast<std::uint8_t>(z),
                static_cast<std::uint8_t>((y % 254U) + 1U)});
        }
    }
    const fs::path path = temporary.Models / "large.vox";
    Write(path, Vox(Model(128U, 128U, 128U, voxels)));
    VoxelDocument document = Load(path);
    Require(document.GetVoxelCount() == voxels.size() &&
        document.HasVoxel({127, 127, 0}) &&
        document.HasVoxel({126, 127, 127}),
        "Reasonably large sparse document failed to load or access voxels.");
    Require(document.SetVoxel({0, 0, 127}, 12U).Changed &&
        document.RemoveVoxel({0, 0, 127}).Changed,
        "Large sparse document add/remove failed.");
}

void TestSessionLifecycleAndInternalProtection(TemporaryProject& temporary)
{
    const fs::path first = temporary.Models / "first.vox";
    const fs::path second = temporary.Models / "second.vox";
    const fs::path external = temporary.External / "external.vox";
    Write(first, Vox(Model(2U, 2U, 2U, {{0U, 0U, 0U, 1U}})));
    Write(second, Vox(Model(3U, 3U, 3U, {{1U, 1U, 1U, 2U}})));
    Write(external, Vox(Model(1U, 1U, 1U, {{0U, 0U, 0U, 3U}})));

    VoxelDocumentSession session;
    Require(session.Open(first).Error == VoxelDocumentSessionError::NoProject,
        "Session must refuse opening without a project.");
    Require(session.SetProjectRoot(temporary.Root / "Project"),
        "Session project setup failed.");
    Require(session.Open(external).Error ==
        VoxelDocumentSessionError::ExternalAsset,
        "Session must refuse external non-imported assets.");

    Require(session.Open(first).Succeeded() &&
        session.HasActiveDocument() && session.SourcePath() ==
            fs::weakly_canonical(first) &&
        session.Revision() == 0U && !session.IsDirty(),
        "Session failed to open and expose the first document.");
    const std::uint64_t firstGeneration = session.Generation();
    Require(session.ActiveDocument()->SetVoxel({1, 1, 1}, 4U).Changed &&
        session.IsDirty() && session.Revision() == 1U,
        "Session did not expose active document dirty/revision state.");

    Require(session.Open(second).Succeeded() &&
        session.Generation() == firstGeneration + 1U &&
        session.SourcePath() == fs::weakly_canonical(second) &&
        !session.IsDirty() && session.Revision() == 0U &&
        session.ActiveDocument()->HasVoxel({1, 1, 1}),
        "Session failed to replace the active document cleanly.");
    session.Close();
    Require(!session.HasActiveDocument() && session.ActiveDocument() == nullptr &&
        session.SourcePath().empty() && !session.IsDirty() &&
        session.Revision() == 0U,
        "Session close left an active object or state behind.");
    Require(session.Open(first).Succeeded(),
        "Session failed to reopen an internal document.");
    session.ClearProject();
    Require(!session.HasActiveDocument() &&
        session.Open(first).Error == VoxelDocumentSessionError::NoProject,
        "Project change/clear must release the active document.");
}
}

int main()
{
    try
    {
        TemporaryProject temporary;
        TestEmptyAndInvalidDocuments(temporary);
        TestLoadingReadingAndAnalyzerCoherence(temporary);
        TestDefaultPaletteAndEmptyBounds(temporary);
        TestMutationsDirtyRevisionAndBounds(temporary);
        TestRevisionJournalChangesSince(temporary);
        TestIndependentSubModelsAndDuplicateRefusal(temporary);
        TestSparseReasonableLargeDocument(temporary);
        TestSessionLifecycleAndInternalProtection(temporary);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
