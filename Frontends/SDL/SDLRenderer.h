// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------
/// @file

#pragma once

#include "BasicTypes.h"
#include "Infrastructure/Constants.h"
#include <SDL3/SDL.h>

namespace vamiga {

class SDLRenderer {

    //
    // SDL resources
    //

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;

public:

    SDLRenderer() = default;

    // Non-copyable, non-movable (owns SDL resources)
    SDLRenderer(const SDLRenderer &) = delete;
    SDLRenderer(SDLRenderer &&) = delete;
    SDLRenderer &operator=(const SDLRenderer &) = delete;
    SDLRenderer &operator=(SDLRenderer &&) = delete;

private:

    //
    // Display geometry
    //

    // Source rect: which part of the 912×313 texture to display
    SDL_FRect srcRect {};

    // Whether the display is NTSC
    bool isNtsc = false;

    // Current integer scale factor
    int scaleFactor = 2;

    // The base display size at 1× (4:3, derived from visible area)
    int baseW = 0;
    int baseH = 0;

    // Largest visible area (everything outside HBLANK/VBLANK)
    static constexpr int LV_X = 4 * HBLANK_CNT;
    static constexpr int LV_W_PAL  = 4 * PAL::HPOS_CNT - LV_X;
    static constexpr int LV_W_NTSC = 4 * NTSC::HPOS_CNT - LV_X;
    static constexpr int LV_Y_PAL  = PAL::VBLANK_CNT;
    static constexpr int LV_Y_NTSC = NTSC::VBLANK_CNT;
    static constexpr int LV_H_PAL  = PAL::VPOS_CNT - LV_Y_PAL - 1;
    static constexpr int LV_H_NTSC = NTSC::VPOS_CNT - LV_Y_NTSC - 1;

    // "Wide" zoom factors (matching Swift GUI defaults: Zoom::WIDE)
    static constexpr float kHScale = 0.8506f;  // 1.0 - 0.2 * 0.747
    static constexpr float kVScale = 0.9936f;  // 1.0 - 0.2 * 0.032

    // Viewport tracking (content area reported by Denise)
    float contentX1 = 0, contentY1 = 0, contentX2 = 0, contentY2 = 0;

    // Tracks pending snap to avoid infinite resize loops
    int expectedW = 0, expectedH = 0;

    void updateBaseSize();
    void snapWindowSize();
    void recomputeViewport();

public:

    ~SDLRenderer();

    //
    // Lifecycle
    //

    bool init(const char *title, int initialScale = 2);
    void shutdown();

    //
    // Rendering
    //

    void updateTexture(const u32 *pixels);
    void render();

    //
    // Display configuration
    //

    void updateContentArea(int x1, int y1, int x2, int y2);
    void setDefaultViewport(bool ntsc = false);
    void setTitle(const char *title);
    void handleResize();

    //
    // Input helpers
    //

    void setRelativeMouseMode(bool enable);
    [[nodiscard]] bool getRelativeMouseMode() const;
};

}
