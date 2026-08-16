#include "VoxelForge/Voxel/VoxelModelSerializer.h"

#include "VoxelForge/Voxel/VoxelFileFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace VoxelForge::Voxel
{
namespace
{

class BinaryWriter final
{
public:
    explicit BinaryWriter(const std::size_t byteCount)
    {
        bytes_.reserve(byteCount);
    }

    void WriteU8(const std::uint8_t value)
    {
        bytes_.push_back(value);
    }

    void WriteU16(const std::uint16_t value)
    {
        WriteU8(static_cast<std::uint8_t>(value));
        WriteU8(static_cast<std::uint8_t>(value >> 8U));
    }

    void WriteU32(const std::uint32_t value)
    {
        WriteU16(static_cast<std::uint16_t>(value));
        WriteU16(static_cast<std::uint16_t>(value >> 16U));
    }

    void WriteU64(const std::uint64_t value)
    {
        WriteU32(static_cast<std::uint32_t>(value));
        WriteU32(static_cast<std::uint32_t>(value >> 32U));
    }

    void WriteBytes(const std::string& value)
    {
        for (const unsigned char byte : value)
        {
            WriteU8(byte);
        }
    }

    [[nodiscard]] const std::vector<std::uint8_t>& Bytes() const noexcept
    {
        return bytes_;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

class BinaryReader final
{
public:
    explicit BinaryReader(const std::vector<std::uint8_t>& bytes) noexcept
        : bytes_(bytes)
    {
    }

    [[nodiscard]] bool ReadU8(std::uint8_t& value) noexcept
    {
        if (position_ >= bytes_.size())
        {
            return false;
        }
        value = bytes_[position_++];
        return true;
    }

    [[nodiscard]] bool ReadU16(std::uint16_t& value) noexcept
    {
        std::uint8_t low = 0;
        std::uint8_t high = 0;
        if (!ReadU8(low) || !ReadU8(high))
        {
            return false;
        }
        value = static_cast<std::uint16_t>(low) |
                (static_cast<std::uint16_t>(high) << 8U);
        return true;
    }

    [[nodiscard]] bool ReadU32(std::uint32_t& value) noexcept
    {
        std::uint16_t low = 0;
        std::uint16_t high = 0;
        if (!ReadU16(low) || !ReadU16(high))
        {
            return false;
        }
        value = static_cast<std::uint32_t>(low) |
                (static_cast<std::uint32_t>(high) << 16U);
        return true;
    }

    [[nodiscard]] bool ReadU64(std::uint64_t& value) noexcept
    {
        std::uint32_t low = 0;
        std::uint32_t high = 0;
        if (!ReadU32(low) || !ReadU32(high))
        {
            return false;
        }
        value = static_cast<std::uint64_t>(low) |
                (static_cast<std::uint64_t>(high) << 32U);
        return true;
    }

    [[nodiscard]] bool ReadString(
        const std::uint32_t byteCount,
        std::string& value)
    {
        if (byteCount > bytes_.size() - position_)
        {
            return false;
        }
        value.clear();
        value.reserve(byteCount);
        for (std::uint32_t index = 0; index < byteCount; ++index)
        {
            value.push_back(static_cast<char>(bytes_[position_++]));
        }
        return true;
    }

    [[nodiscard]] bool AtEnd() const noexcept
    {
        return position_ == bytes_.size();
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t position_ = 0;
};

[[nodiscard]] bool IsValidUtf8(const std::string& text) noexcept
{
    std::size_t position = 0;
    while (position < text.size())
    {
        const auto first = static_cast<std::uint8_t>(text[position]);
        std::uint32_t codePoint = 0;
        std::size_t continuationCount = 0;
        std::uint32_t minimumCodePoint = 0;

        if (first <= 0x7FU)
        {
            ++position;
            continue;
        }
        if ((first & 0xE0U) == 0xC0U)
        {
            codePoint = first & 0x1FU;
            continuationCount = 1;
            minimumCodePoint = 0x80U;
        }
        else if ((first & 0xF0U) == 0xE0U)
        {
            codePoint = first & 0x0FU;
            continuationCount = 2;
            minimumCodePoint = 0x800U;
        }
        else if ((first & 0xF8U) == 0xF0U)
        {
            codePoint = first & 0x07U;
            continuationCount = 3;
            minimumCodePoint = 0x10000U;
        }
        else
        {
            return false;
        }

        if (continuationCount > text.size() - position - 1U)
        {
            return false;
        }
        for (std::size_t index = 0; index < continuationCount; ++index)
        {
            const auto continuation =
                static_cast<std::uint8_t>(text[position + index + 1U]);
            if ((continuation & 0xC0U) != 0x80U)
            {
                return false;
            }
            codePoint = (codePoint << 6U) | (continuation & 0x3FU);
        }
        if (codePoint < minimumCodePoint || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
        {
            return false;
        }
        position += continuationCount + 1U;
    }
    return true;
}

[[nodiscard]] bool CheckedAdd(
    std::uint64_t& value,
    const std::uint64_t addition) noexcept
{
    if (addition > std::numeric_limits<std::uint64_t>::max() - value)
    {
        return false;
    }
    value += addition;
    return true;
}

[[nodiscard]] VoxelSerializationResult FailSave(std::string message)
{
    return {false, std::move(message)};
}

[[nodiscard]] VoxelDeserializationResult FailLoad(std::string message)
{
    return {std::nullopt, std::move(message)};
}

[[nodiscard]] VoxelSerializationResult BuildFileBytes(
    const VoxelModel& model,
    std::vector<std::uint8_t>& output)
{
    using namespace VoxelFileFormat;

    if (model.Name().size() > MaximumNameByteCount)
    {
        return FailSave("Voxel model name exceeds the format limit.");
    }
    if (!IsValidUtf8(model.Name()))
    {
        return FailSave("Voxel model name is not valid UTF-8.");
    }
    if (model.GridCount() > MaximumGridCount)
    {
        return FailSave("Voxel model contains too many grids.");
    }

    std::uint64_t totalVoxelCount = 0;
    std::uint64_t fileByteCount =
        Signature.size() + 2U + 2U + 4U + 8U +
        4U + 4U + model.Name().size() +
        4U + 4U + (PaletteColorCount * 4ULL) +
        4U + 4U;

    for (const VoxelGrid& grid : model.Grids())
    {
        const bool emptyDimensions =
            grid.Width() == 0U && grid.Height() == 0U && grid.Depth() == 0U;
        const bool completeDimensions =
            grid.Width() > 0U && grid.Height() > 0U && grid.Depth() > 0U;
        if (!emptyDimensions && !completeDimensions)
        {
            return FailSave("Voxel grid dimensions are inconsistent.");
        }
        if (grid.Width() > MaximumGridDimension ||
            grid.Height() > MaximumGridDimension ||
            grid.Depth() > MaximumGridDimension)
        {
            return FailSave("Voxel grid dimensions exceed the format limit.");
        }

        const std::uint64_t expectedVoxelCount =
            static_cast<std::uint64_t>(grid.Width()) * grid.Height() * grid.Depth();
        if (expectedVoxelCount != grid.VoxelCount() ||
            expectedVoxelCount > VoxelGrid::MaximumVoxelCount)
        {
            return FailSave("Voxel grid payload does not match its dimensions.");
        }
        if (!CheckedAdd(totalVoxelCount, expectedVoxelCount) ||
            totalVoxelCount > MaximumTotalVoxelCount)
        {
            return FailSave("Voxel model contains too many voxels.");
        }
        if (!CheckedAdd(fileByteCount, 4U + 4U + 4U + 4U + 8U + 8U) ||
            !CheckedAdd(fileByteCount, expectedVoxelCount * 2ULL))
        {
            return FailSave("Voxel file size overflow.");
        }
    }

    if (fileByteCount > MaximumFileByteCount ||
        fileByteCount > std::numeric_limits<std::size_t>::max())
    {
        return FailSave("Voxel file exceeds the format size limit.");
    }

    BinaryWriter writer(static_cast<std::size_t>(fileByteCount));
    for (const std::uint8_t byte : Signature)
    {
        writer.WriteU8(byte);
    }
    writer.WriteU16(MajorVersion);
    writer.WriteU16(MinorVersion);
    writer.WriteU32(FormatFlags);
    writer.WriteU64(fileByteCount);

    writer.WriteU32(ModelSection);
    writer.WriteU32(static_cast<std::uint32_t>(model.Name().size()));
    writer.WriteBytes(model.Name());

    writer.WriteU32(PaletteSection);
    writer.WriteU32(PaletteColorCount);
    for (const VoxelColor& color : model.Palette().Data())
    {
        writer.WriteU8(color.Red);
        writer.WriteU8(color.Green);
        writer.WriteU8(color.Blue);
        writer.WriteU8(color.Alpha);
    }

    writer.WriteU32(GridsSection);
    writer.WriteU32(static_cast<std::uint32_t>(model.GridCount()));
    for (const VoxelGrid& grid : model.Grids())
    {
        writer.WriteU32(GridSection);
        writer.WriteU32(grid.Width());
        writer.WriteU32(grid.Height());
        writer.WriteU32(grid.Depth());
        writer.WriteU64(grid.VoxelCount());
        writer.WriteU64(static_cast<std::uint64_t>(grid.VoxelCount()) * 2ULL);
        for (const Voxel voxel : grid.Data())
        {
            writer.WriteU8(voxel.ColorIndex);
            writer.WriteU8(voxel.Flags);
        }
    }

    if (writer.Bytes().size() != fileByteCount)
    {
        return FailSave("Internal voxel file size mismatch.");
    }
    output = writer.Bytes();
    return {true, {}};
}

[[nodiscard]] VoxelDeserializationResult ParseFileBytes(
    const std::vector<std::uint8_t>& bytes)
{
    using namespace VoxelFileFormat;

    BinaryReader reader(bytes);
    for (const std::uint8_t expected : Signature)
    {
        std::uint8_t actual = 0;
        if (!reader.ReadU8(actual) || actual != expected)
        {
            return FailLoad("Invalid VFVOXEL signature.");
        }
    }

    std::uint16_t majorVersion = 0;
    std::uint16_t minorVersion = 0;
    std::uint32_t formatFlags = 0;
    std::uint64_t declaredFileByteCount = 0;
    if (!reader.ReadU16(majorVersion) || !reader.ReadU16(minorVersion) ||
        !reader.ReadU32(formatFlags) || !reader.ReadU64(declaredFileByteCount))
    {
        return FailLoad("Truncated VFVOXEL header.");
    }
    if (majorVersion != MajorVersion || minorVersion != MinorVersion)
    {
        return FailLoad("Unsupported VFVOXEL version.");
    }
    if (formatFlags != FormatFlags)
    {
        return FailLoad("Unsupported VFVOXEL format flags.");
    }
    if (declaredFileByteCount != bytes.size())
    {
        return FailLoad("VFVOXEL file size does not match its header.");
    }

    std::uint32_t section = 0;
    std::uint32_t nameByteCount = 0;
    std::string name;
    if (!reader.ReadU32(section) || section != ModelSection ||
        !reader.ReadU32(nameByteCount) || nameByteCount > MaximumNameByteCount ||
        !reader.ReadString(nameByteCount, name))
    {
        return FailLoad("Invalid VFVOXEL model section.");
    }
    if (!IsValidUtf8(name))
    {
        return FailLoad("VFVOXEL model name is not valid UTF-8.");
    }

    std::uint32_t paletteColorCount = 0;
    if (!reader.ReadU32(section) || section != PaletteSection ||
        !reader.ReadU32(paletteColorCount) ||
        paletteColorCount != PaletteColorCount)
    {
        return FailLoad("Invalid VFVOXEL palette section.");
    }

    VoxelModel model;
    model.SetName(name);
    for (std::uint32_t index = 0; index < PaletteColorCount; ++index)
    {
        VoxelColor color;
        if (!reader.ReadU8(color.Red) || !reader.ReadU8(color.Green) ||
            !reader.ReadU8(color.Blue) || !reader.ReadU8(color.Alpha))
        {
            return FailLoad("Truncated VFVOXEL palette payload.");
        }
        if (!model.Palette().Set(index, color))
        {
            return FailLoad("Invalid VFVOXEL palette index.");
        }
    }

    std::uint32_t gridCount = 0;
    if (!reader.ReadU32(section) || section != GridsSection ||
        !reader.ReadU32(gridCount) || gridCount > MaximumGridCount)
    {
        return FailLoad("Invalid VFVOXEL grids section.");
    }

    std::uint64_t totalVoxelCount = 0;
    for (std::uint32_t gridIndex = 0; gridIndex < gridCount; ++gridIndex)
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t depth = 0;
        std::uint64_t voxelCount = 0;
        std::uint64_t payloadByteCount = 0;
        if (!reader.ReadU32(section) || section != GridSection ||
            !reader.ReadU32(width) || !reader.ReadU32(height) ||
            !reader.ReadU32(depth) || !reader.ReadU64(voxelCount) ||
            !reader.ReadU64(payloadByteCount))
        {
            return FailLoad("Truncated VFVOXEL grid header.");
        }

        const bool emptyDimensions = width == 0U && height == 0U && depth == 0U;
        const bool completeDimensions = width > 0U && height > 0U && depth > 0U;
        if ((!emptyDimensions && !completeDimensions) ||
            width > MaximumGridDimension || height > MaximumGridDimension ||
            depth > MaximumGridDimension)
        {
            return FailLoad("Invalid VFVOXEL grid dimensions.");
        }
        const std::uint64_t expectedVoxelCount =
            static_cast<std::uint64_t>(width) * height * depth;
        if (voxelCount != expectedVoxelCount ||
            voxelCount > VoxelGrid::MaximumVoxelCount ||
            payloadByteCount != voxelCount * 2ULL ||
            !CheckedAdd(totalVoxelCount, voxelCount) ||
            totalVoxelCount > MaximumTotalVoxelCount)
        {
            return FailLoad("Invalid VFVOXEL grid payload size.");
        }

        VoxelGrid grid;
        if (!grid.Resize(width, height, depth))
        {
            return FailLoad("Unable to allocate VFVOXEL grid.");
        }
        for (std::uint32_t z = 0; z < depth; ++z)
        {
            for (std::uint32_t y = 0; y < height; ++y)
            {
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    Voxel voxel;
                    if (!reader.ReadU8(voxel.ColorIndex) ||
                        !reader.ReadU8(voxel.Flags) ||
                        !grid.Set(x, y, z, voxel))
                    {
                        return FailLoad("Truncated VFVOXEL grid payload.");
                    }
                }
            }
        }
        model.AddGrid(std::move(grid));
    }

    if (!reader.AtEnd())
    {
        return FailLoad("Unexpected trailing data in VFVOXEL file.");
    }
    return {std::move(model), {}};
}

[[nodiscard]] std::filesystem::path AuxiliaryPath(
    const std::filesystem::path& destination,
    const char* suffix)
{
    std::filesystem::path result = destination;
    result += suffix;
    return result;
}

} // namespace

VoxelModelSerializer::FileOperations
VoxelModelSerializer::DefaultFileOperations()
{
    return {
        [](const std::filesystem::path& path, std::error_code& error)
        {
            return std::filesystem::exists(path, error);
        },
        [](const std::filesystem::path& from,
           const std::filesystem::path& to, std::error_code& error)
        {
            std::filesystem::rename(from, to, error);
        },
        [](const std::filesystem::path& path, std::error_code& error)
        {
            return std::filesystem::remove(path, error);
        }};
}

VoxelSerializationResult VoxelModelSerializer::Save(
    const std::filesystem::path& destination,
    const VoxelModel& model)
{
    return Save(destination, model, DefaultFileOperations());
}

VoxelSerializationResult VoxelModelSerializer::Save(
    const std::filesystem::path& destination,
    const VoxelModel& model,
    const FileOperations& operations)
{
    if (destination.empty())
    {
        return FailSave("Voxel file destination is empty.");
    }

    std::vector<std::uint8_t> bytes;
    VoxelSerializationResult buildResult;
    try
    {
        buildResult = BuildFileBytes(model, bytes);
    }
    catch (const std::bad_alloc&)
    {
        return FailSave("Not enough memory to serialize voxel model.");
    }
    if (!buildResult)
    {
        return buildResult;
    }

    const std::filesystem::path temporary = AuxiliaryPath(destination, ".tmp");
    const std::filesystem::path backup = AuxiliaryPath(destination, ".bak");
    std::error_code error;
    if (operations.Exists(temporary, error) || error)
    {
        return FailSave("Stale VFVOXEL temporary or backup file exists.");
    }
    const bool destinationExists = operations.Exists(destination, error);
    if (error)
    {
        return FailSave("Unable to inspect VFVOXEL destination.");
    }
    if (destinationExists &&
        (!std::filesystem::is_regular_file(destination, error) || error))
    {
        return FailSave("VFVOXEL destination is not a regular file.");
    }

    // VF-STAB-01B blocage 3 (revue Codex) : un backup n'est redondant que si la
    // destination est REELLEMENT RECHARGEABLE, pas seulement presente.
    //
    // Le controle precedent ne verifiait qu'un fichier regulier. Une
    // destination tronquee ou corrompue suffisait donc a faire supprimer un
    // `.bak` valide, c'est-a-dire la seule copie recuperable. On recharge
    // desormais la destination avec le lecteur officiel — aucun second
    // parseur — avant tout effacement, et on conserve le backup au moindre
    // doute. Un backup n'est jamais supprime sans preuve qu'il est superflu.
    if (operations.Exists(backup, error) || error)
    {
        if (error)
        {
            return FailSave("Stale VFVOXEL temporary or backup file exists.");
        }
        // Un backup sans son modele est la seule copie survivante : refus, et
        // surtout aucune suppression.
        if (!destinationExists)
        {
            return FailSave(
                "A VFVOXEL backup exists without its model file. Restore the "
                ".bak file manually before saving again.");
        }
        if (!Load(destination))
        {
            return FailSave(
                "A VFVOXEL backup exists and the model file cannot be "
                "reloaded. The backup was kept because it may be the only "
                "recoverable copy; check both files before saving again.");
        }
        // La destination est rechargeable : ce backup est prouve redondant.
        operations.Remove(backup, error);
        if (error)
        {
            return FailSave(
                "A redundant VFVOXEL backup could not be removed. Delete the "
                ".bak file manually before saving again.");
        }
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return FailSave("Unable to create VFVOXEL temporary file.");
        }
        for (const std::uint8_t byte : bytes)
        {
            output.put(static_cast<char>(byte));
        }
        output.flush();
        if (!output)
        {
            output.close();
            operations.Remove(temporary, error);
            return FailSave("Unable to write VFVOXEL temporary file.");
        }
    }

    const VoxelDeserializationResult validation = Load(temporary);
    if (!validation)
    {
        operations.Remove(temporary, error);
        return FailSave("VFVOXEL temporary file validation failed: " +
                        validation.Message);
    }

    const bool hadDestination = operations.Exists(destination, error);
    if (error)
    {
        operations.Remove(temporary, error);
        return FailSave("Unable to inspect VFVOXEL destination before replacement.");
    }
    if (hadDestination)
    {
        operations.Rename(destination, backup, error);
        if (error)
        {
            operations.Remove(temporary, error);
            return FailSave("Unable to create VFVOXEL backup.");
        }
    }

    operations.Rename(temporary, destination, error);
    if (error)
    {
        const std::error_code replacementError = error;
        if (hadDestination)
        {
            std::error_code rollbackError;
            operations.Rename(backup, destination, rollbackError);
            if (rollbackError)
            {
                return FailSave(
                    "VFVOXEL replacement and backup rollback both failed.");
            }
        }
        operations.Remove(temporary, error);
        return FailSave("Unable to replace VFVOXEL destination: " +
                        replacementError.message());
    }

    // VF-STAB-01B blocage 3 : la destination publiee doit etre prouvee
    // RECHARGEABLE avant que le backup ne soit seulement envisage pour
    // suppression. Sans cette preuve, on ne peut ni affirmer que la sauvegarde
    // est saine, ni justifier d'effacer la copie precedente.
    if (!Load(destination))
    {
        if (hadDestination)
        {
            return FailSave(
                "The VFVOXEL file was replaced but cannot be reloaded. Its "
                "backup was kept as the recoverable copy; restore the .bak "
                "file.");
        }
        return FailSave(
            "The VFVOXEL file was written but cannot be reloaded.");
    }

    if (hadDestination)
    {
        operations.Remove(backup, error);
        if (error)
        {
            // VF-STAB-01 bug 8 : le rename atomique a reussi ET la destination
            // vient d'etre rechargee avec succes — le modele EST donc bien
            // enregistre. Un echec de suppression du backup desormais superflu
            // (verrou transitoire d'antivirus ou d'indexeur) etait signale
            // comme un echec de sauvegarde : un faux negatif qui annoncait a
            // l'artiste un travail perdu qui ne l'etait pas.
            //
            // VF-STAB-01B blocage 3 : le message ne PROMET plus le nettoyage.
            // Le code ne garantit qu'une nouvelle TENTATIVE : si le verrou
            // persiste, la sauvegarde suivante refusera et demandera une
            // suppression manuelle. Le texte dit maintenant exactement ce que
            // le programme fait.
            return {true,
                "Voxel model saved and verified. Its backup file could not be "
                "removed and was kept; cleanup will be retried on the next "
                "save, and may need to be done by hand."};
        }
    }
    return {true, {}};
}

VoxelDeserializationResult VoxelModelSerializer::Load(
    const std::filesystem::path& source)
{
    using namespace VoxelFileFormat;

    if (source.empty())
    {
        return FailLoad("Voxel file source is empty.");
    }

    std::error_code error;
    const std::uintmax_t fileByteCount = std::filesystem::file_size(source, error);
    if (error)
    {
        return FailLoad("Unable to inspect VFVOXEL file: " + error.message());
    }
    if (fileByteCount > MaximumFileByteCount ||
        fileByteCount > std::numeric_limits<std::size_t>::max())
    {
        return FailLoad("VFVOXEL file exceeds the format size limit.");
    }

    std::vector<std::uint8_t> bytes;
    try
    {
        bytes.resize(static_cast<std::size_t>(fileByteCount));
    }
    catch (const std::bad_alloc&)
    {
        return FailLoad("Not enough memory to load VFVOXEL file.");
    }

    std::ifstream input(source, std::ios::binary);
    if (!input)
    {
        return FailLoad("Unable to open VFVOXEL file.");
    }
    for (std::uint8_t& byte : bytes)
    {
        char value = 0;
        if (!input.get(value))
        {
            return FailLoad("Unable to read complete VFVOXEL file.");
        }
        byte = static_cast<std::uint8_t>(static_cast<unsigned char>(value));
    }
    char trailing = 0;
    if (input.get(trailing))
    {
        return FailLoad("VFVOXEL file changed while it was being read.");
    }

    try
    {
        return ParseFileBytes(bytes);
    }
    catch (const std::bad_alloc&)
    {
        return FailLoad("Not enough memory to deserialize VFVOXEL file.");
    }
}

} // namespace VoxelForge::Voxel
