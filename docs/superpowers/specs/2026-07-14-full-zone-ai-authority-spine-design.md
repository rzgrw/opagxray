# Full-Zone AI Authority Spine — Design Spec

> **Status reconciliation (2026-11-07):** DESIGN APPROVED; GATE 1 INCOMPLETE. Selective Task 1 recovery is integrated at `04b4ef5b5`; further AI expansion follows the runtime stability/performance comparison; old branch `1d951aa07` is unmerged recovery material. Task 3 grammar/ownership/semantics need reconciliation before implementation. Sonnet-only policy remains; historical `claude-sonnet-5` requires official availability verification, and `f1222d148` provider/version-ladder changes are proposed only. See [PLAN_STATUS](../../PLAN_STATUS.md) for evidence, gates, and current execution order.

**Date:** 2026-07-14

**Status:** Approved for implementation

**Implementation target:** Gate 1, the Garbage vertical slice

**Program target:** AI-authored simulation across every level, with legacy ALife planners retired

## 1. Purpose

OpenXVibeRay will replace legacy ALife authorship with a single AI authority spine. Persistent Sonnet-backed world agents decide how the Zone evolves; a thin deterministic C++ kernel, `xrSim`, owns facts, validates every proposed change, and materializes accepted plans through X-Ray's existing live systems.

The final target is not an AI sidecar or a narrative advisor. It is a fully AI-authored Zone covering factions, territory, ecology, economy, incidents, squads, mutant packs, and salient individuals on every level.

The replacement is incremental. Each transferred scope has exactly one authoring owner, remains playable, and has a measurable acceptance gate. Legacy ALife may temporarily provide pathfinding, spawning, combat, animation, and other bounded execution mechanics, but it loses permission to make strategic or entity-objective decisions as each scope transfers.

This document refines `2026-07-05-agentic-zone-design.md`. Its explicit user-approved decisions override that document where they differ:

- The provider policy for this program is **Sonnet-only**.
- There is **no total API-call cap per session**.
- Concurrency, request rate, retries, and queue size remain bounded for stability.
- The rollout begins with an end-to-end Garbage slice, then expands to the full Zone.

## 2. Scope and decomposition

The work is a program with four independently gated slices:

1. **Garbage vertical slice:** stalkers, bandits, and mutant packs compete over territory and resources; the immortal player can provoke them; AI owns the transferred Garbage scopes.
2. **Cross-level continuity:** adjacent regions, portal travel, persistent groups, and consequences across level transitions.
3. **All-level offline Zone:** every unloaded level continues to evolve through faction, ecology, economy, migration, and incident plans.
4. **Full live authority:** AI objectives govern online groups and salient actors everywhere; legacy ALife planners are disabled.

The implementation plan produced immediately after this spec covers Gate 1. Later gates receive their own plans and, where they introduce new unresolved behavior, focused follow-on design specs. All gates use the same authority, intent, validation, ledger, and materialization contracts defined here.

## 3. Non-goals

Gate 1 does not:

- replace real-time pathfinding, animation, physics, perception, weapon handling, or combat behavior trees;
- put an LLM decision on the critical path of any rendered frame;
- call a model once per NPC per tick;
- add Opus, Haiku, OpenAI, or local-model routing;
- implement save/load persistence for agent memory—the first acceptance gate is one uninterrupted session;
- resume the Metal or Vulkan renderer work;
- preserve legacy authorship inside an AI-owned scope as a fallback.

## 4. Authority architecture

The system has four layers.

### 4.1 AI authority layer

Persistent world agents hold charters, scoped beliefs, memories, and plans. They read scoped projections and submit intent envelopes. They never mutate engine or simulation memory directly.

### 4.2 xrSim kernel

`xrSim` is the authoritative world graph and sole factual writer. It owns stable IDs, world revisions, scope ownership, validation, guardrails, staged application, the request/decision ledger, and record/replay packets.

All failures are values. No provider, codec, validation, authority, or apply failure may throw. This is required on Darwin, where `XRAY_EXCEPTIONS=0` and model-generated input is untrusted.

### 4.3 Materialization executor

The executor compiles committed intents into bounded, typed operations. It binds offline records to live X-Ray objects, redirects existing objectives, reconciles observed outcomes, and prevents duplicate spawning.

