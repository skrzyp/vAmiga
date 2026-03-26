// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the Mozilla Public License v2
//
// See https://mozilla.org/MPL/2.0 for license information
// -----------------------------------------------------------------------------
/// @file

#include "config.h"
#include "ConfigPanel.h"
#include "Misc/RemoteServers/RemoteManagerTypes.h"

#include <imgui.h>
#include <cstdio>
#include <filesystem>

namespace vamiga {

// =========================================================================
// Combo entry tables (shared between UI and persistence)
// =========================================================================

// Labels match the macOS Swift GUI (from Settings.storyboard)
static const ComboEntry agnusRevs[] = {
    {"Early OCS", 0}, {"OCS", 1}, {"ECS (1 MB)", 2}, {"ECS (2 MB)", 3},
};
static const ComboEntry cpuRevs[] = {
    {"68000", 0}, {"68010", 1}, {"68EC020", 2},
};
static const ComboEntry cpuSpeeds[] = {
    {"7 MHz", 0}, {"14 MHz", 2}, {"21 MHz", 3}, {"28 MHz", 4},
    {"35 MHz", 5}, {"42 MHz", 6}, {"84 MHz", 8},
};
static const ComboEntry deniseRevs[] = {
    {"OCS", 0}, {"ECS", 1},
};
static const ComboEntry ciaRevs[] = {
    {"DIP", 0}, {"PLCC", 1},
};
static const ComboEntry rtcModels[] = {
    {"None", 0}, {"Oki", 1}, {"Ricoh", 2},
};
static const ComboEntry videoFormats[] = {
    {"PAL", 0}, {"NTSC", 1},
};
static const ComboEntry chipRam[] = {
    {"256 KB", 256}, {"512 KB", 512}, {"1 MB", 1024}, {"2 MB", 2048},
};
static const ComboEntry slowRam[] = {
    {"None", 0}, {"256 KB", 256}, {"512 KB", 512}, {"768 KB", 768},
    {"1 MB", 1024}, {"1.5 MB", 1536},
};
static const ComboEntry fastRam[] = {
    {"None", 0}, {"64 KB", 64}, {"128 KB", 128}, {"256 KB", 256},
    {"512 KB", 512}, {"1 MB", 1024}, {"2 MB", 2048}, {"4 MB", 4096},
    {"8 MB", 8192},
};
static const ComboEntry extStart[] = {
    {"$E00000", 0xE0}, {"$F00000", 0xF0},
};
static const ComboEntry bankMaps[] = {
    {"Amiga 500", 0}, {"Amiga 1000", 1}, {"Amiga 2000 A", 2}, {"Amiga 2000 B", 3},
};
static const ComboEntry unmapTypes[] = {
    {"Floating", 0}, {"All zeroes", 1}, {"All ones", 2},
};
static const ComboEntry ramInits[] = {
    {"All zeroes", 0}, {"All ones", 1}, {"Randomized", 2},
};
static const ComboEntry driveTypes[] = {
    {"3.5\" DD", 0}, {"3.5\" HD", 1}, {"5.25\" DD", 2},
};
static const ComboEntry dcSpeeds[] = {
    {"Original", 1}, {"Accelerated (2x)", 2}, {"Accelerated (4x)", 4},
    {"Accelerated (8x)", 8}, {"Infinite", -1},
};
static const ComboEntry serDevices[] = {
    {"No device", 0}, {"Null modem cable", 1}, {"Loopback cable", 2},
    {"MIDI Interface", 3}, {"RetroShell", 4}, {"Commander", 5},
};
static const ComboEntry warpModes[] = {
    {"During disk activity", 0}, {"Never", 1}, {"Always", 2},
};
static const ComboEntry blitterLevels[] = {
    {"0 - Move data word by word", 0},
    {"1 - Use up bus cycles", 1},
    {"2 - Full accuracy", 2},
};
static const ComboEntry filterTypes[] = {
    {"None", 0}, {"A500", 1}, {"A1000", 2}, {"A1200", 3},
    {"vAmiga", 4}, {"Low-Pass only", 5}, {"LED-Filter only", 6},
    {"High-Pass only", 7},
};
static const ComboEntry samplingMethods[] = {
    {"Off", 0}, {"Nearest", 1}, {"Linear", 2},
};
static const ComboEntry sampleRateModes[] = {
    {"Fixed", 0}, {"Adaptive", 1},
};
static const ComboEntry palettes[] = {
    {"Color", 0}, {"RGB Direct", 1}, {"Black and White", 2},
    {"Paper White", 3}, {"Green", 4}, {"Amber", 5}, {"Sepia", 6},
};
static const ComboEntry zooms[] = {
    {"Custom", 0}, {"Narrow", 1}, {"Wide", 2}, {"Extreme", 3},
};
static const ComboEntry centers[] = {
    {"Custom", 0}, {"Automatic", 1},
};
static const ComboEntry upscalers[] = {
    {"None", 0}, {"EPX", 1}, {"xBR", 2},
};
static const ComboEntry dotmasks[] = {
    {"None", 0}, {"Bisected", 1}, {"Trisected", 2},
    {"Bisected and shifted", 3}, {"Trisected and shifted", 4},
};
static const ComboEntry scanlines[] = {
    {"None", 0}, {"Embedded", 1}, {"Superimposed", 2},
};
static const ComboEntry blurModes[] = {
    {"Disabled", 0}, {"Enabled", 1},
};
static const ComboEntry flickerModes[] = {
    {"Never", 0}, {"In Interlace Mode", 1},
};
static const ComboEntry rayModes[] = {
    {"Aligned", 0}, {"Misaligned", 1},
};

// Helper macro for array+count from a static array
#define CE(arr) arr, (int)(sizeof(arr)/sizeof(arr[0]))

// =========================================================================
// Tab definitions (data-driven field arrays)
// =========================================================================

// --- Hardware (Chipset + RAM + Memory layout — matches macOS single "Hardware" tab) ---

static const Field hwChipsetFields[] = {
    FColBegin(),
    FSection("Chipset"),
    FCombo("Agnus:", Opt::AGNUS_REVISION, CE(agnusRevs), -1, true),
    FCombo("Denise:", Opt::DENISE_REVISION, CE(deniseRevs)),
    FCombo("CIAs:", Opt::CIA_REVISION, CE(ciaRevs), -2),
    FCombo("RTC:", Opt::RTC_MODEL, CE(rtcModels), -1, true),
    FCombo("Video:", Opt::AMIGA_VIDEO_FORMAT, CE(videoFormats)),
    FColNext(),
    FSection("CPU"),
    FCombo("CPU:", Opt::CPU_REVISION, CE(cpuRevs)),
    FCombo("Speed:", Opt::CPU_OVERCLOCKING, CE(cpuSpeeds)),
    FColEnd(),
};

static const Field hwMemFields[] = {
    FColBegin(),
    FSection("RAM"),
    FCombo("Chip Ram:", Opt::MEM_CHIP_RAM, CE(chipRam), -1, true),
    FCombo("Slow Ram:", Opt::MEM_SLOW_RAM, CE(slowRam), -1, true),
    FCombo("Fast Ram:", Opt::MEM_FAST_RAM, CE(fastRam), -1, true),
    FColNext(),
    FSection("Memory layout"),
    FCombo("Bank Map:", Opt::MEM_BANKMAP, CE(bankMaps)),
    FCombo("Unmapped area:", Opt::MEM_UNMAPPING_TYPE, CE(unmapTypes)),
    FCombo("Init Pattern:", Opt::MEM_RAM_INIT_PATTERN, CE(ramInits)),
    FCombo("Ext ROM:", Opt::MEM_EXT_START, CE(extStart), -1, true),
    FCheck("Save ROMs in snapshots", Opt::MEM_SAVE_ROMS),
    FColEnd(),
};

// --- Performance ---

static const Field perfFields[] = {
    FColBegin(),
    FSection("Warp mode"),
    FCombo("Activation:", Opt::AMIGA_WARP_MODE, CE(warpModes)),
    FSlider("Boot in warp mode for", Opt::AMIGA_WARP_BOOT, 0, 10, -1, false, "%d seconds"),
    FSection("Threading"),
    FCheck("VSYNC", Opt::AMIGA_VSYNC),
    FSlider("Speed:", Opt::AMIGA_SPEED_BOOST, 50, 200, -1, false, "%d %%"),
    FSlider("Run ahead:", Opt::AMIGA_RUN_AHEAD, 0, 7, -1, false, "%d frames"),
    FColNext(),
    FSection("Speed Boosters"),
    FCheck("Put idle CIAs to sleep", Opt::CIA_IDLE_SLEEP, -2),
    FCheck("Put idle audio backend to sleep", Opt::AUD_FASTPATH),
    FCheck("Reduce frame rate in warp mode", Opt::DENISE_FRAME_SKIPPING),
    FSection("Compression"),
    FCheck("Compress workspaces", Opt::AMIGA_WS_COMPRESSION),
    FColEnd(),
};

// --- Compatibility ---

static const Field compatFields[] = {
    FColBegin(),
    FSection("Floppy drives"),
    FCheck("Emulate mechanical delays", Opt::DRIVE_MECHANICS),
    FCombo("Speed:", Opt::DC_SPEED, CE(dcSpeeds)),
    FCheck("Ignore writes to DSKSYNC", Opt::DC_LOCK_DSKSYNC),
    FCheck("Always find a SYNC mark", Opt::DC_AUTO_DSKSYNC),
    FSection("Chipset Features"),
    FCheck("Emulate Slow Ram mirror", Opt::MEM_SLOW_RAM_MIRROR),
    FCheck("Emulate Slow Ram bus delays", Opt::MEM_SLOW_RAM_DELAY),
    FCheck("Emulate dropped register writes", Opt::AGNUS_PTR_DROPS),
    FCombo("Blitter Accuracy", Opt::BLITTER_ACCURACY, CE(blitterLevels)),
    FColNext(),
    FSection("Timing"),
    FCheck("Sync CIA accesses with E-clock", Opt::CIA_ECLOCK_SYNCING, -2),
    FCheck("Emulate TOD bug", Opt::CIA_TODBUG, -2),
    FSection("Keyboard"),
    FCheck("Transmit keycodes bit by bit", Opt::KBD_ACCURACY),
    FSection("Sprites"),
    FCheck("Detect Sprite-Sprite collisions", Opt::DENISE_CLX_SPR_SPR),
    FCheck("Detect Sprite-Playfield collisions", Opt::DENISE_CLX_SPR_PLF),
    FCheck("Detect Playfield-Playfield collisions", Opt::DENISE_CLX_PLF_PLF),
    FColEnd(),
};

// --- Audio ---

static const Field audioFields[] = {
    FColBegin(),
    FSection("Channel Volumes"),
    FSlider("Channel 0", Opt::AUD_VOL0, 0, 100),
    FSlider("Channel 1", Opt::AUD_VOL1, 0, 100),
    FSlider("Channel 2", Opt::AUD_VOL2, 0, 100),
    FSlider("Channel 3", Opt::AUD_VOL3, 0, 100),
    FSection("Channel Panning"),
    FSlider("Channel 0", Opt::AUD_PAN0, 0, 400),
    FSlider("Channel 1", Opt::AUD_PAN1, 0, 400),
    FSlider("Channel 2", Opt::AUD_PAN2, 0, 400),
    FSlider("Channel 3", Opt::AUD_PAN3, 0, 400),
    FColNext(),
    FSection("Master Volume"),
    FSlider("Left",  Opt::AUD_VOLL, 0, 100),
    FSlider("Right", Opt::AUD_VOLR, 0, 100),
    FSection("Processing"),
    FCombo("Filter:", Opt::AUD_FILTER_TYPE, CE(filterTypes)),
    FCombo("Interpolation:", Opt::AUD_SAMPLING_METHOD, CE(samplingMethods)),
    FCombo("Sample Rate:", Opt::AUD_ASR, CE(sampleRateModes)),
    FSlider("Buffer:", Opt::AUD_BUFFER_SIZE, 256, 16384, -1, false, "%d samples"),
    FColEnd(),
};

// --- Video ---

// Video Monitor sub-tab (color + geometry)
static const Field videoMonitorFields[] = {
    FColBegin(),
    FSection("Color"),
    FCombo("Palette:", Opt::MON_PALETTE, CE(palettes)),
    FSlider("Brightness", Opt::MON_BRIGHTNESS, 0, 100),
    FSlider("Contrast", Opt::MON_CONTRAST, 0, 200),
    FSlider("Saturation", Opt::MON_SATURATION, 0, 200),
    FColNext(),
    FSection("Geometry"),
    FCombo("Zoom:", Opt::MON_ZOOM, CE(zooms)),
    FSlider("H Zoom", Opt::MON_HZOOM, 0, 2000),
    FSlider("V Zoom", Opt::MON_VZOOM, 0, 2000),
    FCombo("Center:", Opt::MON_CENTER, CE(centers)),
    FSlider("H Center", Opt::MON_HCENTER, 0, 2000),
    FSlider("V Center", Opt::MON_VCENTER, 0, 2000),
    FColEnd(),
};

// Video Effects sub-tab (shader effects — stored in core for future GPU backend)
static const Field videoEffectsFields[] = {
    FColBegin(),
    FSection("Post-Processing"),
    FCombo("Blur:", Opt::MON_BLUR, CE(blurModes)),
    FSlider("Radius:", Opt::MON_BLUR_RADIUS, 0, 1000),
    FCombo("Bloom:", Opt::MON_BLOOM, CE(blurModes)),
    FSlider("Radius:", Opt::MON_BLOOM_RADIUS, 0, 1000),
    FSlider("Brightness:", Opt::MON_BLOOM_BRIGHTNESS, 0, 1000),
    FSlider("Intensity:", Opt::MON_BLOOM_WEIGHT, 0, 1000),
    FCombo("Flicker:", Opt::MON_FLICKER, CE(flickerModes)),
    FSlider("Weight:", Opt::MON_FLICKER_WEIGHT, 0, 1000),
    FColNext(),
    FSection("Upscaling"),
    FCombo("Enhancer:", Opt::MON_ENHANCER, CE(upscalers)),
    FCombo("Upscaler:", Opt::MON_UPSCALER, CE(upscalers)),
    FCombo("Scanlines:", Opt::MON_SCANLINES, CE(scanlines)),
    FSlider("Brightness:", Opt::MON_SCANLINE_BRIGHTNESS, 0, 1000),
    FSlider("Weight:", Opt::MON_SCANLINE_WEIGHT, 0, 1000),
    FCombo("Dot mask:", Opt::MON_DOTMASK, CE(dotmasks)),
    FSlider("Brightness:", Opt::MON_DOTMASK_BRIGHTNESS, 0, 1000),
    FCombo("Rays:", Opt::MON_DISALIGNMENT, CE(rayModes)),
    FSlider("H Shift:", Opt::MON_DISALIGNMENT_H, 0, 1000),
    FSlider("V Shift:", Opt::MON_DISALIGNMENT_V, 0, 1000),
    FColEnd(),
};

// =========================================================================
// Persistence tables — all Opt:: values that should be saved/loaded
// =========================================================================

static const Opt singleOpts[] = {
    Opt::AMIGA_VIDEO_FORMAT, Opt::CPU_REVISION, Opt::CPU_OVERCLOCKING,
    Opt::AGNUS_REVISION, Opt::AGNUS_PTR_DROPS, Opt::DENISE_REVISION,
    Opt::RTC_MODEL,
    Opt::MEM_CHIP_RAM, Opt::MEM_SLOW_RAM, Opt::MEM_FAST_RAM,
    Opt::MEM_EXT_START, Opt::MEM_BANKMAP, Opt::MEM_UNMAPPING_TYPE,
    Opt::MEM_RAM_INIT_PATTERN, Opt::MEM_SLOW_RAM_DELAY,
    Opt::MEM_SLOW_RAM_MIRROR, Opt::MEM_SAVE_ROMS,
    Opt::DC_SPEED, Opt::DC_LOCK_DSKSYNC, Opt::DC_AUTO_DSKSYNC,
    Opt::AUD_VOL0, Opt::AUD_VOL1, Opt::AUD_VOL2, Opt::AUD_VOL3,
    Opt::AUD_PAN0, Opt::AUD_PAN1, Opt::AUD_PAN2, Opt::AUD_PAN3,
    Opt::AUD_VOLL, Opt::AUD_VOLR, Opt::AUD_FILTER_TYPE,
    Opt::AUD_SAMPLING_METHOD, Opt::AUD_BUFFER_SIZE,
    Opt::AUD_ASR, Opt::AUD_FASTPATH,
    Opt::MON_PALETTE, Opt::MON_BRIGHTNESS, Opt::MON_CONTRAST,
    Opt::MON_SATURATION, Opt::VID_WHITE_NOISE,
    Opt::MON_FLICKER, Opt::MON_FLICKER_WEIGHT,
    Opt::MON_ZOOM, Opt::MON_HZOOM, Opt::MON_VZOOM,
    Opt::MON_CENTER, Opt::MON_HCENTER, Opt::MON_VCENTER,
    Opt::MON_ENHANCER, Opt::MON_UPSCALER,
    Opt::MON_BLUR, Opt::MON_BLUR_RADIUS,
    Opt::MON_BLOOM, Opt::MON_BLOOM_RADIUS,
    Opt::MON_BLOOM_BRIGHTNESS, Opt::MON_BLOOM_WEIGHT,
    Opt::MON_DOTMASK, Opt::MON_DOTMASK_BRIGHTNESS,
    Opt::MON_SCANLINES, Opt::MON_SCANLINE_BRIGHTNESS,
    Opt::MON_SCANLINE_WEIGHT,
    Opt::MON_DISALIGNMENT, Opt::MON_DISALIGNMENT_H,
    Opt::MON_DISALIGNMENT_V,
    Opt::AMIGA_WARP_MODE, Opt::AMIGA_WARP_BOOT, Opt::AMIGA_VSYNC,
    Opt::AMIGA_SPEED_BOOST, Opt::AMIGA_RUN_AHEAD,
    Opt::AMIGA_WS_COMPRESSION, Opt::DENISE_FRAME_SKIPPING,
    Opt::BLITTER_ACCURACY, Opt::KBD_ACCURACY,
    Opt::DENISE_CLX_SPR_SPR, Opt::DENISE_CLX_SPR_PLF,
    Opt::DENISE_CLX_PLF_PLF,
    Opt::SER_DEVICE,
};

struct IdOpt { Opt opt; int count; };
static const IdOpt idOpts2[] = {
    {Opt::CIA_REVISION,2}, {Opt::CIA_IDLE_SLEEP,2},
    {Opt::CIA_TODBUG,2}, {Opt::CIA_ECLOCK_SYNCING,2},
    {Opt::MOUSE_VELOCITY,2}, {Opt::MOUSE_PULLUP_RESISTORS,2},
    {Opt::MOUSE_SHAKE_DETECTION,2},
    {Opt::JOY_AUTOFIRE,2}, {Opt::JOY_AUTOFIRE_BURSTS,2},
    {Opt::JOY_AUTOFIRE_BULLETS,2}, {Opt::JOY_AUTOFIRE_DELAY,2},
};
static const IdOpt idOpts4[] = {
    {Opt::DRIVE_CONNECT,4}, {Opt::DRIVE_TYPE,4},
    {Opt::DRIVE_MECHANICS,4}, {Opt::DRIVE_RPM,4},
    {Opt::DRIVE_PAN,4}, {Opt::DRIVE_STEP_VOLUME,4},
    {Opt::DRIVE_POLL_VOLUME,4}, {Opt::DRIVE_INSERT_VOLUME,4},
    {Opt::DRIVE_EJECT_VOLUME,4},
    {Opt::HDC_CONNECT,4}, {Opt::HDR_STEP_VOLUME,4}, {Opt::HDR_PAN,4},
};
static const i64 serverTypes[] = {
    static_cast<i64>(ServerType::RSH), static_cast<i64>(ServerType::RPC),
    static_cast<i64>(ServerType::GDB), static_cast<i64>(ServerType::PROM),
    static_cast<i64>(ServerType::SER),
};

// =========================================================================
// ConfigPanel implementation
// =========================================================================

static constexpr float kLabelW = 170.0f;

ConfigPanel::ConfigPanel(VAmiga &emu, SDL_Window *window, GamepadSlot *gamepads,
                         PortDevice *portDevice, bool *disconnectKeys)
    : emu(emu), window(window), gamepads(gamepads),
      portDevice(portDevice), disconnectKeys(disconnectKeys)
{
    initSettingsPath();
}

void
ConfigPanel::open()
{
    visible = true;
    maxWindowH = 0;  // Re-measure on next render
    refreshRomInfo();
}

void
ConfigPanel::close()
{
    visible = false;
}

bool
ConfigPanel::isPoweredOn() const
{
    return emu.isPoweredOn();
}

bool
ConfigPanel::trySet(Opt opt, i64 value)
{
    try { emu.set(opt, value); return true; }
    catch (std::exception &e) { setStatus(e.what()); return false; }
}

bool
ConfigPanel::trySet(Opt opt, i64 value, i64 id)
{
    try { emu.set(opt, value, id); return true; }
    catch (std::exception &e) { setStatus(e.what()); return false; }
}

void
ConfigPanel::setStatus(const char *msg, float duration)
{
    statusMsg = msg;
    statusTimer = duration;
}

void
ConfigPanel::refreshRomInfo()
{
    try {
        auto &t = emu.mem.getRomTraits();
        romTitle = t.title ? t.title : "";
        romVersion = t.revision ? t.revision : "";
        hasRom = !romTitle.empty();
    } catch (...) { romTitle.clear(); romVersion.clear(); hasRom = false; }

    try {
        auto &t = emu.mem.getExtTraits();
        extTitle = t.title ? t.title : "";
        hasExt = !extTitle.empty();
    } catch (...) { extTitle.clear(); hasExt = false; }
}

// =========================================================================
// Generic form renderer
// =========================================================================

void
ConfigPanel::renderFields(const Field *fields, int count)
{
    const bool powered = isPoweredOn();
    bool inTable = false;

    // Use the field array pointer as a unique seed for child window IDs
    // (each static field array has a unique address)
    const ImGuiID childIdBase = ImGui::GetID(static_cast<const void *>(fields));

    // Power lock warning is now in the footer bar — no per-section warning

    for (int i = 0; i < count; i++) {
        const Field &f = fields[i];

        // Layout fields don't get PushID — they change the ID scope
        // (BeginChild creates a new scope, so PushID/PopID would mismatch)
        if (f.type == FT::Section || f.type == FT::ColBegin ||
            f.type == FT::ColNext || f.type == FT::ColEnd) {

            switch (f.type) {
                case FT::Section:
                    if (inTable) { ImGui::EndTable(); inTable = false; }
                    ImGui::SeparatorText(f.label);
                    break;
                case FT::ColBegin: {
                    if (inTable) { ImGui::EndTable(); inTable = false; }
                    float halfW = (ImGui::GetContentRegionAvail().x -
                                   ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                    ImGui::BeginGroup();
                    ImGui::BeginChild(childIdBase, ImVec2(halfW, 0),
                                      ImGuiChildFlags_AutoResizeY);
                    break;
                }
                case FT::ColNext:
                    if (inTable) { ImGui::EndTable(); inTable = false; }
                    ImGui::EndChild();
                    ImGui::EndGroup();
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::BeginChild(childIdBase + 1,
                        ImVec2(ImGui::GetContentRegionAvail().x, 0),
                        ImGuiChildFlags_AutoResizeY);
                    break;
                case FT::ColEnd:
                    if (inTable) { ImGui::EndTable(); inTable = false; }
                    ImGui::EndChild();
                    ImGui::EndGroup();
                    break;
                default: break;
            }
            continue;
        }

        // Manage table state BEFORE PushID to keep BeginTable/EndTable
        // at the same ID stack depth (BeginTable pushes an internal ID)
        bool needsTable = (f.type == FT::Combo || f.type == FT::Slider);
        if (!needsTable && inTable) {
            ImGui::EndTable(); inTable = false;
        }
        if (needsTable && !inTable) {
            if (ImGui::BeginTable("##form", 2)) {
                ImGui::TableSetupColumn("L",
                    ImGuiTableColumnFlags_WidthFixed, kLabelW);
                ImGui::TableSetupColumn("W",
                    ImGuiTableColumnFlags_WidthStretch);
                inTable = true;
            }
        }

        ImGui::PushID(i);

        auto applyValue = [&](i64 val) {
            i64 id = f.id;
            if (id == -2) { trySet(f.opt, val, 0); trySet(f.opt, val, 1); }
            else if (id >= 0) { trySet(f.opt, val, id); }
            else { trySet(f.opt, val); }
        };

        switch (f.type) {

            case FT::Check: {
                bool val = emu.get(f.opt, f.id >= 0 ? f.id : 0);
                ImGui::BeginDisabled(f.locked && powered);
                if (ImGui::Checkbox(f.label, &val)) applyValue(val);
                ImGui::EndDisabled();
                break;
            }

            case FT::Combo: {
                if (!inTable) break;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(f.label);
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);

                i64 cur = emu.get(f.opt, f.id >= 0 ? f.id : 0);
                int sel = 0;
                for (int j = 0; j < f.entryCount; j++) {
                    if (f.entries[j].value == cur) { sel = j; break; }
                }

                ImGui::BeginDisabled(f.locked && powered);
                if (ImGui::BeginCombo("##c", f.entries[sel].label)) {
                    for (int j = 0; j < f.entryCount; j++) {
                        bool selected = (j == sel);
                        if (ImGui::Selectable(f.entries[j].label, selected))
                            applyValue(f.entries[j].value);
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndDisabled();
                break;
            }

            case FT::Slider: {
                if (!inTable) break;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(f.label);
                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);

                int val = static_cast<int>(emu.get(f.opt, f.id >= 0 ? f.id : 0));
                ImGui::BeginDisabled(f.locked && powered);
                if (ImGui::SliderInt("##s", &val, f.min, f.max, f.fmt))
                    applyValue(val);
                ImGui::EndDisabled();
                break;
            }

            default: break;
        }

        ImGui::PopID();
    }

    // Close any open table
    if (inTable) ImGui::EndTable();
}

// =========================================================================
// Main render
// =========================================================================

void
ConfigPanel::render()
{
    if (!visible) return;

    // Fixed width, auto height — track max height to prevent shrinking
    constexpr float kWindowW = 680;
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kWindowW, maxWindowH > 0 ? maxWindowH : 200),
        ImVec2(kWindowW, FLT_MAX));

