# Fall Detection: Sensing-Layer Fix

Companion to `fall_detection_state_machine.md`. That doc covers the state machine; this one covers
the sensing layer feeding it, which is where the Sep 16 capture failed.

Each module below is independently implementable and testable. Modules are ordered by dependency —
**M1–M2 ship alone**; M3–M7 only matter if you take the poll-rate change. M8 is independent.

---

## Why

Two real falls captured on hardware (`monitor.log` / `capture.csv`, Sep 16 — same capture, either
replays identically) raised no alarm. Replaying through `tools/fall_replay/fall_replay` reproduces
the device's two `FDEVT` lines exactly, so the failure is fully diagnosable on the host.

**The state machine is not at fault. The detector is blind most of the time.**

- **51 of 197 polled frames returned a person box — 25.9%.**
- Effective rate **4.44 fps** (poll hardcoded 200 ms, `fall_detection.cc:210`); module inference is
  only 55 ms (`perf: [7, 48, 0]`). Every poll got exactly one reply (197 `FDPOLL`, 197 detection
  frames), so the module is not the bottleneck — roughly 18 fps of headroom sits unused.
- Module filters at `tscore: 50`. Accepted scores: `50×3, 58×6, 64×6, 71×7, 77×7, 83×5, 87×10,
  89×7` — **minimum exactly 50, still rising at the cut.** Hard-clipped, not clear of the threshold.
- **Prone bodies score lower and are cut first**: upright mean 76.9, non-upright 69.0. The
  threshold blinds the system precisely when someone is on the ground.

Both falls landed inside blackouts:

| | last upright | blackout | evidence of the fall | blackout |
|---|---|---|---|---|
| Fall #1 | t=26690 | 2.90 s | **one** frame at t=30060 (`w=138 h=78`, ar=1.77) | 3.78 s |
| Fall #2 | none since t=26690 | 8.02 s | 3 frames, t=44310–44770 | 0.88 s |

Fall #2 had no upright baseline at all — no upright box exists between t=26690 and t=46090. No
tuning recovers a fall represented by one frame either side of a three-second gap.

### Secondary bug: the association anchor

Gate is centre-to-centre < `0.35 * h_ref` ≈ 83 px (`fall_posture.cc:151`). In a fall the box
*centre* moves a lot precisely **because** height collapses — upright (h=240, centre y=117) → prone
(h=78, centre y=188). Fall #1's centre moved 92 px, just outside the gate, so the prone box never
re-associated and the track expired holding the stale `r=0.20`. The box **bottom** moved 7 px
(234 → 227). Feet are the ground-plane position; the centre is contaminated by posture. Confirmed
on a scratch copy:

```
box-centre:  FDEVT,3,Upright,Init,30270,vy=0.00,vh=-0.03,drop=-0.01,r=0.20   <- stale upright box
feet-anchor: FDEVT,3,Upright,Init,33610,vy=0.08,vh=-0.18,drop=0.29,r=1.77   <- real prone features
```

### Measured frame spacing — load-bearing for every constant below

Gaps between frames that actually call `Update()`:

```
n=196  min=170  p01=200  p05=210  p25=220  median=220  p75=230  p95=250  max=420
```

The nominal 200 ms poll yields **220 ms** because `fall_detection.cc:210` uses strict `>` plus a
50 ms read timeout and a trailing 10 ms `vTaskDelay`. **Setting the literal to 100 gives ~110–130 ms
(~8 fps), not 100.** Every duration below is sized against this measured distribution.

---

# Phase A — threshold only

One flash, tiny diff, no state-machine code touched. Isolates how much of the blindness is purely
the threshold. **M1 and M2 must ship together or neither does anything.**

## M1 — send `AT+TSCORE=25` at init

**File:** `main/boards/common/fall_detection.cc`

Add `#define FD_TSCORE 25` beside the existing defines (~line 29). In the constructor, after the
existing 2000 ms settle delay (lines 287–292):

```cc
char tscore_cmd[24];
snprintf(tscore_cmd, sizeof(tscore_cmd), "AT+TSCORE=%d\r", FD_TSCORE);
uart_write_bytes(uart_num_, tscore_cmd, strlen(tscore_cmd));
vTaskDelay(pdMS_TO_TICKS(200));
```

