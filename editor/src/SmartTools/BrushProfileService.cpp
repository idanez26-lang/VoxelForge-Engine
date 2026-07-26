#include "SmartTools/BrushProfileService.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <string_view>
#include <system_error>

namespace VoxelForge::Editor
{
namespace
{
struct Json final
{
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Kind Type = Kind::Null;
    bool Boolean = false;
    double Number = 0.0;
    std::string NumberText;
    std::string String;
    std::vector<Json> Array;
    std::map<std::string, Json, std::less<>> Object;
};

class JsonParser final
{
public:
    explicit JsonParser(const std::string_view text)
        : text_(text)
    {
    }

    [[nodiscard]] bool Parse(Json& value)
    {
        SkipWhitespace();
        if (!ParseValue(value))
        {
            return false;
        }

        SkipWhitespace();
        return cursor_ == text_.size();
    }

private:
    void SkipWhitespace()
    {
        while (cursor_ < text_.size() &&
               (text_[cursor_] == ' ' || text_[cursor_] == '\t' || text_[cursor_] == '\r' ||
                text_[cursor_] == '\n'))
        {
            ++cursor_;
        }
    }

    bool Consume(const char expected)
    {
        SkipWhitespace();
        if (cursor_ >= text_.size() || text_[cursor_] != expected)
        {
            return false;
        }

        ++cursor_;
        return true;
    }

    bool ConsumeLiteral(const std::string_view literal)
    {
        if (text_.substr(cursor_, literal.size()) != literal)
        {
            return false;
        }

        cursor_ += literal.size();
        return true;
    }

    bool ParseString(std::string& result)
    {
        if (!Consume('"'))
        {
            return false;
        }

        result.clear();
        while (cursor_ < text_.size())
        {
            const char character = text_[cursor_++];
            if (character == '"')
            {
                return true;
            }

            if (static_cast<unsigned char>(character) < 0x20U)
            {
                return false;
            }

            if (character != '\\')
            {
                result.push_back(character);
                continue;
            }

            if (cursor_ == text_.size())
            {
                return false;
            }

            const char escaped = text_[cursor_++];
            switch (escaped)
            {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u':
                if (!ParseUnicodeEscape(result))
                {
                    return false;
                }
                break;
            default:
                return false;
            }
        }

        return false;
    }

    bool ParseHexCodePoint(unsigned& codePoint)
    {
        if (cursor_ + 4U > text_.size()) return false;
        codePoint = 0U;
        for (int index = 0; index < 4; ++index)
        {
            const char hex = text_[cursor_++];
            if (!std::isxdigit(static_cast<unsigned char>(hex))) return false;
            const unsigned digit = std::isdigit(static_cast<unsigned char>(hex))
                ? static_cast<unsigned>(hex - '0')
                : static_cast<unsigned>(std::tolower(static_cast<unsigned char>(hex)) - 'a' + 10);
            codePoint = codePoint * 16U + digit;
        }
        return true;
    }

    static void AppendUtf8(std::string& result, const unsigned codePoint)
    {
        if (codePoint < 0x80U) result.push_back(static_cast<char>(codePoint));
        else if (codePoint < 0x800U)
        {
            result.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
            result.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        else if (codePoint < 0x10000U)
        {
            result.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        else
        {
            result.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
    }

    bool ParseUnicodeEscape(std::string& result)
    {
        unsigned codePoint = 0U;
        if (!ParseHexCodePoint(codePoint)) return false;
        if (codePoint >= 0xD800U && codePoint <= 0xDBFFU)
        {
            if (cursor_ + 6U > text_.size() || text_[cursor_] != '\\' || text_[cursor_ + 1U] != 'u')
                return false;
            cursor_ += 2U;
            unsigned lowSurrogate = 0U;
            if (!ParseHexCodePoint(lowSurrogate) || lowSurrogate < 0xDC00U || lowSurrogate > 0xDFFFU)
                return false;
            codePoint = 0x10000U + ((codePoint - 0xD800U) << 10U) + (lowSurrogate - 0xDC00U);
        }
        else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU)
        {
            return false;
        }
        AppendUtf8(result, codePoint);
        return true;
    }

    bool ParseNumber(Json& value)
    {
        const std::size_t first = cursor_;
        if (cursor_ < text_.size() && text_[cursor_] == '-')
        {
            ++cursor_;
        }

        if (cursor_ == text_.size())
        {
            return false;
        }

        if (text_[cursor_] == '0')
        {
            ++cursor_;
            if (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[cursor_])))
            {
                return false;
            }
        }
        else if (text_[cursor_] >= '1' && text_[cursor_] <= '9')
        {
            do
            {
                ++cursor_;
            } while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[cursor_])));
        }
        else
        {
            return false;
        }

