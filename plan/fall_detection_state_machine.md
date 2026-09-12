# Fall Detection: 7-State Posture Machine

Replaces the single-frame fall rule. Companion to `plan/fall_detection.md`, which documents the
existing system and its defects — read that first for the file map and hardware wiring.

## Why

The old rule decided a fall from **one frame**:

```c
velocity_y > 40 && ratio_velocity > 0.6 && current_ratio > 1.2
```

Sitting on a chair, crouching, and falling are near-identical *at the instant of descent* — the
bounding box shows the same thing. The only discriminator visible from bbox data is what happens
over the following 1–3 seconds. That is inherently stateful, so no re-tuning of those three
thresholds separates the classes. `SUSTAINED_FALL_MS 1500` sat in the file declared and never
referenced, which says the original design intended a confirmation window and stopped halfway.

**Goal:** detect forward / backward / sideway / axial falls; do not alarm on sitting down, lying
down, or crouching.

## Status

**Phase 0 landed** (uncommitted, compiles clean):

- Removed the 3-way AND and `FALL_RATIO_THRESHOLD` / `VELOCITY_Y_THRESHOLD` / `RATIO_SHIFT_THRESHOLD`
- Fixed the velocity bug — `p.y` / `p.ratio` still hold the previous frame's values where velocity
  is computed, so they are read directly and `last_y` / `last_ratio` are deleted. The old code
  spanned two frames of displacement over one frame of `delta_time`.
- Replaced the 15 s `vTaskDelay` latch with non-blocking `alert_cooldown_until_ms_`
- `ProcessDetectionLine(line, now_ms)` — injectable timestamp, so replay is deterministic
- Confidence gate `MIN_BOX_SCORE` applied before association

**The detector currently never fires — it only logs.** That is expected between Phase 0 and
Phase 5. Do not flash until the state machine lands.

## Architecture

The logic moves into a translation unit with **no ESP-IDF, FreeRTOS or NVS includes**, so the same
source compiles for the host and the target. Without this, tuning ~12 constants means reflashing and
re-enacting falls for each one.

| File | Contents |
|---|---|
| `main/boards/common/fall_posture.h` | `PostureState`, `BoxObservation`, `Tuning`, `TrackedPerson`, `Features`, `class PostureTracker` |
| `main/boards/common/fall_posture.cc` | association, baseline, features, state machine |
| `main/boards/common/fall_detection.cc` | unchanged role: UART, parsing, alert fan-out — delegates to `PostureTracker` |
| `tools/fall_replay/` | host `main.cc` + Python FDLOG driver (currently empty) |

Callbacks are plain function pointers, not `std::function`, to avoid heap allocation on this target.
Register `fall_posture.cc` in `main/CMakeLists.txt` beside the existing `fall_detection.cc` entry
(line 58).

## Frame geometry — 192 × 192

The Arducam 5MP footage is downscaled to **192 × 192** before inference, bounded by the Grove
module's 1.125 MB SRAM. Every pixel threshold in this design must be read against that, and it is
small enough that quantization is a first-order design constraint rather than a rounding detail.

Reference figures at 192 × 192, `dt = 200 ms`:

| Quantity | Value |
|---|---|
| Frame height | 192 px |
| T1 minimum standing height (`0.25 × frame_h`) | 48 px |
| Typical standing `h_ref`, indoor framing | 60–150 px |
| Fixed 60 px association gate | **31% of the frame** |

Per-frame centroid movement, at `h_ref = 100 px`:

| Action | normalized | px / frame |
|---|---|---|
| Walking | ~0.10 H/s | ~2 px |
| Sitting down | 0.09–0.15 H/s | 2–3 px |
| Crouching | 0.17–0.31 H/s | 3–6 px |
| Fall (mean → peak) | 0.35–0.80 H/s | 7–16 px |

**The fall/crouch margin is 2–3 px per frame at `h_ref = 100`, and it scales with `h_ref`.** Below
roughly `h_ref = 80 px` the margin collapses into pixel noise and the classifier cannot work. That
is a constraint on camera placement, not a threshold to tune: **the person must occupy ≳ 40% of
frame height.** Enforce it as a hard gate — a track whose `h_ref` is below the minimum stays
classifiable for presence but must never alarm.

Two consequences for the feature layer:

