#include "VoxelStamps/Workflow/SaveSelectionAsStampWorkflow.h"

#include "VoxelStamps/Library/StampCatalogService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <new>

namespace VoxelForge::Editor::Stamps
{
namespace
{

[[nodiscard]] std::string FailureMessage(const std::string_view fallback, const std::string& detail)
{
    return detail.empty() ? std::string(fallback) : detail;
}

[[nodiscard]] std::string TrimAscii(const std::string_view text)
{
    std::size_t first = 0U;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1U]))) --last;
    return std::string(text.substr(first, last - first));
}

[[nodiscard]] std::string UpperAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

[[nodiscard]] bool IsValidUtf8(const std::string_view text) noexcept
{
    for (std::size_t index = 0U; index < text.size();)
    {
        const unsigned char first = static_cast<unsigned char>(text[index++]);
        if (first < 0x80U) continue;
        unsigned count = 0U;
        std::uint32_t point = 0U;
        std::uint32_t minimum = 0U;
        if ((first & 0xE0U) == 0xC0U) { count = 1U; point = first & 0x1FU; minimum = 0x80U; }
        else if ((first & 0xF0U) == 0xE0U) { count = 2U; point = first & 0x0FU; minimum = 0x800U; }
        else if ((first & 0xF8U) == 0xF0U) { count = 3U; point = first & 0x07U; minimum = 0x10000U; }
        else return false;
        if (index + count > text.size()) return false;
        for (unsigned offset = 0U; offset < count; ++offset)
        {
            const unsigned char next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xC0U) != 0x80U) return false;
            point = (point << 6U) | (next & 0x3FU);
        }
        if (point < minimum || point > 0x10FFFFU || (point >= 0xD800U && point <= 0xDFFFU)) return false;
    }
    return true;
}

[[nodiscard]] bool IsReservedWindowsName(const std::string_view name)
{
    const std::size_t extension = name.find('.');
    const std::string base = UpperAscii(std::string(name.substr(0U, extension)));
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return true;
    if (base.size() == 4U && (base.starts_with("COM") || base.starts_with("LPT")) &&
        base[3] >= '1' && base[3] <= '9') return true;
    return false;
}

[[nodiscard]] SaveSelectionAsStampResult CaptureFailure(const StampCaptureResult& capture)
{
    return {.Status = SaveSelectionAsStampStatus::CaptureFailed,
            .Message = std::string(capture.Diagnostic.Message),
            .Capture = capture};
}

} // namespace

SaveSelectionAsStampWorkflow::SaveSelectionAsStampWorkflow(
    IStampLibraryRepository& library,
    IStampCatalogStore& catalogueStore)
    : library_(library), catalogueStore_(catalogueStore)
{
}

std::string SaveSelectionAsStampWorkflow::ValidateName(const std::string_view name)
{
    const std::string trimmed = TrimAscii(name);
    if (trimmed.empty()) return "A Stamp name cannot be empty.";
    if (trimmed != name) return "A Stamp name cannot start or end with whitespace.";
    if (!IsValidUtf8(name)) return "A Stamp name must be valid UTF-8.";
    if (trimmed.size() > 255U) return "A Stamp name is longer than the Windows filename limit.";
    if (trimmed.back() == '.' || trimmed.back() == ' ')
        return "A Stamp name cannot end with a period or space on Windows.";
    for (const unsigned char character : trimmed)
    {
        if (character < 0x20U || character == '<' || character == '>' || character == ':' ||
            character == '"' || character == '/' || character == '\\' || character == '|' ||
            character == '?' || character == '*')
            return "A Stamp name contains a character forbidden by Windows.";
    }
    if (IsReservedWindowsName(trimmed)) return "A Stamp name uses a reserved Windows device name.";
    return {};
}

