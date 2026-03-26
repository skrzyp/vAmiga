// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------
/// @file

#include "config.h"
#include "GPURenderer.h"
#include "shaders/ShaderBytecode.h"

#include <imgui_impl_sdlgpu3.h>
#include <cstdio>
#include <cstring>

namespace vamiga {

// Lifecycle

bool
GPURenderer::init(SDL_Window *win)
{
    window = win;

    // Create GPU device (Metal on macOS, Vulkan on Linux)
    device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL,
        false, nullptr);
    if (!device) {
        fprintf(stderr, "SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        return false;
    }

    printf("GPU driver: %s\n", SDL_GetGPUDeviceDriver(device));

    // Claim window for GPU rendering
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        fprintf(stderr, "SDL_ClaimWindowForGPUDevice failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_SetGPUSwapchainParameters(device, window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);

    // Create emulator texture
    SDL_GPUTextureCreateInfo texInfo = {};
    texInfo.type = SDL_GPU_TEXTURETYPE_2D;
    texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texInfo.width = HPIXELS;
    texInfo.height = VPIXELS;
    texInfo.layer_count_or_depth = 1;
    texInfo.num_levels = 1;
    texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

    emuTexture = SDL_CreateGPUTexture(device, &texInfo);
    if (!emuTexture) {
        fprintf(stderr, "SDL_CreateGPUTexture failed: %s\n", SDL_GetError());
        return false;
    }

    // Create transfer buffer for CPU→GPU texture upload
    SDL_GPUTransferBufferCreateInfo tbInfo = {};
    tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size = HPIXELS * VPIXELS * 4;

    transferBuf = SDL_CreateGPUTransferBuffer(device, &tbInfo);
    if (!transferBuf) {
        fprintf(stderr, "SDL_CreateGPUTransferBuffer failed: %s\n", SDL_GetError());
        return false;
    }

    // Create sampler
    SDL_GPUSamplerCreateInfo sampInfo = {};
    sampInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    sampInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    samplerNearest = SDL_CreateGPUSampler(device, &sampInfo);
    if (!samplerNearest) {
        fprintf(stderr, "SDL_CreateGPUSampler failed: %s\n", SDL_GetError());
        return false;
    }

    // Create blit pipeline
    if (!createBlitPipeline()) return false;

    return true;
}

void
GPURenderer::shutdown()
{
    if (device) SDL_WaitForGPUIdle(device);

    if (blitPipeline)    { SDL_ReleaseGPUGraphicsPipeline(device, blitPipeline); blitPipeline = nullptr; }
    if (samplerNearest)  { SDL_ReleaseGPUSampler(device, samplerNearest); samplerNearest = nullptr; }
    if (emuTexture)      { SDL_ReleaseGPUTexture(device, emuTexture); emuTexture = nullptr; }
    if (transferBuf)     { SDL_ReleaseGPUTransferBuffer(device, transferBuf); transferBuf = nullptr; }

    if (device && window) {
        SDL_ReleaseWindowFromGPUDevice(device, window);
    }
    if (device) {
        SDL_DestroyGPUDevice(device);
        device = nullptr;
    }
    window = nullptr;
}

SDL_GPUTextureFormat
GPURenderer::getSwapchainFormat() const
{
    if (!device || !window) return SDL_GPU_TEXTUREFORMAT_INVALID;
    return SDL_GetGPUSwapchainTextureFormat(device, window);
}

// Texture upload

void
GPURenderer::uploadTexture(const u32 *pixels)
{
    if (!pixels || !transferBuf) return;

    void *mapped = SDL_MapGPUTransferBuffer(device, transferBuf, true);
    if (!mapped) return;

    std::memcpy(mapped, pixels, HPIXELS * VPIXELS * 4);
    SDL_UnmapGPUTransferBuffer(device, transferBuf);
    textureDirty = true;
}

// Frame rendering

void
GPURenderer::renderFrame(const SDL_FRect &srcRect, ImDrawData *drawData,
                         bool drawBackground)
{
    if (!device || !window) return;

    SDL_GPUCommandBuffer *cmdBuf = SDL_AcquireGPUCommandBuffer(device);
    if (!cmdBuf) return;

    // Upload emulator texture if dirty
    if (textureDirty) {
        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmdBuf);

        SDL_GPUTextureTransferInfo src = {};
        src.transfer_buffer = transferBuf;
        src.offset = 0;

        SDL_GPUTextureRegion dst = {};
        dst.texture = emuTexture;
        dst.w = HPIXELS;
        dst.h = VPIXELS;
        dst.d = 1;

        SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
        SDL_EndGPUCopyPass(copyPass);
        textureDirty = false;
    }

    // Prepare ImGui draw data (uploads vertex/index buffers to GPU)
    if (drawData) {
        ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmdBuf);
    }

