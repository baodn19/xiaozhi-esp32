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

## ⚠ The DetectionTask stack has almost no headroom — read before editing `Associate()`

Shipping the round-3 association fix crashed the device on boot, repeatedly, in a way that looked
nothing like fall detection:

```
assert failed: xQueueSemaphoreTake queue.c:1713 (pxQueue->uxItemSize == 0)
  uart_read_bytes() <- FallDetectionController::DetectionTask()
```
or an interrupt watchdog timeout at the same place. No fall-detection frame in the backtrace.

The cause was a **one-byte struct field**. `PostureTracker::Associate()` holds
`Candidate candidates[kMaxTrackedPeople * kMaxTrackedPeople]` — **225 entries — on the
DetectionTask stack**. Adding a `bool` padded `Candidate` from 12 to 16 bytes, i.e. **+900 bytes of
stack**, in a task whose measured high-water mark was **`FDSTACK_FREE,412`**. It overflowed, and an
overflow there does not abort cleanly: it corrupts what is below and resurfaces as a bad semaphore
handle deep inside the UART driver.

**Rules that follow:**
- **Never add a field to `Candidate`.** Read what you need off `tracks_` in the comparator instead
  (the comparator can capture `this` for free). A field there costs 225x its padded size.
- `kDetectionTaskStackSize` is now **8192** (was 6144). The old peak comment omitted `Associate()`'s
  2.7 KB array entirely, which is why 6144 looked fine on paper.
- **`FDSTACK_FREE` is in every capture — check it.** Treat anything under ~1 KB as a defect, not a
  tight fit. It is the only warning you get before a corruption-class crash.
- The replay harness cannot catch this. It runs on a host with an 8 MB stack, so every offline
  result stayed byte-identical while the device was unbootable. **Offline validation does not cover
  memory footprint** — that is the one thing that still needs a hardware cycle.


---

# Round 4: three of four falls alert — and Phase B is not the lever

`347c393` flashed, **four** falls re-enacted. The operator reported them at ~16s, ~27s, ~57s and
~87s, with alerts ~3s, ~3s and ~7s after falls 2, 3 and 4. Device clock runs **+43.5s** ahead of
operator wall-clock; every fall maps to within 1.5s, so the correspondence is not in doubt:

| operator | device descent onset | alert | latency | result |
|---|---|---|---|---|
| ~16s | 59110 | — | — | **MISS** |
| ~27s | 71040 | 74570 | 3.5s | `ground_confirmed` |
| ~57s | 99610 | 103930 | 4.3s | `ground_confirmed` |
| ~87s | ~131000 (inside a blackout) | 138550 | 7.5s | `ground_confirmed` |

Replay reproduces the device's 17 `FDEVT` lines exactly. Alerts are 29.4s and 34.6s apart, so
round 3's cooldown artefact is gone and all three fired on-device. **Zero false positives** across
150.8s. `FDSTACK_FREE` is a flat **2436 of 8192** — healthy, well clear of the ~1 KB defect line.

## Sensing is the best it has ever been

| | round 2 | round 3 | **round 4** |
|---|---|---|---|
| detection rate | 32.8% | 65.3% | **77.3% (504/652)** |
| span | 56.0s | 70.3s | 150.8s |
| frame rate | 4.41 fps | 4.31 fps | 4.33 fps |
| worst blackout | 6.02s | 4.02s | **3.37s** |

Leave the camera alone.

## The miss is not a sensing failure

The ~16s fall was **observed 8 polls out of 8** — `h` 151→120→86→62→54, aspect 0.21→0.78→1.15→
1.21→1.30→2.70. A textbook descent, fully in frame.

It failed because **the track was born 480ms before the descent started**:

```
53770..54470  id=7  person upright at the LEFT EDGE, h=235..214  ->  upcnt reaches 3 of 5
54470..57670  3.20s blackout                                     ->  id=7 dies
58630         id=9 CREATED, h=151, kInit, upcnt=0
59110..60530  the fall, 8/8 polls observed, in kInit throughout  ->  kInit NEVER alarms, by design
```

`id=7` was the person's real upright track (`h_ref=233`) and it only ever got **700ms / 4 polls**
of upright observation before the detector lost it. Those 4 boxes were left-edge and badly clipped
— `x=15..26`, `w=28..47` for a body of `h=214..235` (aspect 0.13–0.20, against 0.24–0.36 for the
same person mid-frame) — with scores decaying `71 → 64 → 58 → 33` and then nothing for 3.2s.