        if (cursor_ < text_.size() && text_[cursor_] == '.')
        {
            ++cursor_;
            const std::size_t fractionalFirst = cursor_;
            while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[cursor_])))
            {
                ++cursor_;
            }

            if (cursor_ == fractionalFirst)
            {
                return false;
            }
        }

        if (cursor_ < text_.size() && (text_[cursor_] == 'e' || text_[cursor_] == 'E'))
        {
            ++cursor_;
            if (cursor_ < text_.size() && (text_[cursor_] == '+' || text_[cursor_] == '-'))
            {
                ++cursor_;
            }

            const std::size_t exponentFirst = cursor_;
            while (cursor_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[cursor_])))
            {
                ++cursor_;
            }

            if (cursor_ == exponentFirst)
            {
                return false;
            }
        }

        const std::string token(text_.substr(first, cursor_ - first));
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end != token.c_str() + token.size() || !std::isfinite(number))
        {
            return false;
        }

        value = {};
        value.Type = Json::Kind::Number;
        value.Number = number;
        value.NumberText = token;
        return true;
    }

    bool ParseValue(Json& value)
    {
        SkipWhitespace();
        if (cursor_ == text_.size())
        {
            return false;
        }

        switch (text_[cursor_])
        {
        case '"':
            value = {};
            value.Type = Json::Kind::String;
            return ParseString(value.String);
        case '{':
            return ParseObject(value);
        case '[':
            return ParseArray(value);
        case 't':
            if (!ConsumeLiteral("true"))
            {
                return false;
            }
            value = {};
            value.Type = Json::Kind::Bool;
            value.Boolean = true;
            return true;
        case 'f':
            if (!ConsumeLiteral("false"))
            {
                return false;
            }
            value = {};
            value.Type = Json::Kind::Bool;
            return true;
        case 'n':
            if (!ConsumeLiteral("null"))
            {
                return false;
            }
            value = {};
            return true;
        default:
            return ParseNumber(value);
        }
    }

    bool ParseArray(Json& value)
    {
        if (!Consume('['))
        {
            return false;
        }

        value = {};
        value.Type = Json::Kind::Array;
        SkipWhitespace();
        if (cursor_ < text_.size() && text_[cursor_] == ']')
        {
            ++cursor_;
            return true;
        }

        while (true)
        {
            Json item;
            if (!ParseValue(item))
            {
                return false;
            }

            value.Array.push_back(std::move(item));
            if (Consume(']'))
            {
                return true;
            }
            if (!Consume(','))
            {
                return false;
            }
        }
    }

    bool ParseObject(Json& value)
    {
        if (!Consume('{'))
        {
            return false;
        }

        value = {};
        value.Type = Json::Kind::Object;
        SkipWhitespace();
        if (cursor_ < text_.size() && text_[cursor_] == '}')
        {
            ++cursor_;
            return true;
        }

        while (true)
        {
            std::string key;
            Json item;
            if (!ParseString(key) || !Consume(':') || !ParseValue(item) ||
                !value.Object.emplace(std::move(key), std::move(item)).second)
            {
                return false;
            }

            if (Consume('}'))
            {
                return true;
            }
            if (!Consume(','))
            {
                return false;
            }
        }
    }

    std::string_view text_;
    std::size_t cursor_ = 0U;
};

const Json* Field(const Json& object, const std::string_view name)
{
    const auto found = object.Object.find(name);
    return found == object.Object.end() ? nullptr : &found->second;
}

bool StringField(const Json& object, const std::string_view name, std::string& out)
{
    const Json* field = Field(object, name);
    if (field == nullptr || field->Type != Json::Kind::String)
    {
        return false;
    }

    out = field->String;
    return true;
}

bool BoolField(const Json& object, const std::string_view name, bool& out)
{
    const Json* field = Field(object, name);
    if (field == nullptr || field->Type != Json::Kind::Bool)
    {
        return false;
    }

    out = field->Boolean;
    return true;
}

bool NumberField(const Json& object, const std::string_view name, double& out)
{
    const Json* field = Field(object, name);
    if (field == nullptr || field->Type != Json::Kind::Number)
    {
        return false;
    }

    out = field->Number;
    return true;
}

bool UnsignedField(const Json& object, const std::string_view name, std::uint64_t& out)
{
    const Json* field = Field(object, name);
    if (field == nullptr || field->Type != Json::Kind::Number || field->NumberText.empty())
    {
        return false;
    }

    const std::string_view number = field->NumberText;
    if (number.front() == '-' || number.find_first_of(".eE") != std::string_view::npos)
    {
        return false;
    }

    const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), out);
    return error == std::errc{} && end == number.data() + number.size();
}

std::string Escape(const std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value)
    {
        switch (character)
        {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U)
            {
                constexpr char Hex[] = "0123456789abcdef";
                escaped += "\\u00";
                escaped.push_back(Hex[(static_cast<unsigned char>(character) >> 4U) & 0x0FU]);
                escaped.push_back(Hex[static_cast<unsigned char>(character) & 0x0FU]);
            }
            else
            {
                escaped.push_back(character);
            }
            break;
        }
    }

    return escaped;
}