### 4.4 X-Ray live world

Existing engine systems continue to perform frame-rate mechanics. Their observations return to xrSim as facts and consequences. Once a scope becomes AI-owned, those systems execute committed objectives but do not author replacement objectives.

The directional loop is:

`scoped observation → Sonnet intent → xrSim validate/stage/apply → execution plan → live outcome → xrSim event → agent memory`

## 5. Agent hierarchy and ownership

Authority narrows by horizon. The hierarchy is not permission for a parent to write a child's facts; it determines which agent owns each class of plan and who arbitrates proposals.

### 5.1 Zone Director

Exactly one Zone Director owns cross-level pressure, global constraints, and conflicts that span multiple owners. It sees aggregate, guardrail-adjusted state. It may select priorities and arbitrate conflicting proposals, but it never issues per-NPC commands.

### 5.2 Strategic domain minds

- **Faction Minds:** one per represented faction; own faction goals, diplomacy, territory claims, and faction-wide tasking.
- **Ecology Mind:** owns population budgets, migration pressure, predation pressure, nesting, and recovery constraints.
- **Economy Mind:** owns scarcity, sources, sinks, stockpiles, demand, and logistics pressure.
- **Incident Mind:** proposes disturbances and opportunities. It may create conditions but cannot prescribe a guaranteed story outcome.

### 5.3 Region Minds

Each level or explicitly connected region cluster has one Region Mind. It combines committed strategic pressure with local geography, selects local timing and placement, and arbitrates contention inside its region. It cannot rewrite faction diplomacy, global population limits, or another region's plans.

### 5.4 Entity minds

- **Squad Minds:** own persistent goals, route preferences, risk tolerance, and encounter memories for human groups.
- **Pack Minds:** own territory, hunger, fear, nesting, hunting, and migration objectives for mutant groups.
- **Actor Minds:** exist for named or otherwise salient individuals. They wake on meaningful events rather than every tick.

Logical minds may remain as dormant records without consuming a live provider slot. Runtime concurrency—not the number of persistent records—is the bounded resource.

### 5.5 Single-owner rule

Every writable plan scope has one owner at one world revision. Other agents submit proposals to that owner or to the Zone Director. No two agents, and no agent plus a legacy planner, may author the same scope concurrently.

The scope registry stores:

- scope kind and stable scope ID;
- current owner and authority mode;
- current world revision;
- active-plan ID and expiry;
- live/offline bindings;
- last accepted and rejected request IDs.

## 6. World model and information policy

### 6.1 Canonical world graph

xrSim stores coarse, LLM-legible, authoritative state:

- levels, regions, locations, routes, and level-transition portals;
- factions, relations, reputation, claims, and controlled territory;
- squads, packs, salient individuals, and statistical population cohorts;
- resource nodes, stocks, demand, supply, and logistics routes;
- active incidents and their observed effects;
- committed objectives, execution status, and plan expiry;
- stable offline-to-live entity bindings;
- append-only observations, decisions, and consequences.

Persisted handles remain stable index-plus-generation IDs. Persisted simulation quantities use deterministic representations consistent with the existing fixed-point/replay contract.

### 6.2 Facts, beliefs, and plans are separate

- **Canonical facts** are committed only by xrSim from validated commands or observed engine outcomes.
- **Private beliefs** belong to an agent and may be incomplete, stale, or wrong.
- **Plans** are desired future effects. A planned kill, capture, delivery, or migration is never a fact until the engine or deterministic offline executor reports the outcome.

This separation prevents omniscient NPC behavior and stops model narration from inventing world state.

### 6.3 Scoped observations

- The Zone Director receives aggregate state and cross-scope alerts.
- Domain minds receive the facts relevant to their domain plus permitted proposals from other domains.
- Region Minds receive local facts, committed strategic pressure, and cross-border summaries.
- Squad, pack, and actor minds receive sensory observations, known faction broadcasts, and their own memory.

All projections report guardrail-adjusted committed values, never raw rejected proposals. Every projection carries the world revision on which it was built.

### 6.4 Agent memory

Each agent has:

1. a versioned charter;
2. a bounded rolling reflection summary;
3. structured beliefs and relationships;
4. an episodic observation/intent/consequence log;
5. the last committed plan and its expiry.