- **Estimate velocity over a short window, not adjacent frames.** Single-frame differencing at this
  resolution quantizes to ~0.05 H/s per pixel at `h_ref = 100`. A least-squares slope over the last
  3–4 samples, or a difference over a 2–3 frame baseline, costs nothing and recovers most of the
  margin.
- **Smooth the centroid before differencing** (a light EMA on `cy`), since `cy = y + h/2` compounds
  quantization in both `y` and `h`.

## Association — fix the ID swaps, not a too-tight gate

`fall_detection.cc:163-177` is greedy per-box against a fixed 60 px centroid gate, with no
claimed-track marking and no reciprocal-best check.

**Correction to an earlier claim in this design:** I previously argued the gate was too *tight* —
that a fall would move the centroid past 60 px in one frame, break the track, and leave the state
machine silently dead. At 192 × 192 that is wrong. A fall moves the centroid ~7–16 px per frame, so
the gate never breaks on a fall. The real defect runs the other way.

At 31% of the frame, the gate is far too **loose**. Two people standing within 60 px of each other
are freely interchangeable, and with per-box greedy matching and no claimed-marking, one box can
take a track that belongs to another person. That is what breaks normalization: an ID swap blends
two people's heights into one `h_ref` with no external symptom, so every normalized quantity
downstream is quietly garbage. **Distance-invariance is not trustworthy until this is fixed.**

Fix: `matched_this_frame` marking; globally-minimum-first assignment (repeatedly take the smallest
valid box↔track distance) instead of per-box greedy; and **tighten** the gate to `0.35 * h_ref`,
derived from the ~0.16 `h_ref` peak per-frame fall displacement plus 2× margin. At `h_ref = 120`
that is 42 px — tighter than today's 60 px, and it scales correctly with distance instead of being
generous for near people and restrictive for far ones. Fall back to the fixed 60 px only before a
baseline exists.

## Baseline and features

Each track learns its own standing height while upright. All rates are then expressed in **baseline
heights per second**, so one threshold set works at every camera distance.

```
h_n     = h / h_ref                          // 1.0 when standing
r       = w / h
v_cy_n  = (cy - prev_cy)     / dt / h_ref    // POSITIVE = downward
v_bot_n = ((y+h) - prev_bot) / dt / h_ref
v_h_n   = (h - prev_h)       / dt / h_ref
drop_n  = (cy - cy_ref) / h_ref              // depth below the standing datum
bottom_valid = (y + h) < (frame_h - kEdgeMargin)   // feet not cropped
```

```
is_upright = h_n > 0.85 && r < 0.60 && drop_n < 0.08

is_ground  = drop_n > 0.28 && ( r > 1.10        // prone / sideways: wide
                             || h_n < 0.45 )    // axial collapse / foreshortened: short

is_low     = drop_n > 0.10 && !is_ground

descent_sig = (v_cy_n > 0.30 || v_h_n < -0.35)
              && v_h_n  <= +0.05   // height must not GROW  (rejects walking toward camera)
              && v_bot_n > -0.15   // feet must not RISE    (rejects walking away)
```

`is_ground` is an **OR**, not an AND: a fall along the camera axis foreshortens — height collapses
while width stays roughly constant — and never satisfies a "wider than tall" test. The short branch
catches it.

The two `descent_sig` sign guards are load-bearing. Walking toward the camera grows height while the
centroid descends; walking away collapses height while the top edge descends. Both look like falls
without the sign checks.

**Baseline rules**

- EMA α ≈ 0.1 (~2 s at 5 Hz) on `h_ref` and `cy_ref`
- Updated **only** while `is_upright`; **frozen** on entry to `kDescending`
- Normalize by `h_ref`, never by current `h` — `h` collapses during the fall, giving a shrinking
  denominator that inflates late-fall velocities superlinearly
- Reject `h` samples deviating >25% from the EMA — partial occlusion behind furniture truncates `h`
  and mimics an axial collapse
- **Hard minimum size:** a track whose `h_ref` is below ~80 px (of 192) never alarms. Per the frame
  geometry section, below that the fall/crouch margin is smaller than pixel noise, so any alarm from
  such a track is a coin flip. Expose it as `Tuning::min_classify_h_ref` and log suppressions.

**Accepted limitations:** a person already on the floor when the system boots never establishes a
baseline and is undetectable via the track path; and a person too far from the camera
(`h_ref < min_classify_h_ref`) is tracked but never classified.

## The state machine

