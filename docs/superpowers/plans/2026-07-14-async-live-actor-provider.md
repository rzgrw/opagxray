# Async Live Actor Provider Implementation Plan

> **Status reconciliation (2026-11-07):** SHIPPED: `9cf56d00c`, `e19a9a47d`, `f66aafd34`, hardened by `96142b7c0`. Hardened-prompt live non-coast acceptance remains open; historical model values require official availability verification. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let debug actors and session mutant packs request real Anthropic-authored actor intents without ever blocking the engine frame.

**Architecture:** Add an Anthropic adapter for the existing `IActorIntentProvider` contract, then run that provider behind a single-worker queue which owns only copied observation/prompt data. Bridge begin verbs enqueue on the engine thread; a common poll verb applies completed provider results to `WorldState` and actor records on the engine thread. The worker never receives engine pointers or mutates live state.

**Tech Stack:** C++17/20 stdlib threads, mutexes, condition variables and value types; existing libcurl-backed `IAnthropicTransport`; existing `ActorRuntime`, agent bridge, assert-style xrSim tests, and GL runtime.

## Global Constraints

- Darwin uses `XRAY_EXCEPTIONS=0`; configuration, transport, parse, queue, and poll failures return values and do not throw as normal control flow.
- No engine frame may block on provider HTTP.
- Worker jobs contain copied `ActorWakeContext` values only; `WorldState`, actor records, pack registries, and game objects remain engine-thread-owned.
- One worker serializes provider calls for this slice; no uncapped provider fanout.
- Shutdown cancels curl progress and joins the worker; it must not reintroduce the historical engine exit hang.
- Missing keys, disabled providers, unavailable transport, timeout, and HTTP failure produce coast values.
- Invalid model text is rejected as a value and must not replace the last accepted actor intent.
- Existing deterministic actor and Zone-provider behavior remains unchanged.

---

## File Structure

- `src/xrEngine/xrSim/xrSimAgentProvider.h/.cpp`: expose prompt-based Anthropic request construction and text-envelope decoding shared by Zone and actor providers.
- `src/xrEngine/xrSim/xrSimActorProvider.h/.cpp`: Anthropic actor-intent adapter and single-worker async queue.
- `src/xrEngine/xrSim/xrSimActorRuntime.h/.cpp`: split context preparation from main-thread provider-result application.
- `src/xrEngine/xrSim/xrSimPackSpawn.h/.cpp`: prepare/apply provider wakes for registered session packs without exposing registry internals.
- `src/xrEngine/xrSim/xrSimBridge.cpp`: enqueue live actor/pack wakes and poll/apply completions.
- `src/xrEngine/xrSim/xrSimAnthropicTransport.cpp`: bounded cancellation through curl's progress callback.
- `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`: provider codec, non-blocking queue, coast, and bridge tests.
- `src/xrEngine/CMakeLists.txt`: compile the new provider service.
- `tools/ai_thin_harness_smoke.txt` and `tools/tests/test_gl_macos_soak.py`: exercise the live-queue coast path in bridge smoke coverage.
- `docs/HANDOVER.md`: record the new verbs and remaining live-actuation gap.

---

### Task 1: Anthropic Actor Intent Adapter

**Files:**
- Create: `src/xrEngine/xrSim/xrSimActorProvider.h`
- Create: `src/xrEngine/xrSim/xrSimActorProvider.cpp`
- Modify: `src/xrEngine/xrSim/xrSimAgentProvider.h`
- Modify: `src/xrEngine/xrSim/xrSimAgentProvider.cpp`
- Modify: `src/xrEngine/xrSim/xrSimActorRuntime.h`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `ActorWakeContext`, `AgentProviderConfig`, `IAnthropicTransport`, `ParseActorIntentPlan`.
- Produces:
  - `AnthropicTextResult DecodeAnthropicMessagesText(const std::string&)`
  - `AnthropicMessagesRequest BuildAnthropicMessagesRequestForPrompt(const AgentProviderConfig&, const std::string&, uint32_t)`
  - `class AnthropicActorIntentProvider : public IActorIntentProvider`
  - `std::unique_ptr<IActorIntentProvider> CreateLiveActorIntentProvider(const AgentProviderConfig&)`

