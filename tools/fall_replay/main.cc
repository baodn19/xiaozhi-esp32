// Host replay harness for fall_posture.{h,cc}. Reads captured FDLOG lines (straight from an
// `idf.py monitor` capture, or a bare FDLOG stream) on stdin and replays them through the exact
// same PostureTracker used on-device, printing FDEVT/alert/suppress lines to stdout so the two
// can be diffed. See plan/fall_detection_state_machine.md "Verification".
//
// Usage:
//   ./fall_replay < capture.log
//   idf.py -p /dev/ttyACM0 monitor | tee capture.log | ./fall_replay

#include "fall_posture.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Same box-line grammar as FallDetectionController::ProcessDetectionLine -- duplicated here
// rather than shared because the ESP-side parser lives next to UART/board code that does not
// build on the host.
bool ParseBoxes(const char* json, std::vector<BoxObservation>* out) {
    const char* ptr = strstr(json, "\"boxes\"");
    if (!ptr) return false;
    ptr = strchr(ptr, '[');
    if (!ptr) return false;
    ptr++;

    while ((ptr = strchr(ptr, '[')) != nullptr) {
        int x = 0, y = 0, w = 0, h = 0, score = 0, target = 0;
        if (sscanf(ptr, "[%d,%d,%d,%d,%d,%d]", &x, &y, &w, &h, &score, &target) == 6 ||
            sscanf(ptr, "[%d, %d, %d, %d, %d, %d]", &x, &y, &w, &h, &score, &target) == 6) {
            if (target == 0 && w > 0 && h > 0 && score >= 25) {
                // SSCMA reports (x, y) as the box CENTER, not the top-left corner -- see the
                // matching conversion (and its rationale) in FallDetectionController::ProcessDetectionLine.
                out->push_back(BoxObservation{static_cast<float>(x) - static_cast<float>(w) / 2.0f,
                                               static_cast<float>(y) - static_cast<float>(h) / 2.0f,
                                               static_cast<float>(w), static_cast<float>(h), score});
            }
        }
        const char* close_bracket = strchr(ptr, ']');
        ptr = close_bracket ? close_bracket + 1 : ptr + 1;
    }
    return true;
}

// Finds "FDLOG,<seq>,<ms>,<overflow>,<json...>" anywhere in a line -- tolerates an
// `idf.py monitor` prefix such as "I (12345) FallDetection: " -- and extracts <ms> and <json>.
bool ParseFdlogLine(const std::string& line, uint32_t* ms_out, std::string* json_out) {
    size_t pos = line.find("FDLOG,");
    if (pos == std::string::npos) return false;
    const char* p = line.c_str() + pos + strlen("FDLOG,");

    char* end = nullptr;
    strtoul(p, &end, 10);  // seq, unused
    if (end == p || *end != ',') return false;
    p = end + 1;

    unsigned long ms = strtoul(p, &end, 10);
    if (end == p || *end != ',') return false;
    p = end + 1;

    strtoul(p, &end, 10);  // overflow flag, unused
    if (end == p || *end != ',') return false;
    p = end + 1;

    *ms_out = static_cast<uint32_t>(ms);
    *json_out = p;
    return true;
}

void OnFallAlert(void*, uint32_t now_ms, const TrackedPerson& t, const char* reason) {
    printf("FDEVT,%d,ALERT,%s,%u,vy=%.2f,vh=%.2f,drop=%.2f,r=%.2f\n", t.id, reason, now_ms,
           t.last_features.v_cy_n, t.last_features.v_h_n, t.last_features.drop_n, t.last_features.r);
}

void OnEventLog(void*, uint32_t now_ms, const TrackedPerson& t, PostureState from, PostureState to) {
    printf("FDEVT,%d,%s,%s,%u,vy=%.2f,vh=%.2f,drop=%.2f,r=%.2f\n", t.id, PostureStateName(from),
           PostureStateName(to), now_ms, t.last_features.v_cy_n, t.last_features.v_h_n,
           t.last_features.drop_n, t.last_features.r);
}

void OnSuppressLog(void*, uint32_t now_ms, const TrackedPerson& t, const char* reason) {
    printf("FDEVT,%d,SUPPRESS,%s,%u\n", t.id, reason, now_ms);
}

// --trace: dump every active track's state and derived features after each frame. Host-only
// diagnostic -- the device has no equivalent, so this never affects FDEVT reproduction.
void TraceTracks(const PostureTracker& tracker, uint32_t now_ms) {
    for (int i = 0; i < PostureTracker::kMaxTrackedPeople; i++) {
        const TrackedPerson& t = tracker.TrackAt(i);
        if (!t.active) continue;
        const Features& f = t.last_features;
        printf("TRACE,%u,id=%d,%s%s,h=%.0f,h_ref=%.0f,h_n=%.2f,r=%.2f,drop=%.2f,"
               "vcy=%.2f,vh=%.2f,vbot=%.2f,up=%d,gnd=%d,low=%d,desc=%d,"
               "upcnt=%d,gndcnt=%d,rej=%d,peakv=%.2f,pause=%d,miss=%d,bot_ok=%d\n",
               now_ms, t.id, PostureStateName(t.state), t.is_zombie ? "(Z)" : "",
               t.h, t.h_ref, f.h_n, f.r, f.drop_n, f.v_cy_n, f.v_h_n, f.v_bot_n,
               f.is_upright, f.is_ground, f.is_low, f.descent_sig,
               t.upright_confirm_count, t.ground_confirm_count, t.baseline_reject_streak,
               t.peak_norm_vel, t.pause_count, t.frames_until_untracked, f.bottom_valid);
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool trace = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--trace") == 0) trace = true;
    }

    PostureTracker tracker;
    tracker.SetFallAlertCallback(&OnFallAlert);
    tracker.SetEventLogCallback(&OnEventLog);
    tracker.SetSuppressLogCallback(&OnSuppressLog);

    std::string line;
    uint32_t last_ms = 0;
    uint64_t frames = 0;
    while (std::getline(std::cin, line)) {
        uint32_t ms;
        std::string json;
        if (!ParseFdlogLine(line, &ms, &json)) continue;

        std::vector<BoxObservation> boxes;
        // Lines with no "boxes" key (AT-command echoes, model-config echoes) are not detection
        // frames -- ProcessDetectionLine() on-device skips them entirely (fall_detection.cc:157)
        // rather than treating them as a frame with zero boxes, so tracks aren't penalized with a
        // spurious missing-frame tick for a line that was never a detector inference.
        if (!ParseBoxes(json.c_str(), &boxes)) continue;
        tracker.Update(boxes.data(), boxes.size(), ms);
        if (trace) TraceTracks(tracker, ms);
        last_ms = ms;
        frames++;
    }

    fprintf(stderr, "fall_replay: %llu frames replayed, last t=%u ms\n",
            static_cast<unsigned long long>(frames), last_ms);
    return 0;
}