bool ValidUuid(const std::string_view value)
{
    if (value.size() != 36U)
    {
        return false;
    }

    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        if (index == 8U || index == 13U || index == 18U || index == 23U)
        {
            if (value[index] != '-')
            {
                return false;
            }
        }
        else if (!std::isxdigit(static_cast<unsigned char>(value[index])))
        {
            return false;
        }
    }

    return true;
}

std::string NewUuid()
{
    static std::mt19937_64 random(std::random_device{}());

    std::array<unsigned char, 16U> bytes{};
    for (unsigned char& byte : bytes)
    {
        byte = static_cast<unsigned char>(random());
    }

    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3FU) | 0x80U);

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (std::size_t index = 0U; index < bytes.size(); ++index)
    {
        if (index == 4U || index == 6U || index == 8U || index == 10U)
        {
            output << '-';
        }
        output << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }

    return output.str();
}

std::uint64_t Now()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

bool IsRegularNonSymlink(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    return !error && std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status);
}

template <typename T>
bool InRange(const T value, const T first, const T last)
{
    return value >= first && value <= last;
}

bool ParseProfile(const Json& value, BrushProfile& profile)
{
    if (value.Type != Json::Kind::Object || !StringField(value, "uuid", profile.Uuid) ||
        !StringField(value, "name", profile.Name) || !BoolField(value, "favorite", profile.Favorite))
    {
        return false;
    }

    std::string geometry;
    std::string shape;
    std::string action;
    std::string dimension;
    std::string orientation;
    if (!StringField(value, "geometry", geometry) || !StringField(value, "shape", shape) ||
        !StringField(value, "action", action) || !StringField(value, "dimension", dimension) ||
        !StringField(value, "orientation", orientation))
    {
        return false;
    }

    if (geometry != "Pencil" || (shape != "Cube" && shape != "Sphere") ||
        (action != "Add" && action != "Erase" && action != "Paint") ||
        (dimension != "Volume3D" && dimension != "Surface2D") ||
        (orientation != "Auto" && orientation != "X" && orientation != "Y" && orientation != "Z"))
    {
        return false;
    }

    profile.Geometry = SmartGeometry::Pencil;
    profile.Shape = shape == "Cube" ? SmartBrushShape::Cube : SmartBrushShape::Sphere;
    profile.Action = action == "Add" ? SmartAction::Add : action == "Erase" ? SmartAction::Erase : SmartAction::Paint;
    profile.Dimension = dimension == "Volume3D" ? SmartBrushDimension::Volume3D : SmartBrushDimension::Surface2D;
    profile.Orientation = orientation == "Auto" ? SmartBrushOrientation::Auto
                          : orientation == "X"  ? SmartBrushOrientation::X
                          : orientation == "Y"  ? SmartBrushOrientation::Y
                                                  : SmartBrushOrientation::Z;

    std::uint64_t size = 0U;
    std::uint64_t paletteIndex = 1U; // Absent in V1 files written before palette capture.
    std::uint64_t timestamp = 0U;
    double alpha = 0.0;
    if (!UnsignedField(value, "brushSize", size) || !NumberField(value, "previewAlpha", alpha) ||
        !UnsignedField(value, "timestamp", timestamp) || size < 1U ||
        size > static_cast<std::uint64_t>(SmartBrushEngine::MaximumSize()) || alpha < 0.0 || alpha > 1.0)
    {
        return false;
    }
    if (const Json* palette = Field(value, "paletteIndex"); palette != nullptr &&
        !UnsignedField(value, "paletteIndex", paletteIndex)) return false;
    if (paletteIndex < 1U || paletteIndex > 255U) return false;

    profile.BrushSize = static_cast<int>(size);
    profile.PaletteIndex = static_cast<std::size_t>(paletteIndex);
    profile.PreviewAlpha = static_cast<float>(alpha);
    profile.Timestamp = timestamp;
    return BrushProfileService::IsValid(profile);
}

const char* GeometryName(const SmartGeometry value)
{
    return value == SmartGeometry::Pencil ? "Pencil" : "Invalid";
}

const char* ShapeName(const SmartBrushShape value)
{
    return value == SmartBrushShape::Cube ? "Cube"
           : value == SmartBrushShape::Sphere ? "Sphere"
                                             : "Invalid";
}

const char* ActionName(const SmartAction value)
{
    return value == SmartAction::Add   ? "Add"
           : value == SmartAction::Erase ? "Erase"
           : value == SmartAction::Paint ? "Paint"
                                         : "Invalid";
}

const char* DimensionName(const SmartBrushDimension value)
{
    return value == SmartBrushDimension::Volume3D ? "Volume3D"
           : value == SmartBrushDimension::Surface2D ? "Surface2D"
                                                     : "Invalid";
}

const char* OrientationName(const SmartBrushOrientation value)
{
    return value == SmartBrushOrientation::Auto ? "Auto"
           : value == SmartBrushOrientation::X  ? "X"
           : value == SmartBrushOrientation::Y  ? "Y"
           : value == SmartBrushOrientation::Z  ? "Z"
                                                : "Invalid";
}