**This can fail silently, and there is precedent in this very capture.** The only AT response in
the whole log is `{"type": 2, "name": "AT", "code": 5, "data": "Unknown command: "}`, and there are
**zero** `MODELS` replies — the existing `AT+MODELS?\r` (line 289) has never worked in this
firmware. Nothing in the class parses AT replies; `ProcessDetectionLine` drops any line without a
`"boxes"` key (`fall_detection.cc:156-157`), so a stray reply cannot corrupt tracker state. The
reply *is* captured in `FDLOG` under `FD_CAPTURE_MODE`, so verification is free — and mandatory.

If rejected, try `\r\n` — the vendored client's `CMD_SUFFIX`
(`managed_components/wvirgil123__sscma_client/include/sscma_client_commands.h:17`) — before
concluding the command is unsupported. `TSCORE` is in that header's command table (line 35).

Leave the broken `AT+MODELS?` alone; removing it is unrelated scope.

**Done when:** the new capture contains a `"name": "TSCORE"` reply with `"code": 0`, *and* the
`type: 0` config echo in each `INVOKE` reply reads `"config": {"tscore": 25, ...}`.

## M2 — lower the firmware's own score gate

**Files:** `main/boards/common/fall_detection.cc:29`, `tools/fall_replay/main.cc:36`

`MIN_BOX_SCORE = 40` (applied at `fall_detection.cc:171`) is inert today because the module already
filters at 50. **Drop `tscore` to 25 without this and boxes scoring 25–39 arrive and are thrown
away by our own firmware — M1 accomplishes nothing.** Set it to `25`.

The replay harness duplicates the constant: `tools/fall_replay/main.cc:36` hardcodes `score >= 40`.
**Change both together.** If they drift, replaying a new capture stops reproducing device behaviour
and the regression harness is worthless.

**Done when:** both constants read 25 and `./fall_replay < monitor.log` still produces the same two
`FDEVT` lines on the *old* capture (no box in it scores under 50, so this must be a no-op).

## Phase A verification

1. Flash, re-enact both fall types, capture.
2. `grep -o 'FDLOG,[0-9]*,[0-9]*,[0-9]*,.*' monitor.log | grep -v '"name": "INVOKE"'` → expect the
   TSCORE ack. A `code: 5 "Unknown command"` means the change did nothing — **stop here**, the rest
   of the plan assumes a working detection rate.
