# Fall Detection

## Problem
Detect **forward, backward, sideway, and axial (vertical collapse)** falls from bounding boxes only.
Do **not** alarm on **sitting down on a chair, lying down, or crouching**.

## Hardware / data path
```
Arducam 5MP ──► Grove Vision AI V2 (Himax WE2, YOLO-Swift person det.)
                      │ UART 921600 8N1, AT commands
                      ▼
              ESP32-S3 (FallDetectionController, core 1)
                      │
        ┌─────────────┼──────────────┬─────────────┐
        ▼             ▼              ▼             ▼
  I2S → MAX98357A  DualEye UART   ILI9341 LCD   MCP notify
   (alarm sound)   (0x04 WARNING) (chat msg)    (cloud)
```

- Grove: `BOARD_GROVE_TX_PIN` GPIO 11 (ESP TX → Grove RX), `BOARD_GROVE_RX_PIN` GPIO 12 (ESP RX ← Grove TX).
- Speaker amp: `AUDIO_I2S_SPK_GPIO_BCLK` GPIO 2, `LRCK` GPIO 42, `DOUT` GPIO 41.
- DualEye: `EYE_UART_PORT` UART_NUM_2, `EYE_UART_TX_PIN` GPIO 10, 115200.
- Active board: `bread-compact-wifi-lcd-touch` (set in `sdkconfig.defaults`, NanaBot block).

## Files

### Detection core — everything to change lives here
- `main/boards/common/fall_detection.cc` (336 lines) — the whole subsystem: UART setup, poll loop,
  JSON parsing, tracking, fall classification, alert fan-out.
    - `22-38` — all tunable constants (table below) + `kDetectionTaskStackSize`
    - `122-128` `GetBoxDistance()` — Euclidean distance between box **centers**
    - `130-236` `ProcessDetectionLine()` — parse → associate → classify → age out
        - `131-159` parse `"boxes":[[x,y,w,h,score,target],…]` via two `sscanf` format variants
        - `161` frame timestamp taken from `xTaskGetTickCount()` **inside** this function
        - `163-226` greedy nearest-centroid association; new-track creation at `209-224`
        - `181-198` the fall test (3-way AND)
        - `228-235` age-out via `frames_until_untracked > MAX_MISSING_FRAMES`
    - `238-304` `DetectionTask()` — 200 ms poll, 50 ms UART read, line assembly, capture logging
    - `306-331` ctor — UART config, `AT+MODELS?` handshake, task spawn
- `main/boards/common/fall_detection.h` (50 lines)
    - `20` `kMaxTrackedPeople = 15`
    - `22-34` `struct TrackedPerson` — the entire per-person state: `x,y,w,h`, `last_y`,
      `last_ratio`, `last_time`, `frames_until_untracked`, `active`, `ratio`
- `main/CMakeLists.txt:58` — registers `fall_detection.cc` unconditionally for all boards;
  `:65` puts `boards/common` on the include path.

### Board wiring
- `main/boards/bread-compact-wifi-lcd-touch/compact_wifi_board_lcd_touch.cc`
    - `13` include, `109` member `fall_detector_`, `222-231` `InitializeTools()`,
      `229` `static FallDetectionController fall_detector(UART_NUM_1, 11, 12);`
    - `247-251` `SendEyeCommand()` override — raw 1-byte write to DualEye UART
    - `258-271` `NoAudioCodecSimplex` (the I2S output the alarm plays through)
    - Note: `fall_detector_` is stored but **never dereferenced** — there is no start/stop/query API.
- `main/boards/bread-compact-wifi-lcd-touch/config.h`
    - `17-19` speaker I2S pins, `137-140` Grove pins, `143-148` DualEye UART, `151-156` `eye_cmd_t`
    - The Grove pin macros are **not** used — `compact_wifi_board_lcd_touch.cc:229` hardcodes `11, 12`.
- `main/boards/bread-compact-wifi-lcd-touch/README.md` — `17` wiring section, `39-47` speaker table,
  `59-65` Grove table, `68-76` DualEye + power caveat, `179+` cloud system prompt (fall behavior at `195-207`).

### Alert / output path
- `main/boards/common/fall_detection.cc:74-120` `TriggerFallAlert()` — mutex-latched `alert_active_`,
  NVS counter, `AbortSpeaking`, eye `0x04`, alarm task, LCD message, MCP event, then `vTaskDelay(15000)`.
- `main/boards/common/fall_detection.cc:50-60` `AlarmSoundTask` — 20 s loop, `OGG_SUCCESS` every 2 s, 3072 B stack.
- `main/application.cc:1134-1136` `PlaySound` → `main/audio/audio_service.cc:633-654` (non-blocking;
  Ogg-demuxes and pushes to the decode queue).
- `main/assets/lang_config.h:186-191` `OGG_SUCCESS`; asset `main/assets/common/success.ogg`.
  **No dedicated alarm/siren asset exists** — the emergency reuses the generic success chime.