void WriteProfile(std::ostream& output, const BrushProfile& profile)
{
    output << "{\"uuid\":\"" << Escape(profile.Uuid) << "\",\"name\":\"" << Escape(profile.Name)
           << "\",\"favorite\":" << (profile.Favorite ? "true" : "false") << ",\"geometry\":\""
           << GeometryName(profile.Geometry) << "\",\"shape\":\"" << ShapeName(profile.Shape)
           << "\",\"action\":\"" << ActionName(profile.Action) << "\",\"brushSize\":"
           << profile.BrushSize << ",\"dimension\":\"" << DimensionName(profile.Dimension)
           << "\",\"orientation\":\"" << OrientationName(profile.Orientation) << "\",\"paletteIndex\":"
           << profile.PaletteIndex << ",\"previewAlpha\":"
           << std::setprecision(std::numeric_limits<float>::max_digits10) << profile.PreviewAlpha
           << ",\"timestamp\":" << profile.Timestamp << '}';
}

BrushProfile MakeDefaultProfile()
{
    return {std::string(BrushProfileService::DefaultProfileUuid), "Default", false,
        SmartGeometry::Pencil, SmartBrushShape::Cube, SmartAction::Add, 1,
        SmartBrushDimension::Volume3D, SmartBrushOrientation::Auto, 1U, 0.5F, 0U};
}
} // namespace

BrushProfileService::BrushProfileService()
{
    ClearProject();
}

bool BrushProfileService::SetProjectRoot(const std::filesystem::path& root)
{
    ClearProject();

    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(root, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error)
    {
        return false;
    }

    projectRoot_ = canonical;
    error.clear();
    const auto path = ProfilePath();
    const bool hasProfile = std::filesystem::exists(path, error);
    if (error) { ClearProject(); return false; }
    const bool hasPendingTransaction =
        std::filesystem::exists(path.string() + ".tmp", error) || error ||
        std::filesystem::exists(path.string() + ".bak", error) || error;
    if (error) { ClearProject(); return false; }
    if (!hasProfile && !hasPendingTransaction)
        return EnsureDefaultProfile().Succeeded();
    return true;
}

void BrushProfileService::ClearProject() noexcept
{
    projectRoot_.clear();
    profiles_ = {MakeDefaultProfile()};
    recent_.clear();
    activeUuid_ = std::string(DefaultProfileUuid);
}

const std::filesystem::path& BrushProfileService::ProjectRoot() const noexcept
{
    return projectRoot_;
}

std::filesystem::path BrushProfileService::ProfilePath() const
{
    return projectRoot_.empty() ? std::filesystem::path{} : projectRoot_ / "BrushProfiles.json";
}

