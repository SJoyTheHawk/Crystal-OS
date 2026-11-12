# Crystal OS App Platform v2 — Documentation Overview

**Date:** 2026-11-11  
**Status:** Phase 14 closed; Phase 15 in progress; Phase 16 runtime slice implemented
**Documents:** 3 guides + this overview

**Versioning note:** Crystal OS v1 = Phases 1-13 (baseline OS with bundled apps). Crystal OS v2 = Phase 14+ (app platform, publication release). All documents describe v2.

**Phase 14 closure (2026-11-11):** Closed at the user's request after successful firmware builds, host validation, and the Calculator board run. The plan, design, and code checklists are marked complete for the implemented Phase 14 scope. The `crystal_service`/TLS watchdog investigation and deferred hardware checks are tracked separately in [Phase 14 closure notes](PHASE_14_IMPLEMENTATION.md). Phase 15 remains in progress. A–F/G–M/N–R device tests establish pairing/authentication, signed Wi-Fi installs, signature rejection, upload progress, CLI logs/run, browser Live logs including mobile, and management/cleanup. R adds real-token alteration/invalidation for the system query. Browser USB, stream servicing/authorization/shutdown, hosted catalog workflow/scope alignment and remaining tests are detailed in the [closure checklist](APP_PLATFORM_V2_PHASE_15_16_CLOSURE_CHECKLIST.md).

**Phase 16 progress (2026-11-13):** The package-backed declarative runtime
slice builds and has a packable dashboard example. Clock, Weather and
Calculator visual references launch in production LVGL under the host
integration suite. K/L/M now confirm Dashboard layout, time/timer behavior,
repeated lifecycle and cleanup on device. N confirms launcher caption truncation
with full management names, and O confirms reconnect UI. Dynamic property/event/
focus APIs, combined-load/headroom evidence, timing classification and ABI
review/freeze remain open; see the closure
checklist and the Phase 16 checklists in the plan and code guide.
Weather fetching and Calculator computation remain in their current
runtimes, and Weather stays bundled.

---

## What You Have

Three comprehensive documents covering the v2 app platform (Phases 14-17):

### 1. **APP_PLATFORM_V2_IMPLEMENTATION_PLAN.md**
   - **Audience:** Architects, project leads, phase planners
   - **Purpose:** What is being built, why, and when
   - **Contents:**
     - Core architecture decisions (handles, manifests, lifecycle)
     - On-device storage layout
     - Manifest schema (complete spec)
     - Package format (`.capp` binary spec)
     - Registry and boot reconciliation
     - **Host ABI v1 (complete):**  
       - UI widgets: screen, container, row/col, **scroll_view**, label, button, image, list (with **rich items**), slider/switch/arc, chart, textarea, tabview, **spinner**, **dialog**
       - Properties and layout: set_text, set_value, set_hidden, set_enabled, **set_focus**, set_style (colors as 0xRRGGBB), scroll_to, get_size, on/off events, timers
       - **Animations:** anim() for opacity/color/radius with easing
       - Services: time, timer, notify, http (64KB limit documented), net, location, system
       - **State:** nested tables supported, flush on onPause
       - **JSON:** crystal.json.encode/decode bundled
       - Filesystem: read/write/exists/list
     - Three runtime tiers (declarative, script, native)
     - Sandbox and safety model
     - Control panel design
     - Partition table
     - Phase breakdown (14-17) with exit criteria
     - Resource budgets and quotas
     - Animation and UX considerations
     - **Known limitations (new §14):** HTTP body size, network images, keyboard defocus, animation cleanup, no IPC
     - Open questions and future phases
     - Implementation checklist
   - **Length:** ~520 lines
   - **When to read:** First; establishes the complete contract

