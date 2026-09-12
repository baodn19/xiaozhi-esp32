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
            if (target == 0 && w > 0 && h > 0 && score >= 40) {
                out->push_back(BoxObservation{static_cast<float>(x), static_cast<float>(y),
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

}  // namespace

int main() {
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
        ParseBoxes(json.c_str(), &boxes);
        tracker.Update(boxes.data(), boxes.size(), ms);
        last_ms = ms;
        frames++;
    }

    fprintf(stderr, "fall_replay: %llu frames replayed, last t=%u ms\n",
            static_cast<unsigned long long>(frames), last_ms);
    return 0;
}
