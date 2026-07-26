#include "VoxelStamps/Placement/StampPlacementPlan.h"

#include <algorithm>

namespace VoxelForge::Editor::Stamps
{

StampDocumentIdentity MakeStampDocumentIdentity(
    const Asset::Voxel::VoxelDocument& document)
{
    StampDocumentIdentity identity;
    identity.InstanceToken = reinterpret_cast<std::uintptr_t>(&document);
    identity.SourcePath = document.SourcePath().generic_string();
    if (document.AssetId())
    {
        identity.AssetId = *document.AssetId();
    }
    return identity;
}

bool StampPlacementPlan::IsCurrent(
    const Asset::Voxel::VoxelDocument& document,
    const std::uint64_t documentGeneration,
    const std::size_t subModelIndex) const noexcept
{
    try
    {
        return Document == MakeStampDocumentIdentity(document) &&
            DocumentGeneration == documentGeneration &&
            DocumentRevision == document.GetRevision() &&
            TargetSubModel == subModelIndex;
    }
    catch (...)
    {
        return false;
    }
}

bool StampPlacementPlan::HasErrors() const noexcept
{
    return std::any_of(
        Diagnostics.begin(), Diagnostics.end(),
        [](const StampPlacementDiagnostic& diagnostic)
        {
            return diagnostic.Severity ==
                StampPlacementDiagnosticSeverity::Error;
        });
}

} // namespace VoxelForge::Editor::Stamps