- `main/application.cc:1095-1105` `SendMcpMessage`; `main/boards/common/board.h:85` `SendEyeCommand` base no-op.
- `main/audio/codecs/no_audio_codec.cc:79-146` ctor, `:218` `Write`, `:271` `EnableOutput`.
- NVS: namespace `fall_tracker`, key `total_falls` (`fall_detection.cc:40-48, 62-72`).

### Tuning / capture tooling
- `FD_CAPTURE_MODE` (`fall_detection.cc:34`, currently **1**) emits at `ESP_LOGI`, tag `FallDetection`:
    - `FDPOLL,<ms>` (`:259`) — poll send time
    - `FDLOG,<seq>,<ms>,<overflow>,<raw JSON line>` (`:272-278`) — every raw UART line + data-loss flag
    - `FDSTACK_FREE,<bytes>` (`:297`) — 1 Hz stack high-water mark
- `FDLOG` alone is enough to replay frames offline and sweep thresholds.
- `tools/fall_replay/` **exists but is empty and untracked** — the replay harness is a placeholder.
  Nothing in the repo parses `FDLOG`.
- Per-frame kinematics are logged at `ESP_LOGD` (`:188-189`), suppressed at the default log level.
- **No tests of any kind.** `test/` is gitignored (commit `e35b042`) and does not exist on disk.

### Reference implementation worth borrowing from
- `main/boards/sensecap-watcher/sscma_camera.cc:83, 111-122` — the upstream SSCMA path does
  `detect_target` + `detect_threshold` filtering. Box type: `managed_components/wvirgil123__sscma_client/include/sscma_client_types.h:89`.
  Independent of `FallDetectionController`, but it is the in-repo example of a confidence gate.
- `main/boards/common/medicine_reminder.h:125-137, 157` — near-identical copy-pasted `AlarmSoundTask`
  (same 20 s / 2 s / `OGG_SUCCESS`). The two alarms can fire concurrently.

## Current algorithm

**Poll** every 200 ms with `AT+INVOKE=1,0,1\r` → Grove replies with a JSON line → keep boxes where
`target == 0` and `w,h > 0` → greedy nearest-centroid match against the 15 track slots (gate: 60 px)
→ per matched track compute:

```
delta_time     = (now - p.last_time) / 1000          // gated at > 0.01 s
velocity_y     = (box.y - p.last_y) / delta_time     // box.y is the TOP edge
current_ratio  = w / h
ratio_velocity = (current_ratio - p.last_ratio) / delta_time

fall if  velocity_y > 40  &&  ratio_velocity > 0.6  &&  current_ratio > 1.2
```

Single-frame trigger, no confirmation window. Unmatched boxes claim a free slot; tracks not updated
this frame age out after 3 missed frames (~600 ms).

| Constant | Value | Line | Notes |
|---|---|---|---|
| `UART_BUF_SIZE` | 1024 | `23` | RX ring is 2× this |
| `TARGET_PERSON_ID` | 0 | `24` | YOLO "person" class |
| `FALL_RATIO_THRESHOLD` | 1.2 | `25` | terminal w/h |
| `DISTANCE_THRESHOLD` | 60.0 px | `26` | association gate |
| `SUSTAINED_FALL_MS` | 1500 | `27` | **declared, never used** |
| `MAX_MISSING_FRAMES` | 3 | `28` | ~600 ms |
| `VELOCITY_Y_THRESHOLD` | 40 px/s | `29` | absolute pixels |
| `RATIO_SHIFT_THRESHOLD` | 0.6 /s | `30` | |
| `FD_CAPTURE_MODE` | 1 | `34` | |
| `kDetectionTaskStackSize` | 6144 | `38` | measured peak ~3.6 KB |

## Diagnosis

### A. Signal is bounded — know the ceiling before tuning
The model emits **bounding boxes only** (no keypoints). The entire feature space is: box centroid
trajectory, top/bottom edge trajectory, w/h ratio, box area, and confidence. Any classifier must be
built from those five.

**`velocity_y` uses `box.y`, the top edge** (`:184`), while `GetBoxDistance` computes centroids
(`:122-128`) — so the code already has centroid math it does not use for classification. The top edge
is the *worst* of the available vertical signals: it drops sharply for sitting, crouching, and even
for bending over. The **bottom edge** (`y+h`, roughly the feet) is the one that stays put in a fall
and in a sit, but translates when walking; **centroid-y normalized by box height** is the one that
separates descent magnitude from camera distance. Neither is tracked.

### B. Per fall type
- **Sideway fall** — body goes horizontal in the image plane, `w/h` clearly exceeds 1.2. This is the
  only type the current rule is actually shaped for.
- **Forward / backward fall along the camera axis** — the body foreshortens: height collapses, width
  roughly constant, so `w/h` does rise, but the box also shrinks overall and the bottom edge can leave
  the frame at close range. Detection is plausible but the ratio spike is smaller and slower than the
  sideway case, so a threshold tuned on sideway falls will miss these.
