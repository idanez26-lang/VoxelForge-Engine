#include "VoxelStamps/Library/StampJsonCatalogStore.h"

#include "VoxelStamps/Library/StampLibraryPaths.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <new>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace VoxelForge::Editor::Stamps
{
namespace
{

constexpr std::size_t MaximumJsonDepth = 32U;
constexpr std::size_t MaximumJsonNodes = StampCatalogMaximumEntries * 12U + 16U;
constexpr std::size_t MaximumJsonStringBytes = 4096U;
constexpr std::size_t MaximumJsonObjectFields = 32U;

struct Json final
{
    enum class Kind { Null, Bool, Number, String, Array, Object };

    Kind Type = Kind::Null;
    std::string Text;
    std::vector<Json> Array;
    std::map<std::string, Json, std::less<>> Object;
};

[[nodiscard]] StampCatalogResult Failure(
    const StampCatalogError error,
    std::string message = {})
{
    return {.Error = error,
            .Message = message.empty() ? std::string(StampCatalogErrorMessage(error))
                                       : std::move(message)};
}

[[nodiscard]] bool IsValidUtf8(const std::string_view text) noexcept
{
    for (std::size_t index = 0U; index < text.size();)
    {
        const unsigned char first = static_cast<unsigned char>(text[index++]);
        if (first < 0x80U) continue;
        unsigned continuationCount = 0U;
        std::uint32_t codePoint = 0U;
        std::uint32_t minimum = 0U;
        if ((first & 0xE0U) == 0xC0U)
        {
            continuationCount = 1U; codePoint = first & 0x1FU; minimum = 0x80U;
        }
        else if ((first & 0xF0U) == 0xE0U)
        {
            continuationCount = 2U; codePoint = first & 0x0FU; minimum = 0x800U;
        }
        else if ((first & 0xF8U) == 0xF0U)
        {
            continuationCount = 3U; codePoint = first & 0x07U; minimum = 0x10000U;
        }
        else return false;
        if (index + continuationCount > text.size()) return false;
        for (unsigned offset = 0U; offset < continuationCount; ++offset)
        {
            const unsigned char next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xC0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        if (codePoint < minimum || codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) return false;
    }
    return true;
}

class JsonParser final
{
public:
    explicit JsonParser(const std::string_view text) : text_(text) {}

    [[nodiscard]] bool Parse(Json& value)
    {
        SkipWhitespace();
        if (!ParseValue(value, 0U)) return false;
        SkipWhitespace();
        return cursor_ == text_.size();
    }

private:
    void SkipWhitespace() noexcept
    {
        while (cursor_ < text_.size() &&
               (text_[cursor_] == ' ' || text_[cursor_] == '\t' ||
                text_[cursor_] == '\r' || text_[cursor_] == '\n')) ++cursor_;
    }

    [[nodiscard]] bool Consume(const char expected)
    {
        SkipWhitespace();
        if (cursor_ >= text_.size() || text_[cursor_] != expected) return false;
        ++cursor_;
        return true;
    }

    [[nodiscard]] bool ConsumeLiteral(const std::string_view literal)
    {
        if (text_.substr(cursor_, literal.size()) != literal) return false;
        cursor_ += literal.size();
        return true;
    }

    [[nodiscard]] bool ParseHex16(std::uint16_t& value)
    {
        if (cursor_ + 4U > text_.size()) return false;
        value = 0U;
        for (unsigned index = 0U; index < 4U; ++index)
        {
            const unsigned char character = static_cast<unsigned char>(text_[cursor_++]);
            std::uint16_t digit = 0U;
            if (character >= '0' && character <= '9') digit = character - '0';
            else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10U;
            else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10U;
            else return false;
            value = static_cast<std::uint16_t>((value << 4U) | digit);
        }
        return true;
    }

    [[nodiscard]] static bool AppendUtf8(std::string& output, const std::uint32_t codePoint)
    {
        if (codePoint > 0x10FFFFU || (codePoint >= 0xD800U && codePoint <= 0xDFFFU)) return false;
        if (codePoint <= 0x7FU) output.push_back(static_cast<char>(codePoint));
        else if (codePoint <= 0x7FFU)
        {
            output.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        else if (codePoint <= 0xFFFFU)
        {
            output.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        return output.size() <= MaximumJsonStringBytes;
    }

    [[nodiscard]] bool ParseString(std::string& output)
    {
        if (!Consume('"')) return false;
        output.clear();
        while (cursor_ < text_.size())
        {
            const unsigned char character = static_cast<unsigned char>(text_[cursor_++]);
            if (character == '"') return output.size() <= MaximumJsonStringBytes && IsValidUtf8(output);
            if (character < 0x20U) return false;
            if (character != '\\')
            {
                output.push_back(static_cast<char>(character));
                if (output.size() > MaximumJsonStringBytes) return false;
                continue;
            }
            if (cursor_ == text_.size()) return false;
            switch (text_[cursor_++])
            {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u':
            {
                std::uint16_t first = 0U;
                if (!ParseHex16(first)) return false;
                std::uint32_t codePoint = first;
                if (first >= 0xD800U && first <= 0xDBFFU)
                {
                    if (cursor_ + 2U > text_.size() || text_[cursor_++] != '\\' || text_[cursor_++] != 'u')
                        return false;
                    std::uint16_t second = 0U;
                    if (!ParseHex16(second) || second < 0xDC00U || second > 0xDFFFU) return false;
                    codePoint = 0x10000U + ((static_cast<std::uint32_t>(first) - 0xD800U) << 10U) +
                        (static_cast<std::uint32_t>(second) - 0xDC00U);
                }
                else if (first >= 0xDC00U && first <= 0xDFFFU) return false;
                if (!AppendUtf8(output, codePoint)) return false;
                break;
            }
            default: return false;
            }
            if (output.size() > MaximumJsonStringBytes) return false;
        }
        return false;
    }

    [[nodiscard]] bool ParseNumber(Json& value)
    {
        const std::size_t first = cursor_;
        if (cursor_ < text_.size() && text_[cursor_] == '-') ++cursor_;
        if (cursor_ == text_.size()) return false;
        if (text_[cursor_] == '0')
        {
            ++cursor_;
            if (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[cursor_]))) return false;
        }
        else if (text_[cursor_] >= '1' && text_[cursor_] <= '9')
        {
            do { ++cursor_; } while (cursor_ < text_.size() &&
                std::isdigit(static_cast<unsigned char>(text_[cursor_])));
        }
        else return false;
        if (cursor_ < text_.size() && text_[cursor_] == '.')
        {
            ++cursor_;
            const std::size_t fraction = cursor_;
            while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[cursor_]))) ++cursor_;
            if (fraction == cursor_) return false;
        }
        if (cursor_ < text_.size() && (text_[cursor_] == 'e' || text_[cursor_] == 'E'))
        {
            ++cursor_;
            if (cursor_ < text_.size() && (text_[cursor_] == '+' || text_[cursor_] == '-')) ++cursor_;
            const std::size_t exponent = cursor_;
            while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[cursor_]))) ++cursor_;
            if (exponent == cursor_) return false;
        }
        value = {};
        value.Type = Json::Kind::Number;
        value.Text.assign(text_.substr(first, cursor_ - first));
        return true;
    }

    [[nodiscard]] bool ParseArray(Json& value, const std::size_t depth)
    {
        if (!Consume('[')) return false;
        value = {}; value.Type = Json::Kind::Array;
        SkipWhitespace();
        if (cursor_ < text_.size() && text_[cursor_] == ']') { ++cursor_; return true; }
        while (true)
        {
            if (value.Array.size() == StampCatalogMaximumEntries) return false;
            Json item;
            if (!ParseValue(item, depth + 1U)) return false;
            value.Array.push_back(std::move(item));
            if (Consume(']')) return true;
            if (!Consume(',')) return false;
        }
    }

    [[nodiscard]] bool ParseObject(Json& value, const std::size_t depth)
    {
        if (!Consume('{')) return false;
        value = {}; value.Type = Json::Kind::Object;
        SkipWhitespace();
        if (cursor_ < text_.size() && text_[cursor_] == '}') { ++cursor_; return true; }
        while (true)
        {
            if (value.Object.size() == MaximumJsonObjectFields) return false;
            std::string key;
            Json item;
            if (!ParseString(key) || !Consume(':') || !ParseValue(item, depth + 1U) ||
                !value.Object.emplace(std::move(key), std::move(item)).second) return false;
            if (Consume('}')) return true;
            if (!Consume(',')) return false;
        }
    }

    [[nodiscard]] bool ParseValue(Json& value, const std::size_t depth)
    {
        if (depth > MaximumJsonDepth || ++nodeCount_ > MaximumJsonNodes) return false;
        SkipWhitespace();
        if (cursor_ >= text_.size()) return false;
        switch (text_[cursor_])
        {
        case '"': value = {}; value.Type = Json::Kind::String; return ParseString(value.Text);
        case '{': return ParseObject(value, depth);
        case '[': return ParseArray(value, depth);
        case 't': if (!ConsumeLiteral("true")) return false; value = {}; value.Type = Json::Kind::Bool; return true;
        case 'f': if (!ConsumeLiteral("false")) return false; value = {}; value.Type = Json::Kind::Bool; return true;
        case 'n': if (!ConsumeLiteral("null")) return false; value = {}; return true;
        default: return ParseNumber(value);
        }
    }

    std::string_view text_;
    std::size_t cursor_ = 0U;
    std::size_t nodeCount_ = 0U;
};

