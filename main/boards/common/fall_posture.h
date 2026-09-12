#pragma once

// Posture classification for fall detection, factored out of fall_detection.cc so it has
// NO ESP-IDF, FreeRTOS or NVS includes -- the same source must compile for the host
// (tools/fall_replay/) and the target. See plan/fall_detection_state_machine.md for the
// design this implements.

#include <cstddef>
#include <cstdint>

// All tunable constants live here (not #define) so a single instance can be swept or replayed
// without touching the algorithm. Defaults are the sweep starting points from the design doc;
// none of them have been confirmed against real hardware captures yet.
struct Tuning {
    // Frame geometry (192x192 after downscale -- see design doc "Frame geometry").
    float frame_w = 192.0f;
    float frame_h = 192.0f;
    float edge_margin = 4.0f;  // px; feet within this of frame_h count as cropped

    // Association. Fixed pixel gate is the fallback before a track has a baseline; once h_ref
    // exists the gate scales with distance instead of being generous for near people only.
    float assoc_gate_px = 60.0f;
    float assoc_gate_ratio = 0.35f;  // * h_ref

    // Baseline (h_ref / cy_ref) learning.
    float baseline_ema_alpha = 0.1f;      // ~2s time constant at 5 Hz
    float baseline_reject_frac = 0.25f;   // reject h samples deviating more than this from h_ref
    float min_classify_h_ref = 80.0f;     // px (of 192); below this a track never alarms

    // T1: kInit -> kUpright.
    int upright_confirm_frames = 10;
    float upright_h_tolerance = 0.20f;
    float upright_min_h_frac_of_frame = 0.25f;

    // Feature thresholds.
    float upright_h_n_min = 0.85f;
    float upright_max_ratio = 0.60f;
    float upright_drop_n_max = 0.08f;

    float ground_drop_n_min = 0.28f;
    float ground_ratio_min = 1.10f;
    float ground_h_n_max = 0.45f;

    float low_drop_n_min = 0.10f;

    float descent_v_cy_n_min = 0.30f;
    float descent_v_h_n_max = -0.35f;
    float descent_v_h_n_grow_max = 0.05f;
    float descent_v_bot_n_min = -0.15f;

    // Transition timers / counts.
    int recovery_upright_streak = 2;
    int low_transient_confirm_frames = 2;
    float low_transient_v_cy_n_max = 0.10f;
    int ground_confirm_frames = 2;
    uint32_t descending_forced_exit_ms = 3500;
    float stall_v_n_max = 0.10f;

    uint32_t ground_confirm_ms = 2500;
    float ground_frames_duty_min = 0.70f;

    uint32_t lying_benign_escalate_ms = 120000;
    float lying_immobile_v_n_max = 0.10f;

    int fall_confirmed_recovery_streak = 3;

    // Ballisticity -- computed from the descent leg immediately preceding ground entry.
    float ballistic_peak_norm_vel_min = 0.32f;
    uint32_t ballistic_max_duration_ms = 1400;

    // Zombie handling for detector dropout on prone bodies.
    int max_missing_frames = 3;
    uint32_t zombie_expiry_ms = 2500;
};

enum class PostureState : uint8_t {
    kInit = 0,           // no valid baseline -- cannot classify, NEVER alarms
    kUpright,            // baseline valid and learning
    kDescending,         // descent episode in progress; accumulators live
    kLowTransient,       // settled at intermediate depth (sit / crouch) -- benign, cancellable
    kGroundUnconfirmed,  // ground pose seen, sustained_fall_ms timer running
    kFallConfirmed,      // alarm fired for this track
    kLyingBenign,        // ground reached via controlled descent -- benign sink
};

const char* PostureStateName(PostureState state);

// One parsed detection box for a single frame, in frame pixel coordinates.
struct BoxObservation {
    float x = 0, y = 0, w = 0, h = 0;
    int score = 0;
};

// Derived, per-frame, per-track quantities the state machine reads. All normalized quantities
// use h_ref (frozen during an episode), never the current (possibly-collapsing) h.
struct Features {
    float h_n = 0;       // h / h_ref, 1.0 when standing
    float r = 0;         // w / h
    float v_cy_n = 0;    // centroid vertical velocity, h_ref/s, positive = downward
    float v_bot_n = 0;   // bottom-edge velocity, h_ref/s
    float v_h_n = 0;     // height rate of change, h_ref/s
    float drop_n = 0;    // depth below the standing datum, in h_ref
    bool bottom_valid = true;  // feet not cropped by the frame edge

    bool is_upright = false;
    bool is_ground = false;
    bool is_low = false;
    bool descent_sig = false;
};

// Per-track state. Everything the state machine and the host replay tool need to reconstruct
// or report on a track's history lives here.
struct TrackedPerson {
    static constexpr int kVelWindow = 4;

    int id = 0;
    bool active = false;

    // Latest raw observation (pixels).
    float x = 0, y = 0, w = 0, h = 0;
    float cy = 0;  // lightly-smoothed centroid y

