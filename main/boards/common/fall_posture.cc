#include "fall_posture.h"

#include <algorithm>
#include <cmath>

namespace {

// Least-squares slope of v(t) over the last n (t_ms, v) samples, units of v per ms. Recovers most
// of the fall/crouch margin that a single-frame difference loses to quantization at 192x192 --
// see plan/fall_detection_state_machine.md "Frame geometry".
float LeastSquaresSlope(const float* t_ms, const float* v, int n) {
    if (n < 2) return 0.0f;

    float t_mean = 0, v_mean = 0;
    for (int i = 0; i < n; i++) {
        t_mean += t_ms[i];
        v_mean += v[i];
    }
    t_mean /= n;
    v_mean /= n;

    float num = 0, den = 0;
    for (int i = 0; i < n; i++) {
        float dt = t_ms[i] - t_mean;
        num += dt * (v[i] - v_mean);
        den += dt * dt;
    }
    if (den < 1e-6f) return 0.0f;
    return num / den;
}

}  // namespace

const char* PostureStateName(PostureState state) {
    switch (state) {
        case PostureState::kInit: return "Init";
        case PostureState::kUpright: return "Upright";
        case PostureState::kDescending: return "Descending";
        case PostureState::kLowTransient: return "LowTransient";
        case PostureState::kGroundUnconfirmed: return "GroundUnconfirmed";
        case PostureState::kFallConfirmed: return "FallConfirmed";
        case PostureState::kLyingBenign: return "LyingBenign";
    }
    return "Unknown";
}

PostureTracker::PostureTracker(const Tuning& tuning) : tuning_(tuning) {}

void PostureTracker::SetFallAlertCallback(FallAlertCallback cb, void* ctx) {
    alert_cb_ = cb;
    alert_ctx_ = ctx;
}

void PostureTracker::SetEventLogCallback(EventLogCallback cb, void* ctx) {
    event_cb_ = cb;
    event_ctx_ = ctx;
}

void PostureTracker::SetSuppressLogCallback(SuppressLogCallback cb, void* ctx) {
    suppress_cb_ = cb;
    suppress_ctx_ = ctx;
}

void PostureTracker::FireAlert(TrackedPerson& t, const char* reason, uint32_t now_ms) {
    if (t.alerted) return;
    t.alerted = true;
    if (alert_cb_) alert_cb_(alert_ctx_, now_ms, t, reason);
}

void PostureTracker::LogSuppressed(const TrackedPerson& t, const char* reason, uint32_t now_ms) {
    if (suppress_cb_) suppress_cb_(suppress_ctx_, now_ms, t, reason);
}

void PostureTracker::PushHistory(TrackedPerson& t, uint32_t now_ms, float cy, float bottom, float h) {
    constexpr int N = TrackedPerson::kVelWindow;
    if (t.hist_count < N) {
        int i = t.hist_count++;
        t.hist_t_ms[i] = static_cast<float>(now_ms);
        t.hist_cy[i] = cy;
        t.hist_bottom[i] = bottom;
        t.hist_h[i] = h;
        return;
    }
    for (int i = 1; i < N; i++) {
        t.hist_t_ms[i - 1] = t.hist_t_ms[i];
        t.hist_cy[i - 1] = t.hist_cy[i];
        t.hist_bottom[i - 1] = t.hist_bottom[i];
        t.hist_h[i - 1] = t.hist_h[i];
    }
    t.hist_t_ms[N - 1] = static_cast<float>(now_ms);
    t.hist_cy[N - 1] = cy;
    t.hist_bottom[N - 1] = bottom;
    t.hist_h[N - 1] = h;
}

void PostureTracker::CreateTrack(const BoxObservation& box, uint32_t now_ms) {
    for (auto& t : tracks_) {
        if (t.active) continue;

        t = TrackedPerson{};
        t.id = next_track_id_++;
        t.active = true;
        t.x = box.x;
        t.y = box.y;
        t.w = box.w;
        t.h = box.h;
        t.cy = box.y + box.h / 2.0f;
        t.h_ref = box.h;   // seed candidate baseline; confirmed at T1
        t.cy_ref = t.cy;
        t.last_seen_ms = now_ms;
        t.state = PostureState::kInit;
        t.state_enter_ms = now_ms;
        PushHistory(t, now_ms, t.cy, box.y + box.h, box.h);
        return;
    }
    // All slots full: box silently dropped. Documented, unchanged limitation from the original
    // single-frame tracker (kMaxTrackedPeople was already sized generously at 15).
}

