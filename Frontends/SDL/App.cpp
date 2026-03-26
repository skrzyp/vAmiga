// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------
/// @file

#include "config.h"
#include "App.h"
#include "Misc/RemoteServers/RemoteManagerTypes.h"

#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace vamiga {

// Lifecycle

bool
App::init(const AppOptions &opts)
{
    // Ensure window appears in foreground on macOS
    SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "1");

    // Initialize SDL
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // Enable viewport tracking (always needed for SDL frontend)
    emu.set(Opt::DENISE_VIEWPORT_TRACKING, true);

    // Load ROM
    printf("Loading ROM: %s\n", opts.rom.c_str());
    try {
        emu.mem.loadRom(opts.rom);
    } catch (std::exception &e) {
        fprintf(stderr, "Failed to load ROM: %s\n", e.what());
        return false;
    }

    // Launch emulator thread
    emu.launch(this, messageCallback);

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
        emu.set(Opt::SRV_PORT, opts.shellPort, static_cast<i64>(ServerType::RSH));
        emu.set(Opt::SRV_ENABLE, true, static_cast<i64>(ServerType::RSH));
        printf("RetroShell server on port %d (telnet localhost %d)\n",
               opts.shellPort, opts.shellPort);
    }

    // Create SDL window
    window = SDL_CreateWindow("vAmiga", 1024, 768,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        shutdown();
        return false;
    }

    // Bring window to front (macOS CLI apps don't auto-focus)
    SDL_RaiseWindow(window);

    // Initialize GPU renderer
    gpu = std::make_unique<GPURenderer>();
    if (!gpu->init(window)) {
        shutdown();
        return false;
    }

    // Set initial viewport
    setDefaultViewport(false);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();

    // Init ImGui backends (SDL3 + SDL_GPU)
    ImGui_ImplSDL3_InitForSDLGPU(window);
    ImGui_ImplSDLGPU3_InitInfo gpuInitInfo = {};
    gpuInitInfo.Device = gpu->getDevice();
    gpuInitInfo.ColorTargetFormat = gpu->getSwapchainFormat();
    gpuInitInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    ImGui_ImplSDLGPU3_Init(&gpuInitInfo);
    imguiReady = true;

    // Create configuration panel and apply saved settings
    configPanel = std::make_unique<ConfigPanel>(emu, window, gamepads,
                                                portDevice, &disconnectKeys);
    configPanel->loadSettings();
    configPanel->applySettings();

    dashboard = std::make_unique<Dashboard>(emu);

    // CLI memory arguments override saved settings only when explicitly given
    try {
        if (opts.chipRamSet) emu.set(Opt::MEM_CHIP_RAM, opts.chipRam);
        if (opts.slowRamSet) emu.set(Opt::MEM_SLOW_RAM, opts.slowRam);
        if (opts.fastRamSet) emu.set(Opt::MEM_FAST_RAM, opts.fastRam);
    } catch (std::exception &e) {
        fprintf(stderr, "Invalid memory configuration: %s\n", e.what());
        return false;
    }

    // Initialize audio
    const int sampleRate = audio.init(emu);
    if (!sampleRate) {
        fprintf(stderr, "Warning: audio initialization failed\n");
    }

    // Power on and run
    emu.powerOn();
    emu.run();

    printf("Emulator running.\n");
    return true;
}

