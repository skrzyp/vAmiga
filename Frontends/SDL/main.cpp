// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------

#include "VAmiga.h"
#include "Misc/RemoteServers/RemoteManagerTypes.h"
#include "SDLRenderer.h"
#include "SDLAudio.h"
#include "KeyMap.h"

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <atomic>
#include <mutex>
#include <csignal>

using namespace vamiga;

// -----------------------------------------------------------------------------
// Global state shared with the message callback
// -----------------------------------------------------------------------------

static std::atomic<bool> quit {false};
static_assert(std::atomic<bool>::is_always_lock_free);

static std::atomic<bool> ntscDirty {false};
static std::atomic<bool> ntscValue {false};

// Viewport tracking (protected by mutex for consistent reads)
static std::mutex vpMutex;
static bool vpDirty = false;
static int vpX1 = 0, vpY1 = 0, vpX2 = 0, vpY2 = 0;

// Mouse grab state (main thread only)
static bool mouseGrabbed = false;
static constexpr const char *kBaseTitle = "vAmiga";

// Signal handler for graceful Ctrl+C shutdown
static void signalHandler(int /*sig*/) { quit.store(true); }

// -----------------------------------------------------------------------------
// Message callback (called from the emulator thread)
// -----------------------------------------------------------------------------

static void
messageCallback(const void * /*listener*/, Message msg)
{
    switch (msg.type) {

        case Msg::VIEWPORT:
        {
            const int hstrt = msg.viewport.hstrt;
            int vstrt = msg.viewport.vstrt;
            const int hstop = msg.viewport.hstop;
            int vstop = msg.viewport.vstop;

            if (hstrt == 0 && vstrt == 0 && hstop == 0 && vstop == 0) break;

            // Convert to texture pixel coordinates (same as Swift GUI)
            constexpr int hblankOffset = 0x12 * 4;  // HBLANK_MIN * 4
            int px1 = 2 * hstrt - hblankOffset;
            int px2 = 2 * hstop - hblankOffset;

            // Clamp to largest visible area
            const bool isNtsc = ntscValue.load();
            constexpr int lvX = 4 * HBLANK_CNT;
            const int vblank = isNtsc ? static_cast<int>(NTSC::VBLANK_CNT) : static_cast<int>(PAL::VBLANK_CNT);
            const int vmax   = isNtsc ? static_cast<int>(NTSC::VPOS_CNT)   : static_cast<int>(PAL::VPOS_CNT);
            const int hmax   = isNtsc ? static_cast<int>(4 * NTSC::HPOS_CNT) : static_cast<int>(4 * PAL::HPOS_CNT);

            if (px1 < lvX) px1 = lvX;
            if (px2 > hmax) px2 = hmax;
            if (vstrt < vblank) vstrt = vblank;
            if (vstop > vmax) vstop = vmax;

            if (px2 > px1 && vstop > vstrt) {
                std::lock_guard<std::mutex> lock(vpMutex);
                vpX1 = px1; vpY1 = vstrt;
                vpX2 = px2; vpY2 = vstop;
                vpDirty = true;
            }
            break;
        }

        case Msg::VIDEO_FORMAT:
            ntscValue.store(msg.value == i64(TV::NTSC));
            ntscDirty.store(true);
            break;

        case Msg::SHUTDOWN:
        case Msg::ABORT:
            quit.store(true);
            break;

        default:
            break;
    }
}

// -----------------------------------------------------------------------------
// Mouse grab management
// -----------------------------------------------------------------------------

static void
setMouseGrab(SDLRenderer &display, VAmiga &emu, bool grab)
{
    display.setRelativeMouseMode(grab);
    mouseGrabbed = display.getRelativeMouseMode();

    // Release all Amiga keys when ungrabbing to prevent stuck keys
    if (!mouseGrabbed) {
        emu.put(Cmd::KEY_RELEASE_ALL, i64(0));
    }

    if (mouseGrabbed) {
        std::string title = std::string(kBaseTitle);
#ifdef __APPLE__
        title += "  [ Cmd+F11 to release mouse ]";
#else
        title += "  [ F11 to release mouse ]";
#endif
        display.setTitle(title.c_str());
    } else {
        display.setTitle(kBaseTitle);
    }
}

// -----------------------------------------------------------------------------
// Argument parsing
// -----------------------------------------------------------------------------

struct AppOptions {
    std::string rom;
    std::string adf;
    bool shell = false;
    int shellPort = 8081;
    int chipRam = 512;
    int slowRam = 512;
    int fastRam = 0;
};