- [ ] **Step 1: Write failing adapter tests**

Add tests that inject `FakeAnthropicTransport`, return an Anthropic Messages JSON body containing:

```text
xrsim_actor_intent_v1
goal avoid_player_until_dark
stance cautious
duration_ms 4000
action stalk target_player crescent
end
```

Assert the adapter sends `/v1/messages`, embeds `xrsim_actor_wake_v1` in the request body, and returns a parsed `ActorProviderResult` with the configured provider/model and `stalk` action. Add missing-key and transport-error cases that return `ok=true`, `coast=true`, with `coastReason=missing_api_key` and `transport_error` respectively.

- [ ] **Step 2: Run the test target and verify RED**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests
```

Expected: compilation fails because `xrSimActorProvider.h` and its symbols do not exist.

- [ ] **Step 3: Add shared prompt/envelope helpers**

Add:

```cpp
struct AnthropicTextResult
{
    bool ok = false;
    std::string text;
    std::string error;
};

AnthropicMessagesRequest BuildAnthropicMessagesRequestForPrompt(
    const AgentProviderConfig& config, const std::string& prompt, uint32_t maxTokens);
AnthropicTextResult DecodeAnthropicMessagesText(const std::string& text);
```

Make `BuildAnthropicMessagesRequest` delegate to the prompt helper. Make `ParseAnthropicMessagesTextResponse` call `DecodeAnthropicMessagesText` and then the existing Zone parser, preserving existing errors.

- [ ] **Step 4: Implement the actor adapter**

Define:

```cpp
class AnthropicActorIntentProvider final : public IActorIntentProvider
{
public:
    explicit AnthropicActorIntentProvider(const AgentProviderConfig& config);
    void SetTransport(IAnthropicTransport* transport);
    void SetOwnedTransport(std::unique_ptr<IAnthropicTransport> transport);
    ActorProviderResult Wake(const ActorWakeContext& context) override;
    void Cancel() override;

private:
    AgentProviderConfig m_config;
    std::unique_ptr<IAnthropicTransport> m_ownedTransport;
    IAnthropicTransport* m_transport = nullptr;
};
```

`Wake` validates config, builds a prompt request with max 1024 tokens, calls transport, decodes the first Anthropic text block, parses `xrsim_actor_intent_v1`, and returns coast values for provider/transport availability failures. `CreateLiveActorIntentProvider` attaches `CreateAnthropicHttpTransport()` only for Anthropic.

- [ ] **Step 5: Run focused tests and verify GREEN**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests
```

Expected: exit 0 and all prior plus adapter assertions pass.

- [ ] **Step 6: Commit**

```bash
git add src/xrEngine/CMakeLists.txt src/xrEngine/xrSim/xrSimAgentProvider.* \
  src/xrEngine/xrSim/xrSimActorProvider.* src/xrEngine/xrSim/xrSimActorRuntime.h \
  src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp
git commit -m "feat: add Anthropic actor intent adapter"
```

---

### Task 2: Single-Worker Actor Provider Queue

**Files:**
- Modify: `src/xrEngine/xrSim/xrSimActorProvider.h`
- Modify: `src/xrEngine/xrSim/xrSimActorProvider.cpp`
- Modify: `src/xrEngine/xrSim/xrSimAgentProvider.h`
- Modify: `src/xrEngine/xrSim/xrSimAnthropicTransport.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`

