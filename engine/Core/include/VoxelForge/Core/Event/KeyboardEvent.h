#pragma once

#include "VoxelForge/Core/Event/Event.h"
#include "VoxelForge/Core/Input/KeyCodes.h"

#include <sstream>
#include <string>

namespace VoxelForge
{

class KeyboardEvent : public Event
{
public:
    [[nodiscard]] KeyCode GetKeyCode() const noexcept { return m_KeyCode; }
    VF_EVENT_CLASS_CATEGORY(EventCategoryInput | EventCategoryKeyboard)

protected:
    explicit KeyboardEvent(KeyCode keyCode) noexcept : m_KeyCode(keyCode) {}

private:
    KeyCode m_KeyCode;
};

class KeyPressedEvent final : public KeyboardEvent
{
public:
    KeyPressedEvent(KeyCode keyCode, bool repeated) noexcept
        : KeyboardEvent(keyCode), m_Repeated(repeated) {}

    [[nodiscard]] bool IsRepeated() const noexcept { return m_Repeated; }

    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream stream;
        stream << GetName() << ": " << static_cast<int>(GetKeyCode())
               << " (repeated=" << (m_Repeated ? "true" : "false") << ")";
        return stream.str();
    }

    VF_EVENT_CLASS_TYPE(KeyPressed)

private:
    bool m_Repeated;
};

class KeyReleasedEvent final : public KeyboardEvent
{
public:
    explicit KeyReleasedEvent(KeyCode keyCode) noexcept : KeyboardEvent(keyCode) {}

    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream stream;
        stream << GetName() << ": " << static_cast<int>(GetKeyCode());
        return stream.str();
    }

    VF_EVENT_CLASS_TYPE(KeyReleased)
};

class KeyTypedEvent final : public KeyboardEvent
{
public:
    explicit KeyTypedEvent(KeyCode keyCode) noexcept : KeyboardEvent(keyCode) {}

    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream stream;
        stream << GetName() << ": " << static_cast<int>(GetKeyCode());
        return stream.str();
    }

    VF_EVENT_CLASS_TYPE(KeyTyped)
};

} // namespace VoxelForge
