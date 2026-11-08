# CoC 1.4.22 script/engine API compatibility audit (2026-11-08)

Static audit of Call of Chernobyl 1.4.22 scripts against this engine's Lua exports.
It was prompted by Windows playtest crash 1 (`no static 'DIK_NUMPADENTER' in class 'key_bindings'`, fixed in `482a142de`).
Base: `dev` at `94a305837`. No engine build or game run was done for this audit. Every finding comes from source and asset analysis.

## Bottom line

- **No further load-time or access-time "no static" crash was found.** Every literal `Class.STATIC` read in CoC scripts resolves against our exports, including the dynamic `clsid` enum. The exceptions are three names in an unreferenced module (F6).
- **Every function in CoC's own engine API dump resolves.** CoC ships `scripts/lua_help.script`, a luabind dump of its engine with CoC-era additions (`kCUSTOM1..15`, `level.get_target_dist`, `DIK_NUMPADENTER`). Against it: no missing namespace function (`level` 66/66, `game`, `relation_registry`, `actor_stats`, `main_menu`) and none of the 47 globals is missing. No `game_object`, `alife_simulator` or UI method that CoC scripts call is missing. All 41 Lua class bases exist.
- **The remaining risk is nil-tolerant dynamic indexing** of luabind classes (`callback[cb]`, `anim[x] or default`, `clsid[cls]`). CoC's luabind (built with `LUABIND_NO_ERROR_CHECKING`) returns nil for an unknown key, but ours raises. Every key that reaches these sites today checks out, except `ui_debug_main.script:661`, which takes user-typed text (F5).
- Two key-constant fixes are committed (F2, F3). Both are unbuilt.

## Commits

| Commit | Change | Status |
|---|---|---|
| `bc1a4ad4f` | `DIK_keys.DIK_PGUP`/`DIK_PGDN` were swapped (PgUp held `SDL_SCANCODE_PAGEDOWN`). The bug is inherited from upstream xray-16. | unbuilt |
| `b21614e1b` | Added the DirectInput aliases `DIK_NEXT`, `DIK_DECIMAL`, `DIK_APPS` and `DIK_SYSRQ` from CoC's `DIK_keys` dump. | unbuilt |
| (docs commit) | This file and `tools/xrdb_unpack.py` | n/a |

Both code commits touch only the name/value list in `src/xrEngine/key_binding_registrator_script.cpp`. Every SDL scancode they use already appears elsewhere in the same file.

## Findings, ranked by crash risk on our engine

