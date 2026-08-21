// =============================================================================
// ARGUS — the engine's many-eyed witness
// =============================================================================
// Named beings have jobs here: the Turtle holds the world up, Kamaji
// routes what things mean, Malleus keeps the ontology honest. ARGUS
// WATCHES. Declare which particles an experiment (or any system) cares
// about, and Argus records each one per frame — position, velocity,
// both orientation ledgers, angular velocity — and answers the RELATIVE
// questions no single particle can: separation, approach speed, and
// whether a body's two orientation representations still agree.
//
// Why it exists (owner, 2026-08-19): every loose assertion this week had
// the same shape — a test asserted one degree of freedom while the
// experiment had thirteen, and the owner's eye kept catching what the
// asserts never looked at (the cube that slid without tumbling, the
// sphere resting 1.9 m inside a ramp). Argus is the instrument the
// assert-or-waive discipline reads from: THE TEST ASSERTS THE SAME
// QUANTITIES THE LOG PRINTS, one source, no drift between what is
// checked and what is seen.
//
// PURE ENGINE MODULE, owner ruling: "not only in physics, we might have
// to use it for others too, like combat etc." Core-profile safe: reads
// ParticleSystem only, writes NOTHING (every accessor takes a read
// lock), so the witness cannot perturb the experiment. Zero cost when
// nobody watches — the ParticleTracer's proven contract, one branch.
//
// Usage:
//   logosphere::Argus argus;
//   argus.watch(cube_id, "cube");
//   argus.watch(slab_id, "slab");
//   // each frame, after the physics step:
//   argus.observe(ps, frame);
//   // queries — the SAME values dump() prints:
//   argus.spin(cube_id);                 // |omega|, latest
//   argus.separation(cube_id, slab_id);  // centre distance, latest
//   argus.approach_speed(cube_id, slab_id);
//   argus.divergence(cube_id);           // q-vs-Euler angle: one body,
//                                        // one orientation (G-23)
//   argus.peak_spin(cube_id);            // latched over the whole watch
//   argus.dump(std::cout, 20);           // the narration, last N frames
// =============================================================================
#pragma once

#include "core/particle_system.h"
#include "particle.h"
#include "math/quat.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace logosphere {

class Argus {
public:
    struct State {
        int frame = -1;
        float x = 0, y = 0, z = 0;
        float vx = 0, vy = 0, vz = 0;
        float ox = 0, oy = 0, oz = 0;          // omega
        float rx = 0, ry = 0, rz = 0;          // Euler ledger
        Quat  q  = Quat::identity();           // quaternion ledger
    };

    void watch(int id, std::string label) {
        Eye& e = eyes_[id];
        e.label = std::move(label);
        e.ring.resize(capacity_);
    }
    void unwatch(int id) { eyes_.erase(id); }
    bool watching() const { return !eyes_.empty(); }
    void set_capacity(size_t frames) { capacity_ = frames; }

    // Record every watched particle. Read-only by construction: the
    // witness cannot perturb what it watches.
    void observe(ParticleSystem& ps, int frame) {
        if (eyes_.empty()) return;             // the zero-cost branch
        auto v = ps.lock_particles_for_read();
        for (auto& [id, e] : eyes_) {
            if (id < 0 || (size_t)id >= v.size()) continue;
            const Particle& p = v[id];
            State s;
            s.frame = frame;
            s.x = p.x; s.y = p.y; s.z = p.z;
            s.vx = p.vx; s.vy = p.vy; s.vz = p.vz;
            s.ox = p.omega_x; s.oy = p.omega_y; s.oz = p.omega_z;
            s.rx = p.rotation_x; s.ry = p.rotation_y; s.rz = p.rotation_z;
            s.q = p.rotation_q;
            e.ring[e.head % capacity_] = s;
            e.head++;
            const float sp = std::sqrt(s.ox*s.ox + s.oy*s.oy + s.oz*s.oz);
            if (sp > e.peak_spin) e.peak_spin = sp;
            const float speed = std::sqrt(s.vx*s.vx + s.vy*s.vy + s.vz*s.vz);
            if (speed > e.peak_speed) e.peak_speed = speed;
            {   // two-band coherence accumulation (G-21, see FOLD_BAND)
                Quat qe = Quat::from_euler(s.rx, s.ry, s.rz);
                Quat r = s.q * qe.conjugate();
                float w = std::fabs(r.w); if (w > 1.0f) w = 1.0f;
                const float d = 2.0f * std::acos(w);
                const float fd = std::fabs(std::fabs(s.ry) - 1.57079633f);
                if (fd < FOLD_BAND) {
                    if (d > e.peak_div_fold) e.peak_div_fold = d;
                } else {
                    if (d > e.peak_div_sharp) e.peak_div_sharp = d;
                }
            }
        }
    }

