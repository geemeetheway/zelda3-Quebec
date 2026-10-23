# Repository Guidelines

## Project Structure & Module Organization

`src/` contains the game implementation in C; `snes/` contains the SNES rendering and emulation code. `assets/` holds Python tools for extracting and compiling game resources, while `tables/` stores resource definitions. `src/platform/` contains platform-specific files. `third_party/` contains bundled dependencies; avoid editing vendored code unless a change specifically requires it. The Visual Studio solution and project are at the repository root.

## Build, Test, and Development Commands

On Linux or macOS, install SDL2 and Python requirements as described in [BUILDING.md](BUILDING.md), then run:

- `make zelda3` — compile the game executable.
- `make` — compile the executable and generate `zelda3_assets.dat`; resource generation requires a supported US ROM named `zelda3.sfc` in the repository root.
- `make clean all` — remove generated build files and rebuild.

On Windows, use the provided batch files or open `Zelda3.sln` in Visual Studio. See [README.md](README.md) for runtime controls and configuration.

## Coding Style & Naming Conventions

Follow the surrounding C style: two-space indentation, braces on the same line, and descriptive functions and variables. C source and header filenames use lowercase with underscores (for example, `load_gfx.c`); existing code uses both PascalCase functions and `g_`-prefixed globals, so match the conventions of the file you change. Keep Python changes compatible with the existing scripts and their formatting. Do not commit generated executables, object files, or extracted assets.

## Testing Guidelines

There is no separate unit-test suite. The GitHub Actions build runs `make -j$(nproc) zelda3` on Linux and macOS. For C changes, build with `make zelda3` and exercise the affected behavior in game when practical; resource-tool changes should be checked by running the relevant script with the required ROM and dependencies.

## Commit & Pull Request Guidelines

Recent commits use short, imperative summaries (for example, “Add BPS support”); follow that style and keep each commit focused. Pull requests should explain the purpose, note possible build or gameplay effects, and list suggested testing steps, as requested by `.github/PULL_REQUEST_TEMPLATE.md`. Include reproduction steps or screenshots when they help reviewers assess a visible change.

## Security & Configuration

Do not commit ROM files, credentials, or machine-specific configuration. Keep local settings in `zelda3.ini`; follow the repository’s asset extraction flow for ROM-derived resources.