**Interfaces:**
- Consumes: owned `std::unique_ptr<IActorIntentProvider>` and copied `ActorWakeContext`.
- Produces:
  - `ActorWakeSubmitResult AsyncActorProviderQueue::Submit(const ActorWakeContext&)`
  - `ActorWakePollResult AsyncActorProviderQueue::Poll(uint64_t requestId)`
  - `void IActorIntentProvider::Cancel()`
  - `void IAnthropicTransport::Cancel()`

- [ ] **Step 1: Write failing queue tests**

Use a blocking fake actor provider controlled by a condition variable. Submit one context, assert `Submit` returns immediately with request 1, assert first `Poll(1)` is `ok=true, ready=false`, release the fake, and poll with a bounded loop until `ready=true`. Assert the result contains the fake plan. Add unknown-id and queue-shutdown value-error assertions.

- [ ] **Step 2: Run tests and verify RED**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests
```

Expected: compilation fails because queue types do not exist.

- [ ] **Step 3: Implement the queue value types and worker**

Add:

```cpp
struct ActorWakeSubmitResult
{
    bool ok = false;
    uint64_t requestId = 0;
    std::string reason;
};

struct ActorWakePollResult
{
    bool ok = false;
    bool ready = false;
    std::string reason;
    ActorProviderResult providerResult;
};
```

`AsyncActorProviderQueue` owns one provider and one thread. `Submit` pushes a copied job under a mutex; the worker pops one at a time, calls `Wake` without holding the mutex, and stores a completion. `Poll` consumes a ready completion exactly once. The destructor calls `Shutdown`, which rejects queued work as `queue_shutdown`, calls provider cancellation, notifies, and joins.

- [ ] **Step 4: Add curl cancellation**

Add default no-op `Cancel()` virtuals to `IAnthropicTransport` and `IActorIntentProvider`. `CurlAnthropicTransport` owns an atomic cancellation flag and sets `CURLOPT_NOPROGRESS=0`, `CURLOPT_XFERINFOFUNCTION`, and `CURLOPT_XFERINFODATA`; the callback returns nonzero after cancellation. Map `CURLE_ABORTED_BY_CALLBACK` to `cancelled`.

- [ ] **Step 5: Run focused tests and verify GREEN**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests
```

Expected: exit 0; queue readiness, consumption, unknown-id, and shutdown tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/xrEngine/xrSim/xrSimActorProvider.* src/xrEngine/xrSim/xrSimActorRuntime.h \
  src/xrEngine/xrSim/xrSimAgentProvider.h src/xrEngine/xrSim/xrSimAnthropicTransport.cpp \
  src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp
git commit -m "feat: queue actor provider wakes off frame"
```

---

### Task 3: Live Actor and Pack Bridge Wakes

**Files:**
- Modify: `src/xrEngine/xrSim/xrSimActorRuntime.h`
- Modify: `src/xrEngine/xrSim/xrSimActorRuntime.cpp`
- Modify: `src/xrEngine/xrSim/xrSimPackSpawn.h`
- Modify: `src/xrEngine/xrSim/xrSimPackSpawn.cpp`
- Modify: `src/xrEngine/xrSim/xrSimBridge.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`
- Modify: `tools/ai_thin_harness_smoke.txt`
- Modify: `tools/tests/test_gl_macos_soak.py`
- Modify: `docs/HANDOVER.md`

**Interfaces:**
- Consumes: async queue, debug actor records, session pack registry, and `ActorRuntime`.
- Produces:
  - `ActorWakeContext ActorRuntime::PrepareWake(...) const`
  - `ActuatorResult ActorRuntime::ApplyProviderResult(...)`
  - `PackWakePrepareResult PreparePackWake(uint32_t, const WorldState&)`
  - `ActuatorResult ApplyPackWakeResult(uint32_t, WorldState&, ActorRuntime&, const ActorProviderResult&, uint32_t)`
  - bridge payloads `agent.actor.wake <squad|mutant_pack> live`, `agent.pack.wake <pack_id> live`, and `agent.actor.poll <request_id>`

- [ ] **Step 1: Write failing runtime and bridge tests**

Refactor tests to verify `PrepareWake` builds the same observation/prompt as synchronous `Wake`, and `ApplyProviderResult` applies a valid plan only on the caller thread. Add bridge tests with provider disabled:

```text
agent.actor.wake squad live -> actor wake queued request=1
agent.actor.poll 1          -> pending or actor wake ... coast=provider_disabled
agent.pack.wake 1 live      -> pack wake queued request=2
agent.actor.poll 2          -> pending or pack wake ... coast=provider_disabled
```

Poll in a bounded loop; no sleep longer than 10 ms. Assert invalid request ids and second consumption fail as values.

- [ ] **Step 2: Run tests and verify RED**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests
```