void PostureTracker::KillTrack(TrackedPerson& t) {
    t = TrackedPerson{};
}

void PostureTracker::Associate(const BoxObservation* boxes, size_t count, uint32_t /*now_ms*/,
                                int* assigned_track_out) {
    for (size_t i = 0; i < count; i++) assigned_track_out[i] = -1;

    struct Candidate {
        float dist;
        int box_idx;
        int track_idx;
    };
    Candidate candidates[kMaxTrackedPeople * kMaxTrackedPeople];
    int n_candidates = 0;

    for (size_t bi = 0; bi < count; bi++) {
        float bcx = boxes[bi].x + boxes[bi].w / 2.0f;
        float bcy = boxes[bi].y + boxes[bi].h / 2.0f;

        for (int ti = 0; ti < kMaxTrackedPeople; ti++) {
            const TrackedPerson& t = tracks_[ti];
            if (!t.active) continue;

            float tcx = t.x + t.w / 2.0f;
            float tcy = t.y + t.h / 2.0f;
            float dx = bcx - tcx, dy = bcy - tcy;
            float dist = std::sqrt(dx * dx + dy * dy);

            // Fixed pixel gate before a baseline exists; once h_ref is known the gate scales with
            // distance instead of being uniformly generous. See design doc "Association".
            float gate = (t.h_ref > 0.0f) ? tuning_.assoc_gate_ratio * t.h_ref : tuning_.assoc_gate_px;
            if (dist < gate) {
                candidates[n_candidates++] = {dist, static_cast<int>(bi), ti};
            }
        }
    }

    std::sort(candidates, candidates + n_candidates,
              [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });

    bool track_claimed[kMaxTrackedPeople] = {};
    for (int i = 0; i < n_candidates; i++) {
        const Candidate& c = candidates[i];
        if (assigned_track_out[c.box_idx] != -1) continue;  // box already claimed a closer track
        if (track_claimed[c.track_idx]) continue;           // track already claimed by a closer box
        assigned_track_out[c.box_idx] = c.track_idx;
        track_claimed[c.track_idx] = true;
    }
}

void PostureTracker::UpdateTrackObservation(TrackedPerson& t, const BoxObservation& box, uint32_t now_ms) {
    float raw_cy = box.y + box.h / 2.0f;
    float raw_bottom = box.y + box.h;

    t.x = box.x;
    t.y = box.y;
    t.w = box.w;
    t.h = box.h;

    // cy = y + h/2 compounds quantization noise from both y and h. The design doc suggests an
    // EMA on cy before differencing; that is deliberately not done here, because it would recur
    // with the windowed least-squares velocity fit below and bias the slope during any transient
    // (verified against the velocity-regression test) -- the regression window is where the
    // smoothing happens instead.
    t.cy = raw_cy;

    PushHistory(t, now_ms, t.cy, raw_bottom, box.h);

    t.last_seen_ms = now_ms;
    t.frames_until_untracked = 0;
    t.is_zombie = false;
}

void PostureTracker::UpdateBaseline(TrackedPerson& t, uint32_t /*now_ms*/) {
    // Baseline only learns while kInit (building toward T1) or kUpright; frozen everywhere else
    // so a fall in progress can't drag its own reference height down mid-episode.
    if (t.state != PostureState::kInit && t.state != PostureState::kUpright) return;
    if (t.h_ref <= 0.0f) {
        t.h_ref = t.h;
        t.cy_ref = t.cy;
        return;
    }

    // Reject h samples that deviate too far from the running baseline -- partial occlusion
    // behind furniture truncates h and would otherwise drag h_ref down to mimic an axial collapse.
    float dev = std::fabs(t.h - t.h_ref) / t.h_ref;
    if (dev > tuning_.baseline_reject_frac) return;

    t.h_ref = tuning_.baseline_ema_alpha * t.h + (1.0f - tuning_.baseline_ema_alpha) * t.h_ref;
    t.cy_ref = tuning_.baseline_ema_alpha * t.cy + (1.0f - tuning_.baseline_ema_alpha) * t.cy_ref;
}

