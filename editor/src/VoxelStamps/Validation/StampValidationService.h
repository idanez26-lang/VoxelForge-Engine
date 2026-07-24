#pragma once

#include "VoxelStamps/Format/VfstampWriter.h"

#include <cstdint>
#include <string>
#include <vector>

namespace VoxelForge::Editor::Stamps
{

enum class StampDiagnosticSeverity { Info, Warning, Error };
enum class StampDiagnosticCategory { Domain, Container, Compatibility, Integrity, Resource, Canonical };

struct StampDiagnostic final
{
    StampDiagnosticCategory Category = StampDiagnosticCategory::Domain;
    StampDiagnosticSeverity Severity = StampDiagnosticSeverity::Info;
    std::uint32_t ChunkId = 0U;
    std::string Expected;
    std::string Actual;
    std::string Explanation;

    [[nodiscard]] bool operator==(const StampDiagnostic&) const noexcept = default;
};

/// Deterministic portable validation result; future migration handlers may add
/// compatibility diagnostics without changing this contract.
struct StampValidationReport final
{
    std::vector<StampDiagnostic> Diagnostics;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] bool HasWarnings() const noexcept;
    [[nodiscard]] bool operator==(const StampValidationReport&) const noexcept = default;
};

[[nodiscard]] StampValidationReport ValidateStamp(
    const VoxelStamp& stamp,
    const StampResourceLimits& limits = DefaultStampResourceLimits());
[[nodiscard]] StampValidationReport ValidateStampForWrite(
    const VoxelStamp& stamp,
    const StampResourceLimits& limits = DefaultStampResourceLimits());
[[nodiscard]] StampValidationReport ValidateStampForPlacement(
    const VoxelStamp& stamp,
    const StampResourceLimits& limits = DefaultStampResourceLimits());

/// Validates an in-memory .vfstamp through the same Reader/codec pipeline as
/// InspectVfstampBytes. This is the complete file-level validation API.
[[nodiscard]] StampValidationReport ValidateVfstampBytes(
    std::span<const std::byte> bytes,
    const StampResourceLimits& limits = DefaultStampResourceLimits());

enum class VfstampCompatibility { Compatible, NewerMinorCompatible, IncompatibleMajor, Invalid };

struct VfstampChunkInspection final
{
    std::uint32_t Id = 0U;
    std::uint32_t Flags = 0U;
    std::uint64_t PayloadBytes = 0U;
    std::uint32_t DeclaredCrc32 = 0U;
    std::uint32_t ComputedCrc32 = 0U;
    bool CrcVerified = false;

    [[nodiscard]] bool operator==(const VfstampChunkInspection&) const noexcept = default;
};

struct VfstampInspection final
{
    std::string Format{"VFSTAMP"};
    std::string Endianness{"little-endian"};
    std::uint16_t MajorVersion = 0U;
    std::uint16_t MinorVersion = 0U;
    VfstampCompatibility Compatibility = VfstampCompatibility::Invalid;
    std::vector<VfstampChunkInspection> Chunks;
    std::vector<StampPaletteEntry> Palette;
    std::uint32_t PaletteCount = 0U;
    std::uint64_t VoxelCount = 0U;
    StampBounds Bounds{};
    StampPivot Pivot{};
    StampTransform Transform{};
    bool StructuralHashVerified = false;
    bool LogicalHashVerified = false;
    std::uint64_t StructuralHash = 0U;
    std::uint64_t LogicalHash = 0U;
    StampValidationReport Report;
};

/// Bounded in-memory inspection. It delegates structural acceptance to the
/// Reader and semantic reconstruction to the codec. Future migrations/chunk
/// handlers extend this result; they do not alter V1 acceptance here.
[[nodiscard]] VfstampInspection InspectVfstampBytes(
    std::span<const std::byte> bytes,
    const StampResourceLimits& limits = DefaultStampResourceLimits());

} // namespace VoxelForge::Editor::Stamps