### 2. **APP_PLATFORM_V2_DESIGN_GUIDE.md**
   - **Audience:** Senior developers, architects making design decisions
   - **Purpose:** Why each decision was made and what trade-offs exist
   - **Contents:**
     - Design principles (resource cost first, handles not pointers, etc.)
     - Lifecycle design and reasoning
     - Resource isolation and admission control patterns
     - State persistence across lifecycle
     - Animation and frame budget discipline
     - Async HTTP design
     - Permission model
     - Sandbox design (count hook, allocator hook, environment)
     - Control panel architecture
     - Partition and storage architecture
     - Phase dependencies
     - Animation design patterns
     - Error handling and recovery
     - Design checklist (per-phase)
   - **Length:** ~360 lines
   - **When to read:** After Implementation Plan; explains the "why"

### 3. **APP_PLATFORM_V2_CODE_GUIDE.md**
   - **Audience:** Developers implementing Phase 14-17 code
   - **Purpose:** Concrete patterns and code templates to follow
   - **Contents:**
     - Project structure and build setup
     - **Script tier: 10 Lua examples** (minimal clock, stateful favorites, async HTTP + JSON, rich list, animation, dialog/spinner, scroll_view, tab focus, state machine navigation, plus 2 patterns)
     - Native tier: 3 C++ examples (minimal, service handles, worker threads)
     - Declarative tier: 3 JSON UI examples (minimal, data binding, conditionals)
     - ABI implementation: host side (C/C++ patterns)
     - Boot and installation code patterns
     - Testing and debugging setup
     - Code review checklists (per-phase)
   - **Length:** ~480 lines
   - **When to read:** When writing code; copy patterns from here

---

## How to Use These Documents

### For Project Planning

1. Read **Implementation Plan** §1-6 (30 min) → understand architecture
2. Read **Implementation Plan** §11 (15 min) → understand phases and dependencies
3. Read **Implementation Plan** §15 (5 min) → review checklist for your phase

### For Architecture Review

1. Read **Implementation Plan** §1-2 (decisions established)
2. Read **Design Guide** (explore trade-offs and reasoning)
3. Read **Implementation Plan** §12-14 (open questions that still need decisions)

### For Code Implementation

