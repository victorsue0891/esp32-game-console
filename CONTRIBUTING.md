# Contributing

Thank you for considering a contribution to this project.  All contributions — bug fixes, new emulator backends, hardware support, documentation — are welcome.

## Getting Started

1. **Fork** the repository and clone your fork locally.
2. Create a **feature branch** off `master`:
   ```bash
   git checkout -b feat/my-feature
   ```
3. Make your changes and **test on real hardware** (or at a minimum verify the build passes via `pio run`).
4. Open a **Pull Request** against `master`.

## Build Requirements

| Tool | Version |
|------|---------|
| PlatformIO | latest |
| ESP-IDF | v5.5.1 (configured automatically by PlatformIO) |
| Python | 3.9+ |

```bash
# Compile
pio run

# Flash
pio run --target upload

# Serial monitor (115200 baud)
pio device monitor
```

## Code Style

- C99, `snake_case` for identifiers, `UPPER_SNAKE_CASE` for macros.
- Keep functions short and single-purpose.
- Avoid dynamic allocation in RAM; use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` for large buffers.
- Do not add emulator-specific logic to `emulator.c`; implement the `emulator_driver_t` vtable instead (see `include/emulator_driver.h`).

## Adding a New Emulator Backend

1. Create a component under `components/<system>_emu/` with its own `CMakeLists.txt`.
2. Implement the public header (`<system>_emu.h`) exposing at minimum: `init`, `run_frame`, `set_joypad`, `get_framebuffer`, `get_audio`, `get_state_size`, `save_state`, `load_state`, `shutdown`.
3. In `src/emulator.c`, add a wrapper `set_input` function (to map the generic `uint16_t` button mask to the native format) and declare a `const emulator_driver_t <system>_driver = { ... }`.
4. Add a pointer to it in the `drivers[]` array. **No other file needs to change.**

## Reporting Bugs

Please use the [Bug Report](.github/ISSUE_TEMPLATE/bug_report.md) template and include a serial log from `pio device monitor`.

## Memory Constraints

RAM budget is tight (~72% used).  Keep the following rules:

- All ROM data and framebuffers must reside in **PSRAM** (`MALLOC_CAP_SPIRAM`).
- Stack-allocated arrays larger than ~256 bytes should be avoided in tasks with 4 KB stacks (e.g., `lcd_flush`).
- The emulator task has a 32 KB stack; the UI task uses the default IDF stack.

## Pull Request Checklist

- [ ] `pio run` succeeds without warnings on new files
- [ ] Tested on real hardware (or state reason for simulation-only testing)
- [ ] No ROM files committed
- [ ] New public API functions documented in the relevant header
- [ ] `CLAUDE.md` updated if architecture changes
