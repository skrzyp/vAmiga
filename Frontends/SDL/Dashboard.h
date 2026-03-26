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
#include <imgui.h>

namespace vamiga {

class Dashboard {

    VAmiga &emu;

    bool visible = false;

    // Ring buffer for time-series data
    static constexpr int kHistorySize = 120;

    struct History {
        float data[kHistorySize] = {};
        int offset = 0;
        int count = 0;
        float last = 0;

        void push(float v) {
            data[offset] = v;
            offset = (offset + 1) % kHistorySize;
            if (count < kHistorySize) count++;
            last = v;
        }
        static float get(void *d, int idx) {
            auto *h = static_cast<History *>(d);
            if (h->count < kHistorySize) return h->data[idx];
            return h->data[(h->offset + idx) % kHistorySize];
        }
        float peak() const {
            if (count == 0) return 1.0f;
            float m = 0;
            for (int i = 0; i < count; i++) {
                float v = data[(offset - 1 - i + kHistorySize) % kHistorySize];
                if (v > m) m = v;
            }
            return m > 0 ? m : 1.0f;
        }
        float trough() const {
            if (count == 0) return 0;
            float m = data[(offset - 1 + kHistorySize) % kHistorySize];
            for (int i = 1; i < count; i++) {
                float v = data[(offset - 1 - i + kHistorySize) % kHistorySize];
                if (v < m) m = v;
            }
            return m;
        }
    };

    // Memory access activity
    History chipR, chipW, slowR, slowW, fastR, fastW, kickR, kickW;

    // DMA channel activity (0.0–1.0)
    History copperDma, blitterDma, diskDma, audioDma, spriteDma, bitplaneDma;

    // Performance
    History cpuLoad, hostFps, amigaFps;

    // CIA activity (0–100%)
    History ciaA, ciaB;

    // Audio buffer
    History audioFill;

public:

    Dashboard(VAmiga &emu) : emu(emu) {}

    void open()  { visible = true; }
    void close() { visible = false; }
    bool isVisible() const { return visible; }

    // Sample metrics from the emulator (call once per frame)
    void update();

    // Render the dashboard window
    void render();

private:

    void renderSparkline(const char *label, const char *sublabel,
                         History &h, float maxVal, ImU32 color,
                         float width = 0);

    void renderGauge(const char *label, const char *sublabel,
                     float value, float maxVal,
                     float radius = 38.0f);

    static ImU32 valueColor(float frac);
};

}