    if (!ImGui::Begin("Configuration", &visible, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    processPendingFileResult();
    if (++romPollCounter >= 60) { romPollCounter = 0; refreshRomInfo(); }

    if (ImGui::BeginTabBar("ConfigTabs")) {

        if (ImGui::BeginTabItem("Hardware"))      { renderHardwareTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Peripherals"))   { renderPeripheralsTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Roms"))          { renderRomTab(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Performance"))   { renderFields(perfFields, IM_ARRAYSIZE(perfFields)); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Compatibility")) { renderFields(compatFields, IM_ARRAYSIZE(compatFields)); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Audio"))         { renderFields(audioFields, IM_ARRAYSIZE(audioFields)); ImGui::EndTabItem(); }

        if (ImGui::BeginTabItem("Video")) {
            if (ImGui::BeginTabBar("VideoSub")) {
                if (ImGui::BeginTabItem("Monitor")) {
                    renderFields(videoMonitorFields, IM_ARRAYSIZE(videoMonitorFields));
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Effects")) {
                    renderFields(videoEffectsFields, IM_ARRAYSIZE(videoEffectsFields));
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Server")) {
            ImGui::SeparatorText("Remote Servers");
            struct SrvEntry { const char *name; i64 type; };
            const SrvEntry servers[] = {
                {"RetroShell", static_cast<i64>(ServerType::RSH)},
                {"RPC",        static_cast<i64>(ServerType::RPC)},
                {"GDB",        static_cast<i64>(ServerType::GDB)},
                {"Prometheus", static_cast<i64>(ServerType::PROM)},
                {"Serial",     static_cast<i64>(ServerType::SER)},
            };
            if (ImGui::BeginTable("srv", 3)) {
                ImGui::TableSetupColumn("Enable",  ImGuiTableColumnFlags_WidthFixed, 130);
                ImGui::TableSetupColumn("Port",    ImGuiTableColumnFlags_WidthFixed, 120);
                ImGui::TableSetupColumn("Verbose", ImGuiTableColumnFlags_WidthStretch);
                for (auto &srv : servers) {
                    ImGui::PushID(static_cast<int>(srv.type));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    bool en = emu.get(Opt::SRV_ENABLE, srv.type);
                    if (ImGui::Checkbox(srv.name, &en)) trySet(Opt::SRV_ENABLE, en, srv.type);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    int port = static_cast<int>(emu.get(Opt::SRV_PORT, srv.type));
                    if (ImGui::InputInt("##p", &port, 0, 0))
                        if (port > 0 && port < 65536) trySet(Opt::SRV_PORT, port, srv.type);
                    ImGui::TableSetColumnIndex(2);
                    bool vb = emu.get(Opt::SRV_VERBOSE, srv.type);
                    if (ImGui::Checkbox("Verbose##v", &vb)) trySet(Opt::SRV_VERBOSE, vb, srv.type);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Pin footer to bottom: fill remaining space above the footer
    const float footerReserve = ImGui::GetFrameHeightWithSpacing() +
                                 ImGui::GetStyle().ItemSpacing.y;
    float avail = ImGui::GetContentRegionAvail().y;
    if (avail > footerReserve) {
        ImGui::Dummy(ImVec2(0, avail - footerReserve));
    }

    // Pinned footer
    ImGui::Separator();
    if (ImGui::Button("Save Settings")) saveSettings();
    ImGui::SameLine();
    if (ImGui::Button("Reset to Defaults")) resetToDefaults();

    // Power lock warning or status message — right-aligned
    if (isPoweredOn()) {
        // Right-align the lock warning
        const char *lockText = "Some options locked while Amiga is on.";
        float lockW = ImGui::CalcTextSize(lockText).x +
                       ImGui::CalcTextSize("Power Off").x +
                       ImGui::GetStyle().FramePadding.x * 2 +
                       ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - lockW +
                         ImGui::GetCursorPosX());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.8f, 0.3f, 1));
        ImGui::TextUnformatted(lockText);
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Power Off")) {
            emu.powerOff();
            setStatus("Powered off. Locked options are now editable.");
        }
    } else if (statusTimer > 0) {
        statusTimer -= ImGui::GetIO().DeltaTime;
        if (statusTimer < 0) statusTimer = 0;
        float alpha = statusTimer > 1.0f ? 1.0f : statusTimer;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.8f, 0.3f, alpha));
        ImGui::TextUnformatted(statusMsg.c_str());
        ImGui::PopStyleColor();
    }

    // Track max window height so the window never shrinks when switching tabs
    float h = ImGui::GetWindowSize().y;
    if (h > maxWindowH) maxWindowH = h;

    ImGui::End();
}

// =========================================================================
// Custom tab renderers (for tabs requiring non-generic UI)
// =========================================================================

void
ConfigPanel::renderHardwareTab()
{
    // Machine preset — inline label + combo (like macOS "Revert to...")
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Revert to:");
    ImGui::SameLine();
    static const ComboEntry presets[] = {
        {"Amiga 500", 1}, {"Amiga 1000", 0},
        {"Amiga 2000", 2}, {"Amiga 500+", 3},
    };
    ImGui::SetNextItemWidth(200);
    if (ImGui::BeginCombo("##preset", "Select preset...")) {
        for (int j = 0; j < IM_ARRAYSIZE(presets); j++) {
            if (ImGui::Selectable(presets[j].label)) {
                bool wasPowered = isPoweredOn();
                bool wasRunning = emu.isRunning();
                try {
                    emu.powerOff();
                    emu.set(static_cast<ConfigScheme>(presets[j].value));
                    if (wasPowered) { emu.powerOn(); if (wasRunning) emu.run(); }
                    setStatus("Machine preset applied.", 2.0f);
                } catch (std::exception &e) {
                    setStatus((std::string("Preset failed: ") + e.what()).c_str(), 4.0f);
                }
            }
        }
        ImGui::EndCombo();
    }

    // Chipset (two-column like macOS)
    renderFields(hwChipsetFields, IM_ARRAYSIZE(hwChipsetFields));

    // RAM | Memory layout (side by side)
    renderFields(hwMemFields, IM_ARRAYSIZE(hwMemFields));
}

void
ConfigPanel::renderRomTab()
{
    float halfW = (ImGui::GetContentRegionAvail().x -
                   ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    ImGui::BeginGroup();
    ImGui::BeginChild(ImGui::GetID("rom_l"), ImVec2(halfW, 0),
                      ImGuiChildFlags_AutoResizeY);

    ImGui::SeparatorText("Kickstart Rom");
    if (hasRom) {
        try {
            auto &t = emu.mem.getRomTraits();
            if (t.title) ImGui::Text("Title:    %s", t.title);
            if (t.revision) ImGui::Text("Revision: %s", t.revision);
            if (t.released) ImGui::Text("Released: %s", t.released);
            if (t.model) ImGui::Text("Model:    %s", t.model);
            ImGui::Text("CRC:      %08X", t.crc);
        } catch (...) {
            ImGui::TextUnformatted(romTitle.c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No ROM loaded");
    }
    ImGui::Spacing();
    if (ImGui::Button("Load ROM..."))
        openFileDialog(PendingDialog::ROM, "ROM files", "rom;bin;kick");

    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::BeginChild(ImGui::GetID("rom_r"), ImVec2(halfW, 0),
                      ImGuiChildFlags_AutoResizeY);

    ImGui::SeparatorText("Extension Rom");
    if (hasExt) {
        try {
            auto &t = emu.mem.getExtTraits();
            if (t.title) ImGui::Text("Title:    %s", t.title);
            if (t.revision) ImGui::Text("Revision: %s", t.revision);
            if (t.crc) ImGui::Text("CRC:      %08X", t.crc);
        } catch (...) {
            ImGui::TextUnformatted(extTitle.c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "No extension ROM");
    }
    ImGui::Spacing();
    if (ImGui::Button("Load Extension ROM..."))
        openFileDialog(PendingDialog::ExtROM, "ROM files", "rom;bin");

    ImGui::EndChild();
    ImGui::EndGroup();
}

void
ConfigPanel::renderDrivesTab()
{
    float halfW = (ImGui::GetContentRegionAvail().x -
                   ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    ImGui::BeginGroup();
    ImGui::BeginChild(ImGui::GetID("drv_l"), ImVec2(halfW, 0),
                      ImGuiChildFlags_AutoResizeY);

    ImGui::SeparatorText("Floppy Drives");

    for (int n = 0; n < 4; n++) {
        ImGui::PushID(n);
        char label[8]; snprintf(label, sizeof(label), "DF%d:", n);

        if (n == 0) {
            // DF0 always connected — label + inline type combo
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            i64 cur = emu.get(Opt::DRIVE_TYPE, 0);
            int sel = 0;
            for (int j = 0; j < IM_ARRAYSIZE(driveTypes); j++)
                if (driveTypes[j].value == cur) sel = j;
            if (ImGui::BeginCombo("##type", driveTypes[sel].label)) {
                for (int j = 0; j < IM_ARRAYSIZE(driveTypes); j++) {
                    bool selected = (j == sel);
                    if (ImGui::Selectable(driveTypes[j].label, selected))
                        trySet(Opt::DRIVE_TYPE, driveTypes[j].value, 0);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        } else {
            bool conn = emu.get(Opt::DRIVE_CONNECT, n);
            if (ImGui::Checkbox(label, &conn))
                trySet(Opt::DRIVE_CONNECT, conn, n);
            if (conn) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                i64 cur = emu.get(Opt::DRIVE_TYPE, n);
                int sel = 0;
                for (int j = 0; j < IM_ARRAYSIZE(driveTypes); j++)
                    if (driveTypes[j].value == cur) sel = j;
                if (ImGui::BeginCombo("##type", driveTypes[sel].label)) {
                    for (int j = 0; j < IM_ARRAYSIZE(driveTypes); j++) {
                        bool selected = (j == sel);
                        if (ImGui::Selectable(driveTypes[j].label, selected))
                            trySet(Opt::DRIVE_TYPE, driveTypes[j].value, n);
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::BeginChild(ImGui::GetID("drv_r"), ImVec2(halfW, 0),
                      ImGuiChildFlags_AutoResizeY);

    ImGui::SeparatorText("Hard Drives");

    for (int n = 0; n < 4; n++) {
        ImGui::PushID(100 + n);
        char hdl[16]; snprintf(hdl, sizeof(hdl), "HD%d:", n);
        bool hdc = emu.get(Opt::HDC_CONNECT, n);
        if (ImGui::Checkbox(hdl, &hdc)) trySet(Opt::HDC_CONNECT, hdc, n);
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::EndGroup();

    // Disk operations (below columns)
    ImGui::SeparatorText("Disk Operations");
    for (int n = 0; n < 4; n++) {
        if (!emu.get(Opt::DRIVE_CONNECT, n)) continue;
        ImGui::PushID(200 + n);
        auto info = emu.df[n]->getInfo();
        char dfLabel[8]; snprintf(dfLabel, sizeof(dfLabel), "DF%d", n);
        ImGui::TextUnformatted(dfLabel);
        ImGui::SameLine();
        if (info.hasDisk) {
            ImGui::Text("inserted%s",
                        info.hasProtectedDisk ? " (WP)" : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Eject")) emu.df[n]->ejectDisk();
            ImGui::SameLine();
            if (ImGui::SmallButton("WP"))
                emu.put(Cmd::DSK_TOGGLE_WP, static_cast<i64>(n));
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("Insert..."))
            openFileDialog(
                static_cast<PendingDialog>(
                    static_cast<int>(PendingDialog::Disk0) + n),
                "Disk images", "adf;adz;dms;img;ima;exe");
        ImGui::PopID();
    }
}

void
ConfigPanel::renderPeripheralsTab()
{
    // Floppy + Hard drives section
    renderDrivesTab();

    ImGui::Spacing();

    // Game port device assignment
    renderGamePortsSection();

    // Game Ports + Joystick autofire
    ImGui::SeparatorText("Game Ports");
    for (int port = 0; port < 2; port++) {
        ImGui::PushID(port);
        char lbl[24]; snprintf(lbl, sizeof(lbl), "Auto-fire (Port %d)", port + 1);
        bool autofire = emu.get(Opt::JOY_AUTOFIRE, port);
        if (ImGui::Checkbox(lbl, &autofire))
            trySet(Opt::JOY_AUTOFIRE, autofire, port);

        if (autofire) {
            bool bursts = emu.get(Opt::JOY_AUTOFIRE_BURSTS, port);
            if (ImGui::Checkbox("Burst Mode", &bursts))
                trySet(Opt::JOY_AUTOFIRE_BURSTS, bursts, port);

            Field f = FSlider("Speed", Opt::JOY_AUTOFIRE_DELAY, 1, 50, port);
            renderFields(&f, 1);

            if (bursts) {
                f = FSlider("Bullets per burst", Opt::JOY_AUTOFIRE_BULLETS, 1, 10, port);
                renderFields(&f, 1);
            }
        }
        ImGui::PopID();
    }

    // Serial Port
    ImGui::SeparatorText("Serial Port");
    static const Field serFields[] = {
        FCombo("Serial:", Opt::SER_DEVICE, CE(serDevices)),
    };
    renderFields(serFields, IM_ARRAYSIZE(serFields));
}

void
ConfigPanel::renderGamePortsSection()
{
    ImGui::SeparatorText("Game Ports");

    // Build device option list: static entries + connected gamepads
    struct DeviceOption { const char *label; PortDevice device; };
    DeviceOption options[4 + kMaxGamepads];
    int numOptions = 0;

    options[numOptions++] = { "No device", PortDevice::None };
    options[numOptions++] = { "Mouse", PortDevice::Mouse };
    options[numOptions++] = { "Keyset 1 (Arrows+Space)", PortDevice::Keyset1 };
    options[numOptions++] = { "Keyset 2 (ESDXC)", PortDevice::Keyset2 };

    // Add connected gamepads
    for (int i = 0; i < kMaxGamepads; i++) {
        if (!gamepads[i].pad) continue;
        const char *name = SDL_GetGamepadName(gamepads[i].pad);
        auto dev = static_cast<PortDevice>(static_cast<int>(PortDevice::Gamepad0) + i);
        options[numOptions++] = { name ? name : "Gamepad", dev };
    }

    // Per-port combo
    for (int p = 0; p < 2; p++) {
        ImGui::PushID(400 + p);

        char label[16];
        snprintf(label, sizeof(label), "Game %d:", p + 1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);

        // Find current selection
        int sel = 0;
        for (int j = 0; j < numOptions; j++) {
            if (options[j].device == portDevice[p]) { sel = j; break; }
        }

        if (ImGui::BeginCombo("##dev", options[sel].label)) {
            for (int j = 0; j < numOptions; j++) {
                bool selected = (j == sel);
                if (ImGui::Selectable(options[j].label, selected)) {
                    PortDevice newDev = options[j].device;
                    // If the other port has this device, swap to None
                    int other = 1 - p;
                    if (newDev != PortDevice::None && portDevice[other] == newDev)
                        portDevice[other] = PortDevice::None;
                    portDevice[p] = newDev;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::PopID();
    }

    // Disconnect keys from keyboard
    ImGui::Checkbox("Disconnect keys from keyboard", disconnectKeys);
}

// =========================================================================
// File dialog helpers
// =========================================================================

void
ConfigPanel::openFileDialog(PendingDialog type, const char *filterName,
                            const char *filterPattern)
{
    {
        std::lock_guard<std::mutex> lock(fileResultMutex);
        pendingDialog = type;
    }
    SDL_DialogFileFilter f[] = { { filterName, filterPattern } };
    SDL_ShowOpenFileDialog(fileDialogCallback, this, window, f, 1, nullptr, false);
}

void SDLCALL
ConfigPanel::fileDialogCallback(void *userdata, const char * const *filelist,
                                int /*filter*/)
{
    auto *self = static_cast<ConfigPanel *>(userdata);
    std::lock_guard<std::mutex> lock(self->fileResultMutex);

    if (!filelist || !filelist[0]) {
        self->fileResultType = PendingDialog::None;
        self->fileResultPath.clear();
        self->pendingDialog = PendingDialog::None;
        return;
    }
    self->fileResultPath = filelist[0];
    self->fileResultType = self->pendingDialog;
    self->pendingDialog = PendingDialog::None;
}

void
ConfigPanel::processPendingFileResult()
{
    std::string path;
    PendingDialog type;
    {
        std::lock_guard<std::mutex> lock(fileResultMutex);
        if (fileResultType == PendingDialog::None) return;
        path = std::move(fileResultPath);
        type = fileResultType;
        fileResultType = PendingDialog::None;
    }

    try {
        switch (type) {
            case PendingDialog::ROM:
                emu.mem.loadRom(path); refreshRomInfo();
                setStatus(("ROM loaded: " + path).c_str());
                break;
            case PendingDialog::ExtROM:
                emu.mem.loadExt(path); refreshRomInfo();
                setStatus(("Extension ROM loaded: " + path).c_str());
                break;
            case PendingDialog::Disk0: case PendingDialog::Disk1:
            case PendingDialog::Disk2: case PendingDialog::Disk3: {
                int n = static_cast<int>(type) - static_cast<int>(PendingDialog::Disk0);
                emu.df[n]->insert(path, false);
                setStatus(("Disk inserted into DF" + std::to_string(n) + ": " + path).c_str());
                break;
            }
            default: break;
        }
    } catch (std::exception &e) {
        setStatus((std::string("Error: ") + e.what()).c_str(), 4.0f);
    }
}

// =========================================================================
// Settings persistence
// =========================================================================

void
ConfigPanel::initSettingsPath()
{
    char *prefPath = SDL_GetPrefPath("dirkwhoffmann", "vAmiga");
    if (!prefPath) {
        fprintf(stderr, "Warning: SDL_GetPrefPath failed: %s\n", SDL_GetError());
        return;
    }
    settingsPath = std::string(prefPath) + "defaults.ini";
    SDL_free(prefPath);
}

void
ConfigPanel::loadSettings()
{
    if (settingsPath.empty()) return;
    try {
        VAmiga::defaults.load(std::filesystem::path(settingsPath));
        printf("Settings loaded from %s\n", settingsPath.c_str());
    } catch (...) {}
}

void
ConfigPanel::applySettings()
{
    auto &d = VAmiga::defaults;
    for (auto opt : singleOpts) {
        try { emu.set(opt, d.get(opt)); } catch (...) {}
    }
    for (auto &io : idOpts2) {
        for (int i = 0; i < io.count; i++) {
            try { emu.set(io.opt, d.get(io.opt, i), i); } catch (...) {}
        }
    }
    for (auto &io : idOpts4) {
        for (int i = 0; i < io.count; i++) {
            try { emu.set(io.opt, d.get(io.opt, i), i); } catch (...) {}
        }
    }
    for (auto st : serverTypes) {
        try { emu.set(Opt::SRV_PORT, d.get(Opt::SRV_PORT, st), st); } catch (...) {}
        try { emu.set(Opt::SRV_VERBOSE, d.get(Opt::SRV_VERBOSE, st), st); } catch (...) {}
        try { emu.set(Opt::SRV_ENABLE, d.get(Opt::SRV_ENABLE, st), st); } catch (...) {}
    }
    printf("Settings applied to emulator.\n");
}

void
ConfigPanel::saveSettings()
{
    if (settingsPath.empty()) { setStatus("No settings path."); return; }

    // Sync emulator state → defaults storage
    auto &d = VAmiga::defaults;
    for (auto opt : singleOpts) {
        try { d.set(opt, emu.get(opt)); } catch (...) {}
    }
    for (auto &io : idOpts2) {
        for (int i = 0; i < io.count; i++) {
            try { d.set(io.opt, std::to_string(emu.get(io.opt, i)), {i}); } catch (...) {}
        }
    }
    for (auto &io : idOpts4) {
        for (int i = 0; i < io.count; i++) {
            try { d.set(io.opt, std::to_string(emu.get(io.opt, i)), {i}); } catch (...) {}
        }
    }
    for (auto st : serverTypes) {
        auto is = static_cast<isize>(st);
        try { d.set(Opt::SRV_ENABLE, std::to_string(emu.get(Opt::SRV_ENABLE, st)), {is}); } catch (...) {}
        try { d.set(Opt::SRV_PORT, std::to_string(emu.get(Opt::SRV_PORT, st)), {is}); } catch (...) {}
        try { d.set(Opt::SRV_VERBOSE, std::to_string(emu.get(Opt::SRV_VERBOSE, st)), {is}); } catch (...) {}
    }

    try {
        VAmiga::defaults.save(std::filesystem::path(settingsPath));
        setStatus(("Settings saved to " + settingsPath).c_str());
    } catch (std::exception &e) {
        setStatus((std::string("Save failed: ") + e.what()).c_str(), 4.0f);
    }
}

void
ConfigPanel::resetToDefaults()
{
    bool wasPowered = isPoweredOn();
    bool wasRunning = emu.isRunning();
    try {
        VAmiga::defaults.remove();
        if (wasPowered) emu.powerOff();
        applySettings();
        if (wasPowered) { emu.powerOn(); if (wasRunning) emu.run(); }
        refreshRomInfo();
        setStatus("All settings reset to defaults.");
    } catch (std::exception &e) {
        setStatus((std::string("Reset failed: ") + e.what()).c_str(), 4.0f);
    }
}

}