    const State* latest(int id) const {
        auto it = eyes_.find(id);
        if (it == eyes_.end() || it->second.head == 0) return nullptr;
        return &it->second.ring[(it->second.head - 1) % capacity_];
    }
    const State* previous(int id) const {
        auto it = eyes_.find(id);
        if (it == eyes_.end() || it->second.head < 2) return nullptr;
        return &it->second.ring[(it->second.head - 2) % capacity_];
    }

    // ---- the relative questions -------------------------------------
    float separation(int a, int b) const {
        const State* pa = latest(a); const State* pb = latest(b);
        if (!pa || !pb) return -1.0f;
        const float dx = pa->x - pb->x, dy = pa->y - pb->y, dz = pa->z - pb->z;
        return std::sqrt(dx*dx + dy*dy + dz*dz);
    }
    // Positive while closing, from the relative velocity projected on
    // the separation axis — the same convention the contact events use.
    float approach_speed(int a, int b) const {
        const State* pa = latest(a); const State* pb = latest(b);
        if (!pa || !pb) return 0.0f;
        float dx = pb->x - pa->x, dy = pb->y - pa->y, dz = pb->z - pa->z;
        const float d = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (d < 1e-9f) return 0.0f;
        dx /= d; dy /= d; dz /= d;
        return (pa->vx - pb->vx) * dx + (pa->vy - pb->vy) * dy +
               (pa->vz - pb->vz) * dz;
    }
    float spin(int id) const {
        const State* s = latest(id);
        return s ? std::sqrt(s->ox*s->ox + s->oy*s->oy + s->oz*s->oz) : 0.0f;
    }
    float peak_spin(int id) const {
        auto it = eyes_.find(id);
        return it == eyes_.end() ? 0.0f : it->second.peak_spin;
    }
    float peak_speed(int id) const {
        auto it = eyes_.find(id);
        return it == eyes_.end() ? 0.0f : it->second.peak_speed;
    }
    // One body, one orientation: the angle between what the two ledgers
    // claim. Nonzero means a consumer-visible orientation exists that
    // the physics one disagrees with (G-23).
    // THE FOLD BAND (G-21, measured 2026-08-21). float32 Euler
    // extraction cannot encode orientations within ~0.04 rad of the
    // gimbal fold (|pitch| = pi/2) to better than 0.014 rad: a
    // 2M-sample round-trip sweep of from_euler(to_euler_zyx(q)) shows
    // mean error 0.0002 rad away from the fold and worst 0.014 AT it,
    // exactly where every live tumble spike landed (0.0110-0.0137).
    // Coherence contracts are therefore TWO-BAND (owner ruling:
    // adaptive thresholds): sharp where the representation can speak,
    // the representational ceiling where it cannot. peak_divergence()
    // accumulates the two bands separately at observe() time.
    static constexpr float FOLD_BAND = 0.05f;   // rad from |pitch|=pi/2
    float fold_distance(int id) const {
        const State* s = latest(id);
        if (!s) return 3.14159265f;
        return std::fabs(std::fabs(s->ry) - 1.57079633f);
    }
    // fold_band=false: worst divergence observed AWAY from the fold.
    // fold_band=true: worst divergence observed INSIDE the band.
    float peak_divergence(int id, bool fold_band) const {
        auto it = eyes_.find(id);
        if (it == eyes_.end()) return 0.0f;
        return fold_band ? it->second.peak_div_fold
                         : it->second.peak_div_sharp;
    }

    float divergence(int id) const {
        const State* s = latest(id);
        if (!s) return 0.0f;
        Quat qe = Quat::from_euler(s->rx, s->ry, s->rz);
        Quat r = s->q * qe.conjugate();
        float w = std::fabs(r.w); if (w > 1.0f) w = 1.0f;
        return 2.0f * std::acos(w);
    }

