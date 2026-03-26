// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------
/// @file

#include "config.h"
#include "Dashboard.h"

#include <algorithm>
#include <cstdio>
#include <cmath>

namespace vamiga {

// Lifecycle

void
Dashboard::update()
{
    if (!visible) return;

    // Emulator performance
    auto em = emu.getMetrics();
    cpuLoad.push(static_cast<float>(em.cpuLoad * 100.0));
    amigaFps.push(static_cast<float>(em.fps));
    hostFps.push(ImGui::GetIO().Framerate);

    // Memory access activity (smoothed average from core)
    auto &m = emu.mem.getMetrics();
    chipR.push(static_cast<float>(m.chipReads.accumulated));
    chipW.push(static_cast<float>(m.chipWrites.accumulated));
    slowR.push(static_cast<float>(m.slowReads.accumulated));
    slowW.push(static_cast<float>(m.slowWrites.accumulated));
    fastR.push(static_cast<float>(m.fastReads.accumulated));
    fastW.push(static_cast<float>(m.fastWrites.accumulated));
    kickR.push(static_cast<float>(m.kickReads.accumulated));
    kickW.push(static_cast<float>(m.kickWrites.accumulated));

    // DMA channel activity (0.0–1.0 from core)
    auto &a = emu.agnus.getMetrics();
    copperDma.push(static_cast<float>(a.copperActivity));
    blitterDma.push(static_cast<float>(a.blitterActivity));
    diskDma.push(static_cast<float>(a.diskActivity));
    audioDma.push(static_cast<float>(a.audioActivity));
    spriteDma.push(static_cast<float>(a.spriteActivity));
    bitplaneDma.push(static_cast<float>(a.bitplaneActivity));

    // CIA activity (idle → active inversion, clamped)
    auto ca = emu.ciaA.getMetrics();
    auto cb = emu.ciaB.getMetrics();
    ciaA.push(std::clamp(static_cast<float>((1.0 - ca.idlePercentage) * 100.0), 0.0f, 100.0f));
    ciaB.push(std::clamp(static_cast<float>((1.0 - cb.idlePercentage) * 100.0), 0.0f, 100.0f));

    // Audio buffer fill level
    auto &au = emu.audioPort.getStats();
    audioFill.push(static_cast<float>(au.fillLevel * 100.0));
}

// Rendering

void
Dashboard::render()
{
    if (!visible) return;

    ImGui::SetNextWindowSize(ImVec2(680, 0), ImGuiCond_Once);

    if (!ImGui::Begin("Dashboard", &visible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    const float colW = (ImGui::GetContentRegionAvail().x -
                        ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f;

    // --- Memory Access Activity ---
    ImGui::SeparatorText("Memory Access");
    if (ImGui::BeginTable("mem", 4)) {
        for (int i = 0; i < 4; i++)
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        renderSparkline("Chip RAM", "R/W", chipR, 0, IM_COL32(100, 180, 255, 255), colW);
        ImGui::TableSetColumnIndex(1);
        renderSparkline("Slow RAM", "R/W", slowR, 0, IM_COL32(100, 180, 255, 255), colW);
        ImGui::TableSetColumnIndex(2);
        renderSparkline("Fast RAM", "R/W", fastR, 0, IM_COL32(100, 180, 255, 255), colW);
        ImGui::TableSetColumnIndex(3);
        renderSparkline("Kickstart", "R/W", kickR, 0, IM_COL32(130, 130, 255, 255), colW);

        ImGui::EndTable();
    }

    // --- DMA Activity ---
    ImGui::SeparatorText("DMA Activity");
    if (ImGui::BeginTable("dma", 3)) {
        for (int i = 0; i < 3; i++)
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

        float dmaW = (ImGui::GetContentRegionAvail().x -
                      ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        renderSparkline("Copper", "DMA", copperDma, 0, IM_COL32(255, 200, 80, 255), dmaW);
        ImGui::TableSetColumnIndex(1);
        renderSparkline("Blitter", "DMA", blitterDma, 0, IM_COL32(255, 160, 60, 255), dmaW);
        ImGui::TableSetColumnIndex(2);
        renderSparkline("Disk", "DMA", diskDma, 0, IM_COL32(255, 120, 40, 255), dmaW);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        renderSparkline("Audio", "DMA", audioDma, 0, IM_COL32(180, 255, 100, 255), dmaW);
        ImGui::TableSetColumnIndex(1);
        renderSparkline("Sprite", "DMA", spriteDma, 0, IM_COL32(255, 100, 200, 255), dmaW);
        ImGui::TableSetColumnIndex(2);
        renderSparkline("Bitplane", "DMA", bitplaneDma, 0, IM_COL32(100, 220, 255, 255), dmaW);

        ImGui::EndTable();
    }

    // --- Performance Gauges ---
    ImGui::SeparatorText("Performance");
    if (ImGui::BeginTable("perf", 4)) {
        for (int i = 0; i < 4; i++)
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        renderGauge("Host", "CPU %", cpuLoad.last, 100.0f);
        ImGui::TableSetColumnIndex(1);
        renderGauge("Host", "FPS", hostFps.last, 120.0f);
        ImGui::TableSetColumnIndex(2);
        renderGauge("Amiga", "FPS", amigaFps.last, 60.0f);
        ImGui::TableSetColumnIndex(3);
        renderGauge("Audio", "Fill %", audioFill.last, 100.0f);

        ImGui::EndTable();
    }

    // --- CIA Activity ---
    ImGui::SeparatorText("CIA Activity");
    if (ImGui::BeginTable("cia", 2)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);

        float ciaW = (ImGui::GetContentRegionAvail().x -
                      ImGui::GetStyle().ItemSpacing.x) / 2.0f;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        renderSparkline("CIA A", "Activity %", ciaA, 0, IM_COL32(200, 130, 255, 255), ciaW);
        ImGui::TableSetColumnIndex(1);
        renderSparkline("CIA B", "Activity %", ciaB, 0, IM_COL32(200, 130, 255, 255), ciaW);

        ImGui::EndTable();
    }

    ImGui::End();
}

// Drawing helpers

void
Dashboard::renderSparkline(const char *label, const char *sublabel,
                           History &h, float maxVal, ImU32 color,
                           float width)
{
    ImGui::PushID(label);
    ImGui::PushID(sublabel);

    ImGui::TextUnformatted(label);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
    ImGui::TextUnformatted(sublabel);
    ImGui::PopStyleColor();

    // Auto-range: zoom into min–max of actual data to show variation
    float yMin, yMax;
    if (maxVal > 0) {
        yMin = 0;
        yMax = maxVal;
    } else {
        yMin = h.trough();
        yMax = h.peak();
        float range = yMax - yMin;
        if (range < 0.001f) range = std::max(yMax * 0.1f, 1.0f);
        yMin = std::max(0.0f, yMin - range * 0.15f);
        yMax += range * 0.15f;
    }

    int numSamples = h.count > 0 ? h.count : 1;

    ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.12f, 1));
    ImGui::PlotHistogram("##plot", History::get, &h, numSamples,
                         0, nullptr, yMin, yMax,
                         ImVec2(width > 0 ? width : -FLT_MIN, 48));
    ImGui::PopStyleColor(3);

    // Current value overlay
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", h.last);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1));
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();

    ImGui::PopID();
    ImGui::PopID();
}

void
Dashboard::renderGauge(const char *label, const char *sublabel,
                       float value, float maxVal, float radius)
{
    ImGui::PushID(label);
    ImGui::PushID(sublabel);

    // Reserve space
    const float itemW = radius * 2 + 16;

    // Center the gauge in the available column width
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > itemW) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - itemW) * 0.5f);
    }

    // Label above
    ImGui::TextUnformatted(label);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1));
    ImGui::TextUnformatted(sublabel);
    ImGui::PopStyleColor();

    // Gauge position
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center(pos.x + itemW * 0.5f, pos.y + radius + 2);

    ImDrawList *dl = ImGui::GetWindowDrawList();

    // Arc parameters: 225° sweep (from 7:30 to 4:30 on a clock)
    const float startAngle = (float)M_PI * 0.75f;
    const float endAngle   = (float)M_PI * 2.25f;
    const float thickness  = 5.0f;
    const int segments     = 40;

    // Background arc
    dl->PathArcTo(center, radius, startAngle, endAngle, segments);
    dl->PathStroke(IM_COL32(50, 50, 50, 255), 0, thickness);

    // Value arc
    float frac = std::clamp(value / maxVal, 0.0f, 1.0f);
    if (frac > 0.01f) {
        float valueAngle = startAngle + frac * (endAngle - startAngle);
        dl->PathArcTo(center, radius, startAngle, valueAngle, segments);
        dl->PathStroke(valueColor(frac), 0, thickness);
    }

    // Tick marks at start and end
    auto tick = [&](float angle) {
        float c = cosf(angle), s = sinf(angle);
        float r0 = radius - 8, r1 = radius + 3;
        dl->AddLine(ImVec2(center.x + c * r0, center.y + s * r0),
                    ImVec2(center.x + c * r1, center.y + s * r1),
                    IM_COL32(100, 100, 100, 255), 1.5f);
    };
    tick(startAngle);
    tick(endAngle);

    // Value text (centered in gauge)
    char buf[32];
    if (value >= 10.0f)
        snprintf(buf, sizeof(buf), "%.0f", value);
    else
        snprintf(buf, sizeof(buf), "%.1f", value);

    float fontScale = 22.0f / ImGui::GetFontSize();
    float textW = ImGui::CalcTextSize(buf).x * fontScale;
    dl->AddText(nullptr, 22.0f,
                ImVec2(center.x - textW * 0.5f, center.y - 11.0f),
                IM_COL32(255, 255, 255, 255), buf);

    ImGui::Dummy(ImVec2(itemW, radius * 2 + 8));

    ImGui::PopID();
    ImGui::PopID();
}

ImU32
Dashboard::valueColor(float frac)
{
    // Green (0.0) → Yellow (0.5) → Red (1.0)
    int r, g;
    if (frac < 0.5f) {
        float t = frac * 2.0f;
        r = static_cast<int>(100 + t * 155);
        g = 220;
    } else {
        float t = (frac - 0.5f) * 2.0f;
        r = 255;
        g = static_cast<int>(220 - t * 180);
    }
    return IM_COL32(r, g, 40, 255);
}

}