[[nodiscard]] const Json* Field(const Json& object, const std::string_view key)
{
    const auto found = object.Object.find(key);
    return found == object.Object.end() ? nullptr : &found->second;
}

template <typename T>
[[nodiscard]] bool UnsignedField(const Json& value, T& result)
{
    static_assert(std::is_unsigned_v<T>);
    if (value.Type != Json::Kind::Number || value.Text.empty() || value.Text.front() == '-' ||
        value.Text.find_first_of(".eE") != std::string::npos) return false;
    const auto [end, error] = std::from_chars(
        value.Text.data(), value.Text.data() + value.Text.size(), result, 10);
    return error == std::errc{} && end == value.Text.data() + value.Text.size();
}

[[nodiscard]] bool ParseUuid(const std::string_view value, Core::UUID& result)
{
    if (value.size() != 16U) return false;
    std::uint64_t parsed = 0U;
    for (const unsigned char character : value)
    {
        std::uint64_t digit = 0U;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10U;
        else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10U;
        else return false;
        parsed = (parsed << 4U) | digit;
    }
    if (parsed == 0U) return false;
    result = Core::UUID{parsed};
    return true;
}

[[nodiscard]] bool ContainsControlCharacter(const std::string_view text) noexcept
{
    return std::any_of(text.begin(), text.end(), [](const unsigned char character) {
        return character < 0x20U || character == 0x7FU;
    });
}