1. Read **Implementation Plan** §6 (ABI spec you're implementing)
2. Read **Design Guide** §6-8 (design patterns for your tier)
3. Read **Code Guide** §2-4 (code examples for your language/tier)
4. Reference **Code Guide** §5-8 while coding (implementation patterns)

### For Testing and Verification

1. Read **Implementation Plan** §11 (phase exit criteria)
2. Read **Code Guide** §7 (testing templates)
3. Read **Code Guide** §8 (code review checklists)

---

## Quick Reference: Key Decisions

| Decision | Location | Rationale |
|----------|----------|-----------|
| Resource cost is primary constraint | Plan §1, Design §1 | ESP32-S3 has real limits |
| Handles, not pointers | Plan §1.2, Design §1.2 | Safety and isolation |
| Immutable packages, mutable state | Plan §1.3, Design §1.3 | Clean uninstall |
| One app at a time, lifecycle explicit | Plan §1.4, Design §1.4 | Simpler model |
| ABI is the contract | Plan §1.5, Design §1.5 | Longevity and flexibility |
| onCreate runs every launch | Plan §6.6, Design §2.2 | No saved state tree |
| onPause is only guaranteed write | Plan §6.6, Design §2.3 | Crash safety |
| Async HTTP only | Plan §6.3, Design §6.1 | LVGL task must not block |
| Sandbox: count hook + allocator hook | Plan §8, Design §8 | Responsiveness + memory safety |
| Namespace isolation for script apps | Plan §6.4, Design §4.1 | No collisions, clean uninstall |
| LittleFS for apps partition | Plan §10, Design §10 | Power-fail safe, wear-balanced |
| Boot reconciliation | Plan §5, Design §10.2 | Hand-delete is legitimate uninstall |

---

## Mapping to Existing Documents

### How v2 Relates to v1 Platform

| v1 Component | v1 Location | v2 Equivalent | v2 Location |
|--------------|------------|---------------|------------|
| Bundled apps (Clock, Weather, Calculator) | DESIGN.md §7-9 | Kept as-is; become `origin=BUILTIN` | Plan §5, Code §3.1 |
| CrystalApp lifecycle | IMPLEMENTATION_PLAN.md §4 | Extended; still onCreate/onResume/onPause/onDestroy | Plan §6.6 |
| CrystalState NVS | IMPLEMENTATION_PLAN.md §4 | Bundled apps unchanged; script apps get namespaced keys | Plan §6.4, Code §3.2 |
| Animations | DESIGN.md §2, §12 | No change in animation engine; apps must respect frame budget | Design §5, Design §12 |
| LVGL facade | DESIGN.md §10 | ABI v1 crystallizes façade; declarative tier (Phase 16) | Plan §6, Design §6, Code §4 |

### Bus App Lessons Incorporated

| Bus App Finding | Implementation Plan | Design Guide | Code Guide |
|-----------------|-------------------|-------------|-----------|
| No per-instance resource ownership | §11 Phase 14 req. 1 | §3.1 | §3.2, §5.1 |
| No memory budget enforcement | §11 Phase 14-15 | §3.2 | §4.2, §5.1 |
| No permission model | §11 Phase 14, §7.1 | §7 | §5.4 |
| No transactional teardown verification | §11 Phase 14 req. 2 | §3 | §6.3 |
| No state namespace isolation | §11 Phase 14 req. 3 | §4.1 | §3.2, §5.3 |
| Async HTTP pattern validated | §6.3, §11 Phase 17 | §6 | §2.3, §5.4 |
| onCreate budget guidance needed | §6.6, §11 Phase 17 | §2.5, Design §12.2 | §2.1 |

---

## Document Consistency Notes

All three documents are aligned:

- **Numbers and specs:** Implementation Plan is authoritative. Design Guide explains why. Code Guide shows how.
- **Phase sequencing:** All three agree on Phase 14 → 15 → 16 → 17 order (with 15-17 potentially reorderable after 14 stable).
- **ABI spec:** Implemented in Code Guide §5; designed in Design Guide §6; specified in Implementation Plan §6.
- **Examples:** Code Guide examples use v1's Clock, Weather, Calculator as baseline; all show v2 patterns.
- **Checklists:** Phase checklists in Implementation Plan §15 → Code Guide §8 with detailed code examples.

---

## Next Steps

### If You're Starting Phase 14

1. **Day 1:** Read Implementation Plan §1-6 (1 hour)
2. **Day 2:** Read Implementation Plan §11 Phase 14 spec (1 hour)
3. **Day 3-4:** Read Design Guide §3 (resource isolation) and §8.3 (uninstall cleanup) (2 hours)
4. **Day 5+:** Start coding; reference Code Guide §5 (boot/install) and §6 (ABI implementation)

### If You're Reviewing Phase 14 Design

1. **Read:** Implementation Plan §1.2, §6, §11 Phase 14
2. **Read:** Design Guide §1-3, §10
3. **Discuss:** Are admission control thresholds correct? Is LittleFS the right choice? Should Phase 15 integration happen sooner?

### If You're Planning Phase 15-17

1. **Read:** Implementation Plan §11 for your phase
2. **Read:** Design Guide for your tier (§6 for HTTP, §8 for sandbox, §4 for declarative)
3. **Read:** Code Guide §2-4 for your tier
4. **Reference:** Existing v1 implementations (Clock, Weather, Calculator) as baseline

### If You're Debugging a Phase 14+ Issue

1. **Check:** Implementation Plan §12 "Open Questions" for known issues
2. **Check:** Design Guide §13 "Error Handling and Recovery"
3. **Check:** Code Guide §7 "Testing and Debugging"
4. **Reference:** APP_PLATFORM_BUS_LESSONS.md for similar resource/memory failures

---

## Known Unknowns (Open Decisions)

These are documented in Implementation Plan §12 and Design Guide §14. Decide before the named phase:

| Question | Phase to Decide | Location |
|----------|-----------------|----------|
| ABI stability: How long does `abi: 1` live? | Before Phase 16 ships | Plan §12.1 |
| Curated catalog delivery | Resolved before Phase 15: a computer-side client fetches a manually published, signed JSON catalog and prebuilt `.capp` files over HTTPS; install to device uses the developer transport; no server build service | Plan §12.2 |
| Native app isolation: Coding rules or force-disable on uninstall? | Before Phase 18 | Plan §12.3 |
| App lifecycle during OTA: Graceful shutdown or forced termination? | Before Phase 12 OTA ships | Plan §12.4 |

---

## File Locations

```
/Users/szemy/Workspace/ESP32 Crystal OS/docs/
├── APP_PLATFORM.md                           (v1 reference; don't update)
├── APP_PLATFORM_V2_IMPLEMENTATION_PLAN.md    (NEW; use this for Phase 14+)
├── APP_PLATFORM_V2_DESIGN_GUIDE.md           (NEW; use this for architecture)
├── APP_PLATFORM_V2_CODE_GUIDE.md             (NEW; use this for coding)
├── APP_PLATFORM_BUS_LESSONS.md               (existing; reference for validation)
├── BUS_APP_VALIDATION_SUMMARY.md             (existing; reference for evidence)
└── (other docs)
```

The v1 `APP_PLATFORM.md` remains for reference. The three v2 documents are standalone and complete.

---

## How to Hand Off to an Agent

Phase 14 is closed. The prompts below are reusable templates; their sample
checkboxes do not represent the current implementation status. Use the completed
phase checklists and closure notes above for that status.

**To delegate Phase 14 implementation:**

```
I'm ready to start Phase 14: Package format, registry, and resource management.

Here are the three guides:
1. APP_PLATFORM_V2_IMPLEMENTATION_PLAN.md — what to build
2. APP_PLATFORM_V2_DESIGN_GUIDE.md — why it's designed this way
3. APP_PLATFORM_V2_CODE_GUIDE.md — concrete code patterns

Your goals:
- Implement LittleFS boot reconciliation (Plan §5, Design §10.2, Code §6.1)
- Implement .capp format and crystal pack CLI (Plan §4, Code §4.1)
- Implement per-app handles and resource tracking (Plan §1.2, Design §3, Code §5.1-5.2)
- Convert Calculator to .capp format and test install/launch/uninstall cycle
- Exit gate: Calculator passes clean cycle 10 times, crystal_app_verify_cleanup detects zero leaks

Start with the Code Guide §5 (boot and installation patterns); reference Design Guide §3 for resource isolation rationale.
```

**To delegate code review:**

```
Review Phase 14 implementation against these criteria:

From Implementation Plan §15 Checklist:
- [ ] LittleFS partition format at factory
- [ ] .capp format round-trip: pack → sign → install → uninstall
- [ ] Admission control rejects when memory_kb sum exceeds
- (etc. — copy full checklist)

From Code Guide §8 Code Checklist:
- [ ] All services use create(app_id) and destroy() pair
- [ ] crystal_app_verify_cleanup() detects leaked tasks
- (etc.)

Cross-reference Design Guide §3 for resource isolation patterns and Design Guide §10 for partition design rationale.
```

---

## Summary

You now have:

1. **A complete v2 architecture** (Implementation Plan)
2. **Design reasoning and trade-offs** (Design Guide)
3. **Concrete code patterns and templates** (Code Guide)

All three are aligned, traceable, and ready to hand to agents or teams for implementation. The documents incorporate all bus app lessons and are grounded in the ESP32-S3's real resource constraints.

**Start with the Implementation Plan.** It's the source of truth. Everything else flows from it.

