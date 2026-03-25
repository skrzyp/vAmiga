// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------
/// @file

#include "config.h"
#include "SDLAudio.h"
#include <algorithm>
#include <cstring>

namespace vamiga {

SDLAudio::~SDLAudio()
{
    shutdown();
}

int
SDLAudio::init(VAmiga &emulator, int requestedRate)
{
    emu.store(&emulator);

    SDL_AudioSpec spec {};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = requestedRate;

    stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &spec,
        callback,
        this);

    if (!stream) {
        fprintf(stderr, "SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
        return 0;
    }

    // Query the actual output format (second arg = output side of the stream)
    SDL_AudioSpec obtained {};
    SDL_GetAudioStreamFormat(stream, nullptr, &obtained);

    const int actualRate = obtained.freq != 0 ? obtained.freq : requestedRate;

    printf("Audio: %d Hz, %d channels, format 0x%x\n",
           actualRate, obtained.channels, obtained.format);

    // Tell the emulator the host sample rate
    emulator.set(Opt::HOST_SAMPLE_RATE, actualRate);

    // Start playback
    SDL_ResumeAudioStreamDevice(stream);

    return actualRate;
}

void
SDLAudio::shutdown()
{
    // SDL_DestroyAudioStream guarantees no callback is in-flight after it
    // returns, so it is safe to clear the emu pointer afterward.
    if (stream) {
        SDL_DestroyAudioStream(stream);
        stream = nullptr;
    }
    emu.store(nullptr);
}

void
SDLAudio::flush()
{
    if (stream) SDL_ClearAudioStream(stream);
}

void SDLCALL
SDLAudio::callback(void *userdata, SDL_AudioStream *stream,
                   int additionalAmount, int /*totalAmount*/)
{
    auto *self = static_cast<SDLAudio *>(userdata);
    if (!self) return;

    auto *emulator = self->emu.load();
    if (!emulator) return;

    // additionalAmount is in bytes. Each frame = 2 floats = 8 bytes.
    isize frames = static_cast<isize>(additionalAmount) / static_cast<isize>(2 * sizeof(float));
    if (frames <= 0) return;
    frames = std::min(frames, static_cast<isize>(maxFrames));

    isize copied = emulator->audioPort.copyInterleaved(self->buffer.data(), frames);

    // Zero-fill on underflow
    if (copied < frames) {
        std::memset(self->buffer.data() + copied * 2, 0,
                    static_cast<size_t>((frames - copied) * 2) * sizeof(float));
    }

    SDL_PutAudioStreamData(stream, self->buffer.data(),
                           static_cast<int>(frames * 2 * sizeof(float)));
}

}
