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
#include <imgui_impl_sdlrenderer3.h>
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
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    // Configure emulator
    try {
        emu.set(Opt::MEM_CHIP_RAM, opts.chipRam);
        emu.set(Opt::MEM_SLOW_RAM, opts.slowRam);
        emu.set(Opt::MEM_FAST_RAM, opts.fastRam);
    } catch (std::exception &e) {
        fprintf(stderr, "Invalid memory configuration: %s\n", e.what());
        return false;
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

    // Create SDL window and renderer
    window = SDL_CreateWindow("vAmiga", 1024, 768,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        shutdown();
        return false;
    }

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        shutdown();
        return false;
    }
    SDL_SetRenderVSync(renderer, 1);

    // Bring window to front (macOS CLI apps don't auto-focus)
    SDL_RaiseWindow(window);

    // Create emulator texture
    // vAmiga core outputs ABGR in memory on LE (HI_HI_LO_LO(0xFF,b,g,r)).
    // SDL_PIXELFORMAT_RGBA32 is host-endian RGBA = ABGR8888 on LE — matches.
    emuTexture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        HPIXELS, VPIXELS);
    if (!emuTexture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        shutdown();
        return false;
    }
    SDL_SetTextureScaleMode(emuTexture, SDL_SCALEMODE_NEAREST);

    // Set initial viewport
    setDefaultViewport(false);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Disable imgui.ini (always start fresh)
    io.IniFilename = nullptr;

    // Dark theme
    ImGui::StyleColorsDark();

    // Init ImGui backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    imguiReady = true;

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
            ImGui_ImplSDLRenderer3_NewFrame();
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
    emu.pause();
    emu.powerOff();

    audio.shutdown();

    if (imguiReady) {
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiReady = false;
    }

    if (emuTexture) { SDL_DestroyTexture(emuTexture); emuTexture = nullptr; }
    if (renderer)   { SDL_DestroyRenderer(renderer);   renderer   = nullptr; }
    if (window)     { SDL_DestroyWindow(window);       window     = nullptr; }

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
                // Forward to emulator only when grabbed and ImGui doesn't want it
                if (mouseGrabbed && !io.WantCaptureKeyboard) {
                    handleKeyEvent(event.key, true);
                }
                break;

            case SDL_EVENT_KEY_UP:
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
                // Click on the Amiga display to grab mouse
                if (!mouseGrabbed && event.button.button == SDL_BUTTON_LEFT) {
                    // Grab if clicking on the emu window area or the background
                    // (not on ImGui floating windows like RetroShell or menu bar)
                    if (emuWindowHovered || !io.WantCaptureMouse) {
                        setMouseGrab(true);
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

    // Copy emulator texture
    emu.videoPort.lockTexture();
    const u32 *pixels = emu.videoPort.getTexture();
    if (pixels && emuTexture) {
        SDL_UpdateTexture(emuTexture, nullptr, pixels, HPIXELS * sizeof(u32));
    }
    emu.videoPort.unlockTexture();
}

void
App::render()
{
    // Build ImGui UI (menu bar, floating windows)
    // (NewFrame was already called at the start of the main loop)
    buildUI();

    // Finalize ImGui
    ImGui::Render();

    // Set render scale for HiDPI (ImGui works in logical coordinates)
    const ImGuiIO &io = ImGui::GetIO();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x,
                                 io.DisplayFramebufferScale.y);

    // Render: black background → Amiga display → ImGui overlay
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (!showEmuDecorations) {
        renderEmuWindow();  // Amiga texture as background (no decorations)
    }
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
}

// ImGui UI

void
App::buildUI()
{
    // Main menu bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Quit", "Ctrl+Q")) quit.store(true);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Window Decorations", nullptr, &showEmuDecorations);
            ImGui::MenuItem("RetroShell", nullptr, &showRetroShell);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Decorated Amiga window (with titlebar, resize, border)
    if (showEmuDecorations) {
        renderEmuWindowDecorated();
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

    ImGui::Image(reinterpret_cast<ImTextureID>(emuTexture), ImVec2(w, h), uv0, uv1);

    emuWindowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    ImGui::End();
    ImGui::PopStyleVar();
}

void
App::renderEmuWindow()
{
    // Render the Amiga display directly into the SDL renderer background.
    // Coordinates must be in the scaled space (SDL_SetRenderScale is active),
    // so we use the logical window size.

    const ImGuiIO &io = ImGui::GetIO();
    const float winW = io.DisplaySize.x;
    const float winH = io.DisplaySize.y;
    if (winW <= 0 || winH <= 0) return;

    // Account for menu bar height (in logical coordinates)
    const float topOffset = ImGui::GetFrameHeight();

    const float areaW = winW;
    const float areaH = winH - topOffset;
    if (areaH <= 0) return;

    // Fit 4:3 into the available area below the menu bar
    constexpr float targetAspect = 4.0f / 3.0f;
    float w = areaW;
    float h = w / targetAspect;
    if (h > areaH) {
        h = areaH;
        w = h * targetAspect;
    }

    // Center horizontally, pin to top of area (below menu bar)
    const float dx = (areaW - w) * 0.5f;
    const float dy = topOffset;

    // UV coordinates for viewport cropping
    const SDL_FRect src = srcRect;
    const SDL_FRect dst = { dx, dy, w, h };

    SDL_RenderTexture(renderer, emuTexture, &src, &dst);

    // When not using decorations, the emu area is "hovered" whenever
    // ImGui isn't consuming the mouse (no floating windows under cursor)
    emuWindowHovered = !ImGui::GetIO().WantCaptureMouse;
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

            constexpr int hblankOffset = 0x12 * 4;
            int px1 = 2 * hstrt - hblankOffset;
            int px2 = 2 * hstop - hblankOffset;

            const bool curNtsc = ntscValue.load();
            constexpr int lvX = 4 * HBLANK_CNT;
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
    emu.put(Cmd::MOUSE_MOVE_REL, CoordCmd(0, static_cast<double>(e.xrel),
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
    emu.put(Cmd::MOUSE_BUTTON, GamePadCmd(0, action));
}

// Viewport

void
App::recomputeViewport()
{
    const int lvX = LV_X;
    const int lvY = isNtsc ? LV_Y_NTSC : LV_Y_PAL;
    const int lvW = isNtsc ? LV_W_NTSC : LV_W_PAL;
    const int lvH = isNtsc ? LV_H_NTSC : LV_H_PAL;

    const float visW = hScale * static_cast<float>(lvW);
    const float visH = vScale * static_cast<float>(lvH);

    float originX = 0;
    float originY = 0;

    if (contentX2 > contentX1 && contentY2 > contentY1) {
        const float contentW = contentX2 - contentX1;
        const float contentH = contentY2 - contentY1;

        originX = contentX1 - 0.5f * (visW - contentW);
        originY = contentY1 - 0.5f * (visH - contentH);

        originX = std::max(originX, static_cast<float>(lvX));
        originX = std::min(originX, static_cast<float>(lvX + lvW) - visW);
        originY = std::max(originY, static_cast<float>(lvY));
        originY = std::min(originY, static_cast<float>(lvY + lvH) - visH);
    } else {
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