[[nodiscard]] std::string LowerAscii(const std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const unsigned char character : text)
        result.push_back(static_cast<char>(std::tolower(character)));
    return result;
}

[[nodiscard]] std::string GenericUtf8Path(const std::filesystem::path& path)
{
    const std::u8string value = path.lexically_normal().generic_u8string();
    std::string result;
    result.reserve(value.size());
    for (const char8_t character : value) result.push_back(static_cast<char>(character));
    return result;
}

[[nodiscard]] std::string FilenameUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.filename().u8string();
    std::string result;
    result.reserve(value.size());
    for (const char8_t character : value) result.push_back(static_cast<char>(character));
    return result;
}

[[nodiscard]] std::filesystem::path PathFromUtf8(const std::string_view text)
{
    std::u8string utf8;
    utf8.reserve(text.size());
    for (const unsigned char character : text)
        utf8.push_back(static_cast<char8_t>(character));
    return std::filesystem::path(utf8);
}

[[nodiscard]] std::string PortablePathKey(const std::filesystem::path& path)
{
    const std::string normalized = GenericUtf8Path(path);
    std::string result;
    result.reserve(normalized.size());
    for (const unsigned char character : normalized)
    {
        result.push_back(character >= 'A' && character <= 'Z'
            ? static_cast<char>(character - 'A' + 'a')
            : static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] bool IsProjectCreationPath(const std::filesystem::path& path)
{
    const std::string generic = GenericUtf8Path(path);
    if (generic.find('\\') != std::string::npos ||
        ContainsControlCharacter(generic) || !IsPortableRelativePath(path) ||
        LowerAscii(GenericUtf8Path(path.extension())) != ".vfstamp") return false;
    const std::filesystem::path normalized = path.lexically_normal();
    const std::filesystem::path withinCreations = normalized.lexically_relative(ProjectCreationsRelativePath);
    return !withinCreations.empty() && !withinCreations.is_absolute() &&
        IsPortableRelativePath(withinCreations);
}

[[nodiscard]] bool EntryLess(const StampCatalogEntry& left, const StampCatalogEntry& right)
{
    if (left.Reference.Id.Value() != right.Reference.Id.Value())
        return left.Reference.Id.Value() < right.Reference.Id.Value();
    return GenericUtf8Path(left.Reference.RelativePath) < GenericUtf8Path(right.Reference.RelativePath);
}

[[nodiscard]] StampCatalogResult ValidateEntry(const StampCatalogEntry& entry)
{
    if (entry.Reference.Id.Value() == 0U || entry.Reference.ContentHash.empty() ||
        entry.Reference.ContentHash.size() > 128U || !IsValidUtf8(entry.Reference.ContentHash) ||
        ContainsControlCharacter(entry.Reference.ContentHash) ||
        !IsProjectCreationPath(entry.Reference.RelativePath))
        return Failure(StampCatalogError::Invalid, "Catalogue entry identity or portable Project Library path is invalid.");
    if (entry.FileName.empty() || entry.FileName.size() > 255U || !IsValidUtf8(entry.FileName) ||
        ContainsControlCharacter(entry.FileName) ||
        entry.FileName != FilenameUtf8(entry.Reference.RelativePath) ||
        entry.FileName.find_first_of("/\\") != std::string::npos)
        return Failure(StampCatalogError::Invalid, "Catalogue entry filename must match its portable path.");
    const StampResourceLimits& limits = DefaultStampResourceLimits();
    const bool canonicalHash = entry.Reference.ContentHash.size() == 16U &&
        std::all_of(entry.Reference.ContentHash.begin(), entry.Reference.ContentHash.end(),
            [](const unsigned char character) {
                return (character >= '0' && character <= '9') ||
                    (character >= 'a' && character <= 'f');
            });
    if (!canonicalHash || entry.FileBytes == 0U ||
        entry.FileBytes > limits.HardFileBytes ||
        entry.Dimensions.X == 0U || entry.Dimensions.Y == 0U || entry.Dimensions.Z == 0U ||
        entry.Dimensions.X > limits.HardAxisLength ||
        entry.Dimensions.Y > limits.HardAxisLength ||
        entry.Dimensions.Z > limits.HardAxisLength ||
        entry.VoxelCount == 0U || entry.VoxelCount > limits.HardVoxelCount ||
        entry.PaletteCount == 0U || entry.PaletteCount > 256U)
        return Failure(StampCatalogError::Invalid, "Catalogue entry dimensions, counts or source size is invalid.");
    return {};
}

[[nodiscard]] StampCatalogResult ValidateCatalogue(StampCatalog catalogue)
{
    if (catalogue.Version != StampCatalogVersion)
        return Failure(StampCatalogError::UnsupportedVersion);
    if (catalogue.Entries.size() > StampCatalogMaximumEntries)
        return Failure(StampCatalogError::Invalid, "Catalogue entry count exceeds the V1 bound.");
    std::map<std::uint64_t, std::string> ids;
    std::map<std::string, std::uint64_t> paths;
    for (StampCatalogEntry& entry : catalogue.Entries)
    {
        entry.Reference.RelativePath = entry.Reference.RelativePath.lexically_normal();
        StampCatalogResult entryValidation = ValidateEntry(entry);
        if (!entryValidation.Succeeded()) return entryValidation;
        if (!ids.emplace(entry.Reference.Id.Value(), entry.Reference.ContentHash + "\n" +
                PortablePathKey(entry.Reference.RelativePath)).second ||
            !paths.emplace(PortablePathKey(entry.Reference.RelativePath), entry.Reference.Id.Value()).second)
            return Failure(StampCatalogError::IdentityCollision);
    }
    std::sort(catalogue.Entries.begin(), catalogue.Entries.end(), EntryLess);
    return {.Catalog = std::move(catalogue)};
}

[[nodiscard]] bool AppendEscapedJsonString(std::string& output, const std::string_view value)
{
    if (!IsValidUtf8(value)) return false;
    constexpr std::string_view Hex{"0123456789ABCDEF"};
    output.push_back('"');
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U)
            {
                output += "\\u00";
                output.push_back(Hex[(character >> 4U) & 0x0FU]);
                output.push_back(Hex[character & 0x0FU]);
            }
            else output.push_back(static_cast<char>(character));
            break;
        }
        if (output.size() > StampCatalogMaximumBytes) return false;
    }
    output.push_back('"');
    return output.size() <= StampCatalogMaximumBytes;
}

