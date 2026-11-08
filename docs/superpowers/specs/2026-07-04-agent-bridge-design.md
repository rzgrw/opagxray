# Agent Bridge — Design

**Date:** 2026-07-04
**Status:** Approved (rz)
**Workstream:** T in `2026-07-04-engine-v2-roadmap.md`

## Purpose

Let a coding agent load into the game, control the player character, and observe game state and visuals — so engine changes (Metal parity, AI behavior, hardening) are verified autonomously instead of manually.

## Architecture

New `xrEngine` subsystem `CAgentBridge`, active only when the engine is launched with `-agent_bridge [socket_path]`. Default socket: `$app_data_root$/agent_bridge.sock` (Unix domain socket — native POSIX idiom, faster than TCP loopback, filesystem-permission scoped).

**Windows transport (2026-11-08).** Windows Python lacks `AF_UNIX`, so the Windows engine binds TCP on `127.0.0.1` only. The port comes from `-agent_bridge_port <n>`, or the OS picks one, and is published atomically to `$app_data_root$/agent_bridge.port`. A bind failure disables the bridge with a log line instead of asserting. The protocol, queues and threading are shared code; only a small socket shim differs. `-agent_background` runs an instance unobtrusively (no focus, off-screen, muted). See `docs/windows-dev-setup.md`.

**Threading model.** A dedicated bridge thread owns the socket: accepts one client at a time, reads newline-delimited requests, writes responses. Parsed requests go into a mutex-guarded queue; `CRenderDevice`'s frame loop drains the queue **once per frame on the main thread** (console, Lua, and SDL input are main-thread-only). Command latency = one frame (~16 ms). Responses carry request IDs so the client may pipeline.

**Lifecycle.** Socket file unlinked at startup (stale-proof) and shutdown. Bridge thread join is bounded (≤2 s) per the engine's teardown hardening rules; an unresponsive bridge thread is detached with a log line, never a hang. Zero footprint when the flag is absent.

## Protocol (v1)

Line-based text, UTF-8: `<id> <verb> [payload]\n` → `<id> ok [payload]\n` or `<id> err <message>\n`. Multi-line payloads (lua results, state) are escaped (`\n` → `\\n`).

| Verb | Payload | Effect |
|---|---|---|
| `hello` | — | returns `agent_bridge v1 <engine build>` |
| `cmd` | console command line | executes via CConsole on main thread; output lines captured and returned |
| `lua` | Lua expression/chunk | runs via the game script engine; return value(s) `tostring`ed (tables shallow-dumped one level) |
| `key` | `<sdl_key_name> down\|up\|tap` | synthetic SDL key event into the real input pipeline (`tap` = down + up next frame) |
| `mouse` | `move <dx> <dy>` or `btn <left\|right\|middle> down\|up\|tap` | synthetic mouse events |
| `state` | — | canned summary: level name, actor position/direction, health, in-game time, FPS — one line, `key=value` pairs |
| `shot` | `<name>` | engine screenshot written to `$screenshots$/<name>.jpg`; responds with the resolved path when the write completes |
| `bye` | — | closes the client connection (game keeps running) |

`state` and `shot` are conveniences implementable on top of `lua`/`cmd`, kept as verbs because they are the hot verification loop.

## Client

`tools/agentctl.py` (stdlib-only Python): `agentctl.py <socket> <verb> [payload...]`, plus `--script <file>` to run a sequence with per-step timeouts. `nc -U` works for ad-hoc use.

## Error handling

- Unknown verb / malformed line → `err` with reason, connection stays up.
- Lua errors → `err` with the Lua error string (never a dialog, never fatal).
- `cmd` of a console command that VERIFYs is not intercepted — engine failure semantics are unchanged by the bridge.
- Client disconnect mid-frame → pending responses dropped, queue cleared, listener re-accepts.

## Testing

Self-verifying loop (also the acceptance test): launch CoC with `-agent_bridge`, then from the shell — `hello` → `cmd start server(1/single/alife/load) client(localhost)` → poll `state` until level loaded → `key w down`, verify `state` position advances, `key w up` → `shot walk_test` → view screenshot → `cmd quit` → verify clean exit. This script becomes the standard harness for Metal Tasks 22–24 (GL-vs-Metal screenshot comparison) and workstream A scenario tests.

## Non-goals (v1)

- Multiple simultaneous clients, authentication, remote (non-localhost) access.
- Deterministic frame-stepping / fixed-timestep replay (future: pairs with xrSim snapshots).
- Binary protocol; v1 stays greppable text.