Features PostureTracker::ComputeFeatures(const TrackedPerson& t) const {
    Features f;
    if (t.h_ref <= 0.0f) return f;  // no baseline candidate yet

    float slope_cy = LeastSquaresSlope(t.hist_t_ms, t.hist_cy, t.hist_count);
    float slope_bot = LeastSquaresSlope(t.hist_t_ms, t.hist_bottom, t.hist_count);
    float slope_h = LeastSquaresSlope(t.hist_t_ms, t.hist_h, t.hist_count);

    f.h_n = t.h / t.h_ref;
    f.r = (t.h > 0.0f) ? t.w / t.h : 0.0f;
    f.v_cy_n = slope_cy * 1000.0f / t.h_ref;
    f.v_bot_n = slope_bot * 1000.0f / t.h_ref;
    f.v_h_n = slope_h * 1000.0f / t.h_ref;
    f.drop_n = (t.cy - t.cy_ref) / t.h_ref;
    f.bottom_valid = (t.y + t.h) < (tuning_.frame_h - tuning_.edge_margin);

    f.is_upright = f.h_n > tuning_.upright_h_n_min && f.r < tuning_.upright_max_ratio &&
                   f.drop_n < tuning_.upright_drop_n_max;

    // OR, not AND: an axial (camera-axis) fall foreshortens -- height collapses while width stays
    // roughly constant -- and never satisfies "wider than tall". The short branch catches it.
    f.is_ground = f.drop_n > tuning_.ground_drop_n_min &&
                  (f.r > tuning_.ground_ratio_min || f.h_n < tuning_.ground_h_n_max);

    f.is_low = f.drop_n > tuning_.low_drop_n_min && !f.is_ground;

    // The two sign guards are load-bearing: walking toward the camera grows height while the
    // centroid descends; walking away collapses height while the top edge descends. Both look
    // like falls without them.
    f.descent_sig = (f.v_cy_n > tuning_.descent_v_cy_n_min || f.v_h_n < tuning_.descent_v_h_n_max) &&
                     f.v_h_n <= tuning_.descent_v_h_n_grow_max && f.v_bot_n > tuning_.descent_v_bot_n_min;

    return f;
}

bool PostureTracker::ComputeBallistic(const TrackedPerson& t, uint32_t as_of_ms) const {
    uint32_t duration = as_of_ms - t.descent_start_ms;
    return t.peak_norm_vel > tuning_.ballistic_peak_norm_vel_min && t.pause_count == 0 &&
           duration < tuning_.ballistic_max_duration_ms;
}

void PostureTracker::EnterState(TrackedPerson& t, PostureState new_state, uint32_t now_ms) {
    PostureState old = t.state;
    if (event_cb_) event_cb_(event_ctx_, now_ms, t, old, new_state);

    t.state = new_state;
    t.state_enter_ms = now_ms;
    t.upright_streak = 0;

    switch (new_state) {
        case PostureState::kInit:
            break;
        case PostureState::kUpright:
            t.pause_count = 0;
            t.stall_frames = 0;
            t.peak_norm_vel = 0;
            t.low_confirm_count = 0;
            t.ground_confirm_count = 0;
            break;
        case PostureState::kDescending:
            t.descent_start_ms = now_ms;
            t.peak_norm_vel = 0;
            t.stall_frames = 0;
            t.pause_count = 0;
            t.low_confirm_count = 0;
            t.ground_confirm_count = 0;
            break;
        case PostureState::kLowTransient:
            break;
        case PostureState::kGroundUnconfirmed:
            t.ground_enter_ms = now_ms;
            t.ground_frames = 0;
            t.window_frames = 0;
            break;
        case PostureState::kFallConfirmed:
            break;
        case PostureState::kLyingBenign:
            t.lying_since_ms = now_ms;
            break;
    }
}

