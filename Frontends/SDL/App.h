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
#include "Infrastructure/Constants.h"
#include "SDLAudio.h"
#include "ConfigPanel.h"
#include "Dashboard.h"
#include "GPURenderer.h"
#include "KeyMap.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace vamiga {

// Command line options
struct AppOptions {
    std::string rom;
    std::string adf;
    bool shell = false;
    int shellPort = 8081;
    int chipRam = 512;
    int slowRam = 512;
    int fastRam = 0;

    // Track which options were explicitly provided on the command line
    bool chipRamSet = false;
    bool slowRamSet = false;
    bool fastRamSet = false;
};

class App {

    //
    // Emulator
    //

    VAmiga emu;
    SDLAudio audio;
    std::unique_ptr<ConfigPanel> configPanel;
    std::unique_ptr<Dashboard> dashboard;

    //
    // SDL resources
    //

    SDL_Window *window = nullptr;
    std::unique_ptr<GPURenderer> gpu;

    //
    // Thread-safe state (emulator thread -> main thread)
    //

    std::atomic<bool> ntscDirty {false};
    std::atomic<bool> ntscValue {false};

    std::mutex vpMutex;
    bool vpDirty = false;
    int vpX1 = 0, vpY1 = 0, vpX2 = 0, vpY2 = 0;

    //
    // Display geometry
    //

    SDL_FRect srcRect {};
    bool isNtsc = false;

    // Largest visible area constants
    static constexpr int LV_X = 4 * HBLANK_CNT;
    static constexpr int LV_W_PAL  = 4 * PAL::HPOS_CNT - LV_X;
    static constexpr int LV_W_NTSC = 4 * NTSC::HPOS_CNT - LV_X;
    static constexpr int LV_Y_PAL  = PAL::VBLANK_CNT;
    static constexpr int LV_Y_NTSC = NTSC::VBLANK_CNT;
    static constexpr int LV_H_PAL  = PAL::VPOS_CNT - LV_Y_PAL - 1;
    static constexpr int LV_H_NTSC = NTSC::VPOS_CNT - LV_Y_NTSC - 1;

    // Viewport tracking (content area reported by Denise)
    float contentX1 = 0, contentY1 = 0, contentX2 = 0, contentY2 = 0;

    //
    // UI state
    //

    bool showEmuDecorations = true;

    // Set during buildUI
    bool emuWindowHovered = false;

    bool showRetroShell = false;
    bool imguiReady = false;
    bool shellNeedsInit = true;
    bool shellScrollToBottom = false;
    char shellInputBuf[256] = {};

    //
    // Input state
    //

    bool mouseGrabbed = false;

    //
    // Gamepads and port assignment
    //

    GamepadSlot gamepads[kMaxGamepads] = {};
    PortDevice portDevice[2] = { PortDevice::Mouse, PortDevice::None };
    bool disconnectKeys = true;

    static constexpr KeysetDef kKeyset1 = {
        SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
        SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_SPACE
    };
    static constexpr KeysetDef kKeyset2 = {
        SDL_SCANCODE_E, SDL_SCANCODE_X,
        SDL_SCANCODE_S, SDL_SCANCODE_D,
        SDL_SCANCODE_C
    };

public:

    // Quit flag (public so the signal handler can set it)
    std::atomic<bool> quit {false};

    App() = default;
    ~App() = default;

    App(const App &) = delete;
    App(App &&) = delete;
    App& operator= (const App &) = delete;
    App& operator= (App &&) = delete;

    //
    // Lifecycle
    //

    bool init(const AppOptions &opts);
    void run();
    void shutdown();

private:

    //
    // Main loop
    //

    void processEvents();
    void update();
    void render();

    //
    // ImGui UI
    //

    void buildUI();
    void renderEmuWindow();
    void renderEmuWindowDecorated();
    void renderRetroShell();
    void renderDashboard();

    //
    // Message callback
    //

    static void messageCallback(const void *listener, Message msg);
    void handleMessage(Message msg);

    //
    // Input
    //

    void setMouseGrab(bool grab);
    void handleKeyEvent(const SDL_KeyboardEvent &e, bool pressed);
    void handleMouseMotion(const SDL_MouseMotionEvent &e);
    void handleMouseButton(const SDL_MouseButtonEvent &e, bool pressed);

    static bool isGrabHotkey(const SDL_KeyboardEvent &e);

    //
    // Gamepad
    //

    void handleGamepadAdded(SDL_JoystickID id);
    void handleGamepadRemoved(SDL_JoystickID id);
    void handleGamepadButton(const SDL_GamepadButtonEvent &e);
    void handleGamepadAxis(const SDL_GamepadAxisEvent &e);

    int portForDevice(PortDevice dev) const;
    bool handleKeysetEvent(SDL_Scancode sc, bool pressed);

    //
    // Viewport
    //

    void recomputeViewport();
    void updateContentArea(int x1, int y1, int x2, int y2);
    void setDefaultViewport(bool ntsc);
};

}