```cpp
enum class PostureState : uint8_t {
    kInit = 0,           // no valid baseline — cannot classify, NEVER alarms
    kUpright,            // baseline valid and learning
    kDescending,         // descent episode in progress; accumulators live
    kLowTransient,       // settled at intermediate depth (sit / crouch) — benign, cancellable
    kGroundUnconfirmed,  // ground pose seen, sustained_fall_ms timer running
    kFallConfirmed,      // alarm fired for this track
    kLyingBenign,        // ground reached via controlled descent — benign sink
};
```

Implemented as one `UpdatePosture(TrackedPerson&, const Features&, uint32_t now)` with a `switch`
on state.

| # | From → To | Guard | Effect |
|---|---|---|---|
| T1 | `kInit` → `kUpright` | 10 consecutive frames, `r < 0.60`, `h` within ±20% of EMA, `h > 0.25*frame_h` | Baseline accepted; if this never fires the track never alarms |
| T2 | `kUpright` → `kDescending` | `descent_sig` | Freeze `h_ref`/`cy_ref`; reset accumulators |
| T3 | `kDescending` self | update `peak_norm_vel`, `stall_frames`; `pause_count++` per stall run ≥ 2 | — |
| T4 | `kDescending` → `kUpright` | `upright_streak >= 2` | Bend-over / jitter recovery |
| T5 | `kDescending` → `kLowTransient` | `is_low && abs(v_cy_n) < 0.10`, 2 frames | Sit / crouch landing |
| T6 | `kDescending` → `kGroundUnconfirmed` | `is_ground`, 2 frames | Latch `was_ballistic`; `ground_enter_ms = now` |
| T7 | `kDescending` → forced exit | `now - descent_start_ms > 3500` | Re-evaluate predicates and route out; prevents a stuck episode |
| T8 | `kLowTransient` → `kUpright` | `upright_streak >= 2` | Crouch / sit recovery; unfreeze baseline |
| T9 | `kLowTransient` → `kDescending` | `descent_sig` | Fall-from-seated re-enters the descent path |
| T10 | `kGroundUnconfirmed` → `kUpright` | `upright_streak >= 2`, before expiry | **CANCEL — this is crouch rejection.** No alarm |
| T11 | `kGroundUnconfirmed` → `kFallConfirmed` | `now - ground_enter_ms >= 2500` && `ground_frames >= 0.7*window_frames` && `was_ballistic` | **ALARM**, non-blocking |
| T12 | `kGroundUnconfirmed` → `kLyingBenign` | expiry, duty passes, `!was_ballistic` | Deliberate lie-down sink; log `FDEVT,SUPPRESS` loudly |
| T13 | `kLyingBenign` → `kUpright` | `upright_streak >= 2` | Retain `h_ref` |
| T14 | `kLyingBenign` → `kFallConfirmed` | immobile on the floor > 120 s | Long-lie escalation |
| T15 | `kFallConfirmed` → `kUpright` | `upright_streak >= 3` && past cooldown | Clears `alerted` |
| T16 | any → track death | `frames_until_untracked > MAX_MISSING_FRAMES` | Zombie handling, below |

### Ballisticity

`was_ballistic` is computed from the **measured descent leg immediately preceding ground entry**,
whatever state that leg began in:

```
was_ballistic = peak_norm_vel > 0.32 && pause_count == 0 && descent_duration < 1400 ms
```

There is deliberately **no "came from sitting" bypass**. A flag that let a seated origin skip the
ballistic test would alarm on someone who crouches and then lies down on the floor — explicitly a
non-alarm action. Fall-from-seated is handled by T9 routing back through `kDescending` instead, so
the descent is always measured.

### T16 — detector dropout on prone bodies

Person detectors are trained overwhelmingly on upright people, so confidence tends to collapse once
a body is horizontal. Combined with the confidence gate and `MAX_MISSING_FRAMES 3`, the track dies
~600 ms after landing — deleting the confirmation evidence.

So a track dying from `kDescending` or `kGroundUnconfirmed` is held as a zombie for ~2500 ms to
allow re-acquisition, and **if the zombie expires while ballistic, the alert fires**: "went down
fast, then the detector lost them" is a fall, not a non-event.

### Alert plumbing

