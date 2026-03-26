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
#include <imgui.h>

namespace vamiga {

class GPURenderer {

    SDL_GPUDevice *device = nullptr;
    SDL_Window *window = nullptr;

    // Emulator framebuffer texture (HPIXELS x VPIXELS, RGBA)
    SDL_GPUTexture *emuTexture = nullptr;
    SDL_GPUTransferBuffer *transferBuf = nullptr;
    bool textureDirty = false;

    // Blit pipeline (fullscreen triangle, passthrough fragment)
    SDL_GPUGraphicsPipeline *blitPipeline = nullptr;
    SDL_GPUSampler *samplerNearest = nullptr;

    // Push constant data for the vertex shader
    struct BlitUniforms {
        float uvMinX, uvMinY;
        float uvMaxX, uvMaxY;
    };

public:

    GPURenderer() = default;
    ~GPURenderer() = default;

    GPURenderer(const GPURenderer &) = delete;
    GPURenderer& operator=(const GPURenderer &) = delete;

    bool init(SDL_Window *window);
    void shutdown();

    // Upload emulator framebuffer to GPU (call before renderFrame)
    void uploadTexture(const u32 *pixels);

    // Render: Amiga display + ImGui overlay → swapchain
    // drawBackground: draw Amiga texture as fullscreen quad (false = ImGui only)
    void renderFrame(const SDL_FRect &srcRect, ImDrawData *drawData,
                     bool drawBackground);

    // Accessors for ImGui integration
    SDL_GPUDevice *getDevice() const { return device; }
    SDL_GPUTextureFormat getSwapchainFormat() const;
    SDL_GPUTexture *getEmuTexture() const { return emuTexture; }

private:

    SDL_GPUShader *createShader(SDL_GPUShaderStage stage,
                                const uint8_t *spirvCode, size_t spirvSize,
                                const char *mslCode, size_t mslSize,
                                int numSamplers, int numUniformBuffers);

    bool createBlitPipeline();
};

}
