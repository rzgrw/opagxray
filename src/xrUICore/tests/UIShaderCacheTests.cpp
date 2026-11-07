// Regression coverage for the production cache key and shared_str ordering.
#include "Common/Common.hpp"
#include "xrCore/xrCore.h"

// This test does not initialize rendering or register Lua exports.
struct xrImTextureData;
#define ENGINE_API XR_IMPORT
#define DECLARE_SCRIPT_REGISTER_FUNCTION(...)
#include "xrUICore/XML/UITextureMaster.h"
#undef DECLARE_SCRIPT_REGISTER_FUNCTION

#include <array>
#include <cstdio>

namespace
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

bool test_ordering()
{
    // shared_str orders interned addresses, not spelling. Never assume which
    // name sorts first; the Cartesian product includes both crossed orders.
    const std::array<shared_str, 3> textures = {"ui/cache_a", "ui/cache_b", "ui/cache_c"};
    const std::array<shared_str, 3> shaders = {"hud/cache_a", "hud/cache_b", "hud/cache_c"};
    std::array<sh_pair, 9> keys;
    size_t count = 0;
    for (const auto& texture : textures)
        for (const auto& shader : shaders)
            keys[count++] = {texture, shader};

    for (const auto& a : keys)
    {
        if (!check(!(a < a), "key must not precede itself"))
            return false;
        for (const auto& b : keys)
        {
            if (!check(!(a < b && b < a), "crossed keys violate asymmetry"))
                return false;
            if (a.texture_name == b.texture_name)
            {
                if (!check((a < b) == (a.shader_name < b.shader_name), "shader must break texture ties"))
                    return false;
            }
            else if (!check((a < b) == (a.texture_name < b.texture_name), "texture must be the primary key"))
                return false;
            for (const auto& c : keys)
            {
                if (!check(!(a < b && b < c) || a < c, "key ordering must be transitive"))
                    return false;
            }
        }
    }

    // Cover all 24 insertion orders of a crossed 2x2 key set, checking the
    // same xr_map container type used by CUITextureMaster, without shaders.
    const std::array<sh_pair, 4> crossed = {keys[0], keys[1], keys[3], keys[4]};
    std::array<size_t, 4> order = {0, 1, 2, 3};
    do
    {
        xr_map<sh_pair, size_t> cache;
        for (const auto index : order)
            cache.emplace(crossed[index], index);
        if (!check(cache.size() == crossed.size(), "cache must retain each distinct pair"))
            return false;
        for (size_t i = 0; i < crossed.size(); ++i)
        {
            // Re-intern strings to also exercise equivalence of fresh keys.
            const sh_pair lookup{shared_str(crossed[i].texture_name.c_str()),
                shared_str(crossed[i].shader_name.c_str())};
            const auto found = cache.find(lookup);
            if (!check(found != cache.end() && found->second == i, "cache lookup must preserve pair identity"))
                return false;
            if (!check(!cache.emplace(lookup, 99).second, "duplicate key must not insert"))
                return false;
        }
        for (const auto index : order)
            if (!check(cache.erase(crossed[index]) == 1, "cache erase must find every key"))
                return false;
        if (!check(cache.empty(), "cache must be empty after erasure"))
            return false;
    } while (std::next_permutation(order.begin(), order.end()));
    return true;
}
} // namespace

int main()
{
    // Only the real string pool is needed; no filesystem/game data/renderer.
    str_container strings;
    g_pStringContainer = &strings;
    const bool passed = test_ordering();
    g_pStringContainer = nullptr;
    if (!passed)
        return 1;
    std::puts("PASS: production shader key ordering; 24 cache insertion/lookup/erase permutations");
    return 0;
}
