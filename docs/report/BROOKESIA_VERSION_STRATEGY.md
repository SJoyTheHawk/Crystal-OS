# ESP-Brookesia Version Strategy

**Document status**: Active (2026-10-18)  
**Current Brookesia version**: 0.4.2 (released 2024-12-11)  
**Latest upstream version**: 0.7.0 (architectural redesign, May 2026)

## Executive Summary

Crystal OS should **remain on esp-brookesia 0.4.2** and continue the current
vendoring approach with documented local patches. The project is already
architecturally independent where it matters, and updating to v0.7+ would
introduce substantial migration risk without clear user-facing benefits at this
stage of development.

## Current State Analysis

### Brookesia Usage in Crystal OS

**Version**: 0.4.2, vendored as a project component at `components/esp-brookesia/`

**Local modifications** (documented in `LOCAL_PATCHES.md`):
1. Disabled Recents screen with null guards in manager source
2. Replaced default wallpaper with Crystal OS branding

**Integration points**:
- `ESP_Brookesia_Phone` and `ESP_Brookesia_PhoneApp` base classes
- `ESP_Brookesia_Gesture` for gesture event data structures
- Status bar, launcher grid, and app lifecycle (`close()`/`run()` destroy-on-switch)
- Visual area calculation and app container management

**Coupling assessment**: **Loose**
- Only 2 references to `esp_brookesia` symbols in Crystal components
- Crystal OS uses Brookesia's public API, not internal implementation details
- Most custom functionality lives in parallel `crystal_*` components

### Crystal OS Independence

Crystal OS has already built **parallel infrastructure** that doesn't depend on
Brookesia internals:

| Subsystem | Implementation | Brookesia Dependency |
|-----------|----------------|---------------------|
| HTTP client | `crystal_http` | **None** — custom implementation |
| Shell & overlays | `crystal_shell` | **Low** — uses gesture events only |
| Core services | `crystal_core` | **None** — independent services |
| Gesture arbiter | `crystal_shell` | **Low** — wraps Brookesia gesture |
| Keyboard overlay | `crystal_shell` | **None** — shell-owned |
| Settings | `crystal_shell` | **None** — shell-owned overlay |
| Power management | `crystal_hal` + `crystal_core` | **None** |
| App framework | `crystal_app` | **Medium** — extends `PhoneApp` |

The project memory documents capture this independence:
- *"perform() eats the HTTP body"* — Crystal implemented `crystal_http` to work around Brookesia/ESP-IDF HTTP limitations
- *"Brookesia HOME bypasses shell hooks"* — Crystal built its own navigation model
- *"Reserve space, never fill it"* — Crystal established its own design patterns

### Upstream Version Landscape

**v0.4.2** (December 2024) — Current Crystal OS baseline
- Stable release with known behavior
- 24h format clock support
- Gesture navigation back disabled by default
- 720x1280 stylesheet added