[[nodiscard]] bool AppendUnsigned(std::string& output, const std::uint64_t value)
{
    char buffer[32]{};
    const auto [end, error] = std::to_chars(std::begin(buffer), std::end(buffer), value, 10);
    if (error != std::errc{}) return false;
    output.append(buffer, end);
    return output.size() <= StampCatalogMaximumBytes;
}

[[nodiscard]] StampCatalogResult CatalogueFromJson(const Json& root)
{
    if (root.Type != Json::Kind::Object) return Failure(StampCatalogError::Invalid);
    const Json* version = Field(root, "version");
    const Json* entries = Field(root, "entries");
    std::uint32_t parsedVersion = 0U;
    if (version == nullptr || entries == nullptr || !UnsignedField(*version, parsedVersion))
        return Failure(StampCatalogError::Invalid, "Catalogue requires an unsigned version and entries array.");
    if (parsedVersion != StampCatalogVersion) return Failure(StampCatalogError::UnsupportedVersion);
    if (entries->Type != Json::Kind::Array || entries->Array.size() > StampCatalogMaximumEntries)
        return Failure(StampCatalogError::Invalid, "Catalogue entries array exceeds the V1 bound.");

    StampCatalog catalogue;
    catalogue.Version = parsedVersion;
    catalogue.Entries.reserve(entries->Array.size());
    for (const Json& item : entries->Array)
    {
        if (item.Type != Json::Kind::Object) return Failure(StampCatalogError::Invalid, "Catalogue entry must be an object.");
        const Json* uuid = Field(item, "uuid");
        const Json* hash = Field(item, "contentHash");
        const Json* path = Field(item, "relativePath");
        const Json* name = Field(item, "fileName");
        const Json* bytes = Field(item, "fileBytes");
        const Json* dimensions = Field(item, "dimensions");
        const Json* voxels = Field(item, "voxelCount");
        const Json* palette = Field(item, "paletteCount");
        if (uuid == nullptr || hash == nullptr || path == nullptr || name == nullptr || bytes == nullptr ||
            dimensions == nullptr || voxels == nullptr || palette == nullptr ||
            uuid->Type != Json::Kind::String || hash->Type != Json::Kind::String ||
            path->Type != Json::Kind::String || name->Type != Json::Kind::String ||
            dimensions->Type != Json::Kind::Array || dimensions->Array.size() != 3U)
            return Failure(StampCatalogError::Invalid, "Catalogue entry is missing a required V1 field.");

        StampCatalogEntry entry;
        if (!ParseUuid(uuid->Text, entry.Reference.Id) || !UnsignedField(*bytes, entry.FileBytes) ||
            !UnsignedField(*voxels, entry.VoxelCount) || !UnsignedField(*palette, entry.PaletteCount) ||
            !UnsignedField(dimensions->Array[0], entry.Dimensions.X) ||
            !UnsignedField(dimensions->Array[1], entry.Dimensions.Y) ||
            !UnsignedField(dimensions->Array[2], entry.Dimensions.Z))
            return Failure(StampCatalogError::Invalid, "Catalogue entry contains an invalid numeric field.");
        entry.Reference.ContentHash = hash->Text;
        entry.Reference.RelativePath = PathFromUtf8(path->Text);
        entry.FileName = name->Text;
        catalogue.Entries.push_back(std::move(entry));
    }
    return ValidateCatalogue(std::move(catalogue));
}