    // Acquire swapchain texture
    SDL_GPUTexture *swapTex = nullptr;
    Uint32 swapW, swapH;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, window,
                                                &swapTex, &swapW, &swapH)) {
        SDL_SubmitGPUCommandBuffer(cmdBuf);
        return;
    }
    if (!swapTex || swapW == 0 || swapH == 0) {
        SDL_SubmitGPUCommandBuffer(cmdBuf);
        return;
    }

    // Begin render pass (clear to black)
    SDL_GPUColorTargetInfo colorTarget = {};
    colorTarget.texture = swapTex;
    colorTarget.clear_color = { 0, 0, 0, 1 };
    colorTarget.load_op = SDL_GPU_LOADOP_CLEAR;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(
        cmdBuf, &colorTarget, 1, nullptr);

    // Draw Amiga display as fullscreen triangle (only in non-decorated mode)
    if (drawBackground) {
        SDL_BindGPUGraphicsPipeline(renderPass, blitPipeline);

        // Compute UV coordinates from srcRect (pixel coords → normalized)
        BlitUniforms uniforms;
        uniforms.uvMinX = srcRect.x / static_cast<float>(HPIXELS);
        uniforms.uvMinY = srcRect.y / static_cast<float>(VPIXELS);
        uniforms.uvMaxX = (srcRect.x + srcRect.w) / static_cast<float>(HPIXELS);
        uniforms.uvMaxY = (srcRect.y + srcRect.h) / static_cast<float>(VPIXELS);

        SDL_PushGPUVertexUniformData(cmdBuf, 0, &uniforms, sizeof(uniforms));

        // Bind emulator texture + sampler to fragment shader slot 0
        SDL_GPUTextureSamplerBinding texBind = {};
        texBind.texture = emuTexture;
        texBind.sampler = samplerNearest;
        SDL_BindGPUFragmentSamplers(renderPass, 0, &texBind, 1);

        // Set viewport to maintain 4:3 aspect ratio
        float targetAspect = 4.0f / 3.0f;
        float vpW = static_cast<float>(swapW);
        float vpH = static_cast<float>(swapH);
        float fitW = vpW;
        float fitH = fitW / targetAspect;
        if (fitH > vpH) {
            fitH = vpH;
            fitW = fitH * targetAspect;
        }
        float vpX = (vpW - fitW) * 0.5f;
        float vpY = (vpH - fitH) * 0.5f;

        SDL_GPUViewport viewport = {};
        viewport.x = vpX;
        viewport.y = vpY;
        viewport.w = fitW;
        viewport.h = fitH;
        viewport.min_depth = 0;
        viewport.max_depth = 1;
        SDL_SetGPUViewport(renderPass, &viewport);

        // Draw fullscreen triangle (3 vertices, no vertex buffer)
        SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 0);

        // Reset viewport to full window for ImGui
        SDL_GPUViewport fullVp = {};
        fullVp.w = static_cast<float>(swapW);
        fullVp.h = static_cast<float>(swapH);
        fullVp.max_depth = 1;
        SDL_SetGPUViewport(renderPass, &fullVp);
    }

    // Draw ImGui overlay
    if (drawData) {
        ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmdBuf, renderPass);
    }

    SDL_EndGPURenderPass(renderPass);
    SDL_SubmitGPUCommandBuffer(cmdBuf);
}

// Shader helpers

SDL_GPUShader *
GPURenderer::createShader(SDL_GPUShaderStage stage,
                          const uint8_t *spirvCode, size_t spirvSize,
                          const char *mslCode, size_t mslSize,
                          int numSamplers, int numUniformBuffers)
{
    SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);

    SDL_GPUShaderCreateInfo info = {};
    info.stage = stage;
    info.num_samplers = static_cast<Uint32>(numSamplers);
    info.num_uniform_buffers = static_cast<Uint32>(numUniformBuffers);
    info.entrypoint = "main";

    if (formats & SDL_GPU_SHADERFORMAT_MSL) {
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.code = reinterpret_cast<const Uint8 *>(mslCode);
        info.code_size = mslSize;
        info.entrypoint = "main0";
    } else if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = spirvCode;
        info.code_size = spirvSize;
    } else {
        fprintf(stderr, "No supported shader format found\n");
        return nullptr;
    }

    SDL_GPUShader *shader = SDL_CreateGPUShader(device, &info);
    if (!shader) {
        fprintf(stderr, "SDL_CreateGPUShader failed: %s\n", SDL_GetError());
    }
    return shader;
}

bool
GPURenderer::createBlitPipeline()
{
    // Create shaders
    SDL_GPUShader *vertShader = createShader(
        SDL_GPU_SHADERSTAGE_VERTEX,
        shaders::blit_vert_spirv, shaders::blit_vert_spirv_size,
        shaders::blit_vert_msl, shaders::blit_vert_msl_size,
        0, 1);  // 0 samplers, 1 uniform buffer (push constants)

    SDL_GPUShader *fragShader = createShader(
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        shaders::blit_frag_spirv, shaders::blit_frag_spirv_size,
        shaders::blit_frag_msl, shaders::blit_frag_msl_size,
        1, 0);  // 1 sampler, 0 uniform buffers

    if (!vertShader || !fragShader) {
        if (vertShader) SDL_ReleaseGPUShader(device, vertShader);
        if (fragShader) SDL_ReleaseGPUShader(device, fragShader);
        return false;
    }

    // Pipeline create info
    SDL_GPUColorTargetDescription colorDesc = {};
    colorDesc.format = getSwapchainFormat();

    SDL_GPUGraphicsPipelineTargetInfo targetInfo = {};
    targetInfo.color_target_descriptions = &colorDesc;
    targetInfo.num_color_targets = 1;

    SDL_GPUGraphicsPipelineCreateInfo pipeInfo = {};
    pipeInfo.vertex_shader = vertShader;
    pipeInfo.fragment_shader = fragShader;
    pipeInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeInfo.target_info = targetInfo;

    // No vertex input (fullscreen triangle from gl_VertexIndex)
    pipeInfo.vertex_input_state = {};

    // Default rasterizer (no culling for fullscreen triangle)
    pipeInfo.rasterizer_state = {};
    pipeInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipeInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;

    blitPipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeInfo);

    // Release shader objects (pipeline retains them)
    SDL_ReleaseGPUShader(device, vertShader);
    SDL_ReleaseGPUShader(device, fragShader);

    if (!blitPipeline) {
        fprintf(stderr, "SDL_CreateGPUGraphicsPipeline failed: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

}
