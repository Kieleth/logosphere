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
    float divergence(int id) const {
        const State* s = latest(id);
        if (!s) return 0.0f;
        Quat qe = Quat::from_euler(s->rx, s->ry, s->rz);
        Quat r = s->q * qe.conjugate();
        float w = std::fabs(r.w); if (w > 1.0f) w = 1.0f;
        return 2.0f * std::acos(w);
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