void PostureTracker::UpdatePosture(TrackedPerson& t, const Features& f, uint32_t now_ms) {
    switch (t.state) {
        case PostureState::kInit: {
            // T1: baseline accepted once the box has held a stable, upright shape for
            // upright_confirm_frames consecutive frames. Until this fires the track never alarms.
            float r = (t.h > 0.0f) ? t.w / t.h : 0.0f;
            bool ratio_ok = r < tuning_.upright_max_ratio;
            bool height_stable_ok =
                t.h_ref > 0.0f && std::fabs(t.h - t.h_ref) <= tuning_.upright_h_tolerance * t.h_ref;
            bool height_frac_ok = t.h > tuning_.upright_min_h_frac_of_frame * tuning_.frame_h;

            if (ratio_ok && height_stable_ok && height_frac_ok) {
                t.upright_confirm_count++;
            } else {
                t.upright_confirm_count = 0;
            }

            if (t.upright_confirm_count >= tuning_.upright_confirm_frames) {
                t.has_baseline = true;
                EnterState(t, PostureState::kUpright, now_ms);
            }
            break;
        }

        case PostureState::kUpright: {
            if (f.descent_sig) {  // T2
                EnterState(t, PostureState::kDescending, now_ms);
            }
            break;
        }

        case PostureState::kDescending: {
            // T3: track the peak descent speed and count distinct mid-fall pauses (a "stall run"
            // reaching 2 frames counts once, so a long pause doesn't inflate pause_count).
            float norm_vel = std::max(f.v_cy_n, -f.v_h_n);
            t.peak_norm_vel = std::max(t.peak_norm_vel, norm_vel);
            bool stalled = std::fabs(f.v_cy_n) < tuning_.stall_v_n_max;
            if (stalled) {
                t.stall_frames++;
                if (t.stall_frames == 2) t.pause_count++;
            } else {
                t.stall_frames = 0;
            }

            if (f.is_upright) {
                t.upright_streak++;
            } else {
                t.upright_streak = 0;
            }
            if (t.upright_streak >= tuning_.recovery_upright_streak) {  // T4
                EnterState(t, PostureState::kUpright, now_ms);
                break;
            }

            if (f.is_low && std::fabs(f.v_cy_n) < tuning_.low_transient_v_cy_n_max) {
                t.low_confirm_count++;
            } else {
                t.low_confirm_count = 0;
            }
            if (t.low_confirm_count >= tuning_.low_transient_confirm_frames) {  // T5
                EnterState(t, PostureState::kLowTransient, now_ms);
                break;
            }

            if (f.is_ground) {
                t.ground_confirm_count++;
            } else {
                t.ground_confirm_count = 0;
            }
            if (t.ground_confirm_count >= tuning_.ground_confirm_frames) {  // T6
                t.was_ballistic = ComputeBallistic(t, now_ms);
                EnterState(t, PostureState::kGroundUnconfirmed, now_ms);
                break;
            }

            if (now_ms - t.descent_start_ms > tuning_.descending_forced_exit_ms) {  // T7
                if (f.is_ground) {
                    t.was_ballistic = ComputeBallistic(t, now_ms);
                    EnterState(t, PostureState::kGroundUnconfirmed, now_ms);
                } else if (f.is_low) {
                    EnterState(t, PostureState::kLowTransient, now_ms);
                } else {
                    EnterState(t, PostureState::kUpright, now_ms);
                }
            }
            break;
        }

        case PostureState::kLowTransient: {
            if (f.is_upright) {
                t.upright_streak++;
            } else {
                t.upright_streak = 0;
            }
            if (t.upright_streak >= tuning_.recovery_upright_streak) {  // T8
                EnterState(t, PostureState::kUpright, now_ms);
                break;
            }
            if (f.descent_sig) {  // T9: fall-from-seated re-enters the measured descent path
                EnterState(t, PostureState::kDescending, now_ms);
            }
            break;
        }

        case PostureState::kGroundUnconfirmed: {
            t.window_frames++;
            if (f.is_ground) t.ground_frames++;

            if (f.is_upright) {
                t.upright_streak++;
            } else {
                t.upright_streak = 0;
            }

            bool expired = (now_ms - t.ground_enter_ms) >= tuning_.ground_confirm_ms;

            if (!expired && t.upright_streak >= tuning_.recovery_upright_streak) {  // T10: crouch rejection
                EnterState(t, PostureState::kUpright, now_ms);
                break;
            }

            if (expired) {
                float duty = t.window_frames > 0 ? static_cast<float>(t.ground_frames) / t.window_frames : 0.0f;
                bool sustained_ballistic_ground = (duty >= tuning_.ground_frames_duty_min) && t.was_ballistic;

                if (sustained_ballistic_ground) {  // T11
                    bool classify_ok = t.h_ref >= tuning_.min_classify_h_ref;
                    EnterState(t, PostureState::kFallConfirmed, now_ms);
                    if (classify_ok) {
                        FireAlert(t, "ground_confirmed", now_ms);
                    } else {
                        LogSuppressed(t, "min_h_ref", now_ms);
                    }
                } else {
                    // Everything else at expiry -- low duty (flickery ground pose) or a controlled,
                    // non-ballistic descent -- sinks to the benign lying state. T14 is the backstop
                    // for a slow syncopal collapse that never looked ballistic.
                    EnterState(t, PostureState::kLyingBenign, now_ms);
                    LogSuppressed(t, "controlled_descent", now_ms);
                }
            }
            break;
        }

        case PostureState::kLyingBenign: {
            if (f.is_upright) {
                t.upright_streak++;
            } else {
                t.upright_streak = 0;
            }
            if (t.upright_streak >= tuning_.recovery_upright_streak) {  // T13
                EnterState(t, PostureState::kUpright, now_ms);
                break;
            }

            // Immobility restarts the long-lie timer -- a person shifting position is not the
            // "immobile on the floor" case T14 exists to catch.
            bool moving = std::fabs(f.v_cy_n) >= tuning_.lying_immobile_v_n_max ||
                          std::fabs(f.v_h_n) >= tuning_.lying_immobile_v_n_max;
            if (moving) t.lying_since_ms = now_ms;

            if (now_ms - t.lying_since_ms > tuning_.lying_benign_escalate_ms) {  // T14
                bool classify_ok = t.h_ref >= tuning_.min_classify_h_ref;
                EnterState(t, PostureState::kFallConfirmed, now_ms);
                if (classify_ok) {
                    FireAlert(t, "long_lie", now_ms);
                } else {
                    LogSuppressed(t, "min_h_ref", now_ms);
                }
            }
            break;
        }

        case PostureState::kFallConfirmed: {
            if (f.is_upright) {
                t.upright_streak++;
            } else {
                t.upright_streak = 0;
            }
            if (t.upright_streak >= tuning_.fall_confirmed_recovery_streak) {  // T15
                t.alerted = false;
                EnterState(t, PostureState::kUpright, now_ms);
            }
            break;
        }
    }
}