[[nodiscard]] bool IsSafeRegularFile(const std::filesystem::path& path, std::error_code& error)
{
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::exists(status) && std::filesystem::is_regular_file(status) &&
        !std::filesystem::is_symlink(status);
}

[[nodiscard]] bool InspectPath(
    const std::filesystem::path& path,
    bool& exists,
    bool& symbolicLink,
    std::error_code& error) noexcept
{
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear(); exists = false; symbolicLink = false; return true;
    }
    if (error) return false;
    exists = std::filesystem::exists(status) || std::filesystem::is_symlink(status);
    symbolicLink = std::filesystem::is_symlink(status);
    return true;
}

[[nodiscard]] std::filesystem::path TemporaryPath(const std::filesystem::path& path)
{
    std::filesystem::path result = path;
    const std::filesystem::path::string_type suffix{
        static_cast<std::filesystem::path::value_type>('.'),
        static_cast<std::filesystem::path::value_type>('t'),
        static_cast<std::filesystem::path::value_type>('m'),
        static_cast<std::filesystem::path::value_type>('p')};
    result += suffix;
    return result;
}

[[nodiscard]] std::filesystem::path BackupPath(const std::filesystem::path& path)
{
    std::filesystem::path result = path;
    const std::filesystem::path::string_type suffix{
        static_cast<std::filesystem::path::value_type>('.'),
        static_cast<std::filesystem::path::value_type>('b'),
        static_cast<std::filesystem::path::value_type>('a'),
        static_cast<std::filesystem::path::value_type>('k')};
    result += suffix;
    return result;
}

[[nodiscard]] bool RemoveOwnRegularFile(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    if (!IsSafeRegularFile(path, error)) return error == std::errc::no_such_file_or_directory;
    return std::filesystem::remove(path, error) && !error;
}

class StandardTransactionFileSystem final : public IStampCatalogTransactionFileSystem
{
public:
    bool Rename(
        const std::filesystem::path& source,
        const std::filesystem::path& destination,
        std::string& error) override
    {
        std::error_code filesystemError;
        std::filesystem::rename(source, destination, filesystemError);
        if (!filesystemError) return true;
        error = filesystemError.message();
        return false;
    }
};

} // namespace