static AppOptions
parseArgs(int argc, char *argv[])
{
    AppOptions opts;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if ((arg == "--rom" || arg == "-r") && i + 1 < argc) {
            opts.rom = argv[++i];
        } else if ((arg == "--adf" || arg == "-a") && i + 1 < argc) {
            opts.adf = argv[++i];
        } else if (arg == "--shell" || arg == "-s") {
            opts.shell = true;
            if (i + 1 < argc && argv[i+1][0] != '-') {
                try { opts.shellPort = std::stoi(argv[++i]); }
                catch (...) { fprintf(stderr, "Invalid port for --shell\n"); exit(1); }
                if (opts.shellPort < 1 || opts.shellPort > 65535) {
                    fprintf(stderr, "Port must be 1-65535\n"); exit(1);
                }
            }
        } else if (arg == "--chip" && i + 1 < argc) {
            try { opts.chipRam = std::stoi(argv[++i]); }
            catch (...) { fprintf(stderr, "Invalid value for --chip\n"); exit(1); }
        } else if (arg == "--slow" && i + 1 < argc) {
            try { opts.slowRam = std::stoi(argv[++i]); }
            catch (...) { fprintf(stderr, "Invalid value for --slow\n"); exit(1); }
        } else if (arg == "--fast" && i + 1 < argc) {
            try { opts.fastRam = std::stoi(argv[++i]); }
            catch (...) { fprintf(stderr, "Invalid value for --fast\n"); exit(1); }
        } else {
            fprintf(stderr, "Usage: VASDL --rom <path> [options]\n");
            fprintf(stderr, "  --rom  <path>    Kickstart ROM (required)\n");
            fprintf(stderr, "  --adf  <path>    Floppy disk image\n");
            fprintf(stderr, "  --shell [port]   RetroShell server (default: 8081)\n");
            fprintf(stderr, "  --chip <KB>      Chip RAM in KB (default: 512)\n");
            fprintf(stderr, "  --slow <KB>      Slow RAM in KB (default: 512)\n");
            fprintf(stderr, "  --fast <KB>      Fast RAM in KB (default: 0)\n");
            exit(1);
        }
    }

    if (opts.rom.empty()) {
        fprintf(stderr, "Error: --rom <path> is required\n");
        exit(1);
    }

    return opts;
}

// -----------------------------------------------------------------------------
// Input handling
// -----------------------------------------------------------------------------

static bool
isGrabHotkey(const SDL_KeyboardEvent &event)
{
#ifdef __APPLE__
    return event.scancode == SDL_SCANCODE_F11 && (event.mod & SDL_KMOD_GUI);
#else
    return event.scancode == SDL_SCANCODE_F11;
#endif
}

static void
handleKeyEvent(VAmiga &emu, const SDL_KeyboardEvent &event, bool pressed)
{
    auto it = sdlToAmiga.find(event.scancode);
    if (it == sdlToAmiga.end()) return;

    if (pressed) {
        emu.put(Cmd::KEY_PRESS, KeyCmd { .keycode = it->second, .delay = 0 });
    } else {
        emu.put(Cmd::KEY_RELEASE, KeyCmd { .keycode = it->second, .delay = 0 });
    }
}

static void
handleMouseMotion(VAmiga &emu, const SDL_MouseMotionEvent &event)
{
    if (!mouseGrabbed) return;
    emu.put(Cmd::MOUSE_MOVE_REL, CoordCmd(0, double(event.xrel), double(event.yrel)));
}

