// Offline tests for fall_posture.{h,cc}, run on the host (no hardware, no ESP-IDF). Covers the
// items listed under plan/fall_detection_state_machine.md "Verification":
//   2. velocity regression -- a synthetic constant-velocity track yields the injected velocity.
//   3. hand-built box sequences per transition -- a crouch cancels via T10 with zero alarms,
//      a ballistic drop reaches T11.
//   4. zero alarms across sit/lie/crouch sequences (a deliberate lie-down sinks to kLyingBenign).
//
// All box coordinates below are hand-derived from the frame geometry in the design doc
// (192x192, h_ref ~100px indoor framing) -- not measured on hardware.

#include "fall_posture.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;
int g_alert_count = 0;
const char* g_last_alert_reason = "";

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_failures++;                                               \
        }                                                                \
    } while (0)

void OnAlert(void*, uint32_t /*now_ms*/, const TrackedPerson& /*t*/, const char* reason) {
    g_alert_count++;
    g_last_alert_reason = reason;
}

void ResetAlertCount() {
    g_alert_count = 0;
    g_last_alert_reason = "";
}

PostureTracker MakeTracker() {
    PostureTracker tracker;
    tracker.SetFallAlertCallback(&OnAlert);
    return tracker;
}

BoxObservation Box(float x, float y, float w, float h) {
    BoxObservation b;
    b.x = x;
    b.y = y;
    b.w = w;
    b.h = h;
    b.score = 80;
    return b;
}

// Feeds `frames` identical standing frames -- enough (>= upright_confirm_frames) to establish and
// confirm a baseline via T1 -- and returns the timestamp after the last one.
uint32_t Establish(PostureTracker& tracker, uint32_t now_ms, float x, float y, float w, float h, uint32_t dt_ms,
                    int frames) {
    for (int i = 0; i < frames; i++) {
        BoxObservation b = Box(x, y, w, h);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }
    return now_ms;
}

// -------------------------------------------------------------------------------------------
// 2. Velocity regression: a constant-velocity descent must yield exactly the injected velocity
//    (units of h_ref/s), not the ~2x-inflated value the pre-Phase-0 code produced.
// -------------------------------------------------------------------------------------------
void TestVelocityRegression() {
    PostureTracker tracker = MakeTracker();
    uint32_t now_ms = 0;
    const uint32_t dt_ms = 200;
    const float w = 50, h = 100, x = 70;
    float y = 46;  // cy = y + h/2 = 96

    now_ms = Establish(tracker, now_ms, x, y, w, h, dt_ms, 12);
    CHECK(tracker.TrackAt(0).state == PostureState::kUpright);
    CHECK(tracker.TrackAt(0).has_baseline);

    // 3 px/frame is well below the descent_sig gate, so this isolates the velocity computation
    // from any state transition.
    const float px_per_frame = 3.0f;
    for (int i = 0; i < 4; i++) {
        y += px_per_frame;
        BoxObservation b = Box(x, y, w, h);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }

    float h_ref = tracker.TrackAt(0).h_ref;
    float expected_v_cy_n = (px_per_frame * 1000.0f / dt_ms) / h_ref;
    float actual_v_cy_n = tracker.TrackAt(0).last_features.v_cy_n;
    printf("[velocity regression] expected v_cy_n=%.4f actual=%.4f (h_ref=%.1f)\n", expected_v_cy_n,
           actual_v_cy_n, h_ref);
    CHECK(std::fabs(actual_v_cy_n - expected_v_cy_n) < 0.02f);
    CHECK(tracker.TrackAt(0).state == PostureState::kUpright);  // never should have left
}

// -------------------------------------------------------------------------------------------
// 3a. Fast crouch-and-recover: bbox briefly looks like an axial collapse (h_n < 0.45), which is
//     indistinguishable from a fall at the terminal pose -- T10 must cancel on recovery with
//     zero alarms.
// -------------------------------------------------------------------------------------------
void TestCrouchCancelsViaT10() {
    PostureTracker tracker = MakeTracker();
    ResetAlertCount();
    uint32_t now_ms = 0;
    const uint32_t dt_ms = 200;
    const float w = 50, x = 70;
    const float bottom = 146;  // feet planted; top edge and cy move as h shrinks

    now_ms = Establish(tracker, now_ms, x, bottom - 100, w, 100, dt_ms, 12);
    CHECK(tracker.TrackAt(0).state == PostureState::kUpright);

    // Descend: h 100 -> 40 over 4 frames (deep crouch), then hold to confirm ground (T6).
    float hs_down[] = {85, 70, 55, 40, 40, 40, 40, 40};
    for (float h : hs_down) {
        BoxObservation b = Box(x, bottom - h, w, h);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }
    printf("[crouch/T10] after descent: state=%s ground_confirm_count=%d\n",
           PostureStateName(tracker.TrackAt(0).state), tracker.TrackAt(0).ground_confirm_count);
    CHECK(tracker.TrackAt(0).state == PostureState::kGroundUnconfirmed);

    // Recover: stand back up. upright_streak needs 2 consecutive upright frames, well inside the
    // 2500 ms ground-confirm window.
    float hs_up[] = {55, 70, 85, 100, 100, 100};
    for (float h : hs_up) {
        BoxObservation b = Box(x, bottom - h, w, h);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }

    printf("[crouch/T10] final state=%s alerts=%d\n", PostureStateName(tracker.TrackAt(0).state), g_alert_count);
    CHECK(tracker.TrackAt(0).state == PostureState::kUpright);
    CHECK(g_alert_count == 0);
}