**v0.5.0** (Available on ESP Registry)
- ESP32-P4 support added
- Additional stylesheets
- Status: GitHub release not yet published (see [Issue #66](https://github.com/espressif/esp-brookesia/issues/66))

**v0.7.0** (May 2026) — Major architectural redesign
- [Espressif blog post](https://developer.espressif.com/blog/2026/05/esp-brookesia-new-architecture-0-7/) describes "layered architecture with well-defined extension points"
- Designed to be "easier to port, easier to scale, and easier to reuse"
- Status: **Breaking changes expected** based on "new architecture" framing

## Decision Matrix

### Question 1: Should Crystal OS update to v0.7+?

**Decision: No, not now.**

**Rationale**:

1. **High migration risk**
   - v0.7 is described as an architectural redesign, suggesting breaking API changes
   - 18 months of upstream evolution since Crystal's 0.4.2 baseline
   - Crystal is at Phase 13 of production development — introducing foundational changes now adds instability

2. **Uncertain benefit**
   - No documented feature in v0.5.0 or v0.7.0 that addresses a blocking Crystal OS issue
   - Crystal has already worked around limitations (custom HTTP, custom gestures, custom overlays)
   - The disabled-Recents patch works correctly and is stable

3. **No blocking issues**
   - Brookesia 0.4.2 performs correctly with Crystal's null-guard patches
   - All Phase 0-11 exit criteria passed on hardware
   - Phase 12-13 work doesn't require Brookesia changes

4. **Better use of resources**
   - Migration effort (API changes, testing, regression) would delay Phases 12-17
   - Value delivery is in the layers *above* Brookesia, not in the widget foundation

**Exception conditions** (revisit this decision if):
- A critical security vulnerability is discovered in Brookesia 0.4.2
- A v0.7+ feature becomes essential for Phase 14-17 app platform work
- Brookesia 0.4.2 blocks a confirmed hardware target (unlikely — Crystal targets ESP32-S3)

### Question 2: Should Crystal OS fork the Brookesia repository?

**Decision: No, continue vendoring with documented patches.**

**Rationale**:

1. **Minimal divergence**
   - Only 2 modifications: disabled Recents + wallpaper replacement
   - These are surgical, well-contained changes
   - LOCAL_PATCHES.md clearly documents what was changed and why

2. **Forking overhead**
   - Creates maintenance burden to track upstream commits
   - Suggests more divergence than actually exists
   - Makes it harder to selectively pull upstream bug fixes

3. **Current approach is correct**
   - Vendored as a project component (takes precedence over `managed_components/`)
   - Modifications documented in LOCAL_PATCHES.md
   - Easy to diff against upstream if needed

**Best practice**: Continue the vendoring approach. If Crystal ever needs >5 substantial patches or modifies core Brookesia logic, then reconsider forking.

### Question 3: Can Crystal OS become independent of Brookesia?

**Decision: Partially independent already; full independence is unnecessary.**

**Current independence level**: **~70%**

**Already independent**:
- ✅ HTTP client (`crystal_http` replaces Brookesia/ESP-IDF HTTP)
- ✅ Gesture arbitration (`crystal_shell` arbiter wraps Brookesia gesture events)
- ✅ System overlays (keyboard, quick settings, Settings app are shell-owned)
- ✅ Core services (RTC, WiFi, power, timers in `crystal_core`)
- ✅ Hardware abstraction (`crystal_hal` with device adapters)

**Still relies on Brookesia**:
- 🔶 `ESP_Brookesia_PhoneApp` base class and lifecycle callbacks
- 🔶 `ESP_Brookesia_Phone` app container and manager
- 🔶 Status bar widget implementation
- 🔶 Launcher grid widget and icon rendering
- 🔶 Visual area calculation and screen management

**Going fully independent** would require:
- Reimplementing `PhoneApp` lifecycle container
- Reimplementing status bar widget (battery, WiFi, clock, indicators)
- Reimplementing launcher grid with icon positioning and scrolling
- Reimplementing app visual area management

**Cost-benefit assessment**: **Not worth it**

The remaining Brookesia dependencies are:
1. **Working correctly** — no bugs or limitations blocking Crystal
2. **Stable** — widgets don't require upstream changes
3. **Well-encapsulated** — Crystal extends them cleanly via public API

The value Crystal delivers is in the **layers above** the widget foundation:
- Novel gesture arbiter and card transition
- Shell-owned keyboard with field lifting
- Custom Settings overlay architecture
- App registry and lifecycle model
- Service architecture and power management

Reimplementing Brookesia's widget layer would be **high effort for zero user-facing value**.

## Strategy Recommendations

### Short Term (Phases 12-17, current development)

1. **Stay on Brookesia 0.4.2**
   - Continue vendoring at `components/esp-brookesia/`
   - Maintain LOCAL_PATCHES.md for any new modifications
   - Do not attempt migration to v0.5.0 or v0.7.0

2. **Document all local changes**
   - Update LOCAL_PATCHES.md if adding new Brookesia modifications
   - Keep null-guard pattern for disabled Recents
   - Preserve wallpaper replacement process

3. **Monitor upstream selectively**
   - Watch Espressif's esp-brookesia releases for **critical security fixes only**
   - Do not track feature additions or enhancements
   - Check release notes for anything marked "security" or "CVE"

4. **Preserve architectural independence**
   - Continue building features in `crystal_*` components, not Brookesia modifications
   - Keep gesture arbiter, overlays, and services in the Crystal layer
   - Use Brookesia's public API, never reach into internal implementation

### Long Term Considerations

**Reconsider migration to v0.7+** only if:

| Condition | Likelihood | Action if occurs |
|-----------|-----------|------------------|
| Critical security vulnerability in 0.4.2 | Low | Apply minimal patch or upgrade |
| v0.7+ provides blocking feature for Phase 14-17 | Low | Evaluate migration cost vs. value |
| Accumulate >5 substantial LOCAL_PATCHES | Low | Consider forking or upgrading |
| Brookesia 0.4.2 blocks new hardware target | Very low | Evaluate target requirements |

**Reconsider going fully independent** if:

| Condition | Likelihood | Action if occurs |
|-----------|-----------|------------------|
| Brookesia lifecycle model blocks app platform | Medium | Extract to `crystal_phone` component |
| Need fundamental changes to app container | Low | Implement custom container |
| LOCAL_PATCHES become unmaintainable | Very low | Fork or reimplement |

**Most likely long-term path**: Crystal OS remains on Brookesia 0.4.2 through v1.0,
then selectively evaluates v0.7+ **after** Phases 14-17 are complete and the app
platform architecture is validated. By that point, the cost-benefit of migration
will be clearer, and v0.7+ will have matured.

## Technical Context

### Why Crystal Built Parallel Infrastructure

The project memory documents reveal that Crystal OS built its own subsystems
to work around limitations discovered during development:

1. **`crystal_http`** — *"perform() eats the HTTP body"*  
   ESP-IDF's `esp_http_client_perform()` consumes the response body, making
   `read_response()` always empty. Crystal implemented `open/fetch_headers/read/close`
   pattern for the Weather app and future HTTPS needs.

2. **`crystal_shell` gesture arbiter** — *"Brookesia HOME bypasses shell hooks"*  
   Brookesia's HOME gesture consults no hooks, only BACK routes through callbacks.
   Crystal built a unified arbiter that owns all edge gestures and can delegate
   to apps or system overlays.

3. **Shell-owned keyboard** — *"LVGL textarea never defocuses itself"*  
   LVGL 8.4 has no DEFOCUSED handler, so fields keep blinking. Crystal's keyboard
   overlay sends defocus on teardown and manages field visibility lifting.

These weren't Brookesia limitations per se — they were integration challenges
that Crystal solved at a higher layer. This demonstrates that the **architecture
is already layered correctly**: Brookesia provides widgets, Crystal provides
the integration and policy layer above them.

### Brookesia API Surface Used by Crystal

From source analysis, Crystal components reference:

**Class inheritance**:
- `ESP_Brookesia_PhoneApp` — base class for all Crystal apps
- App lifecycle callbacks: `onCreate()`, `onResume()`, `onPause()`, `onDestroy()`

**Data structures**:
- `ESP_Brookesia_PhoneAppData_t` — app metadata for launcher registration
- `ESP_Brookesia_GestureInfo_t` — gesture event data (direction, progress)
- `ESP_Brookesia_CoreAppEventData_t` — app lifecycle event payloads

**Objects**:
- `ESP_Brookesia_Phone` — system instance pointer
- `ESP_Brookesia_Gesture` — gesture event source

**Methods**:
- `phone->getManager().getGesture()` — access gesture subsystem
- Various app lifecycle methods inherited from `PhoneApp`

All of these are **public API**. Crystal does not reach into Brookesia's
private implementation, headers, or internal data structures. This clean
dependency boundary is why Crystal can stay on 0.4.2 indefinitely.

## Implementation Notes

### Keeping LOCAL_PATCHES.md Current

When modifying vendored Brookesia files, update LOCAL_PATCHES.md immediately:

```markdown
# Crystal OS local override

This directory vendors ESP-Brookesia 0.4.2 from the ESP-IDF Component Registry.
It is a project component so ESP-IDF selects it ahead of `managed_components/`.

## Modifications

1. **Disabled Recents screen** (Phase 11, 2026-09-11)
   - Files: `src/systems/phone/esp_brookesia_phone_manager.cpp`
   - Change: Treat `nullptr` Recents object as supported configuration
   - Rationale: Crystal uses fixed card order, not recents chooser
   - Keep null guards when updating vendored component

2. **Crystal OS wallpaper** (2026-10-18)
   - Files: `src/assets/images/middle/esp_brookesia_image_middle_wallpaper_dark_480_480.c`
   - Change: Replaced default wallpaper with Crystal branding
   - Rationale: Product identity

The upstream component remains licensed under Apache-2.0; see `license.txt`.
```

### Evaluating Future Upstream Fixes

If a critical Brookesia fix is published:

1. **Read the upstream commit** to understand scope
2. **Check if LOCAL_PATCHES conflict** with the fix
3. **Apply the fix manually** to vendored component if needed
4. **Test the Phase 11 validation checklist** (Settings, power, disabled Recents)
5. **Update LOCAL_PATCHES.md** to document the merge

Do not blindly update to a new Brookesia release. Cherry-pick specific fixes
only when they address confirmed Crystal OS issues.

### If Migration Becomes Necessary

Should Crystal OS ever need to migrate to v0.7+:

**Pre-migration checklist**:
- [ ] Document all LOCAL_PATCHES as Git patches
- [ ] Archive current working firmware binary and ELF
- [ ] Run full Phase 0-13 validation baseline on 0.4.2
- [ ] Create migration branch, do not work on main
- [ ] Review v0.7 changelog for breaking API changes
- [ ] Update LOCAL_PATCHES to v0.7 equivalent locations
- [ ] Rebuild and fix compilation errors
- [ ] Re-run Phase 0-13 validation on new version
- [ ] Compare behavior differences and assess acceptability

**If validation fails**: roll back, document blockers, and stay on 0.4.2.

## References

- **Upstream repository**: https://github.com/espressif/esp-brookesia
- **ESP Registry component**: https://components.espressif.com/components/espressif/esp-brookesia
- **v0.7 architecture blog**: https://developer.espressif.com/blog/2026/05/esp-brookesia-new-architecture-0-7/
- **Crystal OS implementation plan**: `docs/IMPLEMENTATION_PLAN.md`
- **Crystal OS design**: `docs/DESIGN.md`
- **Brookesia local patches**: `components/esp-brookesia/LOCAL_PATCHES.md`

## Conclusion

Crystal OS's current Brookesia strategy is **architecturally sound**:

1. **Vendored 0.4.2 is stable** and meets all Phase 0-11 requirements
2. **Local patches are minimal** and well-documented
3. **Crystal's architecture is already independent** where it matters
4. **Migration to v0.7+ carries high risk** with no clear benefit at Phase 13

**The recommendation is clear**: stay on Brookesia 0.4.2, continue vendoring
with documented patches, and focus development effort on the Crystal layers
above the widget foundation. Revisit this decision only if blocking issues
emerge or after Phase 14-17 app platform work validates the architecture.
