# SP1: macOS Baseline — Implementation Plan

> **Status reconciliation (2026-11-07):** SHIPPED: `sp1-macos-baseline` tag `13f151f04` (commit `585dda21f`). Historical checkboxes are preserved. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Verify that Viberay builds and runs end-to-end on macOS Apple Silicon with the OpenGL renderer, producing a playable game session.

**Architecture:** The engine already has macOS CI builds, SDL2 windowing, OpenAL audio, and an OpenGL renderer. SP1 validates this by building locally, fixing any issues found, and documenting the macOS development workflow.

**Tech Stack:** C++17, CMake 3.23+, Clang, SDL2, OpenAL, Homebrew

**Spec:** `docs/superpowers/specs/2026-03-29-macos-metal-port-design.md`

---

## File Map

**Files we may need to modify** (only if issues are found during build/run):
- `CMakeLists.txt` — Root build config
- `cmake/XRay.Compiler.GNULike.cmake` — macOS compiler flags
- `src/xr_3da/entry_point.cpp` — Main entry point
- `src/xrCore/xrCore.cpp` — Path initialization
- `src/xrCore/LocatorAPI.cpp` — Game data discovery
- `src/Common/PlatformApple.inl` — macOS type definitions

**Files we will create:**
- `docs/macos-dev-setup.md` — macOS development setup guide
- `res/fsgame_macos.ltx` — macOS-specific fsgame template (if needed)

---

### Task 1: Install Dependencies

**Files:** None (system setup only)

- [ ] **Step 1: Install Xcode Command Line Tools**

Run:
```bash
xcode-select --install
```
Expected: Already installed, or installs CLT. Verify with:
```bash
clang --version
```
Expected: Apple clang 16+ output.

- [ ] **Step 2: Install Homebrew dependencies**

Run:
```bash
brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo
```
Expected: All packages install successfully. Verify:
```bash
brew list cmake sdl2 openal-soft
```

- [ ] **Step 3: Verify CMake finds all dependencies**

Run from repo root:
```bash
cmake -B build-test -DCMAKE_BUILD_TYPE=Release --log-level VERBOSE 2>&1 | grep -E "(Found|NOT FOUND|SDL2|OpenAL|LZO|Ogg|Vorbis|Theora|JPEG)"
```
Expected: All libraries show "Found" — no "NOT FOUND" lines. If any are missing, check Homebrew install and `CMAKE_PREFIX_PATH`.

- [ ] **Step 4: Clean up test build dir**

Run:
```bash
rm -rf build-test
```

---

### Task 2: Build the Engine

**Files:**
- Possibly modify: `cmake/XRay.Compiler.GNULike.cmake` (if build errors)
- Possibly modify: `CMakeLists.txt` (if dependency issues)

- [ ] **Step 1: Configure CMake build**

