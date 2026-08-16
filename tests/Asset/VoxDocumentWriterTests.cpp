#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentWriter.h"
#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Vox/VoxModelAnalyzer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <span>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using namespace VoxelForge::Asset;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

class TemporaryDirectory final
{
public:
    TemporaryDirectory()
    {
        Path = fs::temp_directory_path() /
            ("VoxelForgeVoxWriter-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(Path);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        fs::remove_all(Path, ignored);
    }
    fs::path Path;
};

Vox::VoxModel Source(
    std::vector<Vox::VoxModelMetadata> models,
    const bool customPalette = false)
{
    Vox::VoxModel source;
    source.Version = 150U;
    source.Models = std::move(models);
    source.Palette = Vox::DefaultVoxPalette();
    source.HasCustomPalette = customPalette;
    source.HasPackChunk = source.Models.size() > 1U;
    source.DeclaredModelCount = static_cast<std::uint32_t>(source.Models.size());
    if (customPalette)
    {
        source.Palette[1U] = {10U, 20U, 30U, 255U};
        source.Palette[255U] = {200U, 150U, 100U, 128U};
    }
    return source;
}

Voxel::VoxelDocument Build(
    Vox::VoxModel source,
    const fs::path& path = "writer-test.vox")
{
    auto loaded = Voxel::VoxDocumentLoader{}.Build(source, path, "asset-id");
    Require(loaded.Succeeded(), loaded.Message.empty()
        ? "Unable to build writer test document." : loaded.Message);
    return std::move(*loaded.Document);
}

void Write(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    Require(static_cast<bool>(output), "Unable to write VOX writer fixture.");
}

std::uint32_t ReadU32(
    const std::vector<std::uint8_t>& bytes,
    const std::size_t offset)
{
    Require(offset + 4U <= bytes.size(), "U32 read exceeds writer output.");
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::size_t FindChunk(
    const std::vector<std::uint8_t>& bytes,
    const std::string_view id)
{
    const auto found = std::search(bytes.begin(), bytes.end(), id.begin(), id.end());
    return found == bytes.end()
        ? std::string::npos
        : static_cast<std::size_t>(std::distance(bytes.begin(), found));
}

void VerifyRoundTrip(
    const Voxel::VoxelDocument& document,
    const fs::path& path)
{
    const Voxel::VoxDocumentWriteResult written =
        Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(written.Succeeded(), written.Message.empty()
        ? "Writer serialization failed without a message." : written.Message);
    Write(path, written.Bytes);
    const auto loaded = Voxel::VoxDocumentLoader{}.Load(path, "asset-id");
    Require(loaded.Succeeded(), loaded.Message.empty()
        ? "Writer output reload failed without a message." : loaded.Message);
    std::string difference;
    const bool equivalent = Voxel::AreVoxelDocumentsEquivalent(
        document, *loaded.Document, difference);
    Require(equivalent, difference.empty()
            ? "Writer round-trip documents differ without a diagnostic."
            : difference);
}

void TestMinimalEmptyAndChunkSizes(TemporaryDirectory& temporary)
{
    const Voxel::VoxelDocument empty = Build(Source({
        {{1U, 1U, 1U}, {}}}));
    const auto result = Voxel::VoxDocumentWriter{}.Serialize(empty);
    Require(result.Succeeded(), result.Message);
    Require(result.Bytes.size() >= 56U &&
        std::string(result.Bytes.begin(), result.Bytes.begin() + 4U) == "VOX " &&
        ReadU32(result.Bytes, 4U) == 150U,
        "Writer did not emit a valid VOX header.");
    Require(FindChunk(result.Bytes, "MAIN") == 8U &&
        ReadU32(result.Bytes, 12U) == 0U &&
        ReadU32(result.Bytes, 16U) == result.Bytes.size() - 20U,
        "MAIN chunk sizes are incorrect.");
    const std::size_t xyzi = FindChunk(result.Bytes, "XYZI");
    Require(xyzi != std::string::npos && ReadU32(result.Bytes, xyzi + 4U) == 4U &&
        ReadU32(result.Bytes, xyzi + 12U) == 0U,
        "Empty XYZI chunk is not valid.");
    Require(FindChunk(result.Bytes, "RGBA") == std::string::npos,
        "Default palette should use the canonical implicit VOX palette.");
    VerifyRoundTrip(empty, temporary.Path / "empty.vox");
}

void TestDeterminismPaletteAndAnalysis(TemporaryDirectory& temporary)
{
    const Voxel::VoxelDocument document = Build(Source({
        {{8U, 7U, 6U}, {
            {7U, 6U, 5U, 255U}, {0U, 0U, 0U, 1U},
            {3U, 2U, 1U, 17U}, {1U, 5U, 4U, 1U}}}}, true));
    const auto first = Voxel::VoxDocumentWriter{}.Serialize(document);
    const auto second = Voxel::VoxDocumentWriter{}.Serialize(document);
    Require(first.Succeeded() && second.Succeeded() &&
        first.Bytes == second.Bytes,
        "Identical documents must produce identical VOX bytes.");
    const std::size_t xyzi = FindChunk(first.Bytes, "XYZI");
    const std::size_t rgba = FindChunk(first.Bytes, "RGBA");
    Require(xyzi != std::string::npos &&
        ReadU32(first.Bytes, xyzi + 4U) == 4U + 4U * 4U &&
        rgba != std::string::npos && ReadU32(first.Bytes, rgba + 4U) == 1024U,
        "XYZI or RGBA chunk size is incorrect.");
    const fs::path output = temporary.Path / "custom.vox";
    Write(output, first.Bytes);
    const Vox::VoxModelAnalysis analysis = Vox::VoxModelAnalyzer{}.Analyze(output);
    Require(analysis.Valid && analysis.ModelCount == 1U &&
        analysis.VoxelCount == 4U && analysis.SizeX == 8U &&
        analysis.SizeY == 7U && analysis.SizeZ == 6U &&
        analysis.HasCustomPalette,
        "Writer output is inconsistent with VoxModelAnalyzer.");
    VerifyRoundTrip(document, output);
}

void TestMultiModelPackAndLimits(TemporaryDirectory& temporary)
{
    const Voxel::VoxelDocument multi = Build(Source({
        {{256U, 2U, 2U}, {{255U, 1U, 1U, 4U}}},
        {{3U, 4U, 5U}, {{2U, 3U, 4U, 9U}, {0U, 0U, 0U, 2U}}}}));
    const auto written = Voxel::VoxDocumentWriter{}.Serialize(multi);
    Require(written.Succeeded(), written.Message);
    const std::size_t pack = FindChunk(written.Bytes, "PACK");
    Require(pack != std::string::npos && ReadU32(written.Bytes, pack + 4U) == 4U &&
        ReadU32(written.Bytes, pack + 12U) == 2U,
        "Multi-model writer output has an invalid PACK chunk.");
    VerifyRoundTrip(multi, temporary.Path / "multi.vox");

    Voxel::VoxelDocument tooWide = Build(Source({
        {{257U, 1U, 1U}, {}}}));
    const auto refused = Voxel::VoxDocumentWriter{}.Serialize(tooWide);
    Require(!refused.Succeeded() &&
        refused.Error == Voxel::VoxDocumentWriteError::InvalidDimensions,
        "XYZI dimensions beyond 256 must be refused without truncation.");

    // VF-STAB-01 bug 3 (arbitrage Tony du 06/08). The loader tolerates up to
    // 2048 per axis so third-party files still open, but the writer can never
    // store more than 256. Such a document used to open fully editable and then
    // fail forever at save time, losing the work. It now opens READ-ONLY, and
    // the refusal happens before any edit is accepted.
    Require(tooWide.IsReadOnly() && !tooWide.ReadOnlyReason().empty(),
        "A document the VOX format cannot write back must open read-only.");
    const auto blockedSet = tooWide.SetVoxel({0, 0, 0}, 2U);
    Require(!blockedSet.Succeeded && !blockedSet.Changed &&
        blockedSet.Error == Voxel::VoxelDocumentError::ReadOnlyDocument,
        "A read-only document must refuse an edit, with an explicit reason.");
    Require(!tooWide.IsDirty(),
        "A refused edit must not mark a read-only document dirty.");

    // VF-STAB-01A bug 1 (revue Codex) : la lecture seule doit être ABSOLUE.
    // SetVoxel passait par ValidateMutation et était couvert, mais RemoveVoxel,
    // SetPaletteColor et ReplacePalette n'empruntaient aucun validateur portant
    // la garde et modifiaient donc encore le document. Chaque mutateur public
    // est vérifié ici, refus explicite ET absence d'effet de bord.
    const auto paletteBefore = tooWide.GetPaletteSnapshot();
    const auto revisionBefore = tooWide.GetRevision();
    const auto voxelCountBefore = tooWide.GetVoxelCount();

    const auto blockedRemove = tooWide.RemoveVoxel({0, 0, 0});
    Require(!blockedRemove.Succeeded && !blockedRemove.Changed &&
        blockedRemove.Error == Voxel::VoxelDocumentError::ReadOnlyDocument,
        "A read-only document must refuse RemoveVoxel.");

    const auto blockedReplaceColor = tooWide.ReplaceVoxelColor({0, 0, 0}, 3U);
    Require(!blockedReplaceColor.Succeeded && !blockedReplaceColor.Changed &&
        blockedReplaceColor.Error ==
            Voxel::VoxelDocumentError::ReadOnlyDocument,
        "A read-only document must refuse ReplaceVoxelColor.");

    const auto blockedPaletteColor =
        tooWide.SetPaletteColor(5U, {9U, 9U, 9U, 255U});
    Require(!blockedPaletteColor.Succeeded && !blockedPaletteColor.Changed &&
        blockedPaletteColor.Error ==
            Voxel::VoxelDocumentError::ReadOnlyDocument,
        "A read-only document must refuse SetPaletteColor.");

    auto replacement = tooWide.GetPaletteSnapshot();
    replacement.Colors[7U] = {1U, 2U, 3U, 255U};
    replacement.HasCustomPalette = true;
    const auto blockedPalette = tooWide.ReplacePalette(replacement);
    Require(!blockedPalette.Succeeded && !blockedPalette.Changed &&
        blockedPalette.Error == Voxel::VoxelDocumentError::ReadOnlyDocument,
        "A read-only document must refuse ReplacePalette.");

    // Aucun de ces refus n'a laissé de trace : palette, révision, nombre de
    // voxels et drapeau « modifié » sont tous intacts.
    Require(tooWide.GetPaletteSnapshot() == paletteBefore &&
        tooWide.GetRevision() == revisionBefore &&
        tooWide.GetVoxelCount() == voxelCountBefore && !tooWide.IsDirty(),
        "A refused edit must leave a read-only document byte-for-byte intact.");

    // Preview and commit share ValidateVoxelChanges, so both must refuse: the
    // artist never sees a preview of an edit that can never be written.
    const std::array<Voxel::VoxelDocumentChange, 1U> change{
        Voxel::VoxelDocumentChange{
            .SubModelIndex = 0U,
            .Position = {0, 0, 0},
            .ExistedBefore = false,
            .PaletteIndexBefore = 0U,
            .ExistsAfter = true,
            .PaletteIndexAfter = 2U}};
    Require(tooWide.ValidateVoxelChanges(change).Error ==
        Voxel::VoxelDocumentError::ReadOnlyDocument,
        "Preview and commit must refuse a read-only document identically.");

    // VF-STAB-01B blocage 2, tests 1 à 4 (revue Codex). Les chemins par lot
    // doivent refuser AUSSI avec un lot VIDE : un lot vide sortait en succès
    // (« ensemble vide ») avant toute garde, ce qui aurait fait d'un document
    // non enregistrable une cible d'écriture apparemment acceptée.
    const std::span<const Voxel::VoxelDocumentChange> emptyBatch{};

    // 1. ApplyVoxelChanges, lot vide.
    const auto emptyApply = tooWide.ApplyVoxelChanges(emptyBatch);
    Require(!emptyApply.Succeeded && !emptyApply.Changed &&
        emptyApply.Error == Voxel::VoxelDocumentError::ReadOnlyDocument,
        "A read-only document must refuse ApplyVoxelChanges with an empty "
        "batch instead of reporting an empty-set success.");

    // 2. ApplyVoxelChanges, lot non vide.
    const auto filledApply = tooWide.ApplyVoxelChanges(change);
    Require(!filledApply.Succeeded && !filledApply.Changed &&
        filledApply.Error == Voxel::VoxelDocumentError::ReadOnlyDocument,
        "A read-only document must refuse ApplyVoxelChanges with a real "
        "batch.");

    // 3. ApplyCompositeChanges, lot vide (et sans changement de palette).
    const auto emptyComposite =
        tooWide.ApplyCompositeChanges(emptyBatch, nullptr);
    Require(!emptyComposite.Succeeded && !emptyComposite.Changed &&
        emptyComposite.Error == Voxel::VoxelDocumentError::ReadOnlyDocument,
        "A read-only document must refuse ApplyCompositeChanges with an empty "
        "batch instead of reporting an empty-set success.");

    // 4. ApplyCompositeChanges, lot non vide accompagné d'un changement de
    //    palette : ni les voxels ni la palette ne doivent passer.
    Voxel::VoxelDocumentPaletteChange paletteChange;
    paletteChange.Before = tooWide.GetPaletteSnapshot();
    paletteChange.After = paletteChange.Before;
    paletteChange.After.Colors[9U] = {3U, 2U, 1U, 255U};
    paletteChange.After.HasCustomPalette = true;
    const auto filledComposite =
        tooWide.ApplyCompositeChanges(change, &paletteChange);
    Require(!filledComposite.Succeeded && !filledComposite.Changed &&
        filledComposite.Error == Voxel::VoxelDocumentError::ReadOnlyDocument,
        "A read-only document must refuse a composite voxel + palette batch.");

    // VF-STAB-01B blocage 1 : l'état « modifié » ne peut pas être atteint par
    // l'historique. UpdateDirtyFromHistory écrivait `dirty_` directement, sans
    // aucune mutation — un document non enregistrable se déclarait donc « à
    // enregistrer ».
    tooWide.UpdateDirtyFromHistory(false);
    Require(!tooWide.IsDirty(),
        "History state must never make a read-only document dirty.");
    tooWide.UpdateDirtyFromHistory(true);
    Require(!tooWide.IsDirty(),
        "A read-only document must stay clean when history reports saved.");
    tooWide.MarkSaved();
    Require(!tooWide.IsDirty(),
        "MarkSaved must keep a read-only document clean.");

    // Après TOUS ces refus : contenu, palette, révision et état inchangés.
    Require(tooWide.GetPaletteSnapshot() == paletteBefore &&
        tooWide.GetRevision() == revisionBefore &&
        tooWide.GetVoxelCount() == voxelCountBefore && !tooWide.IsDirty(),
        "Batch refusals and history updates must leave a read-only document "
        "completely unchanged.");

    // A document inside the writable ceiling stays fully editable.
    Voxel::VoxelDocument writable = Build(Source({
        {{256U, 1U, 1U}, {}}}));
    Require(!writable.IsReadOnly() && writable.ReadOnlyReason().empty(),
        "A document at the 256 ceiling must remain editable.");
    Require(writable.SetVoxel({0, 0, 0}, 2U).Changed,
        "A writable document must still accept edits.");
}

void TestMutationsAndEquivalence(TemporaryDirectory& temporary)
{
    Voxel::VoxelDocument document = Build(Source({
        {{4U, 4U, 4U}, {{0U, 0U, 0U, 2U}, {1U, 1U, 1U, 3U}}}}));
    Require(document.SetVoxel({2, 2, 2}, 4U).Changed,
        "Unable to add writer test voxel.");
    Require(document.RemoveVoxel({0, 0, 0}).Changed,
        "Unable to remove writer test voxel.");
    Require(document.SetPaletteColor(4U, {5U, 6U, 7U, 255U}).Changed,
        "Unable to customize writer test palette.");
    VerifyRoundTrip(document, temporary.Path / "mutated.vox");

    Voxel::VoxelDocument different = Build(Source({
        {{4U, 4U, 4U}, {{1U, 1U, 1U, 3U}}}}));
    std::string difference;
    Require(!Voxel::AreVoxelDocumentsEquivalent(document, different, difference) &&
        !difference.empty(),
        "Document equivalence must report real voxel differences.");
}
}

int main()
{
    try
    {
        TemporaryDirectory temporary;
        TestMinimalEmptyAndChunkSizes(temporary);
        TestDeterminismPaletteAndAnalysis(temporary);
        TestMultiModelPackAndLimits(temporary);
        TestMutationsAndEquivalence(temporary);
        std::cout << "VOX document writer tests passed.\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "VOX document writer tests failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