- Per-track `alerted` flag replaces any global latch; one global `alert_cooldown_until_ms_` remains
- Extend the MCP payload at `fall_detection.cc:102-111` with the track ID and the evidence
  (`peak_norm_vel`, `drop_n`, `ground_frames/window_frames`) so field false-positives are
  diagnosable without a reflash
- Add `FDEVT,<track>,<from>,<to>,<features...>` under `FD_CAPTURE_MODE`, alongside the existing
  `FDLOG` / `FDPOLL` / `FDSTACK_FREE`

## Deferred

- **Labeled corpus and threshold sweep** — needs hardware. 7 scenarios (4 fall types + sit + lie +
  crouch) × ≥8 reps × 3 distances × 2 orientations. Add a ground-truth marker (long-press
  `BOOT_BUTTON` → `FDMARK,<label>`); without labels the corpus cannot be scored.
- **Poll rate.** Left at 200 ms. A fall descent is ~0.6–1.0 s = 4–5 frames, and every "2 consecutive
  frames" debounce costs 400 ms of a 900 ms event. Dropping `fall_detection.cc:254` to 100 ms is a
  2-line change but needs hardware to confirm Grove sustains it. A 192 × 192 input is cheap to
  infer, so 10 Hz is plausible — but note the interaction with quantization: halving `dt` also
  halves per-frame pixel displacement, so raising the rate only helps **if** velocity is estimated
  over a window rather than between adjacent frames. Do the windowed-velocity change first.
- **Continuous mode** (`AT+INVOKE=-1,0,1`), as `main/boards/sensecap-watcher/sscma_camera.cc:397`
  already uses — streams at native model fps with the same box format.

## Honest limits — do not engineer around these

- **Axial collapse vs. a deep crouch held 3 s is not separable from bbox alone.** Same terminal
  geometry, and the only separator is descent speed, which for a slow syncopal slump sits in the
  crouch range. This is a point on an ROC curve, not a solvable problem. T14 is the backstop.
- **Deliberate lying down and a fall share an identical terminal pose.** Only descent dynamics
  separate them.
- **The 192 × 192 frame bounds how well this can ever work.** At `h_ref = 100 px` the fall/crouch
  separation is 2–3 px per frame; below `h_ref ≈ 80 px` it is pure noise. Distant people are not
  detectable at any threshold setting — that is a sensor limit, not a tuning failure.
- Near-field foot cropping silently disables detection; quantify its frequency from real captures.
  At 192 × 192 a person close enough for good margin is also close to filling the frame, so the
  usable band between "too small to classify" and "feet cropped" is narrower than it looks. Measure
  it before committing to a camera position.
- `MedicineReminderController` (`main/boards/common/medicine_reminder.h:125-137`) can play its own
  20 s alarm concurrently with the fall alarm.
- **All pose and velocity numbers above are estimated from human kinematics, not measured on this
  rig.** They are sweep starting points. The frame geometry is now known (192 × 192); the score
  scale for `MIN_BOX_SCORE` is still unconfirmed and one FDLOG line settles it.

## Verification

**Offline — the primary loop**

1. Host build in `tools/fall_replay/` compiles the shared TU and replays a captured FDLOG.
2. Regression test for the velocity fix: a synthetic constant-velocity track must yield exactly the
   injected velocity. Pre-fix it read ~2×.
3. Unit-test each transition with hand-built box sequences — a crouch must reach
   `kGroundUnconfirmed` and cancel via T10 with zero alarms; a ballistic drop must reach T11.
4. Assert 0 alarms across all sit / lie / crouch sequences; report recall per fall type.

**On hardware — after the state machine lands**

5. `idf.py build flash monitor` with `FD_CAPTURE_MODE 1`; confirm `FDEVT` transitions match the
   expected sequence while walking, sitting, and crouching in front of the camera.
6. Trigger a fall and confirm `FDLOG` lines keep flowing *through* the alert window — the direct
   test that the Phase 0 cooldown fix worked.
7. `FDSTACK_FREE` stays healthy; `UpdatePosture` adds stack depth to a task sized at 6144 B.
8. End-to-end alarm: audio out the MAX98357A, DualEye `0x04`, LCD message, MCP event, and the NVS
   `total_falls` counter incrementing once per fall, not once per frame.

**Cannot be simulated:** detector confidence collapse on prone bodies (the likely dominant miss
mode), Grove inference latency and jitter, UART throughput at 10 Hz, true frame resolution and score
scale, near-field cropping frequency.
