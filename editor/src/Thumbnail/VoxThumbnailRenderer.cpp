#include "VoxThumbnailRenderer.h"

#include "VoxelForge/Asset/Vox/VoxImporter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>

namespace VoxelForge::Editor
{
namespace
{
constexpr float IsoX = 0.8660254038F;
constexpr float IsoY = 0.5F;
constexpr float Margin = 16.0F;
constexpr float MaximumVoxelScale = 24.0F;
constexpr std::size_t MaximumRenderedVoxels = 300000U;

struct RenderVoxel final
{
    float X = 0.0F;
    float Y = 0.0F;
    float Z = 0.0F;
    float Depth = 0.0F;
    Asset::Vox::VoxColor Color;
};

struct PixelPoint final
{
    float X = 0.0F;
    float Y = 0.0F;
};

std::array<std::uint8_t, 4> Shade(
    const Asset::Vox::VoxColor color,
    const float factor)
{
    const auto component = [factor](const std::uint8_t value)
    {
        return static_cast<std::uint8_t>(std::clamp(
            static_cast<int>(std::lround(static_cast<float>(value) * factor)),
            0, 255));
    };
    return {component(color.Red), component(color.Green),
        component(color.Blue), 255U};
}

void SetPixel(
    ThumbnailImage& image,
    const int x,
    const int y,
    const std::array<std::uint8_t, 4>& color)
{
    if (x < 0 || y < 0 || x >= static_cast<int>(image.Width) ||
        y >= static_cast<int>(image.Height))
        return;
    const std::size_t offset =
        (static_cast<std::size_t>(y) * image.Width +
         static_cast<std::size_t>(x)) * 4U;
    std::copy(color.begin(), color.end(), image.Pixels.begin() + offset);
}

float Edge(const PixelPoint a, const PixelPoint b, const PixelPoint point)
{
    return (point.X - a.X) * (b.Y - a.Y) -
        (point.Y - a.Y) * (b.X - a.X);
}

void FillTriangle(
    ThumbnailImage& image,
    const PixelPoint a,
    const PixelPoint b,
    const PixelPoint c,
    const std::array<std::uint8_t, 4>& color)
{
    const int minimumX = std::max(0, static_cast<int>(std::floor(
        std::min({a.X, b.X, c.X}))));
    const int maximumX = std::min(static_cast<int>(image.Width) - 1,
        static_cast<int>(std::ceil(std::max({a.X, b.X, c.X}))));
    const int minimumY = std::max(0, static_cast<int>(std::floor(
        std::min({a.Y, b.Y, c.Y}))));
    const int maximumY = std::min(static_cast<int>(image.Height) - 1,
        static_cast<int>(std::ceil(std::max({a.Y, b.Y, c.Y}))));
    const float area = Edge(a, b, c);
    if (std::abs(area) < 0.0001F) return;
    for (int y = minimumY; y <= maximumY; ++y)
    {
        for (int x = minimumX; x <= maximumX; ++x)
        {
            const PixelPoint point{
                static_cast<float>(x) + 0.5F,
                static_cast<float>(y) + 0.5F};
            const float first = Edge(a, b, point);
            const float second = Edge(b, c, point);
            const float third = Edge(c, a, point);
            if ((first >= 0.0F && second >= 0.0F && third >= 0.0F) ||
                (first <= 0.0F && second <= 0.0F && third <= 0.0F))
                SetPixel(image, x, y, color);
        }
    }
}

void FillQuad(
    ThumbnailImage& image,
    const std::array<PixelPoint, 4>& points,
    const std::array<std::uint8_t, 4>& color)
{
    FillTriangle(image, points[0], points[1], points[2], color);
    FillTriangle(image, points[0], points[2], points[3], color);
}

PixelPoint ToPixel(
    const ThumbnailFraming& framing,
    const float x,
    const float y,
    const float z)
{
    const ThumbnailPoint projected = ProjectThumbnailPoint(x, y, z);
    return {projected.X * framing.Scale + framing.OffsetX,
        projected.Y * framing.Scale + framing.OffsetY};
}

void DrawCube(
    ThumbnailImage& image,
    const ThumbnailFraming& framing,
    const RenderVoxel& voxel)
{
    if (framing.Scale < 1.2F)
    {
        const PixelPoint point = ToPixel(
            framing, voxel.X + 0.5F, voxel.Y + 0.5F, voxel.Z + 0.5F);
        const auto color = Shade(voxel.Color, 0.95F);
        SetPixel(image, static_cast<int>(std::lround(point.X)),
            static_cast<int>(std::lround(point.Y)), color);
        return;
    }
    const float x = voxel.X;
    const float y = voxel.Y;
    const float z = voxel.Z;
    const std::array<PixelPoint, 4> top{
        ToPixel(framing, x, y, z + 1.0F),
        ToPixel(framing, x + 1.0F, y, z + 1.0F),
        ToPixel(framing, x + 1.0F, y + 1.0F, z + 1.0F),
        ToPixel(framing, x, y + 1.0F, z + 1.0F)};
    const std::array<PixelPoint, 4> right{
        ToPixel(framing, x + 1.0F, y, z),
        ToPixel(framing, x + 1.0F, y + 1.0F, z),
        ToPixel(framing, x + 1.0F, y + 1.0F, z + 1.0F),
        ToPixel(framing, x + 1.0F, y, z + 1.0F)};
    const std::array<PixelPoint, 4> left{
        ToPixel(framing, x, y + 1.0F, z),
        ToPixel(framing, x + 1.0F, y + 1.0F, z),
        ToPixel(framing, x + 1.0F, y + 1.0F, z + 1.0F),
        ToPixel(framing, x, y + 1.0F, z + 1.0F)};
    FillQuad(image, right, Shade(voxel.Color, 0.72F));
    FillQuad(image, left, Shade(voxel.Color, 0.56F));
    FillQuad(image, top, Shade(voxel.Color, 1.0F));
}

ThumbnailImage CreateBackground()
{
    ThumbnailImage image;
    image.Width = VoxThumbnailWidth;
    image.Height = VoxThumbnailHeight;
    image.Pixels.resize(
        static_cast<std::size_t>(image.Width) * image.Height * 4U);
    for (std::uint32_t y = 0U; y < image.Height; ++y)
    {
        const float blend = static_cast<float>(y) /
            static_cast<float>(image.Height - 1U);
        const std::uint8_t value = static_cast<std::uint8_t>(
            std::lround(34.0F + blend * 16.0F));
        for (std::uint32_t x = 0U; x < image.Width; ++x)
            SetPixel(image, static_cast<int>(x), static_cast<int>(y),
                {value, static_cast<std::uint8_t>(value + 2U),
                 static_cast<std::uint8_t>(value + 7U), 255U});
    }
    return image;
}
}

bool ThumbnailFraming::IsFinite() const noexcept
{
    return Width > 0U && Height > 0U && Scale > 0.0F &&
        std::isfinite(Scale) && std::isfinite(OffsetX) &&
        std::isfinite(OffsetY) && std::isfinite(MinimumX) &&
        std::isfinite(MaximumX) && std::isfinite(MinimumY) &&
        std::isfinite(MaximumY);
}

ThumbnailPoint ProjectThumbnailPoint(
    const float x,
    const float y,
    const float z) noexcept
{
    return {(x - y) * IsoX, (x + y) * IsoY - z};
}

ThumbnailFraming CalculateThumbnailFraming(
    const std::vector<Asset::Vox::VoxDimensions>& dimensions,
    const std::uint32_t width,
    const std::uint32_t height)
{
    ThumbnailFraming result;
    result.Width = width;
    result.Height = height;
    if (dimensions.empty() || width == 0U || height == 0U) return result;
    result.MinimumX = std::numeric_limits<float>::max();
    result.MinimumY = std::numeric_limits<float>::max();
    result.MaximumX = std::numeric_limits<float>::lowest();
    result.MaximumY = std::numeric_limits<float>::lowest();
    float modelOffset = 0.0F;
    for (const auto& dimension : dimensions)
    {
        const float maximumX = modelOffset + static_cast<float>(dimension.X);
        const float maximumY = static_cast<float>(dimension.Y);
        const float maximumZ = static_cast<float>(dimension.Z);
        for (const float x : {modelOffset, maximumX})
            for (const float y : {0.0F, maximumY})
                for (const float z : {0.0F, maximumZ})
                {
                    const ThumbnailPoint point = ProjectThumbnailPoint(x, y, z);
                    result.MinimumX = std::min(result.MinimumX, point.X);
                    result.MaximumX = std::max(result.MaximumX, point.X);
                    result.MinimumY = std::min(result.MinimumY, point.Y);
                    result.MaximumY = std::max(result.MaximumY, point.Y);
                }
        modelOffset = maximumX + 2.0F;
    }
    const float spanX = std::max(0.001F, result.MaximumX - result.MinimumX);
    const float spanY = std::max(0.001F, result.MaximumY - result.MinimumY);
    const float availableWidth = std::max(1.0F,
        static_cast<float>(width) - Margin * 2.0F);
    const float availableHeight = std::max(1.0F,
        static_cast<float>(height) - Margin * 2.0F);
    result.Scale = std::min({
        availableWidth / spanX,
        availableHeight / spanY,
        MaximumVoxelScale});
    result.OffsetX = (static_cast<float>(width) - spanX * result.Scale) * 0.5F -
        result.MinimumX * result.Scale;
    result.OffsetY = (static_cast<float>(height) - spanY * result.Scale) * 0.5F -
        result.MinimumY * result.Scale;
    return result;
}

ThumbnailRenderResult SoftwareVoxThumbnailRenderer::Render(
    const std::filesystem::path& voxPath)
{
    const auto imported = Asset::Vox::VoxImporter{}.Inspect(voxPath);
    if (!imported.Result.Succeeded || !imported.Asset)
        return {false, {}, imported.Result.Message};
    const Asset::Vox::VoxModel& model = *imported.Asset;
    std::vector<Asset::Vox::VoxDimensions> dimensions;
    dimensions.reserve(model.Models.size());
    for (const auto& subModel : model.Models)
        dimensions.push_back(subModel.Dimensions);
    const ThumbnailFraming framing = CalculateThumbnailFraming(dimensions);
    if (!framing.IsFinite())
        return {false, {}, "Unable to calculate finite thumbnail framing."};
    std::size_t totalVoxels = 0U;
    for (const auto& subModel : model.Models)
        totalVoxels += subModel.Voxels.size();
    const std::size_t stride = std::max<std::size_t>(
        1U, (totalVoxels + MaximumRenderedVoxels - 1U) /
            MaximumRenderedVoxels);
    std::vector<RenderVoxel> voxels;
    voxels.reserve(std::min(totalVoxels, MaximumRenderedVoxels));
    float modelOffset = 0.0F;
    std::size_t globalIndex = 0U;
    for (const auto& subModel : model.Models)
    {
        for (const auto& voxel : subModel.Voxels)
        {
            if ((globalIndex++ % stride) != 0U) continue;
            const float x = modelOffset + static_cast<float>(voxel.X);
            const float y = static_cast<float>(voxel.Y);
            const float z = static_cast<float>(voxel.Z);
            voxels.push_back({x, y, z, x + y + z,
                model.Palette[voxel.ColorIndex]});
        }
        modelOffset += static_cast<float>(subModel.Dimensions.X) + 2.0F;
    }
    std::stable_sort(voxels.begin(), voxels.end(),
        [](const RenderVoxel& left, const RenderVoxel& right)
        {
            return std::tie(left.Depth, left.Z, left.Y, left.X) <
                std::tie(right.Depth, right.Z, right.Y, right.X);
        });
    ThumbnailImage image = CreateBackground();
    for (const RenderVoxel& voxel : voxels)
        DrawCube(image, framing, voxel);
    return {true, std::move(image), {}};
}

std::shared_ptr<IVoxThumbnailRenderer> CreateDefaultVoxThumbnailRenderer()
{
    return std::make_shared<SoftwareVoxThumbnailRenderer>();
}

} // namespace VoxelForge::Editor
