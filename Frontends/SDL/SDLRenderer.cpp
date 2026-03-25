// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------

#include "SDLRenderer.h"
#include <algorithm>

namespace vamiga {

SDLRenderer::~SDLRenderer()
{
    shutdown();
}

void
SDLRenderer::updateBaseSize()
{
    // 4:3 base size derived from srcRect height
    baseH = std::max(1, int(srcRect.h));
    baseW = baseH * 4 / 3;
    baseW = std::max(1, baseW & ~1);
    baseH &= ~1;
    if (baseH < 2) baseH = 2;
}

void
SDLRenderer::snapWindowSize()
{
    if (!window || baseW <= 0 || baseH <= 0) return;

    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);
    if (winW <= 0 || winH <= 0) return;

    int scaleByW = std::max(1, winW / baseW);
    int scaleByH = std::max(1, winH / baseH);
    scaleFactor = std::min(scaleByW, scaleByH);

    const int targetW = baseW * scaleFactor;
    const int targetH = baseH * scaleFactor;

    if (winW != targetW || winH != targetH) {
        expectedW = targetW;
        expectedH = targetH;
        SDL_SetWindowSize(window, targetW, targetH);
    }
}

void
SDLRenderer::recomputeViewport()
{
    const int lvX = LV_X;
    const int lvY = isNtsc ? LV_Y_NTSC : LV_Y_PAL;
    const int lvW = isNtsc ? LV_W_NTSC : LV_W_PAL;
    const int lvH = isNtsc ? LV_H_NTSC : LV_H_PAL;

    const float visW = kHScale * static_cast<float>(lvW);
    const float visH = kVScale * static_cast<float>(lvH);

    float originX = 0;
    float originY = 0;

    if (contentX2 > contentX1 && contentY2 > contentY1) {
        // Auto-center around content area (same as Swift GUI center=AUTO)
        const float contentW = contentX2 - contentX1;
        const float contentH = contentY2 - contentY1;

        originX = contentX1 - 0.5f * (visW - contentW);
        originY = contentY1 - 0.5f * (visH - contentH);

        // Clamp to largestVisible
        originX = std::max(originX, float(lvX));
        originX = std::min(originX, float(lvX + lvW) - visW);
        originY = std::max(originY, float(lvY));
        originY = std::min(originY, float(lvY + lvH) - visH);
    } else {
        // No tracking data — center in largestVisible
        originX = static_cast<float>(lvX) + (static_cast<float>(lvW) - visW) * 0.5f;
        originY = static_cast<float>(lvY) + (static_cast<float>(lvH) - visH) * 0.5f;
    }

    srcRect = { originX, originY, visW, visH };
    updateBaseSize();
}

bool
SDLRenderer::init(const char *title, int initialScale)
{
    scaleFactor = initialScale;

    // Set default viewport so we have srcRect for base size
    setDefaultViewport(false);

    int winW = baseW * scaleFactor;
    int winH = baseH * scaleFactor;

    window = SDL_CreateWindow(title, winW, winH,
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
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

    // vAmiga core outputs ABGR in memory on little-endian (HI_HI_LO_LO(0xFF,b,g,r)).
    // SDL_PIXELFORMAT_RGBA32 is host-endian RGBA, which is ABGR8888 on LE — matches.
    texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        HPIXELS, VPIXELS);

    if (!texture) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        shutdown();
        return false;
    }

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    return true;
}

void
SDLRenderer::shutdown()
{
    if (texture)  { SDL_DestroyTexture(texture);   texture  = nullptr; }
    if (renderer) { SDL_DestroyRenderer(renderer);  renderer = nullptr; }
    if (window)   { SDL_DestroyWindow(window);      window   = nullptr; }
}

void
SDLRenderer::updateTexture(const u32 *pixels)
{
    if (!texture || !pixels) return;
    SDL_UpdateTexture(texture, nullptr, pixels, HPIXELS * sizeof(u32));
}

void
SDLRenderer::render()
{
    if (!renderer || !texture) return;

    int outW = 0, outH = 0;
    SDL_GetRenderOutputSize(renderer, &outW, &outH);
    if (outW <= 0 || outH <= 0) return;  // Minimized window

    // Fill the entire render output (SDL handles HiDPI scaling internally)
    const SDL_FRect dstRect = {
        .x = 0, .y = 0,
        .w = static_cast<float>(outW),
        .h = static_cast<float>(outH)
    };

    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, &srcRect, &dstRect);
    SDL_RenderPresent(renderer);
}

void
SDLRenderer::updateContentArea(int x1, int y1, int x2, int y2)
{
    contentX1 = static_cast<float>(x1);
    contentY1 = static_cast<float>(y1);
    contentX2 = static_cast<float>(x2);
    contentY2 = static_cast<float>(y2);
    recomputeViewport();
}

void
SDLRenderer::setDefaultViewport(bool ntsc)
{
    isNtsc = ntsc;
    contentX1 = contentY1 = contentX2 = contentY2 = 0;
    recomputeViewport();
}

void
SDLRenderer::setTitle(const char *title)
{
    if (window) SDL_SetWindowTitle(window, title);
}

void
SDLRenderer::handleResize()
{
    if (!window) return;

    // If this resize matches what we requested via snapWindowSize, skip
    if (expectedW > 0 && expectedH > 0) {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window, &winW, &winH);
        if (std::abs(winW - expectedW) <= 2 && std::abs(winH - expectedH) <= 2) {
            expectedW = expectedH = 0;
            return;
        }
    }
    expectedW = expectedH = 0;
    snapWindowSize();
}

void
SDLRenderer::setRelativeMouseMode(bool enable)
{
    if (window) SDL_SetWindowRelativeMouseMode(window, enable);
}

bool
SDLRenderer::getRelativeMouseMode() const
{
    return window ? SDL_GetWindowRelativeMouseMode(window) : false;
}

}
