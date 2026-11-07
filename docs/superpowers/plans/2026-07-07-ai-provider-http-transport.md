# AI Provider HTTP Transport Implementation Plan

> **Status reconciliation (2026-11-07):** SHIPPED: `47e4badb1`; actor wakes gained async execution later. `claude-sonnet-5` is a historical value requiring official verification before live use; no substitution is approved here. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing Anthropic provider shell capable of owning a real HTTP transport while keeping deterministic-null and coast behavior as the CI floor.

**Architecture:** Add a focused `xrSimAnthropicTransport` unit behind the existing `IAnthropicTransport` interface. The unit exposes a factory and status helpers, compiles to a no-network unavailable transport when curl is absent, and uses libcurl only when `XRAY_AGENT_HTTP` and `CURL::libcurl` are available. The bridge reports whether live transport is linked, but live wakes remain out of frame-critical paths until the async IO runtime lands.

**Tech Stack:** C++20, CMake 3.23, optional libcurl, existing `xrSimCore` standalone test executable.

## Global Constraints

- macOS builds use `XRAY_EXCEPTIONS=0`; all transport failures return values and never throw for normal failures.
- GL remains the runtime host; no renderer changes are part of this plan.
- Missing API key, unsupported provider, unavailable curl, timeouts, and non-200 HTTP responses all coast as normal values.
- The first live provider remains Sonnet-tier by default (`claude-sonnet-5`).
- Provider network calls must not be added to frame-thread bridge wake verbs in this slice.

---

### Task 1: Optional Transport Factory

**Files:**
- Create: `src/xrEngine/xrSim/xrSimAnthropicTransport.h`
- Create: `src/xrEngine/xrSim/xrSimAnthropicTransport.cpp`
- Modify: `src/xrEngine/xrSim/xrSimAgentProvider.h`
- Modify: `src/xrEngine/xrSim/xrSimAgentProvider.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `xrSim::IAnthropicTransport`, `xrSim::AnthropicAgentProviderShell`, `xrSim::AgentProviderConfig`.
- Produces: `xrSim::CreateAnthropicHttpTransport()`, `xrSim::IsAnthropicHttpTransportAvailable()`, `xrSim::DescribeAnthropicHttpTransport()`, `AnthropicAgentProviderShell::SetOwnedTransport(...)`.

- [x] **Step 1: Write the failing test**

Add a test that includes `xrSim/xrSimAnthropicTransport.h`, calls the factory, and asserts factory nullness matches availability.

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j10 --target xrSimWorldStateTests`

Expected: FAIL because `xrSimAnthropicTransport.h` does not exist.

- [x] **Step 3: Write minimal implementation**

Create the header and fallback implementation, add owned-transport storage to `AnthropicAgentProviderShell`, and add the new files to `xrSimCore`.

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests`

Expected: PASS.

### Task 2: Curl-backed Send

**Files:**
- Modify: `src/xrEngine/xrSim/xrSimAnthropicTransport.cpp`
- Modify: `src/xrEngine/CMakeLists.txt`

**Interfaces:**
- Consumes: `AnthropicMessagesRequest { method, path, anthropicVersion, contentType, body }`.
- Produces: `AnthropicTransportResult { ok, status, latencyMs, body, error }`.

- [x] **Step 1: Write the failing test**

Keep the test network-free by asserting `DescribeAnthropicHttpTransport()` returns either `curl` or `unavailable`; the build should compile whether curl is found or absent.

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j10 --target xrSimWorldStateTests`

Expected: FAIL until CMake defines the optional curl path and compiles the transport source.

- [x] **Step 3: Write minimal implementation**

When `XRAY_AGENT_HTTP_CURL=1`, implement `Send()` with libcurl: POST to `https://api.anthropic.com/v1/messages`, set `x-api-key`, `anthropic-version`, `content-type`, `CURLOPT_TIMEOUT_MS`, `CURLOPT_NOSIGNAL`, collect the body, record response status, and return `ok=false` only for transport-level failures.

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests`

Expected: PASS without performing a live network request.

### Task 3: Bridge Status and Handover

**Files:**
- Modify: `src/xrEngine/xrSim/xrSimBridge.cpp`
- Modify: `src/xrEngine/xrSim/tests/xrSimWorldStateTests.cpp`
- Modify: `docs/HANDOVER.md`

**Interfaces:**
- Consumes: `DescribeAnthropicHttpTransport()`.
- Produces: bridge-visible `agent.provider live` output with `transport=<curl|unavailable>`.

- [x] **Step 1: Write the failing test**

Extend `TestAgentBridgeAliases()` to assert `agent.provider live` includes `transport=`.

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j10 --target xrSimWorldStateTests && ./bin/arm64/Release/xrSimWorldStateTests`

Expected: FAIL because bridge output does not include transport state yet.

- [x] **Step 3: Write minimal implementation**

Append transport state to the live-provider bridge output and update the handover note to say the HTTP adapter is linked when curl is available.

- [x] **Step 4: Run final verification**

Run:

```bash
cmake --build build -j10 --target xrSimWorldStateTests
./bin/arm64/Release/xrSimWorldStateTests
python3 -m unittest tools.tests.test_gl_macos_soak -v
```

Expected: PASS.