void
App::run()
{
    try {
        while (!quit.load()) {
            // Start ImGui frame BEFORE processing events so that
            // io.WantCaptureMouse/Keyboard reflect the current frame
            ImGui_ImplSDLGPU3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            processEvents();
            update();
            render();
            emu.wakeUp();
        }
    } catch (std::exception &e) {
        fprintf(stderr, "Runtime error: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "Unknown runtime error\n");
    }
}

void
App::shutdown()
{
    // Auto-save settings before shutting down
    if (configPanel) {
        configPanel->saveSettings();
    }

    emu.pause();
    emu.powerOff();

    audio.shutdown();

    if (imguiReady) {
        ImGui_ImplSDLGPU3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiReady = false;
    }

    for (auto &g : gamepads) {
        if (g.pad) { SDL_CloseGamepad(g.pad); g = {}; }
    }

    if (gpu)    { gpu->shutdown(); gpu.reset(); }
    if (window) { SDL_DestroyWindow(window); window = nullptr; }

    SDL_Quit();
}

// Main loop phases

void
App::processEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {

        // Feed events to ImGui, but NOT when mouse is grabbed
        // (prevents invisible cursor from accidentally clicking menu items)
        if (!mouseGrabbed) {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        const ImGuiIO &io = ImGui::GetIO();

        switch (event.type) {

            case SDL_EVENT_QUIT:
                quit.store(true);
                break;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.repeat) break;
                // Cmd+Q / Ctrl+Q to quit
                if (event.key.scancode == SDL_SCANCODE_Q &&
                    (event.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL))) {
                    quit.store(true);
                    break;
                }
                if (isGrabHotkey(event.key)) {
                    if (mouseGrabbed) setMouseGrab(false);
                    break;
                }
                // Keyset → joystick (only when grabbed)
                if (mouseGrabbed && handleKeysetEvent(event.key.scancode, true)) {
                    if (disconnectKeys) break;
                }
                // Forward to Amiga keyboard
                if (mouseGrabbed && !io.WantCaptureKeyboard) {
                    handleKeyEvent(event.key, true);
                }
                break;

            case SDL_EVENT_KEY_UP:
                if (mouseGrabbed && handleKeysetEvent(event.key.scancode, false)) {
                    if (disconnectKeys) break;
                }
                if (mouseGrabbed && !io.WantCaptureKeyboard && !isGrabHotkey(event.key)) {
                    handleKeyEvent(event.key, false);
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (!io.WantCaptureMouse) {
                    handleMouseMotion(event.motion);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                // Click on the Amiga display image to grab mouse
                if (!mouseGrabbed && event.button.button == SDL_BUTTON_LEFT) {
                    if (emuWindowHovered) {
                        setMouseGrab(true);
                        // Forward the grab-click to the Amiga so the user
                        // doesn't have to click twice (once to grab, once to act)
                        handleMouseButton(event.button, true);
                        break;
                    }
                }
                if (!io.WantCaptureMouse) {
                    handleMouseButton(event.button, true);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (!io.WantCaptureMouse) {
                    handleMouseButton(event.button, false);
                }
                break;

            case SDL_EVENT_GAMEPAD_ADDED:
                handleGamepadAdded(event.gdevice.which);
                break;

            case SDL_EVENT_GAMEPAD_REMOVED:
                handleGamepadRemoved(event.gdevice.which);
                break;

            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                handleGamepadButton(event.gbutton);
                break;

            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                handleGamepadAxis(event.gaxis);
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (mouseGrabbed) setMouseGrab(false);
                audio.flush();
                break;

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                audio.flush();
                break;

            default:
                break;
        }
    }
}

void
App::update()
{
    // Handle NTSC/PAL switch
    if (ntscDirty.exchange(false)) {
        setDefaultViewport(ntscValue.load());
    }

    // Update viewport from tracking
    {
        std::lock_guard<std::mutex> lock(vpMutex);
        if (vpDirty) {
            vpDirty = false;
            updateContentArea(vpX1, vpY1, vpX2, vpY2);
        }
    }

    // Sample dashboard metrics
    if (dashboard) dashboard->update();

    // Recompute viewport every frame (picks up geometry option changes)
    recomputeViewport();

    // Upload emulator framebuffer to GPU
    emu.videoPort.lockTexture();
    const u32 *pixels = emu.videoPort.getTexture();
    if (pixels && gpu) {
        gpu->uploadTexture(pixels);
    }
    emu.videoPort.unlockTexture();
}

void
App::render()
{
    // Build ImGui UI (menu bar, floating windows)
    // (NewFrame was already called at the start of the main loop)
    buildUI();

    // Finalize ImGui and render via GPU
    ImGui::Render();
    if (gpu) {
        gpu->renderFrame(srcRect, ImGui::GetDrawData(), !showEmuDecorations);
    }
}

// ImGui UI

void
App::buildUI()
{
    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Configuration...")) {
                if (configPanel) configPanel->open();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q")) quit.store(true);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Machine")) {
            bool isPoweredOn = emu.isPoweredOn();
            bool isRunning = emu.isRunning();

            // Power toggle
            bool powered = isPoweredOn;
            if (ImGui::MenuItem("Power", nullptr, &powered)) {
                if (powered) { emu.powerOn(); emu.run(); }
                else { emu.powerOff(); }
            }

            // Pause toggle (only meaningful when powered on)
            bool paused = isPoweredOn && !isRunning;
            if (ImGui::MenuItem("Pause", nullptr, &paused, isPoweredOn)) {
                if (paused) emu.pause();
                else emu.run();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Soft Reset", nullptr, false, isPoweredOn)) {
                emu.softReset();
            }
            if (ImGui::MenuItem("Hard Reset", nullptr, false, isPoweredOn)) {
                emu.hardReset();
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Window Decorations", nullptr, &showEmuDecorations);
            ImGui::MenuItem("RetroShell", nullptr, &showRetroShell);
            if (ImGui::MenuItem("Dashboard")) {
                if (dashboard) dashboard->open();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Decorated Amiga window (with titlebar, resize, border)
    if (showEmuDecorations) {
        renderEmuWindowDecorated();
    }

    // Configuration panel
    if (configPanel) {
        configPanel->render();
    }

    // Dashboard
    if (dashboard) {
        dashboard->render();
    }

    // RetroShell console
    if (showRetroShell) {
        renderRetroShell();
    }
}

void
App::renderEmuWindowDecorated()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_Once);

    ImGui::Begin("Amiga", nullptr,
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse);

    // Fit texture maintaining 4:3 aspect ratio
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    constexpr float aspect = 4.0f / 3.0f;

    float w = avail.x;
    float h = w / aspect;
    if (h > avail.y) {
        h = avail.y;
        w = h * aspect;
    }

    // Center in available space
    ImVec2 cursor = ImGui::GetCursorPos();
    cursor.x += (avail.x - w) * 0.5f;
    cursor.y += (avail.y - h) * 0.5f;
    ImGui::SetCursorPos(cursor);

    // UV coordinates for viewport cropping
    const ImVec2 uv0(srcRect.x / static_cast<float>(HPIXELS),
                     srcRect.y / static_cast<float>(VPIXELS));
    const ImVec2 uv1((srcRect.x + srcRect.w) / static_cast<float>(HPIXELS),
                     (srcRect.y + srcRect.h) / static_cast<float>(VPIXELS));

    ImGui::Image(reinterpret_cast<ImTextureID>(gpu ? gpu->getEmuTexture() : nullptr), ImVec2(w, h), uv0, uv1);

    // Only grab mouse when clicking directly on the Amiga image, not the titlebar
    emuWindowHovered = ImGui::IsItemHovered();

    ImGui::End();
    ImGui::PopStyleVar();
}

void
App::renderEmuWindow()
{
    // In non-decorated mode, the GPU renderer draws the Amiga display directly.
    // Here we just track mouse hover state for the grab logic.

    const ImGuiIO &io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        emuWindowHovered = false;
        return;
    }

    // The GPU renderer draws the Amiga display centered in the window
    // with 4:3 aspect ratio. Approximate the display rect for hover detection.
    const float winW = io.DisplaySize.x;
    const float winH = io.DisplaySize.y;
    const float topOffset = ImGui::GetFrameHeight();
    const float areaW = winW;
    const float areaH = winH - topOffset;
    if (areaW <= 0 || areaH <= 0) { emuWindowHovered = false; return; }

    constexpr float targetAspect = 4.0f / 3.0f;
    float w = areaW, h = w / targetAspect;
    if (h > areaH) { h = areaH; w = h * targetAspect; }
    float dx = (areaW - w) * 0.5f;
    float dy = topOffset;

    const float mx = io.MousePos.x, my = io.MousePos.y;
    emuWindowHovered = (mx >= dx && mx < dx + w && my >= dy && my < dy + h);
}