bool SaveSelectionAsStampWorkflow::SameProjectRoot(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept
{
    return left.lexically_normal() == right.lexically_normal();
}

SaveSelectionAsStampResult SaveSelectionAsStampWorkflow::Begin(
    const SaveSelectionAsStampBeginRequest& request)
{
    if (snapshot_ || saving_)
        return {.Status = SaveSelectionAsStampStatus::Refused,
                .Message = "A Save Selection As workflow is already active. Cancel it before starting another."};
    if (request.ProjectRoot.empty())
        return {.Status = SaveSelectionAsStampStatus::Refused, .Message = "Save Selection As requires an active project."};
    if (request.Document == nullptr)
        return {.Status = SaveSelectionAsStampStatus::Refused, .Message = "Save Selection As requires an active voxel document."};
    if (request.Selection == nullptr || request.Selection->Empty())
        return {.Status = SaveSelectionAsStampStatus::Refused, .Message = "Save Selection As requires a non-empty voxel selection."};

    try
    {
        SelectionService selectionSnapshot;
        selectionSnapshot.SetDocumentGeneration(request.Selection->DocumentGeneration());
        if (!selectionSnapshot.Apply(request.Selection->Voxels(), SelectionMode::Replace))
            return {.Status = SaveSelectionAsStampStatus::Refused, .Message = "The voxel selection snapshot could not be created."};

        StampCaptureRequest captureRequest{
            .Document = *request.Document,
            .Selection = selectionSnapshot,
            .StampId = Core::UUID{},
            .ExpectedDocumentGeneration = request.DocumentGeneration,
            .ExpectedDocumentRevision = request.DocumentRevision,
            .SubModelIndex = request.SubModelIndex,
            .PivotContext = request.PivotContext,
            .Limits = request.Limits};
        StampCaptureResult capture = StampCaptureService::Capture(captureRequest);
        if (!capture.IsSuccess()) return CaptureFailure(capture);
        const bool requiresSoftLimitConfirmation =
            capture.LimitEvaluation.HasWarning();
        snapshot_ = Snapshot{.ProjectRoot = request.ProjectRoot.lexically_normal(),
                             .Document = request.Document,
                             .DocumentGeneration = request.DocumentGeneration,
                             .DocumentRevision = request.DocumentRevision,
                             .SelectionVoxels = std::vector<Asset::Voxel::VoxelPosition>(
                                 request.Selection->Voxels().begin(), request.Selection->Voxels().end()),
                             .Capture = std::move(capture),
                             .RequiresSoftLimitConfirmation = requiresSoftLimitConfirmation};
        return {.Status = SaveSelectionAsStampStatus::Ready,
                .Message = snapshot_->RequiresSoftLimitConfirmation
                    ? "The selection exceeds a soft Stamp limit. Confirm Save Anyway to continue."
                    : "Selection snapshot is ready to save.",
                .RequiresSoftLimitConfirmation = snapshot_->RequiresSoftLimitConfirmation,
                .Capture = snapshot_->Capture};
    }
    catch (const std::bad_alloc&)
    {
        return {.Status = SaveSelectionAsStampStatus::CaptureFailed,
                .Message = "Save Selection As could not allocate its immutable selection snapshot."};
    }
}

SaveSelectionAsStampResult SaveSelectionAsStampWorkflow::ValidateDraft(
    const SaveSelectionAsStampDraft& draft) const
{
    if (!snapshot_)
        return {.Status = SaveSelectionAsStampStatus::Refused, .Message = "No Save Selection As workflow is active."};
    const std::string error = ValidateName(draft.Name);
    if (!error.empty()) return {.Status = SaveSelectionAsStampStatus::Refused, .Message = error};
    if (snapshot_->RequiresSoftLimitConfirmation && !draft.ConfirmSoftLimit)
        return {.Status = SaveSelectionAsStampStatus::Refused,
                .Message = "This selection exceeds a soft limit. Use Save Anyway to confirm." ,
                .RequiresSoftLimitConfirmation = true};
    return {.Status = SaveSelectionAsStampStatus::Ready, .Message = "Save Selection As draft is valid.",
            .RequiresSoftLimitConfirmation = snapshot_->RequiresSoftLimitConfirmation};
}

SaveSelectionAsStampResult SaveSelectionAsStampWorkflow::Save(
    const SaveSelectionAsStampDraft& draft,
    const SaveSelectionAsStampCurrentContext& current)
{
    if (saving_)
        return {.Status = SaveSelectionAsStampStatus::Refused,
                .Message = "A Save Selection As operation is already in progress."};
    const SaveSelectionAsStampResult validation = ValidateDraft(draft);
    if (validation.Status != SaveSelectionAsStampStatus::Ready) return validation;
    if (!SameProjectRoot(snapshot_->ProjectRoot, current.ProjectRoot) ||
        snapshot_->Document != current.Document ||
        current.Document == nullptr ||
        snapshot_->DocumentGeneration != current.DocumentGeneration ||
        snapshot_->DocumentRevision != current.DocumentRevision ||
        current.Selection == nullptr ||
        current.Document->GetRevision() != snapshot_->DocumentRevision ||
        current.Selection->DocumentGeneration() != snapshot_->DocumentGeneration ||
        snapshot_->SelectionVoxels.size() != current.Selection->Voxels().size() ||
        !std::equal(snapshot_->SelectionVoxels.begin(), snapshot_->SelectionVoxels.end(),
            current.Selection->Voxels().begin(), current.Selection->Voxels().end()))
        return {.Status = SaveSelectionAsStampStatus::Refused,
                .Message = "The project, document revision or voxel selection changed after the snapshot was created."};

    saving_ = true;
    struct SavingGuard final
    {
        bool& Saving;
        ~SavingGuard() { Saving = false; }
    } savingGuard{saving_};

    if (!snapshot_->Capture.Stamp)
        return {.Status = SaveSelectionAsStampStatus::InstallFailed,
                .Message = "The immutable Stamp capture is unavailable.",
                .Capture = snapshot_->Capture};
    StampLibraryResult installed = library_.Install(
        *snapshot_->Capture.Stamp, {.PreferredFileStem = draft.Name, .ReplaceExisting = false});
    if (!installed.Succeeded())
        return {.Status = SaveSelectionAsStampStatus::InstallFailed,
                .Message = FailureMessage("The Stamp could not be installed.", installed.Message),
                .Capture = snapshot_->Capture,
                .Installation = std::move(installed)};

    StampCatalogService catalogue(library_, catalogueStore_);
    catalogue.InvalidateCache();
    StampCatalogResult rebuilt = catalogue.RebuildCatalogue();
    SaveSelectionAsStampResult result{
        .Status = rebuilt.Succeeded() ? SaveSelectionAsStampStatus::CompleteSuccess
                                      : SaveSelectionAsStampStatus::PartialSuccess,
        .Message = rebuilt.Succeeded()
            ? "Selection saved to the Project Library."
            : "The Stamp was installed, but the derived catalogue refresh failed. Rebuild the catalogue to recover.",
        .InstalledAsset = installed.Reference,
        .Capture = snapshot_->Capture,
        .Installation = std::move(installed),
        .Catalogue = std::move(rebuilt)};
    snapshot_.reset();
    return result;
}

SaveSelectionAsStampResult SaveSelectionAsStampWorkflow::Cancel() noexcept
{
    if (saving_)
        return {.Status = SaveSelectionAsStampStatus::Refused,
                .Message = "Save Selection As cannot be cancelled while installation is in progress."};
    snapshot_.reset();
    return {.Status = SaveSelectionAsStampStatus::Cancelled,
            .Message = "Save Selection As was cancelled."};
}

bool SaveSelectionAsStampWorkflow::Active() const noexcept { return snapshot_.has_value(); }
bool SaveSelectionAsStampWorkflow::RequiresSoftLimitConfirmation() const noexcept
{
    return snapshot_ && snapshot_->RequiresSoftLimitConfirmation;
}

} // namespace VoxelForge::Editor::Stamps