static void
handleMouseButton(VAmiga &emu, SDLRenderer &display,
                  const SDL_MouseButtonEvent &event, bool pressed)
{
    if (pressed && !mouseGrabbed && event.button == SDL_BUTTON_LEFT) {
        setMouseGrab(display, emu, true);
        return;
    }

    if (!mouseGrabbed) return;

    GamePadAction action;

    switch (event.button) {
        case SDL_BUTTON_LEFT:
            action = pressed ? GamePadAction::PRESS_LEFT : GamePadAction::RELEASE_LEFT;
            break;
        case SDL_BUTTON_RIGHT:
            action = pressed ? GamePadAction::PRESS_RIGHT : GamePadAction::RELEASE_RIGHT;
            break;
        case SDL_BUTTON_MIDDLE:
            action = pressed ? GamePadAction::PRESS_MIDDLE : GamePadAction::RELEASE_MIDDLE;
            break;
        default:
            return;
    }

    emu.put(Cmd::MOUSE_BUTTON, GamePadCmd(0, action));
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    auto opts = parseArgs(argc, argv);

    // Install signal handlers for graceful shutdown
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    printf("vAmiga SDL Frontend\n\n");

    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Create the emulator
    VAmiga emu;

    // Memory configuration
    try {
        emu.set(Opt::MEM_CHIP_RAM, opts.chipRam);
        emu.set(Opt::MEM_SLOW_RAM, opts.slowRam);
        emu.set(Opt::MEM_FAST_RAM, opts.fastRam);
    } catch (std::exception &e) {
        fprintf(stderr, "Invalid memory configuration: %s\n", e.what());
        SDL_Quit();
        return 1;
    }
    emu.set(Opt::DRIVE_CONNECT, true, 1);
    emu.set(Opt::DENISE_VIEWPORT_TRACKING, true);

    printf("Memory: Chip %d KB, Slow %d KB, Fast %d KB\n",
           opts.chipRam, opts.slowRam, opts.fastRam);

    // Load ROM
    printf("Loading ROM: %s\n", opts.rom.c_str());
    try {
        emu.mem.loadRom(opts.rom);
    } catch (std::exception &e) {
        fprintf(stderr, "Failed to load ROM: %s\n", e.what());
        SDL_Quit();
        return 1;
    }

    // Launch emulator thread
    emu.launch(&emu, messageCallback);

    // Load ADF if provided
    if (!opts.adf.empty()) {
        printf("Inserting disk: %s\n", opts.adf.c_str());
        try {
            emu.df0.insert(opts.adf, false);
        } catch (std::exception &e) {
            fprintf(stderr, "Failed to insert disk: %s\n", e.what());
        }
    }

    // Remote servers
    if (opts.shell) {
        emu.set(Opt::SRV_PORT, opts.shellPort, i64(ServerType::RSH));
        emu.set(Opt::SRV_ENABLE, true, i64(ServerType::RSH));
        printf("RetroShell server on port %d (telnet localhost %d)\n",
               opts.shellPort, opts.shellPort);
    }

    // Create renderer
    SDLRenderer display;
    if (!display.init("vAmiga")) {
        fprintf(stderr, "Failed to initialize display\n");
        SDL_Quit();
        return 1;
    }

    // Create audio
    SDLAudio audio;
    int sampleRate = audio.init(emu);
    if (!sampleRate) {
        fprintf(stderr, "Warning: audio initialization failed\n");
    }

    // Start with mouse NOT grabbed
    setMouseGrab(display, emu, false);

    // Power on and run
    emu.powerOn();
    emu.run();

    printf("Emulator running. Click window to grab mouse.\n");

    // -------------------------------------------------------------------------
    // Main loop
    // -------------------------------------------------------------------------

    while (!quit.load()) {

        // 1. Process SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {

            switch (event.type) {

                case SDL_EVENT_QUIT:
                    quit.store(true);
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.repeat) break;  // Ignore auto-repeat
                    if (isGrabHotkey(event.key)) {
                        if (mouseGrabbed) setMouseGrab(display, emu, false);
                        break;
                    }
                    if (mouseGrabbed) {
                        handleKeyEvent(emu, event.key, true);
                    }
                    break;

                case SDL_EVENT_KEY_UP:
                    if (mouseGrabbed && !isGrabHotkey(event.key)) {
                        handleKeyEvent(emu, event.key, false);
                    }
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                    display.handleResize();
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    handleMouseMotion(emu, event.motion);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    handleMouseButton(emu, display, event.button, true);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    handleMouseButton(emu, display, event.button, false);
                    break;

                default:
                    break;
            }
        }

        // 2. Handle NTSC/PAL switch
        if (ntscDirty.exchange(false)) {
            display.setDefaultViewport(ntscValue.load());
            display.handleResize();  // Snap window to new base size
        }

        // 3. Update viewport if tracking reported a change
        {
            std::lock_guard<std::mutex> lock(vpMutex);
            if (vpDirty) {
                vpDirty = false;
                display.updateContentArea(vpX1, vpY1, vpX2, vpY2);
            }
        }

        // 4. Copy texture from emulator
        emu.videoPort.lockTexture();
        const u32 *pixels = emu.videoPort.getTexture();
        display.updateTexture(pixels);
        emu.videoPort.unlockTexture();

        // 5. Render to screen (VSYNC'd by SDL)
        display.render();

        // 6. Signal the emulator thread
        emu.wakeUp();
    }

    // -------------------------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------------------------

    printf("\nShutting down...\n");

    emu.pause();
    emu.powerOff();

    audio.shutdown();
    display.shutdown();
    SDL_Quit();

    return 0;
}