Expected: compilation or assertions fail because prepare/apply and live bridge paths do not exist.

- [ ] **Step 3: Split actor runtime preparation/application**

`PrepareWake` copies actor data, observation, and prompt. `ApplyProviderResult` clears transient commands, rejects invalid results without changing `lastIntent`, preserves the previous command stream for coast results, and executes valid new plans through `ExecuteActorIntent`. Existing synchronous `Wake` becomes prepare -> provider -> apply.

- [ ] **Step 4: Add pack prepare/apply helpers**

`PreparePackWake` finds the pack and returns its copied context or `unknown pack`. `ApplyPackWakeResult` re-finds the pack by stable pack id immediately before applying, so no worker-held pointer can outlive a reset.

- [ ] **Step 5: Add bridge queue routing**

Keep deterministic payloads backward compatible. A trailing `live` enqueues the prepared context and stores a small binding `{requestId, targetKind, actorName/packId}`. `agent.actor.poll` returns `pending` until ready, then re-finds the target and applies the result on the bridge/engine thread. `ai.reset` shuts down and clears the queue before replacing debug state.

- [ ] **Step 6: Extend smoke tooling and handover**

Add disabled-provider live queue commands to `tools/ai_thin_harness_smoke.txt`, including a short sleep before poll. Document the queue/poll verbs, the engine-thread application rule, and that real live object steering/ALife ownership remains the next slice.

- [ ] **Step 7: Run focused verification**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests
python3 -m unittest tools.tests.test_gl_macos_soak -v
cmake --build build -j10 --target xr_3da
```

Expected: exit 0 for all commands; Python reports all tests passing.

- [ ] **Step 8: Run live GL coast smoke**

Launch with `XRAY_AGENT_PROVIDER=off`, load the save, spawn `dog_weak 3 8`, request `agent.pack.wake 1 live`, poll its request, capture a screenshot, verify frame count advances while the job runs, and scan the log for `FATAL`, `SCRIPT RUNTIME ERROR`, `lua_pcall_failed`, `transport_error`, and invalid actor responses. Expected: coast reason `provider_disabled`, no fatal matches, clean `quit` exit.

- [ ] **Step 9: Commit**

```bash
git add src/xrEngine/xrSim/xrSimActorRuntime.* src/xrEngine/xrSim/xrSimPackSpawn.* \
  src/xrEngine/xrSim/xrSimBridge.cpp src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp \
  tools/ai_thin_harness_smoke.txt tools/tests/test_gl_macos_soak.py docs/HANDOVER.md
git commit -m "feat: expose async live actor wakes"
```

---

## Self-Review

- Spec coverage: provider-backed actor packets, no-frame-blocking, value errors, coast behavior, deterministic compatibility, shutdown, bridge verification, and engine-thread state application each have a task.
- Placeholder scan: no deferred implementation markers or unspecified error-handling steps remain.
- Type consistency: provider jobs use `uint64_t` request ids; pack ids remain `uint32_t`; live object ids remain `uint16_t`; all worker payloads are copied `ActorWakeContext` values.
- Scope: applying commands to actual mutant locomotion and full ALife offline/online ownership remain explicitly outside this slice.
