// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------

#pragma once

#include "VAmiga.h"
#include <SDL3/SDL.h>
#include <mutex>
#include <string>

namespace vamiga {

// =========================================================================
//  Field descriptor for data-driven configuration forms
// =========================================================================

// A combo/dropdown entry mapping a display label to a core value.
struct ComboEntry {
    const char *label;
    i64 value;
};

// Field types supported by the generic form renderer.
enum class FT {
    Section,    // Section header (SeparatorText)
    Check,      // Checkbox (bool option)
    Combo,      // Dropdown (maps index to ComboEntry value)
    Slider,     // Integer slider with min/max range
    ColBegin,   // Begin two-column layout (left column)
    ColNext,    // Switch to right column
    ColEnd,     // End two-column layout
};

// Descriptor for a single form field.
struct Field {
    FT type;
    const char *label = nullptr;
    Opt opt = {};
    i64 id = -1;            // -1 = no id; -2 = apply to both id 0 and 1
    bool locked = false;    // Disable when emulator is powered on

    // Combo entries (type == FT::Combo)
    const ComboEntry *entries = nullptr;
    int entryCount = 0;

    // Slider range (type == FT::Slider)
    int min = 0;
    int max = 100;
    const char *fmt = "%d";
};

// Convenience constructors for field descriptors
constexpr Field FSection(const char *label) {
    return { FT::Section, label };
}
constexpr Field FCheck(const char *label, Opt opt, i64 id = -1, bool locked = false) {
    return { FT::Check, label, opt, id, locked };
}
constexpr Field FCombo(const char *label, Opt opt,
                       const ComboEntry *entries, int count,
                       i64 id = -1, bool locked = false) {
    return { FT::Combo, label, opt, id, locked, entries, count };
}
constexpr Field FSlider(const char *label, Opt opt, int min, int max,
                        i64 id = -1, bool locked = false, const char *fmt = "%d") {
    return { FT::Slider, label, opt, id, locked, nullptr, 0, min, max, fmt };
}
constexpr Field FColBegin() { return { FT::ColBegin }; }
constexpr Field FColNext()  { return { FT::ColNext }; }
constexpr Field FColEnd()   { return { FT::ColEnd }; }

// =========================================================================
//  Gamepad slot (shared between App and ConfigPanel)
// =========================================================================

static constexpr int kMaxGamepads = 4;

// Device type assignable to each Amiga control port
enum class PortDevice : int {
    None = 0,
    Mouse,
    Keyset1,
    Keyset2,
    Gamepad0, Gamepad1, Gamepad2, Gamepad3,
};

// Keyboard-to-joystick mapping (one set of keys → directions + fire)
struct KeysetDef {
    SDL_Scancode up, down, left, right, fire;
};

struct GamepadSlot {
    SDL_Gamepad *pad = nullptr;
    SDL_JoystickID id = 0;
    // Analog stick → digital state (edge detection)
    bool stickLeft = false;
    bool stickRight = false;
    bool stickUp = false;
    bool stickDown = false;
};

// =========================================================================
//  Configuration Panel
// =========================================================================
//
//  Tabs:
//    1. Hardware     — machine preset + chipset (data-driven + custom preset)
//    2. Memory       — RAM + memory properties (data-driven)
//    3. ROM          — ROM/ExtROM loading (custom)
//    4. Drives       — floppy/HD drives + disk controller (custom + data-driven)
//    5. Peripherals  — mouse, joystick, serial (partially data-driven)
//    6. Performance  — warp, speed, boosters (data-driven)
//    7. Compatibility— chipset features, collisions (data-driven)
//    8. Audio        — volumes, panning, processing, drive sounds (data-driven)
//    9. Video        — color, geometry, shader effects (data-driven)
//   10. Server       — remote servers (data-driven)
//
//  Not yet implemented (app-level preferences, not core Opt:: options):
//    - General    (fullscreen, mouse capture, pause in background)
//    - Controls   (keyboard-to-joystick mapping)
//    - Captures   (snapshot/screenshot settings)
//
// =========================================================================

class ConfigPanel {

    VAmiga &emu;
    SDL_Window *window;
    GamepadSlot *gamepads;
    PortDevice *portDevice;
    bool *disconnectKeys;

    bool visible = false;

    // File dialog state (protected by fileResultMutex)
    enum class PendingDialog { None, ROM, ExtROM, Disk0, Disk1, Disk2, Disk3 };
    PendingDialog pendingDialog = PendingDialog::None;
    std::mutex fileResultMutex;
    std::string fileResultPath;
    PendingDialog fileResultType = PendingDialog::None;

    // Cached ROM info
    std::string romTitle;
    std::string romVersion;
    bool hasRom = false;
    std::string extTitle;
    bool hasExt = false;
    int romPollCounter = 0;

    // Settings file path (platform-native via SDL_GetPrefPath)
    std::string settingsPath;

    // Status message (fade-out timer)
    std::string statusMsg;
    float statusTimer = 0;

    // Tracked max window height (prevents shrinking when switching tabs)
    float maxWindowH = 0;

public:

    ConfigPanel(VAmiga &emu, SDL_Window *window, GamepadSlot *gamepads,
                PortDevice *portDevice, bool *disconnectKeys);

    void open();
    void close();
    bool isVisible() const { return visible; }
    void render();

    // Settings lifecycle
    void loadSettings();
    void applySettings();
    void saveSettings();

private:

    // Generic form renderer — draws all fields in the array
    void renderFields(const Field *fields, int count);

    // Custom tab renderers (for tabs that need non-generic UI)
    void renderHardwareTab();
    void renderRomTab();
    void renderDrivesTab();
    void renderPeripheralsTab();
    void renderGamePortsSection();

    // File dialog
    static void SDLCALL fileDialogCallback(void *userdata,
                                           const char * const *filelist,
                                           int filter);
    void processPendingFileResult();
    void openFileDialog(PendingDialog type, const char *filterName,
                        const char *filterPattern);

    // Helpers
    void refreshRomInfo();
    void initSettingsPath();
    void resetToDefaults();
    bool trySet(Opt opt, i64 value);
    bool trySet(Opt opt, i64 value, i64 id);
    bool isPoweredOn() const;
    void setStatus(const char *msg, float duration = 3.0f);
};

}