BrushProfileResult BrushProfileService::Load()
{
    if (projectRoot_.empty())
    {
        return {BrushProfileStatus::IoError, "No project is configured."};
    }

    std::string recoveryDiagnostic;
    if (!RecoverTransaction(recoveryDiagnostic))
        return {BrushProfileStatus::IoError, recoveryDiagnostic};

    std::error_code statusError;
    const auto path = ProfilePath();
    if (!std::filesystem::exists(path, statusError) && !statusError)
    {
        profiles_.clear();
        recent_.clear();
        activeUuid_.clear();
        const BrushProfileResult created = EnsureDefaultProfile();
        if (!created.Succeeded()) return created;
        return {BrushProfileStatus::Success, "Created the default Brush Profile."};
    }
    if (statusError || !IsRegularNonSymlink(path))
        return {BrushProfileStatus::IoError, "BrushProfiles.json is not a regular file."};
    const std::uintmax_t bytes = std::filesystem::file_size(path, statusError);
    if (statusError || bytes > MaximumFileBytes)
        return {BrushProfileStatus::Invalid, "BrushProfiles.json exceeds the supported size."};

    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {BrushProfileStatus::IoError, "Unable to read BrushProfiles.json."};
    }

    std::ostringstream text;
    text << input.rdbuf();
    // Windows does not allow Persist() to rename the catalog while this read
    // handle remains open for the V1-to-current migration below.
    input.close();
    if (!input)
        return {BrushProfileStatus::IoError, "Unable to finish reading BrushProfiles.json."};
    Json root;
    if (!JsonParser(text.str()).Parse(root) || root.Type != Json::Kind::Object)
    {
        profiles_ = {MakeDefaultProfile()};
        recent_.clear();
        activeUuid_ = std::string(DefaultProfileUuid);
        return {BrushProfileStatus::Invalid, "BrushProfiles.json is not valid JSON."};
    }

    std::uint64_t version = 0U;
    const Json* profiles = Field(root, "profiles");
    const Json* recent = Field(root, "recent");
    const Json* active = Field(root, "active");
    if (!UnsignedField(root, "version", version) || version != FormatVersion)
    {
        profiles_ = {MakeDefaultProfile()};
        recent_.clear();
        activeUuid_ = std::string(DefaultProfileUuid);
        return {BrushProfileStatus::UnsupportedVersion, "Brush profile version is not supported."};
    }
    if (profiles == nullptr || profiles->Type != Json::Kind::Array ||
        (recent != nullptr && recent->Type != Json::Kind::Array))
    {
        profiles_ = {MakeDefaultProfile()};
        recent_.clear();
        activeUuid_ = std::string(DefaultProfileUuid);
        return {BrushProfileStatus::Invalid, "Brush profile file is missing required fields."};
    }

    std::vector<BrushProfile> loadedProfiles;
    std::vector<std::string> loadedRecent;
    std::size_t ignoredProfiles = 0U;
    for (const Json& item : profiles->Array)
    {
        BrushProfile profile;
        if (!ParseProfile(item, profile))
        {
            ++ignoredProfiles;
            continue;
        }
        // Default is a deterministic safety fallback, never user-defined
        // data even when a catalog was edited outside VoxelForge.
        if (profile.Uuid == DefaultProfileUuid) profile = MakeDefaultProfile();

        const bool duplicate = std::any_of(
            loadedProfiles.begin(),
            loadedProfiles.end(),
            [&profile](const BrushProfile& existing) { return existing.Uuid == profile.Uuid; });
        if (duplicate)
        {
            ++ignoredProfiles;
            continue;
        }
        // Reserve one deterministic slot for Default, even for an oversized
        // legacy catalog.
        if (loadedProfiles.size() >= MaximumProfiles - 1U)
        {
            ++ignoredProfiles;
            continue;
        }
        loadedProfiles.push_back(std::move(profile));
    }

    if (recent != nullptr)
    {
        for (const Json& item : recent->Array)
        {
            const bool exists = item.Type == Json::Kind::String && std::any_of(
                loadedProfiles.begin(),
                loadedProfiles.end(),
                [&item](const BrushProfile& profile) { return profile.Uuid == item.String; });
            if (!exists)
            {
                continue;
            }

            if (std::find(loadedRecent.begin(), loadedRecent.end(), item.String) == loadedRecent.end() &&
                loadedRecent.size() < MaximumRecent)
            {
                loadedRecent.push_back(item.String);
            }
        }
    }

    const bool hasDefault = std::any_of(loadedProfiles.begin(), loadedProfiles.end(), [](const BrushProfile& profile) {
        return profile.Uuid == BrushProfileService::DefaultProfileUuid;
    });
    if (!hasDefault) loadedProfiles.push_back(MakeDefaultProfile());
    profiles_ = std::move(loadedProfiles);
    recent_ = std::move(loadedRecent);
    activeUuid_ = active != nullptr && active->Type == Json::Kind::String && Find(active->String) != nullptr
        ? active->String : std::string(DefaultProfileUuid);
    // A partially corrupt catalog remains untouched on disk: rewriting it here
    // would silently delete the entries we only skipped in memory.
    if (!hasDefault && ignoredProfiles == 0U)
    {
        const BrushProfileResult migrated = Persist();
        if (!migrated.Succeeded()) return migrated;
    }
    return {BrushProfileStatus::Success,
        ignoredProfiles == 0U ? recoveryDiagnostic
                              : "Ignored " + std::to_string(ignoredProfiles) + " invalid Brush Profile(s)."};
}

BrushProfileResult BrushProfileService::Persist()
{
    if (projectRoot_.empty())
    {
        return {BrushProfileStatus::IoError, "No project is configured."};
    }

    if (profiles_.empty() || profiles_.size() > MaximumProfiles ||
        std::any_of(profiles_.begin(), profiles_.end(), [](const BrushProfile& profile) {
            return !BrushProfileService::IsValid(profile);
        }) || ActiveProfile() == nullptr)
    {
        return {BrushProfileStatus::Invalid, "Brush Profile catalog is invalid."};
    }

    std::string recoveryDiagnostic;
    if (!RecoverTransaction(recoveryDiagnostic))
        return {BrushProfileStatus::IoError, recoveryDiagnostic};

    const auto path = ProfilePath();
    const auto temporary = path.string() + ".tmp";
    const auto backup = path.string() + ".bak";

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return {BrushProfileStatus::IoError, "Unable to write BrushProfiles.json."};
    }

    std::vector<const BrushProfile*> orderedProfiles;
    orderedProfiles.reserve(profiles_.size());
    for (const BrushProfile& profile : profiles_) orderedProfiles.push_back(&profile);
    std::sort(orderedProfiles.begin(), orderedProfiles.end(), [](const BrushProfile* left, const BrushProfile* right) {
        return left->Uuid < right->Uuid;
    });

    output << "{\"version\":" << FormatVersion << ",\"active\":\"" << Escape(activeUuid_)
           << "\",\"profiles\":[";
    for (std::size_t index = 0U; index < orderedProfiles.size(); ++index)
    {
        if (index != 0U)
        {
            output << ',';
        }
        WriteProfile(output, *orderedProfiles[index]);
    }

    output << "],\"recent\":[";
    for (std::size_t index = 0U; index < recent_.size(); ++index)
    {
        if (index != 0U)
        {
            output << ',';
        }
        output << '\"' << Escape(recent_[index]) << '\"';
    }
    output << "]}";
    output.close();
    if (!output)
    {
        return {BrushProfileStatus::IoError, "Unable to finish BrushProfiles.json."};
    }

    std::error_code error;
    const bool destinationExists = std::filesystem::exists(path, error) && !error;
    const bool hadDestination = destinationExists && IsRegularNonSymlink(path);
    if (error || (destinationExists && !hadDestination))
    {
        std::filesystem::remove(temporary, error);
        return {BrushProfileStatus::IoError, "Brush profile transaction files are unavailable."};
    }

    if (hadDestination)
    {
        std::filesystem::rename(path, backup, error);
        if (error)
        {
            std::filesystem::remove(temporary, error);
            return {BrushProfileStatus::IoError, "Unable to back up BrushProfiles.json."};
        }
    }

    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        if (hadDestination)
        {
            std::error_code rollback;
            std::filesystem::rename(backup, path, rollback);
        }
        std::filesystem::remove(temporary, error);
        return {BrushProfileStatus::IoError, "Unable to replace BrushProfiles.json."};
    }

    if (hadDestination)
    {
        std::error_code cleanupError;
        std::filesystem::remove(backup, cleanupError);
        if (cleanupError)
            return {BrushProfileStatus::Success,
                "Profile saved, but the transaction backup will be recovered on the next save."};
    }

    return {BrushProfileStatus::Success, recoveryDiagnostic};
}

