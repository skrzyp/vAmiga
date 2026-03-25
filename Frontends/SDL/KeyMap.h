// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------

#pragma once

#include "BasicTypes.h"

#include <SDL3/SDL_scancode.h>
#include <unordered_map>

namespace vamiga {

// SDL scancode to Amiga keyboard scancode mapping

inline const std::unordered_map<SDL_Scancode, u8> sdlToAmiga = {

    // Row 0: ` 1 2 3 4 5 6 7 8 9 0 - = \ (ANSI codes 0x00–0x0D)
    {SDL_SCANCODE_GRAVE,        0x00},
    {SDL_SCANCODE_1,            0x01},
    {SDL_SCANCODE_2,            0x02},
    {SDL_SCANCODE_3,            0x03},
    {SDL_SCANCODE_4,            0x04},
    {SDL_SCANCODE_5,            0x05},
    {SDL_SCANCODE_6,            0x06},
    {SDL_SCANCODE_7,            0x07},
    {SDL_SCANCODE_8,            0x08},
    {SDL_SCANCODE_9,            0x09},
    {SDL_SCANCODE_0,            0x0A},
    {SDL_SCANCODE_MINUS,        0x0B},
    {SDL_SCANCODE_EQUALS,       0x0C},
    {SDL_SCANCODE_BACKSLASH,    0x0D},

    // Numpad 0
    {SDL_SCANCODE_KP_0,         0x0F},

    // Row 1: Q W E R T Y U I O P [ ] (ANSI codes 0x10–0x1B)
    {SDL_SCANCODE_Q,            0x10},
    {SDL_SCANCODE_W,            0x11},
    {SDL_SCANCODE_E,            0x12},
    {SDL_SCANCODE_R,            0x13},
    {SDL_SCANCODE_T,            0x14},
    {SDL_SCANCODE_Y,            0x15},
    {SDL_SCANCODE_U,            0x16},
    {SDL_SCANCODE_I,            0x17},
    {SDL_SCANCODE_O,            0x18},
    {SDL_SCANCODE_P,            0x19},
    {SDL_SCANCODE_LEFTBRACKET,  0x1A},
    {SDL_SCANCODE_RIGHTBRACKET, 0x1B},

    // Numpad 1 2 3
    {SDL_SCANCODE_KP_1,         0x1D},
    {SDL_SCANCODE_KP_2,         0x1E},
    {SDL_SCANCODE_KP_3,         0x1F},

    // Row 2: A S D F G H J K L ; ' (ANSI codes 0x20–0x2A)
    {SDL_SCANCODE_A,            0x20},
    {SDL_SCANCODE_S,            0x21},
    {SDL_SCANCODE_D,            0x22},
    {SDL_SCANCODE_F,            0x23},
    {SDL_SCANCODE_G,            0x24},
    {SDL_SCANCODE_H,            0x25},
    {SDL_SCANCODE_J,            0x26},
    {SDL_SCANCODE_K,            0x27},
    {SDL_SCANCODE_L,            0x28},
    {SDL_SCANCODE_SEMICOLON,    0x29},
    {SDL_SCANCODE_APOSTROPHE,   0x2A},

    // ISO extra key (between left shift and Z on international keyboards)
    {SDL_SCANCODE_NONUSBACKSLASH, 0x30},

    // Numpad 4 5 6
    {SDL_SCANCODE_KP_4,         0x2D},
    {SDL_SCANCODE_KP_5,         0x2E},
    {SDL_SCANCODE_KP_6,         0x2F},

    // Row 3: Z X C V B N M , . / (ANSI codes 0x31–0x3A)
    {SDL_SCANCODE_Z,            0x31},
    {SDL_SCANCODE_X,            0x32},
    {SDL_SCANCODE_C,            0x33},
    {SDL_SCANCODE_V,            0x34},
    {SDL_SCANCODE_B,            0x35},
    {SDL_SCANCODE_N,            0x36},
    {SDL_SCANCODE_M,            0x37},
    {SDL_SCANCODE_COMMA,        0x38},
    {SDL_SCANCODE_PERIOD,       0x39},
    {SDL_SCANCODE_SLASH,        0x3A},

    // Numpad . 7 8 9
    {SDL_SCANCODE_KP_PERIOD,    0x3C},
    {SDL_SCANCODE_KP_7,         0x3D},
    {SDL_SCANCODE_KP_8,         0x3E},
    {SDL_SCANCODE_KP_9,         0x3F},

    // Special keys (0x40–0x5F)
    {SDL_SCANCODE_SPACE,        0x40},
    {SDL_SCANCODE_BACKSPACE,    0x41},
    {SDL_SCANCODE_TAB,          0x42},
    {SDL_SCANCODE_KP_ENTER,     0x43},
    {SDL_SCANCODE_RETURN,       0x44},
    {SDL_SCANCODE_ESCAPE,       0x45},
    {SDL_SCANCODE_DELETE,       0x46},

    {SDL_SCANCODE_KP_MINUS,     0x4A},
    {SDL_SCANCODE_UP,           0x4C},
    {SDL_SCANCODE_DOWN,         0x4D},
    {SDL_SCANCODE_RIGHT,        0x4E},
    {SDL_SCANCODE_LEFT,         0x4F},

    // Function keys
    {SDL_SCANCODE_F1,           0x50},
    {SDL_SCANCODE_F2,           0x51},
    {SDL_SCANCODE_F3,           0x52},
    {SDL_SCANCODE_F4,           0x53},
    {SDL_SCANCODE_F5,           0x54},
    {SDL_SCANCODE_F6,           0x55},
    {SDL_SCANCODE_F7,           0x56},
    {SDL_SCANCODE_F8,           0x57},
    {SDL_SCANCODE_F9,           0x58},
    {SDL_SCANCODE_F10,          0x59},

    // Numpad operators
    {SDL_SCANCODE_KP_DIVIDE,    0x5C},
    {SDL_SCANCODE_KP_MULTIPLY,  0x5D},
    {SDL_SCANCODE_KP_PLUS,      0x5E},

    // Help (mapped to End or F12 — no direct Amiga equivalent on PC)
    {SDL_SCANCODE_END,          0x5F},
    {SDL_SCANCODE_F12,          0x5F},

    // Qualifier keys (0x60–0x67)
    {SDL_SCANCODE_LSHIFT,       0x60},
    {SDL_SCANCODE_RSHIFT,       0x61},
    {SDL_SCANCODE_CAPSLOCK,     0x62},
    {SDL_SCANCODE_LCTRL,        0x63},
    {SDL_SCANCODE_LALT,         0x64},
    {SDL_SCANCODE_RALT,         0x65},
    {SDL_SCANCODE_RCTRL,        0x63},  // Right Ctrl → Amiga Ctrl
    {SDL_SCANCODE_LGUI,         0x66},  // Left Amiga (Cmd/Win/Super)
    {SDL_SCANCODE_RGUI,         0x67},  // Right Amiga
};

}