std::shared_ptr<IStampCatalogTransactionFileSystem>
CreateStandardStampCatalogTransactionFileSystem()
{
    return std::make_shared<StandardTransactionFileSystem>();
}

StampJsonCatalogStore::StampJsonCatalogStore(
    std::shared_ptr<IStampCatalogTransactionFileSystem> transactionFileSystem)
    : transactionFileSystem_(transactionFileSystem ? std::move(transactionFileSystem)
        : CreateStandardStampCatalogTransactionFileSystem())
{
}

bool StampJsonCatalogStore::SetProjectRoot(const std::filesystem::path& projectRoot)
{
    ClearProjectRoot();
    if (projectRoot.empty()) return false;
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(projectRoot, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) return false;
    const std::filesystem::path assets = canonical / "Assets";
    const auto status = std::filesystem::symlink_status(assets, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) return false;
    const std::filesystem::path canonicalAssets = std::filesystem::weakly_canonical(assets, error);
    if (error || !IsPathWithin(canonicalAssets, canonical)) return false;
    projectRoot_ = canonical;
    path_ = projectRoot_ / ProjectForgeLibraryRelativePath / "ForgeCatalog.json";
    return true;
}

void StampJsonCatalogStore::ClearProjectRoot() noexcept
{
    projectRoot_.clear();
    path_.clear();
}

const std::filesystem::path& StampJsonCatalogStore::ProjectRoot() const noexcept { return projectRoot_; }
const std::filesystem::path& StampJsonCatalogStore::Path() const noexcept { return path_; }

StampCatalogResult StampJsonCatalogStore::ValidateConfiguredPath(const bool createLibraryDirectory) const
{
    if (projectRoot_.empty() || path_.empty()) return Failure(StampCatalogError::NotConfigured);
    const std::filesystem::path assets = projectRoot_ / "Assets";
    const std::filesystem::path library = projectRoot_ / ProjectForgeLibraryRelativePath;
    if (!IsPathWithin(path_, library)) return Failure(StampCatalogError::PathEscapesProjectLibrary);
    std::error_code error;
    for (const std::filesystem::path& directory : {projectRoot_, assets})
    {
        const auto status = std::filesystem::symlink_status(directory, error);
        if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status))
            return Failure(StampCatalogError::InvalidProjectRoot);
    }
    auto libraryStatus = std::filesystem::symlink_status(library, error);
    if (error == std::errc::no_such_file_or_directory)
    {
        error.clear();
        if (!createLibraryDirectory) return Failure(StampCatalogError::Missing);
        if (!std::filesystem::create_directory(library, error) || error)
            return Failure(StampCatalogError::IoFailure, error.message());
        libraryStatus = std::filesystem::symlink_status(library, error);
    }
    if (error) return Failure(StampCatalogError::IoFailure, error.message());
    if (std::filesystem::is_symlink(libraryStatus)) return Failure(StampCatalogError::SymbolicLinkRejected);
    if (!std::filesystem::is_directory(libraryStatus)) return Failure(StampCatalogError::PathEscapesProjectLibrary);
    const std::filesystem::path canonicalLibrary = std::filesystem::weakly_canonical(library, error);
    if (error || !IsPathWithin(canonicalLibrary, projectRoot_))
        return Failure(StampCatalogError::PathEscapesProjectLibrary);
    return {};
}