BrushProfileResult BrushProfileService::EnsureDefaultProfile()
{
    if (projectRoot_.empty())
        return {BrushProfileStatus::IoError, "No project is configured."};
    if (Find(std::string(DefaultProfileUuid)) == nullptr)
    {
        if (profiles_.size() == MaximumProfiles)
            return {BrushProfileStatus::Invalid, "Brush Profile limit reached."};
        profiles_.push_back(MakeDefaultProfile());
    }
    if (ActiveProfile() == nullptr) activeUuid_ = std::string(DefaultProfileUuid);
    return Persist();
}

bool BrushProfileService::RecoverTransaction(std::string& diagnostic)
{
    diagnostic.clear();
    const auto path = ProfilePath();
    const std::filesystem::path temporary = path.string() + ".tmp";
    const std::filesystem::path backup = path.string() + ".bak";
    std::error_code error;
    const bool hasPath = std::filesystem::exists(path, error);
    if (error) { diagnostic = "Unable to inspect BrushProfiles.json."; return false; }
    const bool hasTemporary = std::filesystem::exists(temporary, error);
    if (error) { diagnostic = "Unable to inspect Brush Profile temporary file."; return false; }
    const bool hasBackup = std::filesystem::exists(backup, error);
    if (error) { diagnostic = "Unable to inspect Brush Profile backup file."; return false; }
    if ((hasPath && !IsRegularNonSymlink(path)) ||
        (hasTemporary && !IsRegularNonSymlink(temporary)) ||
        (hasBackup && !IsRegularNonSymlink(backup)))
    {
        diagnostic = "Brush Profile transaction files are not regular files.";
        return false;
    }

    if (!hasPath && hasBackup)
    {
        std::filesystem::rename(backup, path, error);
        if (error) { diagnostic = "Unable to restore Brush Profile backup."; return false; }
        diagnostic = "Recovered Brush Profiles from an interrupted save.";
    }
    if (hasTemporary)
    {
        std::filesystem::remove(temporary, error);
        if (error) { diagnostic = "Unable to remove interrupted Brush Profile temporary file."; return false; }
        if (diagnostic.empty()) diagnostic = "Discarded an interrupted Brush Profile temporary save.";
    }
    if (hasPath && hasBackup)
    {
        std::filesystem::remove(backup, error);
        if (error) { diagnostic = "Unable to remove completed Brush Profile backup."; return false; }
        if (diagnostic.empty()) diagnostic = "Cleaned up a completed Brush Profile transaction.";
    }
    return true;
}

BrushProfile* BrushProfileService::Find(const std::string& uuid) noexcept
{
    const auto found = std::find_if(
        profiles_.begin(), profiles_.end(), [&uuid](const BrushProfile& profile) { return profile.Uuid == uuid; });
    return found == profiles_.end() ? nullptr : &*found;
}

const BrushProfile* BrushProfileService::Find(const std::string& uuid) const noexcept
{
    const auto found = std::find_if(
        profiles_.begin(), profiles_.end(), [&uuid](const BrushProfile& profile) { return profile.Uuid == uuid; });
    return found == profiles_.end() ? nullptr : &*found;
}

void BrushProfileService::TouchRecent(const std::string& uuid)
{
    recent_.erase(std::remove(recent_.begin(), recent_.end(), uuid), recent_.end());
    recent_.insert(recent_.begin(), uuid);
    if (recent_.size() > MaximumRecent)
    {
        recent_.resize(MaximumRecent);
    }
}