- **Axial fall (vertical collapse / crumple)** — the person folds straight down into a heap. The box
  becomes small and roughly **square** (`w/h ≈ 0.8–1.1`), rarely crossing 1.2. `FALL_RATIO_THRESHOLD > 1.2`
  hardcodes "wider than tall" and is a structural blind spot for this class. Box **area collapse rate**
  and centroid drop, not ratio, are the signals here.

### C. Per non-fall action
- **Crouching** — height shrinks, width constant or wider (knees out), top edge drops fast. Ratio
  approaches ~1.0 but usually stays under 1.2, so the ratio gate mostly saves us. Recovery within
  1–2 s is the reliable discriminator, and nothing measures it.
- **Sitting on a chair** — top edge drops fast (torso descends), height shrinks (legs fold), width
  constant → ratio rises. A frontal seated pose can land near the threshold. Only the 3-way AND keeps
  this from firing, and it is doing so by luck of threshold placement, not by modeling the difference.
- **Lying down deliberately** — ends in the **exact same terminal pose as a fall** (`w/h > 1.2`,
  low centroid, stationary). No amount of terminal-pose analysis can separate the two. The only
  separators are (1) descent speed and (2) a controlled multi-phase descent vs. a single ballistic
  drop. Descent speed is currently the *only* separator and it is corrupted — see D1.

### D. Code defects blocking the above

1. **Velocity is inflated ~2× and smeared** — `:200-205`:
   ```cpp
   p.last_y = p.y;          // p.y is still the PREVIOUS frame's y, not box.y
   p.last_ratio = p.ratio;  // same one-frame lag
   p.last_time = now;
   p.x = box.x; p.y = box.y; ...   // only updated afterwards
   ```
   From the third matched frame onward the numerator spans **two** frames of displacement while
   `delta_time` spans **one**. The first matched frame is correct (creation seeds `last_y == y`),
   so the error appears only after a track settles — which is exactly when falls happen.
   **Every threshold currently in the file was tuned against this doubled value.** Fix this before
   any tuning work, or the sweep is meaningless.

2. **No sustained/confirmation window** — `SUSTAINED_FALL_MS` (`:27`) is declared and never
   referenced. A fall is declared on a **single frame**. This is precisely the mechanism that would
   separate crouch/sit (transient, recovers) from a fall (terminal pose held). Its absence is the
   single biggest gap for the "actions to avoid" half of the problem.

3. **The alert blocks the detection loop for 15 s** — `TriggerFallAlert()` is called synchronously
   from `ProcessDetectionLine` (`:196`) and ends in `vTaskDelay(pdMS_TO_TICKS(15000))` (`:115`).
   That delay runs **on DetectionTask**, so polling and UART draining stop for 15 s and the 2 KB RX
   ring overflows. Practical consequence for this work: **every captured `FDLOG` stream has a 15 s
   hole immediately after each trigger**, so the post-fall pose — the data needed to build a
   confirmation window — is never recorded.

4. **Association can double-claim and swap IDs** — `:163-177` is greedy per-box with no "already
   claimed" marking and no reciprocal-best check. Two boxes in one frame can match the same track.
   With more than one person in frame, ID swaps produce large phantom `velocity_y` and
   `ratio_velocity` spikes — a false-positive generator that grows with occupancy. `kMaxTrackedPeople`
   was raised to 15 (commit `e66e267`), which raises the collision surface, not lowers it.

5. **Confidence is parsed and discarded** — `score` goes into `RawBox` (`:138, 149`) and is never
   thresholded. Low-confidence jittery boxes feed the tracker at full weight. Compare
   `sscma_camera.cc:111-122`, which gates on `detect_threshold`.

6. **Thresholds are in absolute pixels, so they are distance-dependent** — `VELOCITY_Y_THRESHOLD`
   is 40 px/s regardless of how far the person is from the camera. A near person sitting produces
   more px/s than a far person falling. Normalizing by box height (units of *heights per second*)
   makes one threshold valid across the room; without it there is no single correct value.

7. **Replay is non-deterministic** — `ProcessDetectionLine` reads wall-clock via `xTaskGetTickCount()`
   at `:161` instead of taking the frame timestamp as a parameter. The timestamp must be injectable
   before `tools/fall_replay/` can reproduce a capture byte-for-byte.

8. **Alarm semantics** — the emergency plays `OGG_SUCCESS`, a success chime (`:56`). There is no
   siren asset in `main/assets/common/`.

9. **Minor** — when all 15 slots are full a box is silently dropped (`:209-224`); the destructor
   never stops the task or uninstalls the UART driver (`:333-335`, benign because the instance is
   `static`); `alert_active_` is global rather than per-track, and the MCP payload carries no track ID.

### E. Capture gaps to close before threshold work
`FDLOG` gives raw boxes + timestamps + a loss flag, which is the right foundation. What is missing:
the 15 s post-trigger blackout (D3), a ground-truth label channel (which of the 7 scenarios a capture
contains), and any consumer at all — `tools/fall_replay/` is empty.
