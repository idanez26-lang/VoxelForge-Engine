#include "ThumbnailImage.h"

#include <array>
#include <fstream>
#include <limits>

namespace VoxelForge::Editor
{
namespace
{
constexpr std::array<char, 4> Magic{'V', 'F', 'T', 'N'};
constexpr std::uint32_t FormatVersion = 1U;
constexpr std::uint32_t MaximumDimension = 4096U;

void WriteU32(std::ostream& output, const std::uint32_t value)
{
    const std::array<char, 4> bytes{
        static_cast<char>(value), static_cast<char>(value >> 8U),
        static_cast<char>(value >> 16U), static_cast<char>(value >> 24U)};
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool ReadU32(std::istream& input, std::uint32_t& value)
{
    std::array<unsigned char, 4> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!input) return false;
    value = static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8U) |
        (static_cast<std::uint32_t>(bytes[2]) << 16U) |
        (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return true;
}
}

bool ThumbnailImage::IsValid() const noexcept
{
    if (Width == 0U || Height == 0U || Width > MaximumDimension ||
        Height > MaximumDimension)
        return false;
    const std::uint64_t expected =
        static_cast<std::uint64_t>(Width) * Height * 4U;
    return expected == Pixels.size();
}

bool WriteThumbnailImage(
    const std::filesystem::path& path,
    const ThumbnailImage& image,
    std::string& errorMessage)
{
    errorMessage.clear();
    if (!image.IsValid())
    {
        errorMessage = "Thumbnail image is invalid.";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        errorMessage = "Unable to create thumbnail file.";
        return false;
    }
    output.write(Magic.data(), static_cast<std::streamsize>(Magic.size()));
    WriteU32(output, FormatVersion);
    WriteU32(output, image.Width);
    WriteU32(output, image.Height);
    WriteU32(output, static_cast<std::uint32_t>(image.Pixels.size()));
    output.write(reinterpret_cast<const char*>(image.Pixels.data()),
        static_cast<std::streamsize>(image.Pixels.size()));
    if (!output)
    {
        errorMessage = "Unable to write complete thumbnail file.";
        return false;
    }
    return true;
}

bool ReadThumbnailImage(
    const std::filesystem::path& path,
    ThumbnailImage& image,
    std::string& errorMessage)
{
    image = {};
    errorMessage.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        errorMessage = "Thumbnail file does not exist or is inaccessible.";
        return false;
    }
    std::array<char, 4> magic{};
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    std::uint32_t version = 0U;
    std::uint32_t pixelBytes = 0U;
    if (!input || magic != Magic || !ReadU32(input, version) ||
        !ReadU32(input, image.Width) || !ReadU32(input, image.Height) ||
        !ReadU32(input, pixelBytes))
    {
        errorMessage = "Thumbnail header is invalid or truncated.";
        image = {};
        return false;
    }
    if (version != FormatVersion || image.Width == 0U || image.Height == 0U ||
        image.Width > MaximumDimension || image.Height > MaximumDimension)
    {
        errorMessage = "Thumbnail format or dimensions are unsupported.";
        image = {};
        return false;
    }
    const std::uint64_t expected =
        static_cast<std::uint64_t>(image.Width) * image.Height * 4U;
    if (expected > std::numeric_limits<std::uint32_t>::max() ||
        pixelBytes != expected)
    {
        errorMessage = "Thumbnail pixel size is inconsistent.";
        image = {};
        return false;
    }
    image.Pixels.resize(pixelBytes);
    input.read(reinterpret_cast<char*>(image.Pixels.data()),
        static_cast<std::streamsize>(image.Pixels.size()));
    if (!input || input.peek() != std::char_traits<char>::eof())
    {
        errorMessage = "Thumbnail pixel data is truncated or has trailing data.";
        image = {};
        return false;
    }
    return true;
}

} // namespace VoxelForge::Editor
