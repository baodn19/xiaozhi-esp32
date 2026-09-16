# Fall Detection — Handoff, Sep 16 2026

State as of commit `c27d082` on `feature/improve-fall-detection` (pushed to origin).
Companion to `fall_detection_sensing_fix.md` (the Phase A/B plan) and
`fall_detection_state_machine.md` (the state machine design).

**Next action: flash `c27d082`, re-enact falls, analyse. See "What to do next".**

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
