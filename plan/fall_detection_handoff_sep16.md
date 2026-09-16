# Fall Detection — Handoff, Sep 16 2026

State as of commit `c27d082` on `feature/improve-fall-detection` (pushed to origin).
Companion to `fall_detection_sensing_fix.md` (the Phase A/B plan) and
`fall_detection_state_machine.md` (the state machine design).

**Superseded by rounds 2 and 3 at the end of this document. After three rounds of
capture-and-fix, all three of round 3's re-enacted falls alert offline, two of them through
the full ground-confirmation path. Read "Round 3" first — it has the current state.**

---

## One-paragraph summary

Phase A's premise was right but its mechanism was wrong: the module's score threshold was clipping
39% of detections, but `AT+TSCORE` does not exist in this module's firmware — the threshold had to
be lowered through the SenseCraft AI web UI instead. With that done (`tscore` 50 → 26, detection
rate 25.9% → 54.7%), three re-enacted falls were plainly present in the detector output and still
none alarmed. The cause was not sensing: it was an `h_ref` baseline latch-up bug in the tracker that
made affected tracks permanently incapable of alarming. That plus three supporting fixes now catches
2 of 3 falls offline. The third is a camera-placement/framing problem, not a tuning one.

---

## Hardware facts established (do not re-derive)

| Fact | Evidence |
|---|---|
| Module is **Grove Vision AI V2**, `at_api: v0`, software `2025.01.02` | `AT+VER?` reply |
| Model is **Person Detection — Swift YOLO**, `swift_yolo_nano_person_192_int8_vela` | `AT+INFO?` blob |
| **`AT+TSCORE` / `AT+TIOU` do NOT exist** on this firmware | Probe: both return `code 5` EINVAL, with CR *and* CRLF |
| 11 other AT commands **do** work (`ID NAME VER STAT INFO ALGOS ALGO MODELS MODEL SENSORS SENSOR`) | Probe, all `code 0` |
| Terminator is **not** the issue — bare `\r` is correct | Poll cmd `AT+INVOKE=1,0,1\r` succeeds 259/259 in the same capture |
| `AT+MODELS?` fails **only in the constructor** — sent ~2.3s, before the module's UART is up | Empty echo `"Unknown command: "` at 2.3s; `code 0` at 7.5s from the probe |
| The vendored client lib (`sscma_client_ops.c:1224`) builds `AT+TSCORE=<n>\r\n` and assumes it exists | It targets a newer AT API than this module runs |
| **The threshold is set via SenseCraft AI → Settings → Confidence Threshold slider** | Screenshot showed live (50, 35) matching the runtime config echo exactly |
| The setting **persists across a cold power cycle** | Verified: `tscore: 26` after unplug/replug |
| Model metadata `conf`/`iou` in the `AT+INFO?` blob is **stale packaging data**, not the live values | Blob says `iou: 45`; runtime and UI both say 35 |

**Implication:** the firmware cannot set the threshold at boot. It is provisioned once per module
through SenseCraft. `MIN_BOX_SCORE` in our firmware must stay at or below it (currently both 25/26).

---

## What is committed

| Commit | What |
|---|---|
| `93d1212` | Phase A M1+M2: `AT+TSCORE=25` at init (**dead code** — module rejects it) and `MIN_BOX_SCORE` 40 → 25 in firmware + replay harness |
| `d92deca` | `FD_PROBE_COMMANDS` AT command-surface probe + `tools/probe_report.py` |
| `799c706` | `tools/capture_stats.py` |
| `c27d082` | **The tracker fixes** (below) |

### `c27d082` in detail — four changes in `fall_posture.{h,cc}`

1. **`h_ref` latch-up fix (the real bug).** `h_ref` seeds from the *first* box a track ever sees,
   and samples deviating >25% are rejected — *including from updating `h_ref`*. Seed it from a
   partial detection and every correct sample afterwards is also an "outlier", so `h_ref` never
   recovers, `height_stable_ok` never passes, T1 never fires, and **the track can never alarm**.
   The ~41s fall seeded at `h=120` from a partial box while the person was really `h=193`.
   Now re-seeds after `baseline_reseed_after = 5` consecutive rejections.
   *This fix alone made that fall track end-to-end.*
