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
#include "KeyMap.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <atomic>
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
};

class App {

    //
    // Emulator
    //

    VAmiga emu;
    SDLAudio audio;

    //
    // SDL resources
    //

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;

    // Emulator framebuffer texture (912x313 RGBA32)
    SDL_Texture *emuTexture = nullptr;

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

    // "Wide" zoom factors (matching Swift GUI defaults)
    static constexpr float hScale = 0.8506f;  // 1.0 - 0.2 * 0.747
    static constexpr float vScale = 0.9936f;  // 1.0 - 0.2 * 0.032

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
    // Viewport
    //

    void recomputeViewport();
    void updateContentArea(int x1, int y1, int x2, int y2);
    void setDefaultViewport(bool ntsc);
};

}