// RetroShell console window
//
// The last line of text() is always the prompt + user input (e.g. "vAmiga% help").
// We show everything except the last line as scrollable output,
// and the last line is split into a prompt label + InputText for the command.

void
App::renderRetroShell()
{
    const ImVec4 bgColor(0.05f, 0.05f, 0.05f, 0.92f);
    const ImVec4 textColor(0.0f, 1.0f, 0.4f, 1.0f);

    ImGui::SetNextWindowSize(ImVec2(620, 400), ImGuiCond_Once);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bgColor);

    if (!ImGui::Begin("RetroShell", &showRetroShell)) {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        shellNeedsInit = true;
        shellInputBuf[0] = '\0';
        return;
    }

    // On first open, press Enter to trigger initial prompt display
    if (shellNeedsInit) {
        shellNeedsInit = false;
        emu.retroShell.press(RSKey::RETURN);
    }

    // Get text buffer. Last line = prompt + input + trailing space.
    const char *fullText = emu.retroShell.text();
    const std::string_view text = fullText ? fullText : "";

    // Split: output = all completed lines, lastLine = prompt + user input
    std::string_view output;
    std::string_view lastLine;

    const auto lastNl = text.rfind('\n');
    if (lastNl != std::string_view::npos) {
        output = text.substr(0, lastNl);
        lastLine = text.substr(lastNl + 1);
    } else {
        lastLine = text;
    }

    // Strip trailing space appended by text()
    if (!lastLine.empty() && lastLine.back() == ' ') {
        lastLine.remove_suffix(1);
    }

    // Reserve space for prompt + input at the bottom
    const float inputH = ImGui::GetFrameHeightWithSpacing();

    // Scrollable output area
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);

    if (ImGui::BeginChild("ShellOutput", ImVec2(0, -inputH),
                          ImGuiChildFlags_None, ImGuiWindowFlags_None)) {
        if (!output.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(output.data(), output.data() + output.size());
            ImGui::PopTextWrapPos();
        }

        if (shellScrollToBottom) {
            shellScrollToBottom = false;
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    // Bottom line: prompt label (from lastLine) + InputText for commands.
    // The prompt is the entire lastLine displayed as a label. The user types
    // in a separate InputText right after it. This avoids all prompt parsing.
    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 2));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    // Prompt label
    ImGui::TextUnformatted(lastLine.data(), lastLine.data() + lastLine.size());
    ImGui::SameLine(0, 0);

    // Command input
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);

    if (ImGui::InputText("##ShellInput", shellInputBuf, sizeof(shellInputBuf),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        // Send command text to RetroShell
        for (const char *p = shellInputBuf; *p; ++p) {
            emu.retroShell.press(*p);
        }
        emu.retroShell.press(RSKey::RETURN);
        shellInputBuf[0] = '\0';
        shellScrollToBottom = true;
    }

    const bool inputActive = ImGui::IsItemActive();

    ImGui::PopItemWidth();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(6);

    // Keep input focused whenever the RetroShell window is focused
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !inputActive) {
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// Message callback

void
App::messageCallback(const void *listener, Message msg)
{
    auto *self = const_cast<App *>(static_cast<const App *>(listener));
    self->handleMessage(msg);
}

void
App::handleMessage(Message msg)
{
    switch (msg.type) {

        case Msg::VIEWPORT:
        {
            const int hstrt = msg.viewport.hstrt;
            int vstrt = msg.viewport.vstrt;
            const int hstop = msg.viewport.hstop;
            int vstop = msg.viewport.vstop;

            if (hstrt == 0 && vstrt == 0 && hstop == 0 && vstop == 0) break;

            constexpr int lvX = 4 * HBLANK_CNT;
            int px1 = 2 * hstrt - lvX;
            int px2 = 2 * hstop - lvX;

            const bool curNtsc = ntscValue.load();
            const int vblank = curNtsc ? static_cast<int>(NTSC::VBLANK_CNT) : static_cast<int>(PAL::VBLANK_CNT);
            const int vmax   = curNtsc ? static_cast<int>(NTSC::VPOS_CNT)   : static_cast<int>(PAL::VPOS_CNT);
            const int hmax   = curNtsc ? static_cast<int>(4 * NTSC::HPOS_CNT) : static_cast<int>(4 * PAL::HPOS_CNT);

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
            ntscValue.store(msg.value == static_cast<i64>(TV::NTSC));
            ntscDirty.store(true);
            break;

        case Msg::SHUTDOWN: [[fallthrough]];
        case Msg::ABORT:
            quit.store(true);
            break;

        default:
            break;
    }
}

// Input

bool
App::isGrabHotkey(const SDL_KeyboardEvent &e)
{
#ifdef __APPLE__
    return e.scancode == SDL_SCANCODE_F11 && (e.mod & SDL_KMOD_GUI);
#else
    return e.scancode == SDL_SCANCODE_F11;
#endif
}

void
App::setMouseGrab(bool grab)
{
    if (!window) return;

    SDL_SetWindowRelativeMouseMode(window, grab);
    const bool wasGrabbed = mouseGrabbed;
    mouseGrabbed = SDL_GetWindowRelativeMouseMode(window);

    // When ungrabbing, tell ImGui all mouse buttons are released
    // to prevent "stuck drag" on windows that received mouse-down while grabbed
    if (wasGrabbed && !mouseGrabbed) {
        ImGuiIO &io = ImGui::GetIO();
        for (int i = 0; i < ImGuiMouseButton_COUNT; i++) {
            io.AddMouseButtonEvent(i, false);
        }
    }

    if (mouseGrabbed) {
        std::string title = "vAmiga";
#ifdef __APPLE__
        title += "  [ Cmd+F11 to release mouse ]";
#else
        title += "  [ F11 to release mouse ]";
#endif
        SDL_SetWindowTitle(window, title.c_str());
    } else {
        emu.put(Cmd::KEY_RELEASE_ALL, static_cast<i64>(0));
        SDL_SetWindowTitle(window, "vAmiga");
    }
}

void
App::handleKeyEvent(const SDL_KeyboardEvent &e, bool pressed)
{
    auto it = sdlToAmiga.find(e.scancode);
    if (it == sdlToAmiga.end()) return;

    if (pressed) {
        emu.put(Cmd::KEY_PRESS, KeyCmd { .keycode = it->second, .delay = 0 });
    } else {
        emu.put(Cmd::KEY_RELEASE, KeyCmd { .keycode = it->second, .delay = 0 });
    }
}

void
App::handleMouseMotion(const SDL_MouseMotionEvent &e)
{
    if (!mouseGrabbed) return;
    int port = portForDevice(PortDevice::Mouse);
    if (port < 0) return;
    emu.put(Cmd::MOUSE_MOVE_REL, CoordCmd(port, static_cast<double>(e.xrel),
                                                 static_cast<double>(e.yrel)));
}

void
App::handleMouseButton(const SDL_MouseButtonEvent &e, bool pressed)
{
    if (!mouseGrabbed) return;

    GamePadAction action;
    switch (e.button) {
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
    int port = portForDevice(PortDevice::Mouse);
    if (port < 0) return;
    emu.put(Cmd::MOUSE_BUTTON, GamePadCmd(port, action));
}

// Gamepad

int
App::portForDevice(PortDevice dev) const
{
    if (portDevice[0] == dev) return 0;
    if (portDevice[1] == dev) return 1;
    return -1;
}

bool
App::handleKeysetEvent(SDL_Scancode sc, bool pressed)
{
    auto check = [&](const KeysetDef &ks, int port) -> bool {
        GamePadAction action;
        if      (sc == ks.up)    action = pressed ? GamePadAction::PULL_UP    : GamePadAction::RELEASE_Y;
        else if (sc == ks.down)  action = pressed ? GamePadAction::PULL_DOWN  : GamePadAction::RELEASE_Y;
        else if (sc == ks.left)  action = pressed ? GamePadAction::PULL_LEFT  : GamePadAction::RELEASE_X;
        else if (sc == ks.right) action = pressed ? GamePadAction::PULL_RIGHT : GamePadAction::RELEASE_X;
        else if (sc == ks.fire)  action = pressed ? GamePadAction::PRESS_FIRE : GamePadAction::RELEASE_FIRE;
        else return false;
        emu.put(Cmd::JOY_EVENT, GamePadCmd(port, action));
        return true;
    };

    int p1 = portForDevice(PortDevice::Keyset1);
    if (p1 >= 0 && check(kKeyset1, p1)) return true;

    int p2 = portForDevice(PortDevice::Keyset2);
    if (p2 >= 0 && check(kKeyset2, p2)) return true;

    return false;
}

void
App::handleGamepadAdded(SDL_JoystickID id)
{
    // Find free slot
    int slot = -1;
    for (int i = 0; i < kMaxGamepads; i++) {
        if (!gamepads[i].pad) { slot = i; break; }
    }
    if (slot < 0) return;

    SDL_Gamepad *pad = SDL_OpenGamepad(id);
    if (!pad) return;

    gamepads[slot] = { pad, id };

    // Auto-assign to first port that has None (prefer Port 2)
    auto dev = static_cast<PortDevice>(static_cast<int>(PortDevice::Gamepad0) + slot);
    if (portDevice[1] == PortDevice::None) portDevice[1] = dev;
    else if (portDevice[0] == PortDevice::None) portDevice[0] = dev;

    const char *name = SDL_GetGamepadName(pad);
    printf("Gamepad connected: %s (slot %d)\n", name ? name : "Unknown", slot);
}

void
App::handleGamepadRemoved(SDL_JoystickID id)
{
    for (int i = 0; i < kMaxGamepads; i++) {
        if (gamepads[i].pad && gamepads[i].id == id) {
            auto dev = static_cast<PortDevice>(static_cast<int>(PortDevice::Gamepad0) + i);
            int port = portForDevice(dev);
            if (port >= 0) {
                emu.put(Cmd::JOY_EVENT, GamePadCmd(port, GamePadAction::RELEASE_XY));
                emu.put(Cmd::JOY_EVENT, GamePadCmd(port, GamePadAction::RELEASE_FIRE));
                portDevice[port] = PortDevice::None;
            }
            printf("Gamepad disconnected: slot %d\n", i);
            SDL_CloseGamepad(gamepads[i].pad);
            gamepads[i] = {};
            break;
        }
    }
}

void
App::handleGamepadButton(const SDL_GamepadButtonEvent &e)
{
    int slot = -1;
    for (int i = 0; i < kMaxGamepads; i++) {
        if (gamepads[i].pad && gamepads[i].id == e.which) { slot = i; break; }
    }
    if (slot < 0) return;
    auto dev = static_cast<PortDevice>(static_cast<int>(PortDevice::Gamepad0) + slot);
    int port = portForDevice(dev);
    if (port < 0) return;

    GamePadAction action;
    switch (static_cast<SDL_GamepadButton>(e.button)) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
        case SDL_GAMEPAD_BUTTON_WEST:
            action = e.down ? GamePadAction::PRESS_FIRE : GamePadAction::RELEASE_FIRE;
            break;
        case SDL_GAMEPAD_BUTTON_EAST:
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            action = e.down ? GamePadAction::PRESS_FIRE2 : GamePadAction::RELEASE_FIRE2;
            break;
        case SDL_GAMEPAD_BUTTON_NORTH:
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            action = e.down ? GamePadAction::PRESS_FIRE3 : GamePadAction::RELEASE_FIRE3;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            action = e.down ? GamePadAction::PULL_UP : GamePadAction::RELEASE_Y;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            action = e.down ? GamePadAction::PULL_DOWN : GamePadAction::RELEASE_Y;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            action = e.down ? GamePadAction::PULL_LEFT : GamePadAction::RELEASE_X;
            break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            action = e.down ? GamePadAction::PULL_RIGHT : GamePadAction::RELEASE_X;
            break;
        default:
            return;
    }
    emu.put(Cmd::JOY_EVENT, GamePadCmd(port, action));
}

void
App::handleGamepadAxis(const SDL_GamepadAxisEvent &e)
{
    int slot = -1;
    for (int i = 0; i < kMaxGamepads; i++) {
        if (gamepads[i].pad && gamepads[i].id == e.which) { slot = i; break; }
    }
    if (slot < 0) return;
    auto dev = static_cast<PortDevice>(static_cast<int>(PortDevice::Gamepad0) + slot);
    int port = portForDevice(dev);
    if (port < 0) return;
    auto &g = gamepads[slot];

    static constexpr Sint16 kDeadzone = 8000;

    if (e.axis == SDL_GAMEPAD_AXIS_LEFTX) {
        bool left = e.value < -kDeadzone;
        bool right = e.value > kDeadzone;

        if (left != g.stickLeft) {
            g.stickLeft = left;
            emu.put(Cmd::JOY_EVENT, GamePadCmd(port,
                left ? GamePadAction::PULL_LEFT : GamePadAction::RELEASE_X));
        }
        if (right != g.stickRight) {
            g.stickRight = right;
            emu.put(Cmd::JOY_EVENT, GamePadCmd(port,
                right ? GamePadAction::PULL_RIGHT : GamePadAction::RELEASE_X));
        }
    } else if (e.axis == SDL_GAMEPAD_AXIS_LEFTY) {
        bool up = e.value < -kDeadzone;
        bool down = e.value > kDeadzone;

        if (up != g.stickUp) {
            g.stickUp = up;
            emu.put(Cmd::JOY_EVENT, GamePadCmd(port,
                up ? GamePadAction::PULL_UP : GamePadAction::RELEASE_Y));
        }
        if (down != g.stickDown) {
            g.stickDown = down;
            emu.put(Cmd::JOY_EVENT, GamePadCmd(port,
                down ? GamePadAction::PULL_DOWN : GamePadAction::RELEASE_Y));
        }
    }
}

// Viewport

void
App::recomputeViewport()
{
    const int lvX = LV_X;
    const int lvY = isNtsc ? LV_Y_NTSC : LV_Y_PAL;
    const int lvW = isNtsc ? LV_W_NTSC : LV_W_PAL;
    const int lvH = isNtsc ? LV_H_NTSC : LV_H_PAL;

    // Read zoom from core geometry settings (matching macOS TextureRect.swift)
    float hZoom, vZoom;
    int zoomPreset = static_cast<int>(emu.get(Opt::MON_ZOOM));
    switch (zoomPreset) {
        case 1:  hZoom = 1.0f;   vZoom = 0.27f;  break;  // Narrow
        case 2:  hZoom = 0.747f; vZoom = 0.032f;  break;  // Wide
        case 3:  hZoom = 0.0f;   vZoom = 0.0f;    break;  // Extreme
        default: // Custom
            hZoom = static_cast<float>(emu.get(Opt::MON_HZOOM)) / 1000.0f;
            vZoom = static_cast<float>(emu.get(Opt::MON_VZOOM)) / 1000.0f;
            break;
    }

    const float hScale = 1.0f - 0.2f * hZoom;
    const float vScale = 1.0f - 0.2f * vZoom;
    const float visW = hScale * static_cast<float>(lvW);
    const float visH = vScale * static_cast<float>(lvH);

    float originX = 0;
    float originY = 0;

    int centerMode = static_cast<int>(emu.get(Opt::MON_CENTER));
    if (centerMode == 1 && contentX2 > contentX1 && contentY2 > contentY1) {
        // Auto-center on content area
        const float contentW = contentX2 - contentX1;
        const float contentH = contentY2 - contentY1;

        originX = contentX1 - 0.5f * (visW - contentW);
        originY = contentY1 - 0.5f * (visH - contentH);

        originX = std::max(originX, static_cast<float>(lvX));
        originX = std::min(originX, static_cast<float>(lvX + lvW) - visW);
        originY = std::max(originY, static_cast<float>(lvY));
        originY = std::min(originY, static_cast<float>(lvY + lvH) - visH);
    } else if (centerMode == 0) {
        // Manual center
        float hCenter = static_cast<float>(emu.get(Opt::MON_HCENTER)) / 1000.0f;
        float vCenter = static_cast<float>(emu.get(Opt::MON_VCENTER)) / 1000.0f;
        originX = static_cast<float>(lvX) + hCenter * (static_cast<float>(lvW) - visW);
        originY = static_cast<float>(lvY) + vCenter * (static_cast<float>(lvH) - visH);
    } else {
        // Fallback: center in visible area
        originX = static_cast<float>(lvX) + (static_cast<float>(lvW) - visW) * 0.5f;
        originY = static_cast<float>(lvY) + (static_cast<float>(lvH) - visH) * 0.5f;
    }

    srcRect = { originX, originY, visW, visH };
}

void
App::updateContentArea(int x1, int y1, int x2, int y2)
{
    contentX1 = static_cast<float>(x1);
    contentY1 = static_cast<float>(y1);
    contentX2 = static_cast<float>(x2);
    contentY2 = static_cast<float>(y2);
    recomputeViewport();
}

void
App::setDefaultViewport(bool ntsc)
{
    isNtsc = ntsc;
    contentX1 = contentY1 = contentX2 = contentY2 = 0;
    recomputeViewport();
}

}
