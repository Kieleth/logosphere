// Humanoid Connectivity Validator
//
// Checks the CORRECT invariant for gluon-connected joints:
// pivot coincidence — both parent and child must agree on the
// shared pivot point's world-space location.
//
// For parent P and child C connected by a gluon:
//   pivot_from_parent = P.pos + rotate(P, offset_a)
//   pivot_from_child  = C.pos + rotate(C, offset_b)
//   INVARIANT: |pivot_from_parent - pivot_from_child| ≈ 0
//
// This replaces the old center-to-center distance checks (ASSERT D/E)
// which were semantically wrong — center distance changes with rotation
// when gluons use offset pivot points.

#ifndef HUMANOID_VALIDATOR_H
#define HUMANOID_VALIDATOR_H

#include "core/particle_system.h"
#include "logosphere/physics/physics_system.h"
#include "logosphere/worldgen/humanoid_generator.h"
#include <vector>
#include <string>
#include <array>
#include <cmath>
#include <cstdio>
#include <algorithm>

struct JointStatus {
    std::string name;
    int parent_id, child_id;
    float pivot_error;             // |pivot_A - pivot_B| — THE invariant (should be ~0)
    float center_distance;         // |parent - child| — informational only
    std::array<float,3> pivot_parent;  // World-space pivot computed from parent
    std::array<float,3> pivot_child;   // World-space pivot computed from child
    bool connected;                // pivot_error < tolerance
};

struct HumanoidValidation {
    std::vector<JointStatus> joints;
    int connected_count = 0;
    int disconnected_count = 0;
    float max_pivot_error = 0.0f;
    std::string worst_joint;

    bool all_connected() const { return disconnected_count == 0; }

    // Max pivot error among joints whose name starts with prefix
    float chain_max_error(const std::string& prefix) const {
        float max_err = 0;
        for (const auto& j : joints)
            if (j.name.substr(0, prefix.size()) == prefix)
                max_err = std::max(max_err, j.pivot_error);
        return max_err;
    }

    void print_report() const {
        for (const auto& j : joints) {
            printf("[HUMANOID_JOINT] %-20s parent=%-3d child=%-3d pivot_err=%.4f center_dist=%.3f %s\n",
                   j.name.c_str(), j.parent_id, j.child_id,
                   j.pivot_error, j.center_distance,
                   j.connected ? "CONNECTED" : "DISCONNECTED");
        }
        printf("[HUMANOID_SUMMARY] %d/%d connected, max_error=%.4f at %s\n",
               connected_count, connected_count + disconnected_count,
               max_pivot_error, worst_joint.c_str());
    }
};

// Rotate offset by particle's full Euler rotation (X→Y→Z),
// matching the renderer (particle_geometry_v2.cpp:232-270).
// Z rotation uses CW convention (matching renderer legacy).
inline std::array<float,3> rotate_offset_euler(
    float ox, float oy, float oz,
    float rx, float ry, float rz)
{
    float px = ox, py = oy, pz = oz;

    // X rotation (pitch)
    if (rx != 0) {
        float cx = std::cos(rx), sx = std::sin(rx);
        float ny = py * cx - pz * sx;
        float nz = py * sx + pz * cx;
        py = ny; pz = nz;
    }
    // Y rotation (yaw)
    if (ry != 0) {
        float cy = std::cos(ry), sy = std::sin(ry);
        float nx = px * cy + pz * sy;
        float nz = -px * sy + pz * cy;
        px = nx; pz = nz;
    }
    // Z rotation (CW, matching renderer)
    if (rz != 0) {
        float cz = std::cos(rz), sz = std::sin(rz);
        float nx = px * cz + py * sz;
        float ny = -px * sz + py * cz;
        px = nx; py = ny;
    }
    return {px, py, pz};
}

// Compute world-space pivot point from a particle and its gluon offset
inline std::array<float,3> compute_pivot(
    const Particle& p, const Vec3& offset)
{
    auto rotated = rotate_offset_euler(
        offset.x, offset.y, offset.z,
        p.rotation_x, p.rotation_y, p.rotation_z);
    return {
        p.x + rotated[0],
        p.y + rotated[1],
        p.z + rotated[2]
    };
}

// Check a single joint. Handles gluon direction (particle_a may not be parent).
inline JointStatus check_joint(
    const char* name,
    int parent_id, int child_id,
    const ParticleSystem::ReadView& view,
    const PhysicsSystem& physics,
    float tolerance)
{
    JointStatus status;
    status.name = name;
    status.parent_id = parent_id;
    status.child_id = child_id;

    // get_gluon uses min/max key — order-independent, always finds the gluon.
    // We must check gluon->particle_a to know which offset belongs to which particle.
    const auto* gluon = physics.get_gluon(parent_id, child_id);
    if (!gluon) {
        status.pivot_error = 999.0f;
        status.center_distance = 0.0f;
        status.pivot_parent = {0,0,0};
        status.pivot_child = {0,0,0};
        status.connected = false;
        return status;
    }

    // offset_a is ALWAYS on particle_a, offset_b on particle_b
    if (static_cast<int>(gluon->particle_a) == parent_id) {
        status.pivot_parent = compute_pivot(view[parent_id], gluon->offset_a);
        status.pivot_child  = compute_pivot(view[child_id], gluon->offset_b);
    } else {
        status.pivot_parent = compute_pivot(view[parent_id], gluon->offset_b);
        status.pivot_child  = compute_pivot(view[child_id], gluon->offset_a);
    }

    float dx = status.pivot_parent[0] - status.pivot_child[0];
    float dy = status.pivot_parent[1] - status.pivot_child[1];
    float dz = status.pivot_parent[2] - status.pivot_child[2];
    status.pivot_error = std::sqrt(dx*dx + dy*dy + dz*dz);

    float cx = view[parent_id].x - view[child_id].x;
    float cy = view[parent_id].y - view[child_id].y;
    float cz = view[parent_id].z - view[child_id].z;
    status.center_distance = std::sqrt(cx*cx + cy*cy + cz*cz);

    status.connected = (status.pivot_error < tolerance);
    return status;
}

