#include "fov_gaze.h"
#include "fov_core.h"

namespace fov::gaze {

Status classify(const FovGaze& raw, double age_ms) {
    if (raw.source == FOV_GAZE_NONE) return Status::None;
    // A sample from the future (clock read before the writer's) counts as fresh; only age matters.
    if (!(raw.flags & FOV_GAZE_VALID) || age_ms > FOV_GAZE_STALE_MS) return Status::Stale;
    return (raw.flags & FOV_GAZE_BLINK) ? Status::Blink : Status::Valid;
}

Status from_shm(Sample& out, int64_t now_qpc) {
    FovGaze raw;
    if (!fov_shm_read_gaze(&raw)) return Status::None;
    Status s = classify(raw, fov_qpc_ms(raw.qpc, now_qpc));
    if (s == Status::Valid) {
        out.source = static_cast<FovGazeSource>(raw.source);
        out.kind = static_cast<FovGazeKind>(raw.kind);
        for (int e = 0; e < 2; ++e)
            for (int i = 0; i < 3; ++i) out.eye[e][i] = raw.eye[e][i];
    }
    return s;
}

}  // namespace fov::gaze
