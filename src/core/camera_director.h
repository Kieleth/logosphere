#ifndef LOGOSPHERE_CORE_CAMERA_DIRECTOR_H
#define LOGOSPHERE_CORE_CAMERA_DIRECTOR_H

// CameraDirector — wraps CameraSystem with cinematic primitives.
//
// Generic engine capability. Any game that wants to dolly the
// camera to a world point for a cutscene uses this; the camera
// controller it was using before keeps its state and gets it back
// when CameraDirector releases.
//
// Lifecycle is driven by REAL time, not game time, so dollies
// keep playing during Engine::set_cinematic_pause(true).
//
//   focus_on(target_x, target_y, target_z, distance, tilt_deg, ease_seconds);
//   ... update() called each frame by Engine ...
//   release(ease_seconds);   // or release(0) for an immediate snap-back
//
// Approach. On focus_on() the current camera pose is snapshotted
// as the "pre" pose. The "target" pose is computed: keep the
// horizontal direction the camera was already looking from, but
// pull in to `distance` from the target world point and raise to
// `tilt_deg` above horizontal. Camera position + look-at are
// quintic-smoothstep'd between the two over `ease_seconds`. After
// the ease window the pose holds. release() reverses the dolly
// back to the snapshotted pose over its own ease window.

class CameraSystem;

class CameraDirector {
public:
    CameraDirector() = default;
    ~CameraDirector() = default;

    // Bound at engine init. Owning Engine passes its CameraSystem in.
    void bind(CameraSystem* camera) { camera_ = camera; }

    // Begin dolly. Subsequent focus_on() calls retarget without
    // re-snapshotting (the "pre" pose stays the original so a
    // release() during a chained focus still goes home).
    void focus_on(float target_x, float target_y, float target_z,
                  float distance, float tilt_deg, float ease_seconds);

    // Begin release back to the snapshotted pre-pose. ease_seconds=0
    // snaps immediately. No-op if not active.
    void release(float ease_seconds = 0.4f);

    // Drive easing. Engine calls each frame with REAL delta time
    // (so dollies survive cinematic pause). No-op when inactive.
    void update(float real_dt);

    // True between focus_on and the end of release.
    bool is_active() const { return phase_ != Phase::Idle; }

private:
    enum class Phase { Idle, Engaging, Holding, Releasing };

    void apply_pose(float alpha);   // alpha in [0, 1] over current segment
    static float smoothstep5(float t); // quintic 6t^5 - 15t^4 + 10t^3

    CameraSystem* camera_ = nullptr;

    // Saved pose at the moment of the first focus_on (the home).
    float pre_pos_[3]      = {0, 0, 0};
    float pre_look_at_[3]  = {0, 0, 0};
    bool  has_pre_         = false;

    // Pose targeted by the active dolly (re-set on each focus_on).
    float target_pos_[3]      = {0, 0, 0};
    float target_look_at_[3]  = {0, 0, 0};

    // Pose the segment is interpolating FROM (engaging: pre_;
    // releasing: pose at release-begin). Captured per segment.
    float segment_from_pos_[3]     = {0, 0, 0};
    float segment_from_look_at_[3] = {0, 0, 0};

    Phase phase_       = Phase::Idle;
    float segment_t_   = 0.0f;   // [0, segment_duration_]
    float segment_dur_ = 0.0f;   // seconds; 0 means snap immediately
};

#endif  // LOGOSPHERE_CORE_CAMERA_DIRECTOR_H
