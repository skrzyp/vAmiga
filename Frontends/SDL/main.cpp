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

#include <CLI/CLI.hpp>
#include <cstdio>
#include <csignal>

using namespace vamiga;

// Global quit flag for signal handler
static App *gApp = nullptr;

static void
signalHandler(int /*sig*/)
{
    if (gApp) gApp->quit.store(true);
}

// Main

int main(int argc, char *argv[])
{
    AppOptions opts;

    CLI::App cli{"vAmiga SDL Frontend"};
    cli.set_version_flag("--version", "vAmiga SDL Frontend 0.1");

    // Required
    cli.get_formatter()->column_width(30);

    cli.add_option("-r,--rom", opts.rom, "Kickstart ROM image (required)")
        ->required()
        ->type_name("PATH")
        ->check(CLI::ExistingFile.description(""));

    // Disk images
    cli.add_option("-a,--adf", opts.adf, "Floppy disk image (ADF)")
        ->type_name("PATH")
        ->check(CLI::ExistingFile.description(""));

    // Memory configuration
    cli.add_option("--chip", opts.chipRam, "Chip RAM in KB (256/512/1024/2048)")
        ->type_name("KB")
        ->check(CLI::IsMember({256, 512, 1024, 2048}).description(""));

    cli.add_option("--slow", opts.slowRam, "Slow RAM in KB")
        ->type_name("KB")
        ->check(CLI::NonNegativeNumber.description(""));

    cli.add_option("--fast", opts.fastRam, "Fast RAM in KB")
        ->type_name("KB")
        ->check(CLI::NonNegativeNumber.description(""));

    // Remote servers
    cli.add_option("-s,--shell", opts.shellPort,
        "RetroShell TCP server on port (default: 8081)")
        ->type_name("PORT")
        ->expected(0, 1)
        ->default_val(8081)
        ->check(CLI::Range(1, 65535).description(""));

    CLI11_PARSE(cli, argc, argv);

    // --shell was used if the option was parsed (even without a value)
    opts.shell = cli["--shell"]->count() > 0;

    printf("vAmiga SDL Frontend\n\n");

    App app;
    gApp = &app;

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    if (!app.init(opts)) return 1;

    app.run();
    app.shutdown();

    // Clear signal handler target before App is destroyed
    gApp = nullptr;

    return 0;
}