StampCatalogResult StampJsonCatalogStore::SerializeToMemory(
    const StampCatalog& requested,
    std::vector<std::byte>& bytes)
{
    try
    {
        StampCatalogResult validated = ValidateCatalogue(requested);
        if (!validated.Succeeded()) return validated;
        const StampCatalog& catalogue = validated.Catalog;
        std::string text;
        text.reserve(256U + catalogue.Entries.size() * 192U);
        text += "{\"version\":1,\"entries\":[";
        for (std::size_t index = 0U; index < catalogue.Entries.size(); ++index)
        {
            const StampCatalogEntry& entry = catalogue.Entries[index];
            if (index != 0U) text.push_back(',');
            text += "{\"uuid\":";
            if (!AppendEscapedJsonString(text, entry.Reference.Id.ToString())) return Failure(StampCatalogError::Invalid);
            text += ",\"contentHash\":";
            if (!AppendEscapedJsonString(text, entry.Reference.ContentHash)) return Failure(StampCatalogError::Invalid);
            text += ",\"relativePath\":";
            if (!AppendEscapedJsonString(text, GenericUtf8Path(entry.Reference.RelativePath))) return Failure(StampCatalogError::Invalid);
            text += ",\"fileName\":";
            if (!AppendEscapedJsonString(text, entry.FileName)) return Failure(StampCatalogError::Invalid);
            text += ",\"fileBytes\":";
            if (!AppendUnsigned(text, entry.FileBytes)) return Failure(StampCatalogError::Invalid);
            text += ",\"dimensions\":[";
            if (!AppendUnsigned(text, entry.Dimensions.X)) return Failure(StampCatalogError::Invalid);
            text.push_back(',');
            if (!AppendUnsigned(text, entry.Dimensions.Y)) return Failure(StampCatalogError::Invalid);
            text.push_back(',');
            if (!AppendUnsigned(text, entry.Dimensions.Z)) return Failure(StampCatalogError::Invalid);
            text += "],\"voxelCount\":";
            if (!AppendUnsigned(text, entry.VoxelCount)) return Failure(StampCatalogError::Invalid);
            text += ",\"paletteCount\":";
            if (!AppendUnsigned(text, entry.PaletteCount)) return Failure(StampCatalogError::Invalid);
            text.push_back('}');
            if (text.size() > StampCatalogMaximumBytes) return Failure(StampCatalogError::Invalid);
        }
        text += "]}\n";
        if (text.size() > StampCatalogMaximumBytes) return Failure(StampCatalogError::Invalid);
        bytes.resize(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        StampCatalogResult reread = DeserializeFromMemory(bytes);
        if (!reread.Succeeded()) return Failure(StampCatalogError::Invalid,
            "Catalogue writer did not produce a valid V1 JSON document.");
        return reread;
    }
    catch (const std::bad_alloc&)
    {
        return Failure(StampCatalogError::AllocationFailure);
    }
}

StampCatalogResult StampJsonCatalogStore::DeserializeFromMemory(const std::span<const std::byte> bytes)
{
    try
    {
        if (bytes.empty() || bytes.size() > StampCatalogMaximumBytes)
            return Failure(StampCatalogError::Invalid, "Catalogue JSON is empty or exceeds the V1 byte bound.");
        const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!IsValidUtf8(text)) return Failure(StampCatalogError::Invalid, "Catalogue JSON is not valid UTF-8.");
        Json root;
        if (!JsonParser(text).Parse(root)) return Failure(StampCatalogError::Invalid);
        return CatalogueFromJson(root);
    }
    catch (const std::bad_alloc&)
    {
        return Failure(StampCatalogError::AllocationFailure);
    }
}

StampCatalogResult StampJsonCatalogStore::ReadPath(const std::filesystem::path& path) const
{
    std::error_code error;
    if (!IsSafeRegularFile(path, error))
        return Failure(error ? StampCatalogError::IoFailure : StampCatalogError::Invalid,
            error ? error.message() : "Catalogue path must be a regular non-symbolic file.");
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) return Failure(StampCatalogError::IoFailure, error.message());
    if (size == 0U || size > StampCatalogMaximumBytes)
        return Failure(StampCatalogError::Invalid, "Catalogue JSON is empty or exceeds the V1 byte bound.");
    try
    {
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        std::ifstream input(path, std::ios::binary);
        if (!input) return Failure(StampCatalogError::IoFailure);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input && !bytes.empty()) return Failure(StampCatalogError::IoFailure);
        return DeserializeFromMemory(bytes);
    }
    catch (const std::bad_alloc&)
    {
        return Failure(StampCatalogError::AllocationFailure);
    }
}

StampCatalogResult StampJsonCatalogStore::LoadCatalogue() const
{
    StampCatalogResult configured = ValidateConfiguredPath(false);
    if (!configured.Succeeded()) return configured;
    std::error_code error;
    bool exists = false;
    bool symbolicLink = false;
    for (const std::filesystem::path& transaction : {TemporaryPath(path_), BackupPath(path_)})
    {
        if (!InspectPath(transaction, exists, symbolicLink, error))
            return Failure(StampCatalogError::IoFailure, error.message());
        if (symbolicLink) return Failure(StampCatalogError::SymbolicLinkRejected);
        if (exists)
        {
            StampCatalogResult stale = Failure(StampCatalogError::StaleTransaction);
            stale.Diagnostics.push_back({StampCatalogError::StaleTransaction, transaction.filename(), stale.Message});
            return stale;
        }
    }
    if (!InspectPath(path_, exists, symbolicLink, error))
        return Failure(StampCatalogError::IoFailure, error.message());
    if (!exists) return Failure(StampCatalogError::Missing);
    if (symbolicLink) return Failure(StampCatalogError::SymbolicLinkRejected);
    return ReadPath(path_);
}