2. **M8 feet-anchor association.** Association anchored on box centre; a fall collapses `h` so the
   centre moves by half the height loss even when the body hasn't travelled — the gate rejects the
   prone box precisely because a fall produced it. Now anchors on box bottom.
3. **`upright_confirm_frames` 10 → 5.** Ten consecutive frames is 2.2s at 4.4fps. A real fall
   reached **nine** consecutive and missed by one, while the track that *did* confirm was a
   motionless spurious box. The gate was selecting for stationary false positives over people.
4. **`ballistic_max_duration_ms` 1400 → 1800.** At 4.4fps a descent is 5–7 samples and quantises
   long; the ~41s fall measured 1.61s T2→ground and was written off as `controlled_descent`.

**Confidence:** 1 and 2 are settled (a latched baseline is never correct; M8 is from the plan and
verified on two captures). 3 and 4 are tuning judgements from a **single 3-fall capture** and want
more field data.

---

## Reference data — the Sep 16 captures

Fall times were reported by the operator as **~26s, ~41s, ~57s**.

| | old capture (`monitor.log`, committed) | `capture_conf25_falls.log` |
|---|---|---|
| module `tscore` | 50 | 26 |
| detection rate | 25.9% (51/197) | 54.7% (141/258) |
| score floor | exactly 50 (clipped) | 29 |
| frame rate | 4.44 fps | 4.38 fps |
| worst blackout | 8.47s | 7.66s |
| upright / prone mean score | 76.3 / 69.2 | 68.6 / 47.5 |

**39% of boxes (139/354) scored below 50** in a comparable capture — all previously discarded.

Note prone bodies score far lower than upright (47.5 vs 68.6, a 21-point gap), so the threshold
was biased against exactly the posture that matters.

### Result with `c27d082` (replayed offline)

```
fall ~41s -> ALERT ground_confirmed @45470   drop=0.39 r=2.70
fall ~57s -> ALERT zombie_dropout   @63000   drop=0.50 r=3.63
fall ~26s -> still missed
```

Both alerts fire through real confirmation paths, not dropout guesses.
Replaying the old capture produces **no new alerts** (no false positives introduced).
`fall_posture_test` passes: crouch rejection 0 alerts, ballistic confirm 1, lie-down benign.

---

## Open problems

1. **The ~26s fall is unreachable by tuning.** The person was at the **left frame edge**
   (`cx=10..28`, partly out of frame), jumped **79px laterally** between the only two usable frames,
   and the detector returned **four boxes total** before a 7.66s blackout. Needs camera
   repositioning or the Phase B poll-rate change.
2. **A stationary spurious `41x115` box at `cx≈189`** appears in most frames of every capture and
   never moves. It competes for tracks and trivially satisfies the "stable upright" gate. If it is
   furniture rather than a second person, moving or re-aiming the camera may be worth more than any
   further tuning.
