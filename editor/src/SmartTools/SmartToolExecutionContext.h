#pragma once

#include "SmartTools/SmartToolPlan.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace VoxelForge::Asset::Voxel
{
class VoxelDocument;
}

namespace VoxelForge::Editor
{
class VoxelEditHistory;
class VoxelEditSession;

class SmartToolPreviewPlanConsumer
{
public:
    virtual ~SmartToolPreviewPlanConsumer() = default;
    virtual void ConsumePreviewPlan(SmartToolPlanPtr plan) = 0;
};

class SmartToolCommitPlanConsumer
{
public:
    virtual ~SmartToolCommitPlanConsumer() = default;
    virtual void ConsumeCommitPlan(SmartToolPlanPtr plan) = 0;
};

// Explicit, non-owning dependencies supplied at the execution boundary.
// SmartToolSession intentionally never stores this context.
struct SmartToolExecutionContext final
{
    VoxelEditSession* EditSession = nullptr;
    Asset::Voxel::VoxelDocument* Document = nullptr;
    std::size_t SubModelIndex = 0U;
    std::uint64_t SourceGeneration = 0U;
    VoxelEditHistory* History = nullptr;
    std::optional<std::size_t> PaletteIndex;
    SmartToolPreviewPlanConsumer* PreviewConsumer = nullptr;
    SmartToolCommitPlanConsumer* CommitConsumer = nullptr;
};
} // namespace VoxelForge::Editor