Gate 1 retains this memory for the uninterrupted session. Save/load serialization is deferred, but the data structures must be versionable so Gate 2 can persist them without redesigning the contract.

## 7. Intent and commit contract

### 7.1 Intent envelope

Every Sonnet response decodes into a versioned envelope containing:

- request ID, agent ID, provider, and model;
- owned scope and scope ID;
- base world revision;
- wake reason and urgency;
- intended horizon and priority;
- preconditions and constraints;
- a bounded list of typed proposed operations;
- a concise rationale summary, not hidden chain-of-thought.

The response grammar is schema-only. Prose, Markdown fences, observation echo, unknown records, and out-of-scope operations are rejected as values and recorded.

### 7.2 Commit pipeline

1. Build a scoped, revision-stamped observation.
2. Queue an off-frame provider request.
3. Decode the final assembled response.
4. Validate schema, IDs, enums, ranges, authority, and base revision.
5. Apply conservation, rate, magnitude, and conflict guardrails.
6. Stage the complete change set without mutating live state.
7. Revalidate against the current revision at a simulation-tick boundary.
8. Commit atomically or reject the whole dependent operation group.
9. Append the verdict and committed delta to the ledger.
10. Feed observed results—not presumed success—back to the originating mind.

Stale envelopes are never silently rebased. The scheduler may discard them and enqueue a fresh wake with the new revision.

### 7.3 Typed execution operations

The first common operation vocabulary is:

- `create`, `dissolve`;
- `dispatch`, `move`, `retreat`, `resupply`;
- `claim`, `release`;
- `relate`;
- `set_objective`, `clear_objective`;
- `adjust_population`, `set_migration`;
- `set_supply`, `set_demand`, `set_route`;
- `propose_incident`.

Each operation declares its owner scope, preconditions, bounded effect, expiry, and required live/offline executor. Gate-specific additions must preserve the same validation and ledger path.

## 8. Sonnet runtime and wake scheduler

### 8.1 Provider policy

All live world-agent decisions use the configured Anthropic Sonnet model, currently `claude-sonnet-5`. There is no provider/model fallback in this program. Deterministic-null and recorded-response providers remain the CI and debugging floor but do not make creative world decisions.

The API key is accepted only through runtime environment configuration. It must never be committed, written into the ledger, logged, embedded in a scenario, or passed as a command-line argument visible to other processes.

### 8.2 Event-driven wakes

Meaningful events wake relevant minds: territory crossings, sightings, deaths, shortages, broken preconditions, failed objectives, emissions, player interference, and upstream plan changes. Sparse randomized heartbeats ensure that quiet regions still evolve.

A player event may schedule an asynchronous memory or strategic wake, but it never waits for Sonnet before producing the immediate live reaction. Existing deterministic perception and behavior handles the current frame; the later accepted plan may change future posture, reinforcement, avoidance, pursuit, or ambush behavior.

### 8.3 Runtime bounds

There is no cumulative session call limit. Stability bounds still apply:

- one in-flight request per agent;
- a configurable global concurrency ceiling, initially preserving the existing two-to-three live-request target;
- a configurable requests-per-minute ceiling that adapts downward to provider rate-limit responses;
- a bounded priority queue;
- trigger coalescing for compatible pending wakes;
- per-agent cooldowns and circuit breakers;
- bounded retries for an idempotent wake.

Priority order is:

1. future consequences of near-player events;
2. invalidated active plans and regional conflicts;
3. strategic domain and Zone planning;
4. quiet-region heartbeat and memory maintenance.

### 8.4 Non-blocking execution

Provider I/O, response assembly, and cancellation run off the frame thread. Prepared requests own their copied context. Completed results enter a bounded queue and are drained at a simulation-tick boundary. Shutdown and `ai.reset` cancel or safely abandon in-flight work.

No simulation or render path waits on HTTP, backoff, token streaming, response parsing, or another agent.

## 9. Failure and continuity behavior

Timeouts, transport errors, provider errors, rate limits, cancellation, malformed output, failed validation, and stale revisions all resolve to recorded values.

On failure:

1. the agent keeps its last valid plan until that plan expires;
2. the affected scope coasts on deterministic execution of the accepted plan;
3. retries, when allowed, use bounded exponential backoff with jitter;
4. repeated failures open a temporary per-agent circuit breaker;
5. expiry without replacement becomes a steady hold/continue posture rather than a fabricated new decision;
6. a later valid wake closes the continuity gap without replaying rejected mutations.

Unlimited session calls must not become an infinite retry loop. A rejection caused by the agent's own output cannot immediately re-wake the same agent without a cooldown or a materially changed observation.

The ledger records request metadata, wake reason, scoped revision, prompt/response hashes, model, token counts, latency, retry count, provider result, codec verdict, validation verdict, committed delta, and resulting world events. Secrets and hidden model reasoning are excluded.

## 10. Materialization and legacy cutover

### 10.1 Per-scope authority states

Every scope moves through four states:

1. **Observe:** import legacy entities into stable xrSim identities and compare state without changing behavior.
2. **Shadow:** Sonnet produces plans; xrSim validates and records them; legacy remains the sole authoring writer.
3. **Transfer:** freeze the legacy planner, reconcile live identities and revisions, stage the ownership change, and atomically grant authority to xrSim.
4. **AI-owned:** xrSim commits objectives; the compatibility adapter performs only bounded execution operations.

A failed transfer rolls back before the ownership record changes. Once the transfer commits, legacy authorship cannot silently resume. Developer rollback requires an explicit reverse transfer at a safe boundary; dual writers are never a fallback mode.

### 10.2 Offline and live identity

Stable xrSim IDs map to offline records and, when materialized, to X-Ray object IDs. The binding registry prevents duplicates and makes reconciliation the only path from observed live outcomes back into canonical state.

Player-touched anonymous entities are promoted to persistent salient records when their later identity or memory matters. Statistical cohorts remain appropriate for unobserved anonymous populations.

### 10.3 Live execution boundary

Near-player pathing, targeting, shooting, flanking, fleeing, hunting, and animation remain deterministic live behaviors. Committed AI plans set objectives, posture, routes, reinforcement needs, territorial targets, and memory-informed biases. Live systems choose frame-to-frame mechanics inside those constraints.

The executor redirects existing live entities whenever possible. It does not despawn and recreate a squad merely because its objective changed. Spawn and despawn operations are budgeted and trickled across ticks.

## 11. Gate 1: Garbage vertical slice

### 11.1 Scenario

A fixed scenario manifest creates stable starting conditions in Garbage:

- at least one stalker territorial group;
- at least one bandit territorial group;
- at least one mutant pack with a competing ecological need;
- contested locations, traversable routes, and bounded resource pressure;
- stable IDs and explicit ownership scopes for every participating group.

The version-controlled manifest enumerates exact group counts, entity sections, spawn anchors, initial owners, routes, resource quantities, and agent IDs. It does not inherit starting populations or ownership from an ambient save. The same manifest and seed are used for deterministic preflight and the live acceptance run.

Exact outcomes are not scripted. Factions and packs may contest, retreat, reinforce, relocate, avoid, pursue, or exploit opportunities as long as their actions satisfy the authority and conservation contracts.

### 11.2 Gate 1 authority

The Garbage Region Mind, participating Faction Minds, Ecology Mind, Economy Mind, relevant Squad/Pack Minds, and the Zone Director use the shared Sonnet runtime. xrSim owns the transferred Garbage objectives and canonical consequences. Legacy systems execute movement, spawning, perception, and combat only within committed objectives.

### 11.3 Immortal-player debug contract

The AgentBridge sends `g_god on` before the scenario begins. Immortality is a test harness condition, not an AI fact and not a gameplay-system modification. The player may receive attacks and status effects but must not die during the acceptance run.

The scripted interaction is:

1. minutes 0–10: observe without interference;
2. minutes 10–20: trespass into controlled territory;
3. minutes 20–25: fire warning shots near the territorial group responding to the trespass;
4. minutes 25–30: deliberately damage that group once without requiring a kill, then withdraw;
5. minutes 30–45: remain away while the Zone evolves autonomously;
6. minutes 45–60: return and observe memory-informed behavior.

The run is one uninterrupted 60-minute wall-clock session. Gate 1 does not use save/load.

## 12. Verification and acceptance

### 12.1 Deterministic preflight

Before a live-key soak:

- unit-test intent decoding, scope authorization, revision rejection, conservation/rate guards, authority transfer, coalescing, cooldown, and circuit-breaker behavior;
- inject timeout, rate-limit, malformed-response, cancellation, and stale-revision results;
- replay recorded provider responses with latency and completion-order perturbations and assert identical committed world state and ledger order;
- verify shutdown and `ai.reset` cancel provider work without hangs or use-after-free;
- verify a transferred scope records no legacy-authored objective.

### 12.2 Live 60-minute Sonnet soak

The gate passes only when all of the following hold:

- The engine completes 60 minutes without crash, hang, provider-induced frame blocking, or player death.
- Instrumentation reports zero frame/simulation-thread time waiting on provider work, and frame and simulation counters advance while every provider request is in flight.
- Each participating faction produces at least one accepted AI-authored objective change, and at least one mutant pack produces an accepted AI-authored objective change.
- At least one observable territorial, ecological, or resource consequence occurs without direct player causation.
- The provoked group records the encounter and reacts meaningfully on the player's return. Valid outcomes include hostility, avoidance, reinforcement, warning, pursuit, or ambush.
- Every live objective traces to a validated committed intent.
- Every claimed consequence traces to an observed engine or deterministic offline event.
- AI-owned scopes contain zero legacy-authored decisions after transfer.
- Queue bounds, cooldowns, retry limits, and circuit breakers prevent request storms.
- Any provider or codec failure that occurs during the live run coasts safely and later recovers on a valid wake; the injected failure cases are mandatory in deterministic preflight.
- Engine and AI logs contain no unhandled provider error, applied authority violation, conservation drift, deadlock warning, or secret material. Handled rejections remain in the ledger and do not fail this criterion by themselves.

### 12.3 Required artifacts

The soak produces:

- scenario manifest and seed;
- bridge transcript;
- phase-boundary screenshots;
- before/after and periodic xrSim state snapshots;
- authority-transfer audit;
- decision and consequence ledger;
- requests, tokens, latency, retry, error, queue-depth, and coast statistics;
- a machine-readable acceptance report identifying each criterion as pass or fail with evidence.

API use is measured and reported but does not fail the run because of a total call count or total token count.

## 13. Expansion gates after Garbage

### 13.1 Gate 2: cross-level continuity

Transfer adjacent regions and level portals. Preserve stable squad, pack, actor, plan, and memory identities across traversal and unload/reload. Add a save/load persistence gate only after uninterrupted cross-level continuity is green.

### 13.2 Gate 3: all-level offline Zone

Instantiate Region Minds and canonical coarse state for every supported level. Run strategic faction, ecology, economy, migration, and incident planning while levels are unloaded. Materialization must conserve identities and quantities when a player enters any region.

### 13.3 Gate 4: full live authority

Transfer all remaining live objective scopes. Disable legacy planners globally. Retain only deterministic execution mechanics and explicitly documented compatibility adapters. The final acceptance suite runs cross-level, long-duration, save/load, replay, provider-failure, and player-provocation scenarios.

## 14. Implementation constraints

- Develop and verify against the working OpenGL runtime.
- Use AgentBridge as the standard control, observation, screenshot, and soak harness.
- Preserve existing GL and legacy behavior outside explicitly transferred scopes.
- Use `VERIFY`/value-return conventions; model- or network-originated failure must never depend on C++ exceptions.
- Keep provider code, scheduler, world store, validators, authority registry, materialization bindings, and bridge/test harness in independently testable units.
- Preserve deterministic ordering for store mutation and replay even when provider requests finish out of order.
- Never log or persist an API key.

## 15. Gate 1 deliverable boundary

Gate 1 is complete when the authority spine operates end-to-end in Garbage:

1. persistent scoped agents wake through the Sonnet scheduler;
2. responses decode into revisioned intent envelopes;
3. xrSim authorizes, validates, stages, commits, and records them;
4. committed objectives steer real squads and mutant packs through the compatibility executor;
5. observed outcomes update canonical state and agent memory;
6. transferred scopes reject legacy-authored objectives;
7. the deterministic preflight and immortal-player 60-minute acceptance suite pass with the required artifacts.

Passing Gate 1 does not mean the full-Zone replacement is finished. It proves the one authority spine that subsequent gates expand without redesigning ownership or execution boundaries.