Run from repo root:
```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_UNITY_BUILD=ON \
  -DCPACK_GENERATOR=ZIP
```
Expected: Configuration completes with no errors. Watch for warnings about missing optional deps (mimalloc is optional — that's fine).

- [ ] **Step 2: Build all targets**

Run:
```bash
cmake --build build --parallel $(sysctl -n hw.ncpu)
```
Expected: Build completes successfully. This may take 10-20 minutes on first build.

If there are compile errors:
- Read the error carefully
- Check if it's a macOS-specific issue (missing header, API difference)
- Fix in the appropriate source file
- Re-run the build

- [ ] **Step 3: Verify built artifacts exist**

Run:
```bash
ls -la build/bin/
```
Expected: `xr_3da` executable exists, along with shared libraries (`xrCore`, `xrEngine`, `xrRender_GL`, `xrGame`, etc.).

Run:
```bash
file build/bin/xr_3da
```
Expected: `Mach-O 64-bit executable arm64`

- [ ] **Step 4: Commit any build fixes**

If any source files were modified to fix build errors:
```bash
git add -p  # Review each change
git commit -m "fix: resolve macOS build issues

[describe what was fixed and why]

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Set Up Game Data (Portable Mode)

**Files:**
- Reference: `res/fsgame.ltx`
- Reference: `src/xrCore/LocatorAPI.cpp` (lines 817-904 for path discovery)

The engine uses "portable mode" when it finds `fsgame.ltx` in the current working directory. This is the simplest approach for development.

- [ ] **Step 1: Understand the data directory structure**

The engine needs S.T.A.L.K.E.R. game data. The minimum structure is:
```
game-root/
├── fsgame.ltx          # Path configuration (from res/fsgame.ltx)
├── gamedata/
│   ├── configs/        # system.ltx, game.ltx (REQUIRED)
│   ├── shaders/gl/     # OpenGL shaders (REQUIRED on macOS)
│   ├── scripts/        # Lua game scripts
│   ├── textures/       # Texture files
│   ├── meshes/         # 3D models
│   ├── sounds/         # Audio files
│   └── levels/         # Level data
└── _appdata_/          # Created at runtime for saves, logs, configs
```

- [ ] **Step 2: Prepare game data directory**

Copy your Call of Pripyat (or Call of Chernobyl) game files to a directory. Then copy `fsgame.ltx` from the repo:
```bash
# Assuming game data is at ~/stalker-data/
cp res/fsgame.ltx ~/stalker-data/fsgame.ltx
```

Verify the fsgame.ltx paths match your directory structure:
```bash
cat ~/stalker-data/fsgame.ltx
```
The key line is: `$game_data$ = false|true|$fs_root$|gamedata\`
This means gamedata must be at `~/stalker-data/gamedata/`.

- [ ] **Step 3: Verify GL shaders exist**

Run:
```bash
ls ~/stalker-data/gamedata/shaders/gl/ | head -20
```
Expected: `.glsl` or shader files present. The OpenGL renderer requires these. If missing, they may need to be extracted from game archives (`.db` files).

---

### Task 4: First Launch — Smoke Test

**Files:**
- Possibly modify: `src/xrCore/xrCore.cpp` (if path issues)
- Possibly modify: `src/xrCore/LocatorAPI.cpp` (if fsgame issues)
- Possibly modify: `src/xrEngine/device.cpp` (if SDL window issues)

- [ ] **Step 1: Launch the engine**

Run from the game data directory (portable mode):
```bash
cd ~/stalker-data/
/Users/rz/viberay/build/bin/xr_3da
```

Or pass the config path explicitly:
```bash
/Users/rz/viberay/build/bin/xr_3da -fsltx ~/stalker-data/fsgame.ltx
```

**Possible outcomes:**
- **Window opens, menu renders** → Success! Proceed to Step 2.
- **Crash with error** → Read the error, fix it, rebuild, retry.
- **Missing library error** → Check `DYLD_LIBRARY_PATH` or use `install_name_tool`.
- **SDL error** → Check SDL2 installation, window creation code.
- **OpenGL error** → Check GL version (macOS supports up to GL 4.1).

- [ ] **Step 2: Handle shared library loading**

If the engine can't find its own shared libraries, set the library path:
```bash
cd ~/stalker-data/
DYLD_LIBRARY_PATH=/Users/rz/viberay/build/bin /Users/rz/viberay/build/bin/xr_3da
```

Or create a wrapper script at `build/run-macos.sh`:
```bash
#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export DYLD_LIBRARY_PATH="$SCRIPT_DIR/bin"
cd "${1:-.}"
"$SCRIPT_DIR/bin/xr_3da" "${@:2}"
```

- [ ] **Step 3: Verify main menu renders**

Expected: SDL window opens, OpenGL context is created, main menu is visible with options like "New Game", "Load Game", "Options", "Quit".

Check console output for:
- Renderer name (should show OpenGL)
- No shader compilation errors
- No fatal assertions

- [ ] **Step 4: Commit any runtime fixes**

If source files were modified:
```bash
git add -p
git commit -m "fix: resolve macOS runtime issues

[describe what was fixed and why]

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Full Functionality Verification

**Files:** None expected (verification only)

- [ ] **Step 1: Verify audio**

From the main menu, check if:
- Menu music plays (OpenAL)
- UI click sounds work

If no audio:
```bash
# Check OpenAL device
DYLD_LIBRARY_PATH=/Users/rz/viberay/build/bin /Users/rz/viberay/build/bin/xr_3da 2>&1 | grep -i "openal\|audio\|sound"
```

- [ ] **Step 2: Verify input**

From the main menu:
- Mouse moves cursor
- Keyboard navigates menu (arrow keys, Enter)
- Escape key works

- [ ] **Step 3: Load a level**

Start a new game or load a save:
- Verify level loads without crash
- Walk around for a few minutes
- Check: no rendering artifacts, textures load, lighting works
- Check: framerate is playable (target: 30+ fps at 1080p with OpenGL)

- [ ] **Step 4: Test options menu**

Open Options/Settings:
- Video settings accessible
- Resolution changes work
- Fullscreen/windowed toggle works

---

### Task 6: Document macOS Development Setup

**Files:**
- Create: `docs/macos-dev-setup.md`

- [ ] **Step 1: Write the setup guide**

Create `docs/macos-dev-setup.md` with everything learned in Tasks 1-5:

```markdown
# Viberay — macOS Development Setup

## Prerequisites

- macOS 13+ on Apple Silicon (arm64) or Intel (x86_64)
- Xcode Command Line Tools
- Homebrew

## Install Dependencies

\`\`\`bash
brew install cmake ccache sdl2 lzo libogg libvorbis theora openal-soft jpeg-turbo
\`\`\`

## Build

\`\`\`bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_UNITY_BUILD=ON
cmake --build build --parallel $(sysctl -n hw.ncpu)
\`\`\`

## Run

### Game Data Setup

[Document the portable mode setup from Task 3]

### Launch

[Document the launch command from Task 4]

## Known Issues

[Document any issues found during Tasks 4-5]

## Renderer

macOS currently uses the OpenGL renderer. Metal renderer is planned (SP2).
OpenGL 4.1 is the maximum supported version on macOS.
```

- [ ] **Step 2: Commit the guide**

```bash
git add docs/macos-dev-setup.md
git commit -m "docs: add macOS development setup guide

Covers dependencies, build, game data setup, and launch instructions.
Documents known issues found during SP1 baseline verification.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: SP1 Completion Gate

- [ ] **Step 1: Verify all success criteria**

Checklist:
- [ ] Engine builds on macOS Apple Silicon without errors
- [ ] Engine launches and shows main menu
- [ ] OpenGL renderer works (no visual corruption)
- [ ] Audio plays via OpenAL
- [ ] Input works (mouse + keyboard via SDL2)
- [ ] Can load and play a level for 5+ minutes without crash
- [ ] macOS dev setup is documented
- [ ] All fixes committed to git

- [ ] **Step 2: Tag SP1 completion**

```bash
git tag sp1-macos-baseline -m "SP1: macOS baseline verified — engine builds and runs with OpenGL"
```

- [ ] **Step 3: Note issues for SP2**

Create a brief summary of any issues found that affect the Metal renderer work:
- OpenGL version limitations observed
- Performance bottlenecks noted
- Any platform-specific quirks discovered

This feeds into SP2 (Metal renderer) planning.