BrushProfileResult BrushProfileService::SaveNew(std::string name, const SmartTool& tool)
{
    BrushProfile profile = Capture(std::move(name), tool);
    if (!IsValid(profile))
    {
        return {BrushProfileStatus::Invalid, "Brush profile is invalid."};
    }

    while (Find(profile.Uuid) != nullptr)
    {
        profile.Uuid = NewUuid();
    }

    if (profiles_.size() == MaximumProfiles)
        return {BrushProfileStatus::Invalid, "Brush Profile limit reached."};
    profiles_.push_back(profile);
    const std::string activeBefore = activeUuid_;
    const auto recentBefore = recent_;
    activeUuid_ = profile.Uuid;
    TouchRecent(activeUuid_);
    const BrushProfileResult saved = Persist();
    if (!saved.Succeeded())
    {
        profiles_.pop_back();
        activeUuid_ = activeBefore;
        recent_ = recentBefore;
        return saved;
    }

    return {BrushProfileStatus::Success, saved.Message, profile};
}

BrushProfileResult BrushProfileService::Duplicate(const std::string& uuid, std::string name)
{
    const BrushProfile* source = Find(uuid);
    if (source == nullptr) return {BrushProfileStatus::NotFound, "Brush profile was not found."};
    if (profiles_.size() == MaximumProfiles) return {BrushProfileStatus::Invalid, "Brush Profile limit reached."};
    BrushProfile duplicate = *source;
    duplicate.Uuid = NewUuid();
    while (Find(duplicate.Uuid) != nullptr) duplicate.Uuid = NewUuid();
    duplicate.Name = std::move(name);
    duplicate.Favorite = false;
    duplicate.Timestamp = Now();
    if (!IsValid(duplicate)) return {BrushProfileStatus::Invalid, "Brush profile name is invalid."};
    profiles_.push_back(duplicate);
    const std::string activeBefore = activeUuid_;
    const auto recentBefore = recent_;
    activeUuid_ = duplicate.Uuid;
    TouchRecent(activeUuid_);
    const BrushProfileResult saved = Persist();
    if (!saved.Succeeded())
    {
        profiles_.pop_back();
        activeUuid_ = activeBefore;
        recent_ = recentBefore;
        return saved;
    }
    return {BrushProfileStatus::Success, saved.Message, duplicate};
}

BrushProfileResult BrushProfileService::Overwrite(const std::string& uuid, const SmartTool& tool)
{
    BrushProfile* profile = Find(uuid);
    if (profile == nullptr)
    {
        return {BrushProfileStatus::NotFound, "Brush profile was not found."};
    }
    if (profile->Uuid == DefaultProfileUuid)
        return {BrushProfileStatus::Protected, "The Default Brush Profile cannot be overwritten."};

    const BrushProfile before = *profile;
    BrushProfile replacement = Capture(before.Name, tool);
    replacement.Uuid = before.Uuid;
    replacement.Favorite = before.Favorite;
    *profile = replacement;

    const BrushProfileResult saved = Persist();
    if (!saved.Succeeded())
    {
        *profile = before;
        return saved;
    }

    return {BrushProfileStatus::Success, saved.Message, *profile};
}

BrushProfileResult BrushProfileService::Rename(const std::string& uuid, std::string name)
{
    BrushProfile* profile = Find(uuid);
    if (profile == nullptr)
    {
        return {BrushProfileStatus::NotFound, "Brush profile was not found."};
    }
    if (profile->Uuid == DefaultProfileUuid)
        return {BrushProfileStatus::Protected, "The Default Brush Profile cannot be renamed."};

    const BrushProfile before = *profile;
    profile->Name = std::move(name);
    if (!IsValid(*profile))
    {
        *profile = before;
        return {BrushProfileStatus::Invalid, "Brush profile name is invalid."};
    }

    const BrushProfileResult saved = Persist();
    if (!saved.Succeeded())
    {
        *profile = before;
        return saved;
    }

    return {BrushProfileStatus::Success, saved.Message, *profile};
}

BrushProfileResult BrushProfileService::Delete(const std::string& uuid)
{
    const auto found = std::find_if(
        profiles_.begin(), profiles_.end(), [&uuid](const BrushProfile& profile) { return profile.Uuid == uuid; });
    if (found == profiles_.end())
    {
        return {BrushProfileStatus::NotFound, "Brush profile was not found."};
    }
    if (found->Uuid == DefaultProfileUuid)
        return {BrushProfileStatus::Protected, "The Default Brush Profile cannot be deleted."};

    const auto before = profiles_;
    const auto recentBefore = recent_;
    const std::string activeBefore = activeUuid_;
    const bool deletedActive = activeUuid_ == uuid;
    profiles_.erase(found);
    recent_.erase(std::remove(recent_.begin(), recent_.end(), uuid), recent_.end());
    if (deletedActive) activeUuid_ = std::string(DefaultProfileUuid);

    const BrushProfileResult saved = Persist();
    if (!saved.Succeeded())
    {
        profiles_ = before;
        recent_ = recentBefore;
        activeUuid_ = activeBefore;
        return saved;
    }
    return {BrushProfileStatus::Success, saved.Message,
        deletedActive ? std::optional<BrushProfile>(*ActiveProfile()) : std::nullopt};
}