Classes: **(a)** raises at access (missing static or global read), **(b)** missing method or function, raises when called, **(c)** probably harmless (guarded, dead or unreachable code, or fails identically on CoC's own engine). CoC call sites are `scripts/<file>:<line>` inside `database/config/scripts.db`.

| # | Finding | Engine class / table | CoC call site(s) | Class | Our status | Action |
|---|---|---|---|---|---|---|
| F1 | Missing statics on key tables (`DIK_NUMPADENTER`, `DIK_keys[<ini value>]`, `DIK_keys["DIK_"..x]`) | `key_bindings`, `DIK_keys`, `key_bindings_context` | `axr_keybind.script:24,333-366`; `ui_debug_main`, `xrs_debug_tools` | (a), hot path (key press/hold/release) | Fixed in `482a142de` (plain Lua tables) | Pending in-game check (see windows-dev-setup.md) |
| F2 | `DIK_PGUP`/`DIK_PGDN` values swapped | `DIK_keys` | Any user keybind in `axr_options.ini [global_keybinds]` resolved by `axr_keybind.script:337,352,366` | misbehaves (bind fires on the opposite key) | **Fixed** `bc1a4ad4f` | Unbuilt |
| F3 | 14 `DIK_keys` names from CoC's dump absent (`DIK_NEXT`, `DIK_DECIMAL`, `DIK_APPS`, `DIK_SYSRQ`, plus 10 Japanese-layout keys) | `DIK_keys` | None literal. Reachable only through user-written binds | misbehaves (bind is nil and never fires; no crash since `482a142de`) | **4 added** `b21614e1b` | `DIK_KANA`, `DIK_KANJI`, `DIK_CONVERT`, `DIK_NOCONVERT`, `DIK_AT`, `DIK_COLON`, `DIK_UNDERLINE`, `DIK_CIRCUMFLEX`, `DIK_AX` and `DIK_UNLABELED` have no unambiguous SDL scancode. Not added. |
| F4 | `npc:burer_get_force_anti_aim()` | `game_object` (we export only `burer_{set,get}_force_gravi_attack`) | `xr_conditions.script:1977` (`burer_anti_aim`) | (b), dormant | Missing. It is also absent from CoC's own dump, and no config references `{=burer_anti_aim}` | None. Add only with real `CBurer` semantics if a mod needs it |
| F5 | Dynamic class indexing that expects nil for unknown keys | `callback`, `anim`, `sound`, `MonsterSpace`, `clsid` | `bind_stalker.script:181-182` `callback[cb]` (actor binder init, every load); `mob_walker.script:187` `anim[...] or default_anim_standing`; `mob_walker.script:230,243` `sound[...]`; `mob_remark.script:58` `MonsterSpace[...]`; `ui_debug_main.script:661-662` `clsid[cls]` | (a) if an unknown key arrives | `callback`: all 24 names in the `bind_stalker` table exist. `anim`: the 39 `mob_walker` look paths resolved in `all.spawn` carry only `a=sleep`, and a spot-check of `zat_b18_dog_sit_look` found `a=sit_idle`. Both are valid. `MonsterSpace`: no config feeds `mob_remark` `anim_head` (the `normal`/`angry`/`forester_*` values all belong to `mob_trader`, which uses a Lua table). `clsid[cls]` in the debug spawner takes user-typed text, so a typo aborts the game | Proposed (needs a decision): an engine-side CoC-compat option that makes a missing static on any luabind class return nil, optionally with `Msg("~ no static ...")` once per key. This can be done by wrapping the class metatable `__index` after registration, without modifying the luabind submodule. It would also cover addon scripts that add callback names. Not done, because `482a142de` deliberately kept other classes strict. |
| F6 | `clsid.wpn_fn2000_s`, `clsid.wpn_fort_s`, `clsid.wpn_shotgun_S` are registered by neither the engine nor `class_registrator.script` | `clsid` | `xr_state.script:281,288,295` | (c) | `xr_state.script` is a SoC leftover that no other script references | None |
| F7 | Engine hooks behind `R_ASSERT` that CoC does not define: `pda.fill_faction_state`, `pda.get_max_member_count`, `pda.get_max_power`, `pda.get_max_resource` (and SoC `pda.fill_fraction_state`) | engine to script (`FactionState.cpp:110`, `UIFactionWarWnd.cpp:401-415`, `FractionState.cpp:79`) | n/a | (c), conditional | `CUIFactionWarWnd::Init()` needs `pda_fraction_war.xml`. CoC ships none, and the repo copy lives only under `res/.../ui/styles_/ui_style_cs/`, which `stage_coc_runtime.py` does not stage. So the window is deleted and the hooks are never called. | Note: selecting the CS UI style with CoC data would make these fatal |
| F8 | Soft engine hooks absent in CoC (skipped when missing): `actor_menu_inventory.CUIActorMenu_OnItemFocusLost`, `ai_stalker.CSE_ALifeObject_spawn_supplies`, `alife_storage_manager.CALifeStorageManager_after_load`, `pda.get_favorite_weapon`, `pda.get_monster_back`, `pda.get_monster_icon`, `sim_combat.start_attack` | engine to script (`if (functor(...))`) | n/a | (c) | The engine falls back to native behaviour | None |
| F9 | Console commands referenced by CoC but not registered: `r3_msaa_opt` (main video options check box, DX-only, commented out in `xrRender_console.cpp`), `pda_filter_news`/`pda_filter_talk`, 21 atmosfear `opt_*` entries, and `hud_old_style`, `g_always_run`, `dbg_ph_ai_*`, `show_wnd_rect_all` | console | `ui/ui_mm_opt.xml:447`; `ui/pda_logs.xml:50,57`; `ui/ui_mm_atmosfear_options*.xml`; `ui_mm_opt_con_cmd.script:39-90` | (c) | Check boxes read false through `CConsole::GetBool` (null-safe). The engine never pulls or saves the atmosfear and PDA-filter groups (the script and `CUILogsWnd` drive them). `ui_mm_opt_con_cmd.script` is unreferenced. All real combo boxes on the main options page bind to token classes, so `CUIComboBox`'s `R_ASSERT3(tok, ...)` cannot fire. | None |
| F10 | CoC script bugs that fail identically on CoC's own engine: `phantom_manager:phantom_count()` (field, not a function), `if_then_else`, `file_exists`, `get_storage`, `set_obj_squad`, `level.actor()`, `alife():object_by_id()` | Lua / `level` / `alife_simulator` | `sr_psy_antenna.script:139`, `ph_car.script:822`, `alun_utils.script:1199`, `alun_utils.script:1008` (unused), `sim_objects.script:25`, `ph_hit.script:44` (scheme loaded, no config uses it), `xr_conditions.script:519` | (b) on both engines | Not engine mismatches. None appears in CoC's API dump | None (CoC script issues) |
| F11 | `actor_stats.remove_from_ranking` | `actor_stats` | `bind_stalker.script:73-74` | (c), guarded by `~= nil` | Missing | None |
| F12 | Hardcoded DirectInput key code `dik==28` (Enter) | n/a (our codes are SDL scancodes; 28 = `SDL_SCANCODE_Y`) | `ui_scenes.script:159` | (c) | `ui_scenes` is unreferenced | None. Other key handling uses `DIK_keys` names, and `axr_options.ini` stores names, not numbers. |

Inventory false positives that were checked and excluded: `cse_*:on_register/on_unregister/update/...` (registered through `DEFINE_LUABIND_VIRTUAL_FUNCTION` macros), `ini_file.r_*_ex` (added by CoC `_g.script:1551-1562`), `game.CTime` (class inside the `game` module), `device()` fields, method calls on Lua objects (`sim_board`, squads, `task_objects`), and server-object methods (`community`, `o_torso`, `section_name`, `smart_terrain_task_activate`, `visible_for_map` all exist on `cse_*`).

## Why only static reads differ

Our luabind (`Externals/luabind`, deboostified fork) guards only `class_rep::static_class_gettable` behind `LUABIND_NO_ERROR_CHECKING` (`src/class_rep.cpp:291`). That function also merges base-class constants (`add_base_class`) and resolves `Class.method` through the default table. Instance field writes go to a per-instance table. On CoC's luabind, overload mismatches were undefined behaviour, so working CoC scripts don't depend on them. Namespaces (`level`, `game`, ...) are plain tables on both engines, so a missing function only fails when it is called. In short, the class-static path is the only access-time behaviour CoC relies on that we lack.

## What was and was not verified

Verified (static):
- Extraction: all 334 files in `scripts.db` and 3009 in `configs.db`, every CRC32 matching. `xr.db`, `ai.db` and `shaders.db` carry no scripts.
- Every literal `Class.X` / `Class["X"]` read in scripts against our `class_<>` / `value()` inventory with base resolution. `clsid` was checked against `object_factory_register.cpp` `ADD(...)` plus `class_registrator.script` registrations.
- `lua_help.script` (330 classes, 6 namespaces) diffed against our exports, weighted by script usage.
- Typed call sites (`alife():`, `db.actor:`, `level.`, `get_console():`, `system_ini():` ...) and global calls not defined in any script, `_g.script` or our exports.
- Engine-to-script `functor("module.func")` names against CoC definitions, and console commands referenced by scripts and UI XML against `CMD*` registrations.
- Dynamic class indexing sites and the keys that can reach them (config values, and patrol point flags in the uncompressed `spawns.db/all.spawn`).

Not verified:
- Nothing was compiled or run. Both code commits are **unbuilt**.
- Method calls on locals whose type can't be inferred were checked only against the union of all exported method names. A name that exists on some other class would pass.
- `mob_walker` look-path coverage: 39 of the configured look paths resolved in `all.spawn` by name heuristics. The rest were not inspected.
- Semantics of methods that exist on both engines but behave differently (argument types, return values, callback firing) are out of scope for a name-level audit.
- `lua_help.script` may not list every CoC engine extension. Script-side usage was cross-checked independently, and nothing else surfaced.

## Reproducing

1. Unpack (pure Python, no dependencies; reads the format `src/xrCore/LocatorAPI.cpp` reads):

   ```
   python -I tools/xrdb_unpack.py <coc>/database/config/scripts.db <new-empty-dir>
   python -I tools/xrdb_unpack.py <coc>/database/config/configs.db <new-empty-dir>
   ```

   Chunk 666 is the INI header and chunk 1 the file table (LZHuf when bit 31 is set, ported from `src/xrCore/LzHuf.cpp`). File bodies are LZO1X when `size_real != size_compr` (`rtc_decompress`). `spawns.db` stores `all.spawn` uncompressed. Treat output as data: extract outside the repo and run Python with `-I`.
2. The cross-reference scripts used for this audit (inventory parser, xref, `lua_help` diff, clsid, hooks, console and dynamic-index checks) were throwaway and were not committed. The method is described above. Rebuild the inventory first, because regex-based luabind parsing needs macro and `instance("name")` special cases (`class_<T> instance("game_object")`, `DEFINE_LUABIND_CLASS_WRAPPER_n`, `luabind_class_*`).
