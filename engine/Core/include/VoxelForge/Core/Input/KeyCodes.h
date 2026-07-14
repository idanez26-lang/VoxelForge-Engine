#pragma once

#include <cstdint>

namespace VoxelForge
{

// Physical keyboard positions based on the USB keyboard usage page.
// Values intentionally match SDL_Scancode without exposing SDL publicly.
enum class KeyCode : std::uint16_t
{
    Unknown = 0,

    A = 4, B = 5, C = 6, D = 7, E = 8, F = 9, G = 10,
    H = 11, I = 12, J = 13, K = 14, L = 15, M = 16,
    N = 17, O = 18, P = 19, Q = 20, R = 21, S = 22,
    T = 23, U = 24, V = 25, W = 26, X = 27, Y = 28, Z = 29,

    Digit1 = 30, Digit2 = 31, Digit3 = 32, Digit4 = 33, Digit5 = 34,
    Digit6 = 35, Digit7 = 36, Digit8 = 37, Digit9 = 38, Digit0 = 39,

    Enter = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,

    F1 = 58, F2 = 59, F3 = 60, F4 = 61, F5 = 62, F6 = 63,
    F7 = 64, F8 = 65, F9 = 66, F10 = 67, F11 = 68, F12 = 69,

    Insert = 73,
    Home = 74,
    PageUp = 75,
    Delete = 76,
    End = 77,
    PageDown = 78,
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,

    LeftControl = 224,
    LeftShift = 225,
    LeftAlt = 226,
    LeftSuper = 227,
    RightControl = 228,
    RightShift = 229,
    RightAlt = 230,
    RightSuper = 231
};

} // namespace VoxelForge