**Tuning cannot reach this.** Swept on the replay harness across every surviving fixture:

| variant | round 4 | round 3 | round 2 | `conf25_check` |
|---|---|---|---|---|
| as committed | **3** | 3 | 1 | 0 |
| `zombie_expiry_ms` 2500 → 4000 | 3 | **2** | 1 | 0 |
| `upright_confirm_frames` 5 → 3 | 3 | 3 | 1 | 0 |
| both | **2** | **2** | 1 | 0 |

Neither knob rescues the miss and both cost alerts elsewhere. The gap `id=7` had to survive was
3.20s against a 2.5s `zombie_expiry_ms`; closing that by tuning breaks round 3.

### Latent: the baseline re-seed adopted a prone box

At 60770, after 5 rejections, `UpdateBaseline` re-seeded `h_ref := 54` **from the box of a person
lying on the floor**. For the next 6.5s the tracker's model of that person read `h_n=1.00,
drop_n=0.00` — "standing at normal height, has not dropped" — and `h_ref=54` is below
`min_classify_h_ref=100`, so the track could not have alarmed at all.

It recovered here only because the person got up (`h_ref` 54 → 78 → 193 by 69120, and `id=9` went
on to catch falls 2–4). **A person who stays down leaves it latched on a prone baseline.** The
re-seed (added in `c27d082` to fix the opposite latch-up) needs an upright-pose precondition.

---

# Phase B is **not** necessary — its stated premise is false in this capture

The plan and round 3's next-steps both say the poll-rate change "shortens every blackout". Round 4
falsifies that.

**The module is not being under-asked.** 653 polls, 652 replies, none dropped. `perf` is
`[7, 48, 0]` on all 652 frames — inference is a constant 55ms. `Update()` spacing is
min 190 / median 230 / p95 250 / max 290 ms: no jitter, no starvation, ~18fps of headroom unused.

**All 11 blackouts — 18.61s across 72 polls — consist entirely of polls that were answered with an
empty box list.** Not one was a missing poll. The detector was asked and said "no person".

Blindness is bimodal, and only one mode is a sampling problem. With `p(empty) = 0.227` over 61 runs:

| empty-run length | observed | expected if per-poll failure were independent |
|---|---|---|
| 1 | 39 | 51.0 |
| 2 | 11 | 11.6 |
| 3 | 5 | 2.6 |
| 4 | 4 | 0.60 |
| 5 | 3 | 0.14 |
| 6 | 1 | 0.03 |
| 9 / 12 / 14 | 1 each | ~1e-6 and far below |

Short runs match independent per-poll failure; long runs are impossible under it (a 14-run is
p≈1e-9). **49% of empty polls sit in sustained runs of ≥4; 26% are isolated single misses.**

So doubling the poll rate would:

- halve the observation gap for the **isolated** component (26% of empty polls) — a real gain;
- do **nothing** for the sustained blackouts (49%) — polling a blind detector twice as often
  returns twice as many empty answers. These are the ones that mattered: the 3.20s that killed
  `id=7` and caused the only miss, and the 3.37s that swallowed fall 4's entire descent;
- shave ~one poll interval (~115ms) off each blackout tail — ~1.3s of 18.61s, about **7%**.

**Rate is not irrelevant.** Decimating round 4 to ~2.2fps drops it from 3 alerts to 2 (even frames)
or 1 (odd frames), so 4.33fps sits on a slope, not a plateau, and Phase B would add genuine margin.
But it does not address the one remaining miss, and it costs M3–M7: six modules including a
velocity-window rework (M5), an EMA rewrite the plan itself says **cannot** be bit-neutral (M6), and
a full test migration (M9).

**Verdict: defer Phase B.** It is real margin against the wrong bottleneck. Detector *recall* —
framing and the left edge — is what limits this system now.

## What to do next

1. ~~**The `kInit`-born-mid-fall path.**~~ **Fixed — see "T1b" below.**
2. **The left frame edge — now implicated in a degraded or missed fall in all four rounds.** The
   3.20s blackout that caused this miss began with `w=28` for `h=214`. This is the highest-value
   sensing work, and it is framing, not firmware.
3. **Gate the baseline re-seed on an upright-ish pose.** Cheap, clearly correct, closes the latent
   prone-baseline latch above.
4. **`ALERT_COOLDOWN_MS = 15000` needs revisiting before fall 1 is fixed.** Had it alerted (~60.5s),
   fall 2's alert at 74570 would have been 14.1s later — suppressed on-device.
5. Then reconsider Phase B, with M8 already landed and the decimation cross-check as its gate.
7. Still open from round 1: remove the dead `AT+TSCORE` send, set `FD_PROBE_COMMANDS 0`.

---

# T1b — the fix for the `kInit`-born-mid-fall path

**Round 4 now alerts on 4 of 4 falls, and round 2 on 2 of 3** (it was 3/4 and 1/3).

## The defect

`kInit` had exactly one exit: T1, which needs `upright_confirm_frames = 5` consecutive frames of a
**stable** height. A person who enters the detector's view already collapsing never produces those
frames, so the track sits in `kInit` for the whole fall — and `kInit` has no descent edge at all.
The fall is structurally unreachable, which is why the tuning sweep in round 4 could not touch it.

This is not a one-capture curiosity. It is the *same* failure the doc already recorded for round 2's
~29s fall ("T1 needs 5 consecutive; the count peaked at 2. The track therefore stayed in `kInit`,
which by design never alarms"). Two captures, two sessions, one missing edge.

## The fix

A second exit from `kInit` — **T1b: provisional-baseline descent** — in `fall_posture.cc`:

```cpp
if (t.born_upright && f.descent_sig) {
    EnterState(t, PostureState::kDescending, now_ms);
}
```

`born_upright` is one new `bool` on `TrackedPerson`, set once in `CreateTrack`: did this track's
**first** box look like a standing person — upright aspect (`w/h < upright_max_ratio`) and
person-sized (`h >= min_classify_h_ref`)? It has to be the seed frame, because by the time a
collapse is visible the current box is no longer upright.

Three properties make this safe:

- **`has_baseline` stays `false`.** The track is still a hypothesis for association purposes: it
  keeps the loose fixed pixel gate and keeps losing box-contention ties to confirmed tracks, so
  round 3's confirmed-first fix is fully preserved.
- **Every alarm gate is downstream and untouched.** To fire, the track must still clear
  `descent_sig`, two consecutive ground frames, `ground_confirm_ms = 2500` at
  `ground_frames_duty_min = 0.70`, `was_ballistic` (`peak_norm_vel > 0.32`, `pause_count == 0`,
  `descent_observed_ms < 1800`) **and** `min_classify_h_ref`. T1's own job is rejecting
  *stationary* spurious boxes, and a stationary box passes none of those.
- **It closes the latent prone-baseline latch for free.** `UpdateBaseline` only learns in
  `kInit`/`kUpright`, so moving to `kDescending` freezes `h_ref` at its pre-fall value — the track
  can no longer re-seed its baseline onto a prone box the way round 4's did at 60770.

## Result

Both previously-missed falls now take the **full** path, not a dropout inference:

```
round 4  Init -> Descending 59110 -> GroundUnconfirmed 60290 -> ALERT,ground_confirmed 62930
round 2  Init -> Descending 29020 -> GroundUnconfirmed 30110 -> ALERT,ground_confirmed 32610
```

Latency 3.8s and 3.6s, in line with the 3.5–4.3s of the falls that already worked.

| Fixture | before | after |
|---|---|---|
| `capture_round4_1640` (4 real falls) | 3 | **4** |
| `capture_round2_sep16_1508` (3 real falls) | 1 | **2** |
| `capture_round3_sep16_1608` | 3 | 3 — byte-identical |
| `capture_conf25_falls` | 3 | 3 — byte-identical |
| `capture_conf25_check` | 0 | 0 |
| tscore=50 `monitor.log` (false-positive control) | 0 | **0** |
| `make test` | ALL PASS | ALL PASS |

Two new tests, and the negative one has teeth — deleting the `born_upright` term makes
`TestBornNonUprightNeverAlarms` fail with an alert:

- `TestBornMidFallFiresT1b` — a track seeded upright then collapsing alarms **with
  `has_baseline == false`**, proving it never went through T1.
- `TestBornNonUprightNeverAlarms` — the identical collapse seeded from a wide box stays in `kInit`
  and alarms zero times.

## Cost and caveats

- `sizeof(TrackedPerson)` 240 → **244 B**; ×15 tracks = **+60 B**, and `tracks_` lives inside
  `PostureTracker` (a `FallDetectionController` member), **not** on the DetectionTask stack.
  **`Candidate` is untouched**, so the 225-entry stack hazard from round 3 does not apply here.
- `fall_posture.cc` compiles clean under `-Wall -Wextra -Werror -Wpedantic -Wshadow`. (Three
  `-Wconversion` hits exist in `LeastSquaresSlope` and the duty calculation; all three are
  pre-existing — the count is identical before and after this change.)
- **`idf.py build` was NOT run.** The ESP-IDF Python venv in this environment is broken
  (`python_env/idf5.5_py3.14_env` missing after a Python upgrade), so the target build could not be
  exercised. `fall_posture.cc` is by design free of ESP-IDF/FreeRTOS/NVS includes and this change
  adds none, but **the target build is still unverified — run it before flashing.**
- `ALERT_COOLDOWN_MS = 15000` now bites, exactly as predicted: round 4's new alert at 62930 is
  **11.6s** before the 74570 one, so **on hardware the second will be suppressed.** That is the
  re-enactment spacing (falls 11s apart), not a detection failure — but the cooldown is now the
  binding constraint on closely-spaced falls and should be revisited.

---

# Round 5: T1b lands — 3 of 3 falls alert, and the left edge is now measurable

`2649d85` flashed, **three** falls re-enacted (`capture_round5_1151.log`). Operator reported them at
~35s, ~63s and ~92s. **All three alerted.** Zero false positives across 110.9s.

## The build is genuinely `2649d85`

Replay reproduces **27 of 27** `FDEVT` lines byte-for-byte. The flash took; the analysis below is
about the T1b firmware and nothing else.

```
diff <(grep -o 'FDEVT,[0-9]*,[A-Za-z]*,[A-Za-z]*,[0-9]*' capture_round5_1151.log) \
     <(./tools/fall_replay/fall_replay < capture_round5_1151.log | grep -o '^FDEVT,[0-9]*,[A-Za-z]*,[A-Za-z]*,[0-9]*' | grep -v ,ALERT,)