void PostureTracker::ResolveZombie(TrackedPerson& t, uint32_t now_ms) {
    bool ballistic = (t.zombie_from_state == PostureState::kGroundUnconfirmed)
                          ? t.was_ballistic
                          : ComputeBallistic(t, t.last_seen_ms);

    if (ballistic) {
        // "went down fast, then the detector lost them" is a fall, not a non-event -- person
        // detectors collapse in confidence once a body goes prone, so dropout right after a
        // ballistic descent is itself the confirmation signal.
        if (event_cb_) event_cb_(event_ctx_, now_ms, t, t.state, PostureState::kFallConfirmed);
        bool classify_ok = t.h_ref >= tuning_.min_classify_h_ref;
        if (classify_ok) {
            FireAlert(t, "zombie_dropout", now_ms);
        } else {
            LogSuppressed(t, "min_h_ref", now_ms);
        }
    } else {
        if (event_cb_) event_cb_(event_ctx_, now_ms, t, t.state, PostureState::kInit);
    }
    KillTrack(t);
}

void PostureTracker::Update(const BoxObservation* boxes, size_t count, uint32_t now_ms) {
    size_t consider = count > static_cast<size_t>(kMaxTrackedPeople) ? static_cast<size_t>(kMaxTrackedPeople) : count;

    int assigned[kMaxTrackedPeople];
    Associate(boxes, consider, now_ms, assigned);

    for (size_t i = 0; i < consider; i++) {
        int ti = assigned[i];
        if (ti >= 0) {
            TrackedPerson& t = tracks_[ti];
            t.is_zombie = false;
            UpdateTrackObservation(t, boxes[i], now_ms);
            UpdateBaseline(t, now_ms);
            Features f = ComputeFeatures(t);
            t.last_features = f;
            UpdatePosture(t, f, now_ms);
        } else {
            CreateTrack(boxes[i], now_ms);
        }
    }

    for (int ti = 0; ti < kMaxTrackedPeople; ti++) {
        TrackedPerson& t = tracks_[ti];
        if (!t.active) continue;
        if (t.last_seen_ms == now_ms) continue;  // matched this frame, above

        if (t.is_zombie) {
            if (now_ms - t.zombie_started_ms >= tuning_.zombie_expiry_ms) {
                ResolveZombie(t, now_ms);
            }
            continue;
        }

        t.frames_until_untracked++;
        if (t.frames_until_untracked > tuning_.max_missing_frames) {
            if (t.state == PostureState::kDescending || t.state == PostureState::kGroundUnconfirmed) {
                // Detector confidence collapses on prone bodies -- hold the track as a zombie
                // instead of deleting the in-progress confirmation evidence.
                t.is_zombie = true;
                t.zombie_started_ms = now_ms;
                t.zombie_from_state = t.state;
            } else {
                KillTrack(t);
            }
        }
    }
}