3. **Phase A M1 is dead code.** The `AT+TSCORE=25` send in the `fall_detection.cc` constructor is
   rejected every boot. Harmless but should be removed or replaced with a `AT+STAT?` `is_ready`
   gate (which would also fix the constructor's `AT+MODELS?`, see hardware facts).
4. **`FD_PROBE_COMMANDS` is still `1`** — costs ~8s of startup per boot. Set to `0` for normal
   operation once the command surface question is closed.
5. **Blackouts persist** (worst 7.66s). Phase B (poll rate 200ms → ~100ms, modules M3–M7) is still
   unimplemented and is the remaining lever for descents that fall entirely inside a gap.

---

## Tooling

| Tool | Use |
|---|---|
| `python3 tools/capture_stats.py <log>` | module config echo, detection rate, score histogram, frame gaps, blackouts. Two logs = before/after delta |
| `python3 tools/probe_report.py <log>` | which AT commands the module accepts (needs an `FD_PROBE_COMMANDS` build) |
| `cd tools/fall_replay && make && ./fall_replay < <log>` | replay a capture through the exact on-device state machine; reproduces device `FDEVT` output byte-for-byte |
| `cd tools/fall_replay && make test` | `fall_posture_test` unit tests |

The replay harness is the single most valuable asset here — **every tracker change below was
designed and validated offline against real captures with zero reflashes.** Use it before burning
a hardware cycle.

`MIN_BOX_SCORE` (`fall_detection.cc`) and the hardcoded `score >= 25` in
`tools/fall_replay/main.cc` **must be changed together** or the harness stops reproducing device
behaviour.

---

## How to capture

ESP-IDF v5.5.4 at `~/.espressif/v5.5.4/esp-idf`; device on `/dev/ttyACM0`; user is in `dialout`.

```bash
. /home/baodn19/.espressif/v5.5.4/esp-idf/export.sh
cd /home/baodn19/Bio-FAIRCH/Nanabot/xiaozhi-esp32
idf.py -p /dev/ttyACM0 build flash monitor | tee capture_<name>.log
# Ctrl+] to quit
```

**Save every capture under a distinct name.** Two captures have already been lost to overwriting
`monitor.log`, and old captures are valuable regression fixtures.

---

## What to do next

1. **Flash `c27d082`** and re-enact 3 falls, **noting the wall-clock time of each** — fall timings
   were what unlocked the whole diagnosis. Stand well inside the frame, not at the edge.
2. Analyse:
   ```bash
   python3 tools/capture_stats.py capture_conf25_falls.log capture_new.log
   cd tools/fall_replay && make && ./fall_replay < ../../capture_new.log
   ```
3. **Expected:** `tscore: 26` in the config echo; detection rate ~55%+; alerts for falls that are
   fully in-frame. Compare `FDEVT` ALERT timestamps against your noted fall times.
4. **Then decide:**
   - Falls caught and no false alarms → tighten up: remove dead M1, set `FD_PROBE_COMMANDS 0`,
     consider raising the SenseCraft threshold a little (26 is a diagnostic value; production is
     probably 30–40) and re-verify.
   - Falls still missed with good in-frame data → investigate the specific track in the replay
     trace; the `FDEVT` state transitions tell you which gate rejected it.
   - Missed because of blackouts → Phase B poll-rate change (M3–M7 in the sensing-fix plan).
   - False alarms → items 3 and 4 of `c27d082` are the first suspects; both are single-capture
     tuning judgements.

### Useful debugging recipe

To find why a specific fall did not alarm, dump the raw boxes around it. The parser reports
`(x, y)` as the box **centre**:

```python
# per frame: t, n boxes, each cx/cy/w/h/aspect-ratio/score
# a fall reads as ar rising past 1.0 while cy rises toward the frame bottom
```

Then check the `FDEVT` trace for that time window: no transitions at all means the track never
reached `Upright` (T1 gate); `Upright→Init` means it died via the non-ballistic zombie path;
`SUPPRESS,controlled_descent` means it tracked the fall but classified it benign.

---

# Round 2: the post-`c27d082` capture

`c27d082` was flashed and three falls re-enacted at **~29s, ~43s, ~57s**. **None alerted on the
device.** The replay harness reproduced the device's `FDEVT` output byte-for-byte, which confirmed
the device really was running `c27d082` and the failure was in the tracker, not the flash.

Two further defects were found, both confirmed frame-by-frame from the capture. With them fixed,
**fall #2 alerts**; falls #1 and #3 are not reachable by tracker changes and are explained below.

## ⚠ The reference capture was overwritten

`capture_conf25_falls.log` was re-used as the output filename, so the 54.7%-detection reference
capture the round-1 numbers came from **is gone**. The intact tscore=50 capture survives only as
`monitor.log` inside the older worktrees under `.claude/worktrees/*/`; the copy in the main
checkout was also clobbered (47 frames, was 197). The doc's earlier warning about distinct capture
names stands, and now has a second casualty. Treat `.claude/worktrees/handoff-doc/monitor.log`
(197 frames, 395 FDLOG lines) as the surviving tscore=50 regression fixture.

## Sensing got worse, not better

| | round 1 (`capture_conf25_falls.log`, lost) | round 2 (same filename) |
|---|---|---|
| module `tscore` | 26 | 26 |
| detection rate | 54.7% (141/258) | **32.8% (81/247)** |
| score floor | 29 | 27 |
| frame rate | 4.38 fps | 4.41 fps |
| blackouts >1s | — | **6, totalling ~18s of a 56s capture** |
| worst blackout | 7.66s | 6.02s |

The score floor (27) is still above the module threshold (26), so this is **not** threshold
clipping — the detector genuinely does not see the person for about a third of the capture. Poll
timing is steady (median gap 230ms, max 290ms), so these are frames returning **zero boxes**, not
missed polls. Whatever changed between the two sessions — distance, lighting, framing — cost more
detection than the entire Phase A threshold fix gained. **This is now the dominant problem.**

One piece of good news: the stationary spurious `41x115` box at `cx≈189` is **gone** (one box near
that column in 247 frames, versus most frames previously). That open problem is closed.

## The two defects fixed

### 1. Detector dropout was billed to the descent as if it were slow motion

`ComputeBallistic()` measured `as_of_ms - descent_start_ms` — wall-clock, including time the
detector was blind. Fall #2 was a textbook capture:

```
40500  Init->Upright      h_ref=190, clean (h=193 held for 2.0s)
42580  Upright->Descending h 177->120, r=1.15
42810  h=78  h_n=0.41  r=1.77  peak_norm_vel=0.95  pause_count=0   <- ballistic, 3x the 0.32 gate
       ... detector blind for 2.91s ...
45720  h=54  h_n=0.28  r=1.93  drop_n=0.42  is_ground=1            <- unambiguous ground pose
       ... detector blind for 6.02s ...
49190  zombie expiry -> ResolveZombie -> ballistic? NO -> Descending->Init, no alert
```

The real descent was 0.23s of observed motion. The measured duration was `45720 - 42580 = 3140ms`,
over `ballistic_max_duration_ms = 1800`, so the one signal that was screaming "fall"
(`peak_norm_vel = 0.95`) was overruled by a duration made entirely of blind time.

**Fix:** accumulate `descent_observed_ms` over frames the track was actually matched on, capping
each inter-frame gap at the new `Tuning::max_gap_counted_ms = 600`. Blind time is unknown time,
not slow time. `ComputeBallistic()` now reads that accumulator and takes no timestamp argument.
`descent_start_ms` is still used for the T7 forced-exit timeout, where wall-clock is correct.

With this, fall #2's measured descent is 830ms, it resolves as ballistic, and the zombie path
fires `ALERT,zombie_dropout` at 49190.

### 2. The association gate's pre-baseline fallback was unreachable

```c
float gate = (t.h_ref > 0.0f) ? assoc_gate_ratio * t.h_ref : assoc_gate_px;
```

`CreateTrack()` seeds `t.h_ref = box.h`, so `h_ref > 0` is true from a track's first frame and the
`assoc_gate_px` branch **never executed**. The intended test is `has_baseline`, which is set only
at T1 — and which, it turns out, was written at line 342 and read nowhere.

The effect: an unconfirmed track was gated on a baseline that was still a guess. Fall #1's track
had a provisional `h_ref = 175` from a half-out-of-frame box, giving a 61px gate; the person's next
box was 70px away, was rejected, and started a **second track seeded at prone height (`h_ref=62`)**
— below `min_classify_h_ref`, so incapable of alarming.

**Fix:** `float gate = t.has_baseline ? assoc_gate_ratio * t.h_ref : assoc_gate_px;`
Fall #1's track now survives the collapse intact instead of splitting in two.

## Why falls #1 and #3 still do not alert

Neither is a tuning problem; no threshold change reaches them.

**Fall #1 (~29s) — never established an upright baseline.** The person entered at the extreme left
edge: `w=28px` for a 177px-tall body, i.e. well over half the body out of frame. Four boxes total
before the collapse began, of which the upright gate accepted two (one had `r=0.18` from the
clipped width, one had cropped feet, `bottom_valid=0`). T1 needs 5 consecutive; the count peaked at
2. The track therefore stayed in `kInit`, which by design never alarms. This is the **same failure
as round 1's ~26s fall, at the same edge of the same frame.**

**Fall #3 (~57s) — the descent happened inside a blackout.** The last pre-fall box at 54210 is
upright; the first post-fall box at 57830 is already prone. The 3.62s in between returned zero
boxes. The post-fall track is born prone (`h_ref` 54–80, under `min_classify_h_ref = 100`) with no
upright history. The fall is simply not in the data.

## Verification

| Check | Result |
|---|---|
| Round-2 capture | fall #2 `ALERT,zombie_dropout @49190`; #1, #3 still missed |
| Intact tscore=50 capture (`.claude/worktrees/handoff-doc/monitor.log`) | **byte-identical** to `c27d082` — no false positives, no behaviour change |
| `capture_conf25_check.log` | no alerts, unchanged |
| `make test` | ALL PASS (velocity regression, crouch/T10 0 alerts, ballistic/T11 1 alert, lie-down benign) |
| `idf.py build` | succeeds, `xiaozhi.bin` 0x23e820, 24% partition free |

Alert latency for fall #2 is 6.6s (fall at ~42.6s, alert at 49190). It is set by the zombie clock
restarting when the body was briefly re-acquired at 45720, not by the fix.

## New tooling

`./fall_replay --trace` dumps every active track's state and derived features per frame
(`h`, `h_ref`, `h_n`, `r`, `drop_n`, velocities, each boolean gate, the T1/ground counters,
`peak_norm_vel`, `pause_count`, missing-frame count, zombie flag). This is what turned "no alert"
into a named gate for each of the three falls. Host-only; the device has no equivalent, so it
cannot affect `FDEVT` reproduction.

## What to do next (revised)

1. **Fix the framing before tuning anything else.** Two of the three misses are framing/visibility,
   and detection rate fell to 32.8%. Re-aim or reposition the camera so the whole fall area is
   well inside the frame, then re-capture and check the rate before re-enacting falls. Target the
   ~55% the previous session achieved.
2. **Stand fully in frame and upright for ≥1.5s before falling.** T1 needs 5 consecutive upright
   frames (~1.1s at 4.4fps) and a track that never confirms can never alarm, no matter how clean
   the fall itself looks.
3. **Phase B (poll rate 200ms → ~100ms) is now the highest-value code change.** Fall #3 was lost
   entirely to a 3.62s blackout and fall #2 nearly was. Doubling the rate does not fix zero-box
   frames, but it halves the quantisation that makes short descents hard to measure and gives the
   ground-confirmation window more chances to land two frames.
4. Still open from round 1: remove the dead `AT+TSCORE` send, set `FD_PROBE_COMMANDS 0`.
5. Consider whether `ground_confirm_frames = 2` should tolerate a dropout between the two frames.
   Fall #2 had one clean ground frame (45720, `drop_n=0.42`) and was rescued only by the zombie
   path. Not changed here — it is a single-capture judgement and the zombie path already covers it.

---

# Round 3: three falls, three alerts

`b313645` was flashed and three falls re-enacted at **~35s, ~52s, ~66s**. **One alerted on-device**
(the ~66s one). The replay again reproduced the device's `FDEVT` sequence exactly, so the two
misses were diagnosed offline. Two more defects were found and fixed; **round 3 now alerts on all
three falls**, two through the full ground-confirmation path.

## Sensing is fixed — this was the big win

| | round 1 (lost) | round 2 | **round 3** |
|---|---|---|---|
| detection rate | 54.7% | 32.8% | **65.3% (198/303)** |
| span | — | 56.0s | 70.3s |
| frame rate | 4.38 fps | 4.41 fps | 4.31 fps |
| worst blackout | 7.66s | 6.02s | **4.02s** |
| blackouts >1s | — | 6 (~18s of 56s) | 10, but all shorter; only one >2.6s |

Whatever was changed about the framing between rounds 2 and 3 worked: detection is the best it has
ever been, well past the round-1 reference, and the catastrophic multi-second blackouts are gone.
**Do not change the camera position again without re-measuring.**

The remaining weak spot is the **left frame edge** (`cx < 40`), which has now been implicated in a
missed or degraded fall in all three rounds: clipped widths (`w=23..31` for a full-height body) and,
in round 3, the detector splitting one person into two boxes.

## The two defects fixed

### 3. Detector blind gaps were deflating measured velocity

`b313645` established that unobserved time must not be charged to the descent *duration*. The same
error was still live in the *velocity* term, and it cost the ~35s fall:

```
34620  Upright->Descending   h=120 (from h=193)
       ... detector blind for 2.06s ...
36920  h=57  h_n=0.31  r=2.42  drop_n=0.46  is_ground=1   peak_norm_vel=0.23  <- vs 0.32 gate
40330  zombie expiry -> not ballistic -> Descending->Init, no alert
```

The person collapsed from `h=193` to `h=57` — a total axial collapse — and it measured
`0.23 h_ref/s` because `ComputeFeatures()` runs a least-squares fit over `hist_t_ms`, and those
timestamps were raw wall-clock. Only matched frames reach `PushHistory`, so the fit divided a real
collapse by 2.3s of mostly-blind time. A blackout does not just inflate duration; it deflates every
rate measured across it, by exactly the factor it stretched.

**Fix:** `PushHistory()` now stores **gap-compressed** timestamps, each step capped at the same
`Tuning::max_gap_counted_ms = 600`. `v_cy_n`, `v_bot_n` and `v_h_n` share one time axis, so all
three inherit the fix. The ~35s fall now measures `vy=0.37 vh=-0.57`, resolves ballistic, and fires
`ALERT,zombie_dropout` at 40330.

### 4. Greedy association let an unconfirmed track steal the fall

The ~52s fall was lost to **association**, not classification. At the left frame edge the detector
emitted two boxes for one person, so next to the track that had confirmed a baseline through T1
(`h_ref=179`) sat a second, unconfirmed track a few pixels nearer. When the person went down, the
prone box was inside both gates and nearest-neighbour gave it to the unconfirmed track — which is
in `kInit` and **can never alarm by design**. The confirmed track starved and expired.

Nearest-neighbour is arbitrary between two in-gate claimants, and they are not equivalent: a
confirmed track has passed T1 and is the only kind that can classify a fall; an unconfirmed one is
a hypothesis that may be detector noise.

**Fix:** sort association candidates **confirmed-first, then by distance**. A confirmed track's
gate is the *tighter* of the two (`0.35 * h_ref` ≈ 65px vs the 75px fixed gate), so the preference
only applies where the confirmed track was already a close claimant, and the existing
one-box-per-track guard bounds any mis-assignment to a single frame.

## Result

```
~35s  Upright -> Descending -> ALERT,zombie_dropout                        @40330
~52s  Upright -> Descending -> GroundUnconfirmed -> ALERT,ground_confirmed @55630
~66s  Upright -> Descending -> GroundUnconfirmed -> ALERT,ground_confirmed @70570
```

with correct `FallConfirmed -> Upright` recovery between them as the person got back up. Two of the
three now come through the full ground-confirmation path rather than a dropout inference.

| Check | Result |
|---|---|
| Round-3 capture | **3/3 falls alert** |
| Round-2 capture | unchanged, 1 alert (its other two remain framing/blackout losses) |
| Intact tscore=50 capture | 0 alerts — no false positives |
| `capture_conf25_check.log` | 0 alerts |
| `make test` | ALL PASS (incl. crouch/T10 at 0 alerts and lie-down benign, both velocity-sensitive) |
| `idf.py build` | succeeds |

## ⚠ Expect only 2 alerts on hardware, not 3

`FallDetectionController`'s global `ALERT_COOLDOWN_MS` is **15000**, and the last two alerts are
**14940ms** apart — 60ms inside the cooldown. On-device the third will be suppressed. That is the
re-enactment spacing, not a detection failure. **Space re-enacted falls more than 20s apart** or
the test under-reports.

## ⚠ The capture filename has now been overwritten three times

`capture_conf25_falls.log` has been re-used for every round. Rounds 1 and 2 are gone. Use
`capture_<round>_<date>.log` or the analysis cannot be re-run against history. The surviving
fixtures are the tscore=50 `monitor.log` under `.claude/worktrees/*/` and
`capture_conf25_check.log`.

## What to do next

1. **Flash `f4cdb32` and re-enact, spacing falls >20s apart.** Expect three alerts. This is the
   first build where all three offline paths are clean, so the on-device run is the real test.
2. **Leave the camera where it is.** 65.3% detection is the best result so far and two of the three
   fixes above only mattered because earlier framing was poor. If it must move, re-run
   `capture_stats.py` and confirm the rate before re-enacting falls.
3. **The left frame edge is the last sensing problem.** It has degraded a fall in all three rounds.
   If the fall area cannot be moved inward, consider ignoring boxes whose width is clipped by the
   left edge rather than letting them seed tracks.
4. **Phase B (poll rate 200ms → ~100ms)** is still unimplemented and is still the right lever: it
   shortens every blackout and halves the quantisation that made these velocity estimates fragile
   in the first place.
5. Still open from round 1: remove the dead `AT+TSCORE` send, set `FD_PROBE_COMMANDS 0`.
6. Untouched tuning judgement: `ground_confirm_frames = 2` requires two *consecutive* observed
   ground frames. Round 2's ~43s fall and round 3's ~35s fall each produced exactly one and were
   rescued by the zombie path. Worth revisiting only with more field data.
