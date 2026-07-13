#pragma once
#include "VoxelForge/Core/Event/Event.h"
#include <cstdint>
#include <sstream>
#include <string>

namespace VoxelForge
{
using KeyCode = std::uint32_t;

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
        std::ostringstream s; s << GetName() << ": " << GetKeyCode()
        << " (repeated=" << (m_Repeated ? "true" : "false") << ")"; return s.str();
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
        std::ostringstream s; s << GetName() << ": " << GetKeyCode(); return s.str();
    }
    VF_EVENT_CLASS_TYPE(KeyReleased)
};

class KeyTypedEvent final : public KeyboardEvent
{
public:
    explicit KeyTypedEvent(KeyCode keyCode) noexcept : KeyboardEvent(keyCode) {}
    [[nodiscard]] std::string ToString() const override
    {
        std::ostringstream s; s << GetName() << ": " << GetKeyCode(); return s.str();
    }
    VF_EVENT_CLASS_TYPE(KeyTyped)
};
} // namespace VoxelForge
