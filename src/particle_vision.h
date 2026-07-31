#ifndef PARTICLE_VISION_H
#define PARTICLE_VISION_H

// ViewPlane: 3D plane a particle presents to the viewer (used by vision_system
// for FOV culling / line-of-sight queries).

struct ViewPlane {
    float center_x, center_y, center_z;
    float normal_x, normal_y, normal_z;
    float half_width, half_height;
    float start_x, start_y, start_z;
    float end_x, end_y, end_z;
};

#endif  // PARTICLE_VISION_H
