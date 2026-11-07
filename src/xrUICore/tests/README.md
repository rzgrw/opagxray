# UI shader cache regression

`UIShaderCacheTests.cpp` includes the production `UITextureMaster.h` and uses the
real `shared_str`, string pool, and `xr_map`. It checks ordering laws, primary
texture ordering, shader tie breaking, and insertion/lookup/duplicate/erase
behavior for all 24 orders of a crossed 2x2 key set. It suppresses only the
unrelated Lua registration declaration so this test needs no renderer or game
data. It does not call shader creation or exercise ALife groups.

With an existing engine configuration:

```sh
cmake --build build --target xrUIShaderCacheTests -j10
bin/arm64/Release/xrUIShaderCacheTests
```

The executable location follows the build's configured output directory. This
target is excluded from the default engine build.

For a focused standalone run on Apple Silicon with Homebrew SDL2, compile the
test and the necessary **production** core sources (no replacement comparator,
string pool, allocator, or container). C++20 matches `cmake/XRay.Build.cmake`:

```sh
mkdir -p artifacts/ixray-foundation
clang++ -std=c++20 -DNDEBUG -DXRAY_EXCEPTIONS=0 -DUSE_PURE_ALLOC \
  -Isrc -Isrc/xrCore -IExternals/tracy/public -I/opt/homebrew/include/SDL2 \
  -Wno-unknown-pragmas -Wno-deprecated-declarations \
  -ffunction-sections -fdata-sections \
  src/xrUICore/tests/UIShaderCacheTests.cpp src/xrCore/xrstring.cpp \
  src/xrCore/Threading/Lock.cpp src/xrCore/crc32.cpp src/xrCore/xrMemory.cpp \
  -Wl,-dead_strip -L/opt/homebrew/lib -lSDL2 \
  -o artifacts/ixray-foundation/ui_shader_cache_tests
artifacts/ixray-foundation/ui_shader_cache_tests
```

Dead stripping omits unused engine services from this standalone link. The
integrated CMake target instead links the configured `xrCore` library and its
allocator. A successful run prints `PASS: production shader key ordering; 24
cache insertion/lookup/erase permutations` and exits zero; the baseline
`a4d0fa256` header fails with `crossed keys violate asymmetry` and exits one.
