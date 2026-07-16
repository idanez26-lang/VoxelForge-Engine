#pragma once

#include "VoxelForge/Core/Event/Event.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <utility>

namespace VoxelForge
{

class FileDropPointEvent : public Event
{
public:
    [[nodiscard]] float GetX() const noexcept { return x_; }
    [[nodiscard]] float GetY() const noexcept { return y_; }

    VF_EVENT_CLASS_CATEGORY(EventCategoryApplication | EventCategoryInput)

protected:
    FileDropPointEvent(const float x, const float y) noexcept : x_(x), y_(y) {}

private:
    float x_ = 0.0F;
    float y_ = 0.0F;
};

class FileDropBeginEvent final : public FileDropPointEvent
{
public:
    FileDropBeginEvent(const float x, const float y) noexcept
        : FileDropPointEvent(x, y) {}
    VF_EVENT_CLASS_TYPE(FileDropBegin)
};

class FileDropPositionEvent final : public FileDropPointEvent
{
public:
    FileDropPositionEvent(const float x, const float y) noexcept
        : FileDropPointEvent(x, y) {}
    VF_EVENT_CLASS_TYPE(FileDropPosition)
};

class FileDropFileEvent final : public FileDropPointEvent
{
public:
    FileDropFileEvent(
        std::filesystem::path path,
        const float x,
        const float y)
        : FileDropPointEvent(x, y), path_(std::move(path)) {}

    [[nodiscard]] const std::filesystem::path& GetPath() const noexcept
    {
        return path_;
    }

    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream stream;
        stream << GetName() << ": " << path_.string();
        return stream.str();
    }

    VF_EVENT_CLASS_TYPE(FileDropFile)

private:
    std::filesystem::path path_;
};

class FileDropCompleteEvent final : public FileDropPointEvent
{
public:
    FileDropCompleteEvent(const float x, const float y) noexcept
        : FileDropPointEvent(x, y) {}
    VF_EVENT_CLASS_TYPE(FileDropComplete)
};

} // namespace VoxelForge