// Validate all joints of a humanoid.
// Topology hardcoded from PhysicsHumanoidResult structure.
inline HumanoidValidation validate_humanoid(
    const ParticleSystem& ps,
    const PhysicsSystem& physics,
    const PhysicsHumanoidResult& h,
    float tolerance = 0.02f)
{
    HumanoidValidation result;
    auto view = ps.lock_particles_for_read();

    // Helper to add a joint
    auto add = [&](const char* name, int parent, int child) {
        if (parent < 0 || child < 0) return;
        result.joints.push_back(
            check_joint(name, parent, child, view, physics, tolerance));
    };

    // Spine chain: torso_ids = {hips, abdomen, chest, neck, head, ...}
    if (h.torso_ids.size() >= 2) add("lumbar",             h.torso_ids[0], h.torso_ids[1]);
    if (h.torso_ids.size() >= 3) add("thoracic",           h.torso_ids[1], h.torso_ids[2]);
    if (h.torso_ids.size() >= 4) add("cervical",           h.torso_ids[2], h.torso_ids[3]);
    if (h.torso_ids.size() >= 5) add("atlanto-occipital",  h.torso_ids[3], h.torso_ids[4]);

    // Right arm: {shoulder, upper_arm, forearm, hand}
    // Chest (torso[2]) → shoulder
    if (h.torso_ids.size() >= 3 && h.right_arm_ids.size() >= 1)
        add("r_chest-shoulder", h.torso_ids[2], h.right_arm_ids[0]);
    if (h.right_arm_ids.size() >= 2) add("r_shoulder",  h.right_arm_ids[0], h.right_arm_ids[1]);
    if (h.right_arm_ids.size() >= 3) add("r_elbow",     h.right_arm_ids[1], h.right_arm_ids[2]);
    if (h.right_arm_ids.size() >= 4) add("r_wrist",     h.right_arm_ids[2], h.right_arm_ids[3]);

    // Left arm: {shoulder, upper_arm, forearm, hand}
    if (h.torso_ids.size() >= 3 && h.left_arm_ids.size() >= 1)
        add("l_chest-shoulder", h.torso_ids[2], h.left_arm_ids[0]);
    if (h.left_arm_ids.size() >= 2) add("l_shoulder",  h.left_arm_ids[0], h.left_arm_ids[1]);
    if (h.left_arm_ids.size() >= 3) add("l_elbow",     h.left_arm_ids[1], h.left_arm_ids[2]);
    if (h.left_arm_ids.size() >= 4) add("l_wrist",     h.left_arm_ids[2], h.left_arm_ids[3]);

    // Right leg: {foot, shin, thigh} — hips → thigh is torso_ids[0] → right_leg_ids[2]
    if (h.torso_ids.size() >= 1 && h.right_leg_ids.size() >= 3)
        add("r_hip",   h.torso_ids[0], h.right_leg_ids[2]);
    if (h.right_leg_ids.size() >= 3) add("r_knee",  h.right_leg_ids[2], h.right_leg_ids[1]);
    if (h.right_leg_ids.size() >= 2) add("r_ankle", h.right_leg_ids[1], h.right_leg_ids[0]);
    if (h.right_leg_ids.size() >= 4) add("r_toe",   h.right_leg_ids[0], h.right_leg_ids[3]);

    // Left leg: {foot, shin, thigh, toe} — hips → thigh is torso_ids[0] → left_leg_ids[2]
    if (h.torso_ids.size() >= 1 && h.left_leg_ids.size() >= 3)
        add("l_hip",   h.torso_ids[0], h.left_leg_ids[2]);
    if (h.left_leg_ids.size() >= 3) add("l_knee",  h.left_leg_ids[2], h.left_leg_ids[1]);
    if (h.left_leg_ids.size() >= 2) add("l_ankle", h.left_leg_ids[1], h.left_leg_ids[0]);
    if (h.left_leg_ids.size() >= 4) add("l_toe",   h.left_leg_ids[0], h.left_leg_ids[3]);

    // Compute summary
    result.max_pivot_error = 0.0f;
    result.connected_count = 0;
    result.disconnected_count = 0;
    for (const auto& j : result.joints) {
        if (j.connected) {
            result.connected_count++;
        } else {
            result.disconnected_count++;
        }
        if (j.pivot_error > result.max_pivot_error) {
            result.max_pivot_error = j.pivot_error;
            result.worst_joint = j.name;
        }
    }

    return result;
}

#endif // HUMANOID_VALIDATOR_H
