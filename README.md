# AOT2 Launcher

A compact Windows launcher and C++17 trainer interface for **Attack on Titan 2: Final Battle**. It uses the supplied Cheat Engine table as a reference for its signatures and feature behavior.

![Animated preview of the AOT2 Launcher interface](docs/demo.gif)

Open the [interactive interface preview](docs/demo.html) in a browser.

## Features

- Minimal frameless Win32 interface with category navigation and inline Player Data editing.
- Automatic `AOT2_EU.exe` detection and safe process-handle management.
- Native C++ implementations for the options represented in the launcher, with signature and original-byte checks before patching.
- Feature cleanup restores original bytes when the game process is still available.
- Press **F5** to retry process and signature discovery.

This project is intended for offline, single-player use. Disable corresponding Cheat Engine scripts before using the C++ launcher so two trainers do not patch the same code locations.

## Build

Requirements: Windows 10/11, Visual Studio 2022 with **Desktop development with C++**, and CMake.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable is created at `build/Release/Launcher.exe`.

## Project layout

- `src/main.cpp` — Win32 interface and editable value controls.
- `src/process_manager.*` — process detection, signature scanner, validated memory operations, and patch restoration.
- `src/feature_controller.*` — C++ feature hooks and native option handlers.
- `assets/icons/` — Lucide navigation icons (ISC license).

## Credits

Navigation icons from [Lucide](https://lucide.dev/), ISC licensed; see `assets/lucide/LICENSE`.
