# Squad, pack and lone NPC decisions in freeplay

Proposed for review on 2026-11-20.

## Player experience

Every living stalker and mutant in freeplay pursues an AI-authored objective. A squad might move camp after its scouts encounter dogs. A lone stalker who never saw the dogs could still choose the same road. A player can follow either party, meet them again after a level change and resume their journey from a save. The same rules apply in Cordon, Garbage and the rest of the Zone. There is no story-mode prerequisite.

## Approaches

The player chose one AI per squad or pack. Members follow the shared plan. A lone stalker or mutant chooses its own objective. Giving every member a separate mind would allow personal disagreements within a group and require arbitration and additional model calls. The chosen design puts those decisions at the level the player can observe as coordinated movement. A group mind belongs to the group and survives the loss of its in-world leader.

## Population and ownership

Proposed activation is a saved freeplay setting available for new and existing games. Start the first Cordon playtest with it enabled. Old saves open with their previous behavior until the player enables AI. On first activation, check provider configuration and runtime support before transferring any objectives. If either is missing, explain the issue and leave the save eligible for another attempt. An already AI-enabled save resumes its accepted objectives even during a provider outage.

Enroll existing living NPCs from the ALife server registry when starting or loading freeplay, and update enrollment on births, deaths and membership changes. The population includes stationary stalkers and solitary mutant species. Treat fixed posts, mobility limits and valid activity areas as constraints on available objectives. The player character is outside this population.

For recruited companions, treat explicit player orders as constraints on the owner's choices. A wait order anchors the companion to that location and a follow order anchors travel to the player. The order lasts until the player replaces it or dismisses the companion. Preserve it across saves and expose it in the owner's observation. Immediate danger responses can interrupt execution. Resume the order afterward.

Use actual freeplay group membership. Two dogs standing together can still be independent. A group with one living member gives that survivor sole decision authority. Preserve its current feasible objective when transitioning between a group and a lone owner. On joining a multi-member group, the joining NPC adopts the group's objective. On a split, children inherit the last feasible objective as their starting point and reconsider using only observations available to them. Retire an owner when its last member dies or leaves. On a merge into an existing group, that group's plan wins. Changes in membership, ownership and current objectives commit together.

Persist stable actor and group identities, current membership, accepted objective, observations needed to resume it, and progress in the same save generation as the ALife world. Proximity changes can replace a live engine object without changing the identity of the NPC. Reused server IDs must never inherit a previous occupant's mind. An older freeplay save bootstraps from its actual population. Loading a save invalidates outstanding decisions from the abandoned timeline.

## Decisions that reach the world

Each owner observes its members, their location, known routes, relevant sightings and its current objective. Spawn, membership change, a significant sighting, arrival or failure can wake the owner. Off-screen owners also need opportunities to reconsider. Merge duplicate wakes, bound concurrency and give waiting owners a fair turn. Use the existing asynchronous Sonnet provider path and project model-verification policy. Render and simulation frames never wait for a reply.

Start with decisions whose effects a player can check, such as staying at an available site, moving to a reachable site, or retreating to one. Present valid destinations in the observation. Validate owner, subject, membership revision, current world lifetime and destination again before committing. When an answer arrives late, a route is invalid, or the provider is unavailable, continue the last feasible plan. An owner with no feasible plan can hold position and retry. Record the reason and expose the difference between a hold and a live model decision.

A single owner writes the long-term objective. ALife and CoC scripts can supply membership, navigation and site constraints. Gate competing destination writers at the point of task selection. Feed the accepted objective to both off-screen travel and visible stalker or species-specific movement. Local perception, collision, animation and immediate danger responses remain engine operations. When a reaction ends, resume the objective if feasible or report the obstacle to its owner. Report arrival, casualties and blocked routes from observed state. An order in the xrSim command log establishes only that a decision was recorded.

## Delivery and evidence

1. Discover the actual freeplay population in a private Cordon save. Through the bridge, compare every living stalker and mutant with its sole objective owner. Include a native squad, pack, lone stalker and lone mutant. Record missing bindings and script-owned destination writers.
2. Exercise different valid destinations from the same starting state with recorded decisions. Track the same NPC identities and verify their positions, arrivals or observed obstacles. Cover online and off-screen movement, including a lone owner joining a group, a group losing its leader and a stale reply after a split.
3. Repeat with autonomous model wakes. Observe at least one accepted live decision and its effect for each owner type without manual spawning or waking. Keep a record of provider failures, decision latency, queue age and frame times alongside the existing GL regression gate.
4. Follow a group across the Cordon–Garbage boundary, then save, quit and reload. Reconcile identities, membership, objective and progress. Check a pre-feature save and NPCs added after loading. Expand population and movement checks across installed freeplay maps and species before claiming Zone-wide coverage. Cordon is the first test location.

This design extends the [authority design](2026-07-14-full-zone-ai-authority-spine-design.md) from fixture groups to an actual freeplay population. It uses the existing single-owner, validated-command and asynchronous-provider contracts. The builder chooses the save encoding, binding adapters and scheduler constants after measuring the runtime. The [project inventory](../../PLAN_STATUS.md) records the outstanding GL comparison and AI acceptance work. This proposal does not establish runtime acceptance or reschedule that comparison.