StampCatalogResult StampJsonCatalogStore::WriteCatalogueAtomically(const StampCatalog& catalogue)
{
    // Do not create Assets/ForgeLibrary for an invalid catalogue.  We still
    // report a missing/invalid project root before examining caller content.
    StampCatalogResult configured = ValidateConfiguredPath(false);
    if (!configured.Succeeded() && configured.Error != StampCatalogError::Missing)
        return configured;
    std::vector<std::byte> bytes;
    StampCatalogResult serialized = SerializeToMemory(catalogue, bytes);
    if (!serialized.Succeeded()) return serialized;
    if (configured.Error == StampCatalogError::Missing)
    {
        configured = ValidateConfiguredPath(true);
        if (!configured.Succeeded()) return configured;
    }
    std::error_code error;
    bool exists = false;
    bool symbolicLink = false;
    const std::filesystem::path temporary = TemporaryPath(path_);
    const std::filesystem::path backup = BackupPath(path_);
    for (const std::filesystem::path& transaction : {temporary, backup})
    {
        if (!InspectPath(transaction, exists, symbolicLink, error))
            return Failure(StampCatalogError::IoFailure, error.message());
        if (symbolicLink) return Failure(StampCatalogError::SymbolicLinkRejected);
        if (exists)
        {
            StampCatalogResult stale = Failure(StampCatalogError::StaleTransaction);
            stale.Diagnostics.push_back({StampCatalogError::StaleTransaction, transaction.filename(), stale.Message});
            return stale;
        }
    }

    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return Failure(StampCatalogError::IoFailure, "Unable to create catalogue temporary file.");
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.flush();
        output.close();
        if (!output)
        {
            static_cast<void>(RemoveOwnRegularFile(temporary));
            return Failure(StampCatalogError::IoFailure, "Unable to flush catalogue temporary file.");
        }
    }
    StampCatalogResult temporaryValidation = ReadPath(temporary);
    if (!temporaryValidation.Succeeded())
    {
        static_cast<void>(RemoveOwnRegularFile(temporary));
        return Failure(StampCatalogError::Invalid, "Catalogue temporary file failed post-write validation.");
    }

    if (!InspectPath(path_, exists, symbolicLink, error))
    {
        static_cast<void>(RemoveOwnRegularFile(temporary));
        return Failure(StampCatalogError::IoFailure, error.message());
    }
    if (symbolicLink)
    {
        static_cast<void>(RemoveOwnRegularFile(temporary));
        return Failure(StampCatalogError::SymbolicLinkRejected);
    }
    if (exists && !IsSafeRegularFile(path_, error))
    {
        static_cast<void>(RemoveOwnRegularFile(temporary));
        return Failure(StampCatalogError::Invalid, "Existing catalogue path must be a regular file.");
    }

    std::string transactionError;
    if (exists && !transactionFileSystem_->Rename(path_, backup, transactionError))
    {
        static_cast<void>(RemoveOwnRegularFile(temporary));
        return Failure(StampCatalogError::TransactionFailed,
            "Unable to back up existing catalogue: " + transactionError);
    }
    if (!transactionFileSystem_->Rename(temporary, path_, transactionError))
    {
        bool rollbackOk = true;
        if (exists) rollbackOk = transactionFileSystem_->Rename(backup, path_, transactionError);
        static_cast<void>(RemoveOwnRegularFile(temporary));
        return Failure(rollbackOk ? StampCatalogError::TransactionFailed : StampCatalogError::RollbackFailed,
            rollbackOk ? "Unable to publish catalogue." : "Catalogue backup could not be restored.");
    }
    StampCatalogResult finalValidation = ReadPath(path_);
    if (!finalValidation.Succeeded())
    {
        bool rollbackOk = true;
        if (exists)
        {
            rollbackOk = transactionFileSystem_->Rename(path_, temporary, transactionError) &&
                transactionFileSystem_->Rename(backup, path_, transactionError);
            static_cast<void>(RemoveOwnRegularFile(temporary));
        }
        else rollbackOk = RemoveOwnRegularFile(path_);
        return Failure(rollbackOk ? StampCatalogError::TransactionFailed : StampCatalogError::RollbackFailed,
            rollbackOk ? "Published catalogue failed final validation." : "Catalogue could not be rolled back.");
    }
    if (exists && !RemoveOwnRegularFile(backup))
        return Failure(StampCatalogError::TransactionFailed,
            "Catalogue was published but transaction backup cleanup failed.");
    return finalValidation;
}

} // namespace VoxelForge::Editor::Stamps
