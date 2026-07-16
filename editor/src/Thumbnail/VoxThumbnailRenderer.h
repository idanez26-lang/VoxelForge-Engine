#pragma once

#include "ThumbnailImage.h"
#include "ThumbnailMetadata.h"

#include "VoxelForge/Asset/Vox/VoxModel.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

struct ThumbnailPoint final
{
    float X = 0.0F;
    float Y = 0.0F;

    [[nodiscard]] bool operator==(const ThumbnailPoint&) const noexcept = default;
};

struct ThumbnailFraming final
{
    float Scale = 1.0F;
    float OffsetX = 0.0F;
    float OffsetY = 0.0F;
    float MinimumX = 0.0F;
    float MaximumX = 0.0F;
    float MinimumY = 0.0F;
    float MaximumY = 0.0F;
    std::uint32_t Width = VoxThumbnailWidth;
    std::uint32_t Height = VoxThumbnailHeight;

    [[nodiscard]] bool IsFinite() const noexcept;
    [[nodiscard]] bool operator==(const ThumbnailFraming&) const noexcept = default;
};

[[nodiscard]] ThumbnailPoint ProjectThumbnailPoint(
    float x, float y, float z) noexcept;
[[nodiscard]] ThumbnailFraming CalculateThumbnailFraming(
    const std::vector<Asset::Vox::VoxDimensions>& dimensions,
    std::uint32_t width = VoxThumbnailWidth,
    std::uint32_t height = VoxThumbnailHeight);

struct ThumbnailRenderResult final
{
    bool Succeeded = false;
    ThumbnailImage Image;
    std::string Error;
};

class IVoxThumbnailRenderer
{
public:
    virtual ~IVoxThumbnailRenderer() = default;
    [[nodiscard]] virtual ThumbnailRenderResult Render(
        const std::filesystem::path& voxPath) = 0;
};

class SoftwareVoxThumbnailRenderer final : public IVoxThumbnailRenderer
{
public:
    [[nodiscard]] ThumbnailRenderResult Render(
        const std::filesystem::path& voxPath) override;
};

[[nodiscard]] std::shared_ptr<IVoxThumbnailRenderer>
CreateDefaultVoxThumbnailRenderer();

} // namespace VoxelForge::Editor