3. Re-measure detection rate (today 25.9%) and the score histogram minimum (should sit near 25; if
   still 50, the command didn't take).
4. Re-measure blackout durations around each fall.

**Decision point.** If descents are now observable at 4.4 fps, Phase B's poll-rate change can be
deferred and **M3–M7 become unnecessary** — they exist only to make the rate change safe. M8 (feet
anchor) is independent and worth taking either way.

---

# Phase B — rate decoupling, then the rate change

## The rule that makes this checkable

Each converted gate becomes `count >= min_samples && (now_ms - start_ms) >= duration_ms`.

Durations are sized **strictly below the span the sample floor already guarantees at the old rate**,
so at 4.5 fps the sample term always binds and replay output is **bit-identical by construction**,
not by luck. At 10 fps the duration binds and wall-clock is preserved.

Sizing: `duration = (min_samples - 1) × dt_floor`, `dt_floor` from the measured distribution:

| samples | duration | why it can never bind at the old rate |
|---|---|---|
| 2 | **160** | min measured gap is 170 |
| 3 | **330** | min possible pair is 170 + 200 = 370 |
| 10 | **1800** | needs nine gaps averaging < 200; only 4 of 196 are ≤ 200 |

**Representation: keep the existing `int` counter, add a `uint32_t <name>_start_ms`.** The counter
doubles as the timestamp's validity flag, so `start_ms` is never read while zero — no sentinel
needed. That matters: all four host tests start at `now_ms = 0` and the device tick starts near zero
at boot, so `0` is unusable as a sentinel.

Add two helpers to the anonymous namespace of `fall_posture.cc`; they also deduplicate the
identical increment/reset block appearing at cc:339, 349, 359, 384, 403, 440, 469:

```cpp
inline void BumpStreak(int& count, uint32_t& start_ms, uint32_t now_ms, bool qualifies) {
    if (!qualifies) { count = 0; return; }
    if (count == 0) start_ms = now_ms;
    count++;
}
inline bool StreakMet(int count, uint32_t start_ms, uint32_t now_ms, int min_samples, uint32_t min_ms) {
    return count >= min_samples && (now_ms - start_ms) >= min_ms;
}
```

## M3 — convert the six frame counts

**Files:** `main/boards/common/fall_posture.h`, `main/boards/common/fall_posture.cc`

| Removed (`Tuning`) | Added | Read site |
|---|---|---|
| `upright_confirm_frames = 10` (h:34) | `upright_confirm_min_samples = 10`, `upright_confirm_ms = 1800` | cc:312 |
| `recovery_upright_streak = 2` (h:55) | `recovery_upright_min_samples = 2`, `recovery_upright_ms = 160` | cc:344, 389, 411 |
| `low_transient_confirm_frames = 2` (h:56) | `low_transient_min_samples = 2`, `low_transient_confirm_ms = 160` | cc:354 |
| `ground_confirm_frames = 2` (h:58) | `ground_entry_min_samples = 2`, `ground_entry_confirm_ms = 160` | cc:364 |
| `fall_confirmed_recovery_streak = 3` (h:68) | `fall_confirmed_recovery_min_samples = 3`, `fall_confirmed_recovery_ms = 330` | cc:474 |
| `max_missing_frames = 3` (h:75) | `max_missing_ms = 750` | cc:539 |

New `TrackedPerson` fields: `upright_confirm_start_ms`, `upright_streak_start_ms`,
`low_confirm_start_ms`, `ground_confirm_start_ms`.

`upright_streak_start_ms` is **shared** by `recovery_upright_*` and `fall_confirmed_recovery_*`.
Safe: only one state is active per frame, and `EnterState` (cc:260) resets the counter on every
transition, so a streak accumulated in `kGroundUnconfirmed` cannot leak into `kFallConfirmed`.
Leave cc:260 exactly as is. Document the invariant `fall_confirmed_recovery_* >= recovery_upright_*`
(T15 must stay strictly harder than T13).

> **Naming trap.** Do *not* call the T6 field `ground_confirm_ms` — that name is already taken
> (h:62) for the unrelated 2500 ms sustained-ground window. Rename the existing one to
> `ground_sustain_window_ms` (single read site, cc:409) so the two cannot be confused.

### `max_missing_frames` collapses entirely

Delete `frames_until_untracked` (h:127, cc:190, cc:538–539) and compare:

```cpp
if (now_ms - t.last_seen_ms > tuning_.max_missing_ms) { ... }
```

`last_seen_ms` is already maintained and already used by the zombie path at cc:532. Keep the
`last_seen_ms == now_ms` guard at cc:529 — now redundant, but it documents intent, costs one
compare, and protects against a future reorder of the match/sweep loops.

This fixes two real bugs for free:
- Two UART lines landing in the same 10 ms tick currently tick the counter twice.
- **Silent polls** (the 350/390/420 ms gaps — polls returning nothing parseable) never called
  `Update()` at all, so the counter **under-measured absence exactly when absence was longest** —
  which is the failure mode this whole document is about.

**Honest caveat:** exact equivalence is impossible. Neutrality needs `X >= 3·dt_max` *and*
`X < 4·dt_min`; with dt ∈ [170, 250] that's `X >= 750` and `X < 680`. Contradictory. `750` is
neutral for dt ∈ [187.5, 250], covering 195 of 196 measured gaps. **If the M3 replay diff is
non-empty, look here first.**

**Done when:** `./fall_replay < monitor.log` is byte-identical before and after.

## M4 — fix the hard-coded `stall_frames == 2`

**File:** `main/boards/common/fall_posture.cc:334`

No tuning field today, and `pause_count == 0` is a required term in `ComputeBallistic` (cc:250), so
a spurious pause demotes a real fall to `kLyingBenign` with `"controlled_descent"`. At 10 fps two
frames is 100 ms — well inside noise.

Replace `int stall_frames` with `stall_count` + `stall_start_ms` + **`bool stall_counted`**; add
`stall_min_samples = 2` and `stall_ms = 160`:

```cpp
bool stalled = f.vel_valid && std::fabs(f.v_cy_n) < tuning_.stall_v_n_max;
if (stalled) {
    if (t.stall_count == 0) { t.stall_start_ms = now_ms; t.stall_counted = false; }
    t.stall_count++;
    if (!t.stall_counted &&
        StreakMet(t.stall_count, t.stall_start_ms, now_ms, tuning_.stall_min_samples, tuning_.stall_ms)) {
        t.pause_count++;
        t.stall_counted = true;
    }
} else {
    t.stall_count = 0;
    t.stall_counted = false;
}
```

The latch is **mandatory**, not cosmetic: `== 2` counted a long pause once only because the count
hit 2 exactly; a two-term gate stays true on subsequent frames.

> ⚠️ **`EnterState` must also reset `stall_counted = false` at both cc:267 and cc:275.** Miss this
> and a track that stalls, returns to `kUpright`, then re-enters `kDescending` never counts another
> pause for the rest of its life — a silent, permanent loss of the anti-false-positive mechanism.
> This is the easiest bug to introduce in the entire change.

Do **not** raise `stall_v_n_max`. The problem is the *variance* of the quantity, not where the
threshold sits — that's M5's job.

**Done when:** replay still byte-identical (`stall_ms = 160` < the 170 ms min gap, so the pause
still fires on the 2nd stalled frame exactly as today).

## M5 — time-bound the velocity window

**Files:** `main/boards/common/fall_posture.h:117`, `fall_posture.cc:217-225`

Velocity is correctly time-normalised (least-squares over real `hist_t_ms`, cc:11-30), but the
window's *span* halves at 10 fps (~660 → ~330 ms) and slope variance scales ~1/span², so the
estimate gets **~4× noisier** — attacking the two `0.10f` stationary gates and feeding straight back
into M4.

**A bare `kVelWindow 4 → 8` is not viable**: at the old rate it spans ~1540 ms instead of ~660 ms,
changing every velocity and making the replay diff unreadable. Instead treat 8 as *slot capacity
only* and bound the window by time:

```cpp
// Tuning
uint32_t vel_window_ms   = 780;
int      vel_min_samples = 3;
```

In `ComputeFeatures`, pick the start index by age before fitting:

```cpp
int n = t.hist_count, start = 0;
uint32_t newest = t.hist_t_ms[n - 1];
while (start < n - 1 && (newest - t.hist_t_ms[start]) > tuning_.vel_window_ms) start++;
int m = n - start;
f.vel_valid = (m >= tuning_.vel_min_samples);
```

At the measured 210–250 ms spacing, 780 ms admits exactly the 4 samples the current code uses. At
~110 ms it gives 8 over ~770 ms, recovering the full span factor plus 2× from sample count —
**without touching either velocity threshold.**

This also fixes a pre-existing bug a bare bump would worsen: `hist_count` only resets via
`KillTrack` (cc:120-122), and zombie re-acquisition goes through `PushHistory` with history intact,
so today an 880 ms dropout leaves a window whose slope is dominated by the single post-gap point.
Age-filtering evicts stale samples automatically.

### `vel_valid` is not optional

Add `bool vel_valid` to `Features`. **Without it, the 2–3 frames after every dropout read as
velocity ≈ 0 → `stalled` true → `pause_count++` → a real fall that dropped out mid-descent is
demoted to benign** — exactly the failure mode being eliminated.

- `stalled` (cc:331): `f.vel_valid && ...` → unknown velocity resets the stall run.
- T5 (cc:349): `f.vel_valid && ...` → unknown velocity does not confirm low; keeps the descent alive.
- `lying_immobile` (cc:452): treat unknown as *moving* (restarts a 2-minute backstop; harmless).
- `descent_sig` (cc:242): no change — it requires positive velocity, which a zeroed slope fails.

### Bundle: `hist_t_ms` `float` → `uint32_t`

Same memory. `float32` ULP at 24 h uptime is 8 ms — 3.6% timestamp noise at dt=220, 8% at dt=100,
degrading with uptime. Worse, at the 49.7-day `uint32_t` tick rollover the float timestamps jump
~4.29e9 → ~0 and the slope is catastrophically wrong for a full window: a plausible
once-per-49-days spurious alarm. Have `LeastSquaresSlope` take `const uint32_t*` and difference
against the window start (unsigned subtraction wraps correctly).

**Cost:** +64 B/track × `kMaxTrackedPeople = 15` ≈ +960 B static RAM. Noise on an S3. Add
`static_assert(sizeof(TrackedPerson) <= 320, ...)` so nobody grows it casually.

**Done when:** replay diff is non-empty but **every differing line sits within `vel_window_ms` of a
measured gap ≥ 300 ms**. Falsifiable, not a judgement call.

## M6 — time-aware baseline EMA

**Files:** `main/boards/common/fall_posture.h:29`, `fall_posture.cc:209-210`

`baseline_ema_alpha = 0.1f` is applied per *observed frame*: τ = −1/ln(0.9) = 9.49 samples × 220 ms
= **2.09 s**, matching the h:29 comment. At 100 ms the same α gives τ ≈ 0.95 s.

**No scalar is correct at both rates** — and the measured gaps already span 170→420 ms *within one
capture*, so effective τ varies 2.5× frame to frame today, and one post-dropout sample carries 2.5 s
of weight. Replace with:

```cpp
// Tuning -- replaces baseline_ema_alpha
uint32_t baseline_tau_ms    = 2000;
float    baseline_alpha_max = 0.25f;
```

```cpp
float dt = static_cast<float>(now_ms - t.baseline_last_ms);
float a  = 1.0f - std::expf(-dt / static_cast<float>(tuning_.baseline_tau_ms));
if (a > tuning_.baseline_alpha_max) a = tuning_.baseline_alpha_max;
t.h_ref  = a * t.h  + (1.0f - a) * t.h_ref;
t.cy_ref = a * t.cy + (1.0f - a) * t.cy_ref;
t.baseline_last_ms = now_ms;
```

The clamp is load-bearing: after a 2500 ms zombie gap the unclamped α is 0.71, snapping the baseline
to one re-acquired sample. `h` is protected by `baseline_reject_frac` (cc:206-207) but **`cy_ref` is
not gated at all**, and `drop_n = (cy − cy_ref)/h_ref` drives `is_ground`, `is_low` and `is_upright`.

Needs its own `uint32_t baseline_last_ms` — `UpdateTrackObservation` sets `last_seen_ms = now_ms` at
cc:189 *before* `UpdateBaseline` runs (cc:516 then 517), so `last_seen_ms` is already `now_ms` and
unusable for dt. Initialise it in `CreateTrack`; skip the update when dt == 0.

> **This is the one module that cannot be bit-neutral.** At the modal 220 ms, τ=2000 gives α=0.1042
> vs today's 0.1000 — 4%, orders of magnitude below any threshold's sensitivity, but not identical.
> Its regression criterion relaxes to *identical `FDEVT` transition sequence*.

**Done when:** the `FDEVT` transition sequence on the old capture is unchanged.

## M7 — poll interval 200 → 100 ms

**File:** `main/boards/common/fall_detection.cc:210` (plus the comment on line 209)

One literal. Buffer safety is fine: line assembly is bounded and flags truncation
(`line_overflow`), the driver ring is 2048 B, and the loop drains every 10–60 ms.

Expect **~8 fps (110–130 ms)**, not 10, for the reasons in "Measured frame spacing" above. The
M3–M6 constants are sized to be correct across that whole band.

Deferred follow-up: response-triggered pacing — poll immediately after consuming the previous reply,
which runs at the module's native 55 ms and removes poll-phase jitter entirely. Land it *after* this
plan so the replay diff has one variable at a time.

**Done when:** a fresh capture's measured `Update()` spacing sits in the 110–130 ms band, and the
decimation cross-check (Verification → On hardware, step 2) reproduces the full-rate `FDEVT`
sequence. This module has no offline gate — it is only meaningful against new hardware data.

## M8 — feet-anchor association *(independent of M3–M7)*

**File:** `main/boards/common/fall_posture.cc:137-138, 144-145`

```cc
float bcy = boxes[bi].y + boxes[bi].h;   // feet, not centre
float tcy = t.y + t.h;
```

Gate value and the claimed/reciprocal-best logic are **unchanged**. This does not loosen the gate,
it fixes the anchor — so the ID-swap protection argued for in
`fall_detection_state_machine.md:105-131` is fully preserved.

> **Deliberately not gated on `bottom_valid`.** That field (`fall_posture.cc:227`) is computed and
> read nowhere — dead code today. Falling back to the centroid when feet are cropped is tempting,
> but **97% of upright boxes in the real capture are cropped** (`y+h >= 235`; 58% for prone), so
> such a rule would disable the feet anchor almost always — including for fall #1, the exact case
> it exists to fix. Saturation is a *shared* distortion, not a divergence: standing and lying people
> are both on the ground plane, so both anchors sit near the frame bottom. It also doesn't weaken
> multi-person discrimination, since two standing people have both `bottom ≈ 240` *and*
> `centre_y ≈ 117` — both anchors reduce to `cx`-only there. Clamping `bottom` to `frame_h` for
> comparison is optional polish.

**Done when:** replaying the old capture shows fall #1's prone box at t=30060 re-associating —
`FDEVT,3,Upright,Init,33610,...,r=1.77` instead of `...,30270,...,r=0.20`.

## Why the conversion is necessary, not hygiene

With raw counts at 10 fps the errors do **not** cancel — recovery-side gates (T4/T8/T10/T13/T15) and
confirmation-side gates (T1/T5/T6) both get easier, in opposite-harm directions.

The sharpest case: a descent traverses the `is_low` band (`drop_n` 0.10 → 0.28) on its way to
`is_ground`. At 4.5 fps a fast fall spends 0–1 frames there and passes straight to T6. At 10 fps the
*same physical fall* spends 2–3 frames there, so T5 fires mid-descent, the track diverts to
`kLowTransient`, and **a real fall is classified as sitting down.** The only thing preventing it is
T5's `|v_cy_n| < 0.10` gate — exactly the gate M5's noise increase degrades. **M3 and M5 compound
here; neither alone suffices.**

T6 breaks the other way: 2 raw ground frames = 200 ms instead of ~440 ms, so `kGroundUnconfirmed` is
entered ~240 ms earlier, so `ComputeBallistic` sees a *shorter* duration against an unchanged
`ballistic_max_duration_ms = 1400` → more descents look ballistic → more false alarms.

T5-vs-T6 ordering (cc:354 before cc:364) **cannot** flip: `is_low` is `... && !is_ground` (cc:237),
so the counters are mutually exclusive by construction and can never both cross on one frame. Hold
the two constants equal anyway as a documented invariant — the question goes live the moment anyone
loosens that clause.

---

## M9 — test migration

**File:** `tools/fall_replay/fall_posture_test.cc`

All four tests hardcode `dt_ms = 200` (lines 80, 115, 155, 206) and call `Establish(..., dt_ms, 12)`
where `12` is a bare literal exceeding `upright_confirm_frames = 10`.

Restructure `main()` to `RunAll(200); RunAll(100);` with each test taking `dt_ms`, asserting the same
outcome at both. **That directly tests the property this refactor introduces — rate independence —
which no amount of constant-fixing does.**

`Establish()` should loop until the track reaches `kUpright` (safety cap + `CHECK(false)` if it
never does), then feed `settle_frames = 2` more, preserving today's 12-frame behaviour at dt=200.
The extra frames matter: `TestBallisticFallFiresT11`'s comment (lines 157-162) reasons explicitly
about where `h_ref` settles.

- **`TestVelocityRegression`** — has a premise break *unrelated* to the conversion: `px_per_frame =
  3.0f` at dt=100 is 30 px/s, giving `v_cy_n = 0.30` against `descent_v_cy_n_min = 0.30`, so the
  closing `CHECK(state == kUpright)` flips on a float coin toss. Express the ramp in **px/s**
  (`expected_v_cy_n = px_per_s / h_ref`), and replace the `i < 4` history flush with a time-driven
  loop covering `vel_window_ms + dt_ms`. Load-bearing: the `< 0.02f` tolerance.
- **`TestCrouchCancelsViaT10`** — `hs_up[] = {55,70,85,100,100,100}`: `85/100` fails
  `upright_h_n_min > 0.85` *strictly*, so only three frames are upright, leaving **zero** spare at
  dt=100. Add two more `100` entries. The intermediate `CHECK(state == kGroundUnconfirmed)` at line
  132 asserts the path, not the property — relax it if needed.
- **`TestBallisticFallFiresT11`** — drive the descent by elapsed time (`frac = elapsed_ms/800.0f`),
  else the "same" 4 frames describe a 2× faster fall at dt=100. Load-bearing: `g_alert_count == 1`.
- **`TestDeliberateLieDownIsBenign`** — premise survives (both rates clear `stall_ms = 160`); change
  the 6-frame pause to `while (elapsed < 1200)`. **But it cannot catch the regression we actually
  fear** — its pause is *deliberate*, the 10 fps hazard is *noise-driven*, and synthetic boxes have
  zero pixel noise. It will keep passing whether or not the regression exists.
- **New test, currently missing:** a fast descent with **no** pause must yield `pause_count == 0` at
  both rates. This is the complement that would actually catch M4/M5 going wrong.

Already ms-based and needing no test changes: `ballistic_max_duration_ms`, `ground_confirm_ms`,
`zombie_expiry_ms`, `descending_forced_exit_ms`.

**Done when:** `make -C tools/fall_replay test` reports `ALL PASS` at **both** dt=200 and dt=100,
and the new no-pause test fails if you temporarily revert M4's latch — a test that cannot fail is
not a test.

---

## Verification

### Regression gates, hardest first

| Gate | Modules | Criterion |
|---|---|---|
| Bit-identical | M2, M3, M4 | `./fall_replay < monitor.log` **byte-identical** before/after on the old 200 ms capture. Do not proceed until it is. |
| Bounded diff | M5 | Diff non-empty, but every differing line within `vel_window_ms` of a measured gap ≥ 300 ms |
| Transition sequence | M6 | `FDEVT` sequence unchanged; byte equality explicitly *not* required |
| Re-association | M8 | Fall #1's prone box reaches the track (see M8 "Done when") |
| Unit tests | M9 | `make -C tools/fall_replay test` → `ALL PASS` at both dt=200 **and** dt=100 |

Build and run (the `-I` is relative, so `cd` first):

```
cd tools/fall_replay && make && ./fall_replay < ../../monitor.log
cd tools/fall_replay && make test
```

### On hardware

1. `idf.py build flash monitor` with `FD_CAPTURE_MODE 1`; confirm `FDLOG` keeps flowing *through*
   the alert window and `FDEVT` shows plausible transitions while walking / sitting / crouching.
2. **Decimation cross-check — the cleanest test of rate decoupling.** Take the new ~8 fps capture,
   drop every other detection frame, and confirm the decimated replay yields the same `FDEVT`
   sequence as the full-rate replay. If it doesn't, a rate coupling was missed.
3. Re-enact both fall types → alarm fires. Zero alarms across sit / lie / crouch.

### Watch items (not changes)

- `FDSTACK_FREE` is a flat **476 bytes of 6144** across all 42 heartbeats — 7.7% headroom. Thin
  already, unrelated to this work, and arguably not "healthy" as
  `fall_detection_state_machine.md:308` requires. Watch it after flashing.
- `TSCORE=25` may admit low-quality boxes that drag `h_ref` via `baseline_reject_frac = 0.25`. A
  *new* failure mode created by M1, interacting directly with M6 — **the thing to watch most
  closely in the first new capture.**
- Association in multi-person scenes, given the combined rate increase and feet anchor.
- `std::vector<BoxObservation>` per frame (`fall_detection.cc:163`) goes from 4.4 to ~8 heap
  allocations/s. Low priority; a fixed `BoxObservation[kMaxTrackedPeople]` stack array removes it.

---

## Updates owed to `fall_detection_state_machine.md`

- **Status** (38–41) — the "live bug under investigation" is resolved: tracks-never-progress was
  detector blindness, not a state-machine defect.
- **Association** (105–131) — the gate derivation reasons about *per-frame* centroid displacement
  (~7–16 px) and concludes the gate is too loose. That still holds. Add that displacement **across a
  dropout** is a different quantity (92 px measured), and the fix is the anchor, not the gate.
- **Deferred → Poll rate** (260–266) — done; record measured 220 ms vs nominal 200 ms.
- **Honest limits** (280–283) — foot-cropping frequency was requested and is now measured: **97%
  upright, 58% prone**. The `MIN_BOX_SCORE` score scale is now confirmed (50–89, clipped at tscore).
- **Cannot be simulated** (312–314) — "detector confidence collapse on prone bodies (the likely
  dominant miss mode)" is now measured, not hypothesised: 25.9% overall hit rate, prone mean 69.0 vs
  upright 76.9.

`fall_detection.md` has stale line numbers throughout and documents the pre-`PostureTracker`
algorithm; leave it unless refreshing it separately.
