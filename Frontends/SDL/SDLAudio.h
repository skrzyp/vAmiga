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
#include <array>
#include <atomic>

namespace vamiga {

class SDLAudio {

    SDL_AudioStream *stream = nullptr;
    std::atomic<VAmiga *> emu {nullptr};

    // Persistent buffer (avoids stack allocation in the audio callback)
    static constexpr int maxFrames = 4096;
    std::array<float, maxFrames * 2> buffer {};

public:

    SDLAudio() = default;
    ~SDLAudio();

    SDLAudio(const SDLAudio &) = delete;
    SDLAudio(SDLAudio &&) = delete;
    SDLAudio& operator= (const SDLAudio &) = delete;
    SDLAudio& operator= (SDLAudio &&) = delete;

    //
    // Lifecycle
    //

    int init(VAmiga &emulator, int requestedRate = 44100);
    void shutdown();

    // Clear buffered audio (call on focus change to avoid glitches)
    void flush();

private:

    //
    // Callbacks
    //

    static void SDLCALL callback(void *userdata, SDL_AudioStream *stream,
                                 int additionalAmount, int totalAmount);
};

}