```

Device clock runs **6.7s behind** operator wall-clock, consistent to ±0.3s across all three falls —
the tightest correspondence any round has produced.

| operator | device descent | alert | latency | `kInit` exit | reason |
|---|---|---|---|---|---|
| ~35s | 28640 | 32650 | 4.01s | **T1b** | `ground_confirmed` |
| ~63s | 56090 | 60460 | 4.37s | T1 | **`zombie_dropout`** |
| ~92s | 85250 | 89680 | 4.43s | T1 | `ground_confirmed` |

Alerts are 27.8s and 29.2s apart, both clear of `ALERT_COOLDOWN_MS = 15000`, so **all three fired
on-device** — offline and on-device counts agree for the first time since round 2.
`FDSTACK_FREE` bottoms at **2348 of 8192**; healthy, and unchanged in character by T1b's +60 B.
`make test` **ALL PASS**, now including both T1b tests.

## T1b is responsible for one of the three alerts

Fall 1 is a textbook instance of the defect T1b was written for, and the trace proves the
counterfactual outright:

```
TRACE,27910,id=7,Init,h=151,h_ref=151,upcnt=0     <- track born at cx=15, the LEFT EDGE
TRACE,28120,id=7,Init,h=151,           upcnt=1
TRACE,28410,id=7,Init,h=185,           upcnt=2    <- height jumps, up=0
TRACE,28640,id=7,Descending, r=0.78,   upcnt=0    <- T1b fires. T1 needs upcnt=5.
```

`upcnt` peaked at **2 of the 5** T1 requires, and the subject was already collapsing. Pre-`2649d85`
`id=7` sits in `kInit` for the entire fall and never alarms. **Without T1b round 5 is 2/3, not 3/3.**

Two secondary confirmations that the edge behaves as designed:

- **The baseline freeze works in the field.** `h_ref` stays pinned at 151 through the whole ground
  phase (`h_n=0.36, drop=0.64` for 3s). Round 4's prone-baseline re-seed did **not** recur — moving
  to `kDescending` stops `UpdateBaseline` learning, exactly as the T1b writeup predicted.
- **The new edge rejects non-falls.** `id=3` also took `Init -> Descending` (12420) and returned
  `Descending -> Upright` 500ms later with no alarm. The downstream gates still do their job on the
  T1b path; T1b widens the entrance, not the exit.

## Sensing: best-ever headline, but the headline is flattered by phantoms

| | round 2 | round 3 | round 4 | **round 5** |
|---|---|---|---|---|
| detection rate (any box) | 32.8% | 65.3% | 77.3% | **94.1% (449/477)** |
| span | 56.0s | 70.3s | 150.8s | 110.9s |
| frame rate | 4.41 | 4.31 | 4.33 | 4.29 fps |
| worst blackout (any box) | 6.02s | 4.02s | 3.37s | **0.91s** |

The metric is the one every prior round used (frames with ≥1 box; it reproduces round 4's
504/652 exactly). **But two boxes in this capture never move, and they keep it saturated:**

- **cx≈211, h=115–193** — present in 290 of 477 frames from t=12190 to t=122940. Unidentified;
  assumed fixture, but see the caveat below.
- **cx≈20, w≈23, h≈60** — **this is a second person, seated at the left edge** (operator-confirmed).
  It is **not** a phantom. See the next section.

Both mask the walking subject's absence. Scoring **the walking subject only**:

| | any-box | **walking subject only** |
|---|---|---|
| detection rate | 94.1% | **78.2% (373/477)** |
| worst blackout | 0.91s | **8.22s** |

**78.2% is the honest number for fall analysis**, and it is a marginal gain on round 4, not the leap
the headline suggests.

⚠ **Do not "fix" the metric by suppressing stationary tracks.** An earlier draft of this section
recommended exactly that. It was wrong, and dangerously so — one of the two stationary boxes is a
human being.

## ⚠ A seated person is tracked, and can never trigger an alarm

The operator confirms someone was **sitting at the left edge** for the whole capture. That is the
`cx≈20, w≈23, h≈60` box. The tracker handles them well and the alarm logic cannot touch them.

Two tracks cover this person, and both reach **confirmed** `kUpright` through T1:

```
id=11  t=37670..62260   h_ref converges 60..62   upcnt=5, Upright(Z)
id=21  t=85020..122940  h_ref converges 57..62   upcnt=5, Upright(Z)
```

`min_classify_h_ref = 100`. Every alarm site is gated on `classify_ok = t.h_ref >= min_classify_h_ref`
(`fall_posture.cc:513, 549, 585`), and `born_upright` carries the same `box.h >= min_classify_h_ref`
term (`:131`). **`h_ref` for this person never exceeds 62, so `classify_ok` is permanently false and
T1b is equally unreachable.** If they slumped or fell out of that chair, the system would track it
and stay silent.

This is a **reachability defect of the same class as the one T1b fixed** — not a tuning gap. Lowering
`min_classify_h_ref` is *not* the fix: the constant exists to reject small spurious boxes, and round 1
raised it from 80 to 100 deliberately.

**The cause is the left frame edge.** The person is clipped to a 23px-wide sliver, and only their
upper body is resolved (box spans y 96..156 while the standing subject spans y 2..242). A seated
adult fully in frame would present `h` well above 100. **Framing is what makes them unclassifiable**,
which is the strongest argument yet for re-aiming.

Their detection is also poor in its own right: **48% of frames (230/477)**, with the longest absences
(13.5s, 6.9s, 5.0s) occurring when the walking subject crosses in front of the chair — 43% of the
absences have a large box overlapping the seat, so merge/occlusion explains part but not all of it.

⚠ **The right-hand `cx≈211` box is unresolved.** It sits *above* `min_classify_h_ref` (h 115–193), so
unlike the seated person it **can** spawn alarm-capable tracks (ids 2, 11, 12, 21, 22, 24 span both).
It produced no false positives here. Someone should confirm from the room whether it is a fixture or
a third occupant before anyone reasons about it again — this round's lesson is that the log cannot
tell you.

## The left frame edge, finally quantified

The four longest subject blackouts total 17.3s. **Three of them — 15.7s, 91% — begin with a box
touching or crossing the left boundary**, and the box shape at the moment of loss is round 4's
signature reproduced twice more:

| blackout | last box before loss | span in a 0..240 frame |
|---|---|---|
| **8.22s** | prone, cx=70 w=146 h=54, **bottom-left corner** | **-3 .. 143** |
| 4.29s | upright, cx=18 **w=31 for h=227** (ar 0.14) | 2 .. 34 |
| 3.20s | upright, cx=18 **w=31 for h=227** (ar 0.14) | 2 .. 34 |
| 1.61s | prone, cx=94 w=104 h=54 | 42 .. 146 — *not clipped, and the shortest* |

Round 4's miss began with `w=28` for `h=214`. Round 5 has `w=31` for `h=227`, twice. Same defect.

Aggregated over every subject frame:

```
P(subject lost on next poll | box touches left boundary)  =  11/121 = 9.1%
P(subject lost on next poll | box fully in frame)         =   4/252 = 1.6%
                                            relative risk =  5.7x