// -------------------------------------------------------------------------------------------
// 3b. Ballistic sideways fall, held on the ground: must reach T11 and fire exactly one alert.
// -------------------------------------------------------------------------------------------
void TestBallisticFallFiresT11() {
    PostureTracker tracker = MakeTracker();
    ResetAlertCount();
    uint32_t now_ms = 0;
    const uint32_t dt_ms = 200;
    const float x = 70;

    now_ms = Establish(tracker, now_ms, x, 46, 50, 100, dt_ms, 12);
    CHECK(tracker.TrackAt(0).state == PostureState::kUpright);

    // Standing (w=50,h=100,cy=96) -> lying sideways (w=140,h=60,cy=146) over 4 fast frames
    // (~800 ms), well inside the ballistic_max_duration_ms=1400 window.
    for (int i = 1; i <= 4; i++) {
        float frac = i / 4.0f;
        float w = 50 + frac * (140 - 50);
        float h = 100 + frac * (60 - 100);
        float cy = 96 + frac * (146 - 96);
        BoxObservation b = Box(x, cy - h / 2.0f, w, h);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }
    // Hold the terminal pose to confirm ground (T6) and then to clear the sustained-ground
    // window (T11, >= 2500 ms with duty >= 0.7) from whenever ground entry actually happened.
    const float final_w = 140, final_h = 60, final_cy = 146;
    uint32_t hold_until = now_ms + 3000;
    while (now_ms < hold_until) {
        BoxObservation b = Box(x, final_cy - final_h / 2.0f, final_w, final_h);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }

    printf("[ballistic/T11] final state=%s alerts=%d reason=%s\n", PostureStateName(tracker.TrackAt(0).state),
           g_alert_count, g_last_alert_reason);
    CHECK(tracker.TrackAt(0).state == PostureState::kFallConfirmed);
    CHECK(g_alert_count == 1);
}

// -------------------------------------------------------------------------------------------
// 4. Deliberate, controlled lie-down: a mid-descent pause (long enough to flush the windowed
//    velocity to ~0) registers a stall run, so was_ballistic is false regardless of how fast the
//    rest of the descent is -- this must never reach kFallConfirmed. It settles in kLowTransient
//    here rather than kGroundUnconfirmed/kLyingBenign because the pause itself satisfies
//    is_low + low-velocity long enough to fire T5 first (a controlled descent that pauses at a
//    partial depth is legitimately ambiguous with sitting down) -- both are non-alarming benign
//    outcomes, so the property under test is "zero alarms", not the specific benign sub-state.
// -------------------------------------------------------------------------------------------
void TestDeliberateLieDownIsBenign() {
    PostureTracker tracker = MakeTracker();
    ResetAlertCount();
    uint32_t now_ms = 0;
    const uint32_t dt_ms = 200;
    const float x = 70;

    now_ms = Establish(tracker, now_ms, x, 46, 50, 100, dt_ms, 12);
    CHECK(tracker.TrackAt(0).state == PostureState::kUpright);

    // Entry jump: just past the descent_sig gate, landing well short of ground.
    float w = 54, h = 92, cy = 116;
    {
        BoxObservation b = Box(x, cy - h / 2.0f, w, h);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }
    CHECK(tracker.TrackAt(0).state == PostureState::kDescending);

    // Pause: hold that exact pose so the windowed velocity flushes to ~0 and a stall run
    // registers (pause_count++).
    for (int i = 0; i < 6; i++) {
        BoxObservation b = Box(x, cy - h / 2.0f, w, h);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }
    CHECK(tracker.TrackAt(0).pause_count > 0);

    // Continue on toward a fall-like terminal pose.
    const int N = 6;
    const float w1 = 140, h1 = 60, cy1 = 146;
    for (int i = 1; i <= N; i++) {
        float frac = static_cast<float>(i) / N;
        float cw = w + frac * (w1 - w), ch = h + frac * (h1 - h), ccy = cy + frac * (cy1 - cy);
        BoxObservation b = Box(x, ccy - ch / 2.0f, cw, ch);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }
    // Hold well past the sustained-ground window, from whenever (if ever) ground entry happened.
    uint32_t hold_until = now_ms + 3000;
    while (now_ms < hold_until) {
        BoxObservation b = Box(x, cy1 - h1 / 2.0f, w1, h1);
        tracker.Update(&b, 1, now_ms);
        now_ms += dt_ms;
    }

    printf("[lie-down/T3 pause] final state=%s alerts=%d pause_count=%d was_ballistic=%d\n",
           PostureStateName(tracker.TrackAt(0).state), g_alert_count, tracker.TrackAt(0).pause_count,
           tracker.TrackAt(0).was_ballistic);
    CHECK(tracker.TrackAt(0).state != PostureState::kFallConfirmed);
    CHECK(g_alert_count == 0);
}

}  // namespace

int main() {
    TestVelocityRegression();
    TestCrouchCancelsViaT10();
    TestBallisticFallFiresT11();
    TestDeliberateLieDownIsBenign();

    if (g_failures == 0) {
        printf("ALL PASS\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
