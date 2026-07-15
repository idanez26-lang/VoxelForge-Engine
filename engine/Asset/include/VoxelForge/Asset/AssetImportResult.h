#pragma once

#include <string>
#include <vector>

namespace VoxelForge::Asset
{

enum class AssetImportError
{
    None,
    FileNotFound,
    FileTooSmall,
    FileTooLarge,
    ReadFailure,
    InvalidSignature,
    InvalidHeader,
    InvalidChunk,
    MissingMainChunk,
    MissingSizeChunk,
    InvalidDimensions,
    TooManyVoxels,
    InvalidVoxel,
    TooManyModels,
    TooManyChunks,
    InconsistentModelCount
};

struct AssetImportResult final
{
    bool Succeeded = false;
    AssetImportError Error = AssetImportError::None;
    std::string Message;
    std::vector<std::string> Warnings;
};

} // namespace VoxelForge::Asset