    // ---- the live narration (owner order, 2026-08-20) ----------------
    // "a higher-level Argus in the tests themselves showing the logs so I
    // can see and track what's going on." One compact line per body, and
    // milestone detection so the log names the ARC: touchdown, spin
    // death, motion stop. The test calls narrate() every N frames; the
    // milestones print themselves the frame they happen.
    void narrate(std::ostream& os, int id) const {
        const State* s = latest(id);
        auto it = eyes_.find(id);
        if (!s || it == eyes_.end()) return;
        char line[192];
        std::snprintf(line, sizeof(line),
            "  [argus f%-4d %-10s] pos(%+7.3f,%+7.3f,%6.3f) "
            "|v| %6.3f  |omega| %6.3f  rotY %+7.4f\n",
            s->frame, it->second.label.c_str(), s->x, s->y, s->z,
            std::sqrt(s->vx*s->vx + s->vy*s->vy + s->vz*s->vz),
            std::sqrt(s->ox*s->ox + s->oy*s->oy + s->oz*s->oz), s->ry);
        os << line;
    }
    // Milestones since the previous call for this body; prints any that
    // fired. rest_z: the height at which touchdown is declared.
    void milestones(std::ostream& os, int id, float rest_z) {
        const State* s = latest(id);
        auto it = eyes_.find(id);
        if (!s || it == eyes_.end()) return;
        Eye& e = it->second;
        const float spin = std::sqrt(s->ox*s->ox + s->oy*s->oy + s->oz*s->oz);
        const float speed = std::sqrt(s->vx*s->vx + s->vy*s->vy + s->vz*s->vz);
        char line[160];
        if (!e.ms_touch && s->z <= rest_z + 0.005f) {
            e.ms_touch = true;
            std::snprintf(line, sizeof(line),
                "  [argus === f%-4d %s TOUCHDOWN, |omega| %.3f of %.3f armed "
                "(flight kept %.0f%%) ===]\n",
                s->frame, e.label.c_str(), spin, e.peak_spin,
                e.peak_spin > 0 ? 100.0f * spin / e.peak_spin : 0.0f);
            os << line;
        }
        if (!e.ms_spin_dead && e.peak_spin > 0.5f && spin < 0.1f) {
            e.ms_spin_dead = true;
            std::snprintf(line, sizeof(line),
                "  [argus === f%-4d %s SPIN DEAD ===]\n", s->frame,
                e.label.c_str());
            os << line;
        }
        if (!e.ms_stopped && e.peak_speed > 0.1f && speed < 0.01f &&
            e.ms_touch) {
            e.ms_stopped = true;
            std::snprintf(line, sizeof(line),
                "  [argus === f%-4d %s MOTION STOPPED at (%.4f, %.4f): the "
                "experiment is OVER here ===]\n",
                s->frame, e.label.c_str(), s->x, s->y);
            os << line;
        }
    }
    void reset_milestones(int id) {
        auto it = eyes_.find(id);
        if (it == eyes_.end()) return;
        it->second.ms_touch = it->second.ms_spin_dead =
            it->second.ms_stopped = false;
        it->second.peak_spin = it->second.peak_speed = 0.0f;
    }

    // ---- the narration ------------------------------------------------
    void dump(std::ostream& os, int last_n = 10) const {
        os << "[ARGUS] watching " << eyes_.size() << " particle(s)\n";
        for (const auto& [id, e] : eyes_) {
            os << "  P" << id << " = " << e.label
               << "  (peak |omega| " << e.peak_spin
               << ", peak speed " << e.peak_speed << ")\n";
            const size_t n = e.head < (size_t)last_n ? e.head : (size_t)last_n;
            for (size_t k = e.head - n; k < e.head; ++k) {
                const State& s = e.ring[k % capacity_];
                char line[192];
                std::snprintf(line, sizeof(line),
                    "    f%-5d pos(%7.3f,%7.3f,%7.3f) v(%6.2f,%6.2f,%6.2f) "
                    "omega(%6.3f,%6.3f,%6.3f) rotY %7.4f div %6.4f\n",
                    s.frame, s.x, s.y, s.z, s.vx, s.vy, s.vz,
                    s.ox, s.oy, s.oz, s.ry, div_of(s));
                os << line;
            }
        }
    }

private:
    struct Eye {
        std::string label;
        std::vector<State> ring;
        size_t head = 0;
        float peak_spin = 0.0f;
        float peak_speed = 0.0f;
        bool ms_touch = false, ms_spin_dead = false, ms_stopped = false;
        float peak_div_sharp = 0.0f, peak_div_fold = 0.0f;
    };
    static float div_of(const State& s) {
        Quat qe = Quat::from_euler(s.rx, s.ry, s.rz);
        Quat r = s.q * qe.conjugate();
        float w = std::fabs(r.w); if (w > 1.0f) w = 1.0f;
        return 2.0f * std::acos(w);
    }
    std::map<int, Eye> eyes_;
    size_t capacity_ = 600;    // ten seconds at 60 Hz, per eye
};

}  // namespace logosphere