BrushProfileResult BrushProfileService::SetFavorite(const std::string& uuid, const bool favorite)
{
    BrushProfile* profile = Find(uuid);
    if (profile == nullptr)
    {
        return {BrushProfileStatus::NotFound, "Brush profile was not found."};
    }

    const bool before = profile->Favorite;
    profile->Favorite = favorite;
    const BrushProfileResult saved = Persist();
    if (!saved.Succeeded())
    {
        profile->Favorite = before;
        return saved;
    }

    return {BrushProfileStatus::Success, saved.Message, *profile};
}

BrushProfileResult BrushProfileService::SelectProfile(const std::string& uuid)
{
    BrushProfile* profile = Find(uuid);
    if (profile == nullptr)
    {
        return {BrushProfileStatus::NotFound, "Brush profile was not found."};
    }

    const auto before = recent_;
    const std::string activeBefore = activeUuid_;
    activeUuid_ = uuid;
    TouchRecent(uuid);
    const BrushProfileResult saved = Persist();
    if (!saved.Succeeded())
    {
        recent_ = before;
        activeUuid_ = activeBefore;
        return saved;
    }

    return {BrushProfileStatus::Success, saved.Message, *profile};
}

const std::vector<BrushProfile>& BrushProfileService::Profiles() const noexcept
{
    return profiles_;
}

const std::vector<std::string>& BrushProfileService::Recent() const noexcept
{
    return recent_;
}

std::string_view BrushProfileService::ActiveUuid() const noexcept { return activeUuid_; }
const BrushProfile* BrushProfileService::ActiveProfile() const noexcept { return Find(activeUuid_); }

std::vector<const BrushProfile*> BrushProfileService::SortedProfiles(const std::string_view filter) const
{
    std::string query(filter);
    std::transform(query.begin(), query.end(), query.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    std::vector<const BrushProfile*> result;
    for (const BrushProfile& profile : profiles_)
    {
        std::string name = profile.Name;
        std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (query.empty() || name.find(query) != std::string::npos)
        {
            result.push_back(&profile);
        }
    }

    std::sort(result.begin(), result.end(), [](const BrushProfile* left, const BrushProfile* right) {
        if (left->Favorite != right->Favorite)
        {
            return left->Favorite > right->Favorite;
        }
        if (left->Name != right->Name)
        {
            return left->Name < right->Name;
        }
        return left->Uuid < right->Uuid;
    });
    return result;
}

BrushProfile BrushProfileService::Capture(std::string name, const SmartTool& tool)
{
    const SmartBrushState& brush = tool.Brush();
    return {
        NewUuid(),
        std::move(name),
        false,
        tool.Geometry(),
        brush.Shape,
        tool.Action(),
        brush.Size,
        brush.Dimension,
        brush.Orientation,
        brush.PaletteIndex,
        tool.PreviewAlpha(),
        Now(),
    };
}

bool BrushProfileService::Apply(const BrushProfile& profile, SmartTool& tool)
{
    if (!IsValid(profile))
    {
        return false;
    }

    tool.SetGeometry(profile.Geometry);
    tool.SetAction(profile.Action);
    SmartBrushState& brush = tool.Brush();
    brush.Shape = profile.Shape;
    brush.Size = profile.BrushSize;
    brush.Dimension = profile.Dimension;
    brush.Orientation = profile.Orientation;
    brush.PaletteIndex = profile.PaletteIndex;
    tool.SetPreviewAlpha(profile.PreviewAlpha);
    tool.SetPreview(SmartToolPreviewState::Unavailable);
    tool.ClearStatistics();
    return true;
}

bool BrushProfileService::IsValid(const BrushProfile& profile) noexcept
{
    return ValidUuid(profile.Uuid) && !profile.Name.empty() &&
           profile.Name.size() <= MaximumNameBytes &&
           profile.Name.find_first_not_of(" \t\r\n") != std::string::npos && profile.BrushSize >= 1 &&
           profile.BrushSize <= SmartBrushEngine::MaximumSize() && std::isfinite(profile.PreviewAlpha) &&
           InRange(profile.PreviewAlpha, 0.0F, 1.0F) && profile.PaletteIndex >= 1U && profile.PaletteIndex <= 255U &&
           profile.Geometry == SmartGeometry::Pencil &&
           (profile.Shape == SmartBrushShape::Cube || profile.Shape == SmartBrushShape::Sphere) &&
           (profile.Action == SmartAction::Add || profile.Action == SmartAction::Erase ||
            profile.Action == SmartAction::Paint) &&
           (profile.Dimension == SmartBrushDimension::Volume3D || profile.Dimension == SmartBrushDimension::Surface2D) &&
           static_cast<unsigned>(profile.Orientation) <= static_cast<unsigned>(SmartBrushOrientation::Z);
}
} // namespace VoxelForge::Editor