32% of all subject frames are in the clipped state.
```

**The camera is aimed too far right.** Confident detections of the walking subject cluster at
cx≈45 of 240 — the left fifth — and the seated occupant is further left still, clipped to a 23px
sliver. The walking subject spends a third of their time half-outside the sensor; the seated one
is never fully inside it.

### The 8.22s blackout is the safety-critical one

After fall 2 the subject came to rest in the **bottom-left corner**, box spanning **-3..143** — about
a quarter of the body outside the frame. The detector then saw nothing at all from 57240 to 65230.
**That is 8.2 seconds of blindness on a person lying on the floor**, ending only when they got up.

The alert still fired, but **only through `zombie_dropout`** — the fallback that infers a fall from
the *absence* of data. It is the weakest evidence path in the system: nothing distinguishes it from
a coincidental detector dropout. Fall 3, whose prone body was fully in frame (span 55..159), was
tracked for 58 consecutive frames and produced a clean `ground_confirmed`. That is the control.

### Would fixing the framing improve detection?

**Yes, but the reason has changed, and the headline rate is not where it shows up.** Round 5 has no
misses for framing to rescue. What it buys:

1. **Removes the dependence on `zombie_dropout`.** Fall 2 would have confirmed on observed ground
   frames at ~59050 (`GroundUnconfirmed` 56550 + `ground_confirm_ms` 2500) instead of 60460 —
   **~1.4s earlier and on real evidence**. One of three alerts currently rests on absence.
2. **Removes the dependence on T1b for left-entry falls.** Fall 1's subject was visible for only
   730ms / 3 frames before collapsing *because they entered at the frame boundary*. With adequate
   left coverage they would have walked upright in view for seconds, T1 would have confirmed
   normally, and the track would have had a real baseline. **Left-edge framing is the root cause of
   the condition T1b patches** — T1b is the safety net, not the fix.
3. **Recovers most of the blind time.** The three left-edge blackouts are 69 of 104 subject-blind
   polls (**66%**). At the in-frame loss rate, subject detection projects from 78.2% to **~92%**.
4. **Brings the seated occupant above `min_classify_h_ref` — the biggest win of the four.** They are
   currently tracked at `h_ref` 57–62 against a threshold of 100, so *no fall of theirs can ever
   alarm*. A seated, stationary occupant is also the person in that room most likely to need fall
   detection. Framing is the only thing standing between them and a working alarm; no firmware
   change reaches this without weakening the small-box rejection that keeps false positives at zero.

Cost: re-aiming a camera. **It remains the highest-value remaining work, and it is still not
firmware.** Recommend panning left by ~40–50px of frame width (≈20°) and re-running a 3-fall
re-enactment with at least one fall deliberately placed in the bottom-left corner.

Caveat: one 111s capture, three falls. The claim the doc has carried since round 2 —
"left edge implicated in a degraded or missed fall in every round" — holds for round 5 as
**degraded, not missed**. T1b and the `zombie_dropout` fallback absorbed it this time. Both are
backstops, and both were load-bearing here.

## What to do next

1. **Re-aim the camera left.** Everything above. Sensing, not firmware. It is now the only thing
   standing between the seated occupant and an alarm that can fire at all.
2. **Identify the `cx≈211` box from the room.** It is above `min_classify_h_ref` and therefore
   alarm-capable. Nobody should reason about it from the log again — round 5 already misclassified
   one stationary box as furniture when it was a person.
   **This question is five rounds old.** "Open problems" #2, written at `c27d082`, says of the
   stationary `41x115` box at `cx≈189`: *"If it is furniture rather than a second person, moving or
   re-aiming the camera may be worth more than any further tuning."* It was never answered, and every
   round since has quietly assumed furniture. Answer it by looking at the room, not the log.
3. **Gate the baseline re-seed on an upright pose** — still open. T1b closes it only for tracks that
   take the descent path; a track that never descends can still latch onto a prone box.
4. ~~**Suppress static phantoms.**~~ **Withdrawn.** Proposed in the first draft of the round 5
   writeup on the assumption that the two stationary boxes were furniture. One of them is a seated
   person. Suppressing long-lived zero-motion tracks would make the system deliberately blind to
   stationary occupants — the exact population fall detection exists for. **Do not do this.**
5. **`ALERT_COOLDOWN_MS = 15000` did not bite this round** (27.8s / 29.2s spacing) but remains the
   binding constraint on closely-spaced falls. Unchanged from round 4's assessment.
6. Phase B — still deferred; round 5 does not change the round 4 analysis.
7. Still open from round 1: remove the dead `AT+TSCORE` send, set `FD_PROBE_COMMANDS 0`.