    uint32_t last_seen_ms = 0;
    int frames_until_untracked = 0;

    // Baseline. Updated only while state is kInit or kUpright; frozen everywhere else.
    float h_ref = 0;
    float cy_ref = 0;
    bool has_baseline = false;
    int upright_confirm_count = 0;  // T1 accumulator

    // Short history for windowed (least-squares) velocity, in chronological order.
    float hist_t_ms[kVelWindow] = {};
    float hist_cy[kVelWindow] = {};
    float hist_bottom[kVelWindow] = {};
    float hist_h[kVelWindow] = {};
    int hist_count = 0;

    // State machine.
    PostureState state = PostureState::kInit;
    uint32_t state_enter_ms = 0;
    Features last_features;

    // kDescending accumulators.
    uint32_t descent_start_ms = 0;
    float peak_norm_vel = 0;
    int stall_frames = 0;
    int pause_count = 0;
    int low_confirm_count = 0;
    int ground_confirm_count = 0;
    int upright_streak = 0;  // consecutive-upright counter, reused by every recovery transition

    // kGroundUnconfirmed accumulators.
    uint32_t ground_enter_ms = 0;
    int ground_frames = 0;   // frames since ground_enter_ms where is_ground held
    int window_frames = 0;   // frames since ground_enter_ms, observed (matched) only
    bool was_ballistic = false;

    // kLyingBenign.
    uint32_t lying_since_ms = 0;

    bool alerted = false;  // per-track latch; a global cooldown still lives in FallDetectionController

    // Zombie handling: track missing but held for re-acquisition instead of dying immediately,
    // because person detectors collapse in confidence once a body goes prone.
    bool is_zombie = false;
    uint32_t zombie_started_ms = 0;
    PostureState zombie_from_state = PostureState::kInit;
};

// Callbacks are plain function pointers, not std::function, to avoid heap allocation on target.
using FallAlertCallback = void (*)(void* ctx, uint32_t now_ms, const TrackedPerson& track, const char* reason);
using EventLogCallback = void (*)(void* ctx, uint32_t now_ms, const TrackedPerson& track, PostureState from,
                                   PostureState to);
using SuppressLogCallback = void (*)(void* ctx, uint32_t now_ms, const TrackedPerson& track, const char* reason);

// Association, baseline learning, feature computation and the 7-state posture machine described
// in plan/fall_detection_state_machine.md. Pure logic: no I/O, no threading, no allocation beyond
// the fixed track table below, so it is identical on host and target.
class PostureTracker {
public:
    explicit PostureTracker(const Tuning& tuning = Tuning());

    // Registers alert / logging sinks. Pass nullptr to disable. `ctx` is passed back verbatim.
    void SetFallAlertCallback(FallAlertCallback cb, void* ctx = nullptr);
    void SetEventLogCallback(EventLogCallback cb, void* ctx = nullptr);
    void SetSuppressLogCallback(SuppressLogCallback cb, void* ctx = nullptr);

    // One call per detector frame. `boxes` must already be filtered to the target class and the
    // confidence gate -- association and everything downstream assumes that is done.
    void Update(const BoxObservation* boxes, size_t count, uint32_t now_ms);

    const Tuning& GetTuning() const { return tuning_; }
    void SetTuning(const Tuning& tuning) { tuning_ = tuning; }

    static constexpr int kMaxTrackedPeople = 15;
    const TrackedPerson& TrackAt(int i) const { return tracks_[i]; }

private:
    TrackedPerson tracks_[kMaxTrackedPeople] = {};
    int next_track_id_ = 1;  // 0 means untracked
    Tuning tuning_;

    FallAlertCallback alert_cb_ = nullptr;
    void* alert_ctx_ = nullptr;
    EventLogCallback event_cb_ = nullptr;
    void* event_ctx_ = nullptr;
    SuppressLogCallback suppress_cb_ = nullptr;
    void* suppress_ctx_ = nullptr;

    void Associate(const BoxObservation* boxes, size_t count, uint32_t now_ms, int* assigned_track_out);
    void CreateTrack(const BoxObservation& box, uint32_t now_ms);
    void KillTrack(TrackedPerson& t);
    void ResolveZombie(TrackedPerson& t, uint32_t now_ms);

    void PushHistory(TrackedPerson& t, uint32_t now_ms, float cy, float bottom, float h);
    void UpdateTrackObservation(TrackedPerson& t, const BoxObservation& box, uint32_t now_ms);
    void UpdateBaseline(TrackedPerson& t, uint32_t now_ms);
    Features ComputeFeatures(const TrackedPerson& t) const;
    void UpdatePosture(TrackedPerson& t, const Features& f, uint32_t now_ms);
    void EnterState(TrackedPerson& t, PostureState new_state, uint32_t now_ms);
    bool ComputeBallistic(const TrackedPerson& t, uint32_t as_of_ms) const;

    void FireAlert(TrackedPerson& t, const char* reason, uint32_t now_ms);
    void LogSuppressed(const TrackedPerson& t, const char* reason, uint32_t now_ms);
};
