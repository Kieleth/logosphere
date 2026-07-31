#ifndef OBSERVER_SYSTEM_H
#define OBSERVER_SYSTEM_H

#include <vector>
#include <functional>
#include "particle_geometry_v2.h"  // For Surface struct

// Sensor represents a light-detecting surface (eye, camera sensor, etc)
struct Sensor {
    // Position in world space
    float x, y, z;
    
    // Orientation (where sensor is looking)
    float forward_x, forward_y, forward_z;  // Forward direction
    float up_x, up_y, up_z;                 // Up direction
    
    // Sensor properties
    float fov_horizontal = 90.0f;  // Field of view in degrees
    float fov_vertical = 60.0f;    
    float near_distance = 0.1f;    // Minimum sensing distance
    float far_distance = 1000.0f;  // Maximum sensing distance
    
    // Resolution (how many rays to cast)
    int horizontal_samples = 100;
    int vertical_samples = 75;
    
    // Sensitivity
    float min_light_threshold = 0.001f;  // Minimum detectable light
    float max_light_saturation = 100.0f; // Maximum before saturation
    
    // Parent observer
    int observer_id = -1;
};

// SensorReading represents what one sensor detected
struct SensorReading {
    int sensor_id;
    
    // Image data from this sensor
    struct Sample {
        float intensity;    // How much light
        float r, g, b;     // Color of light
        int surface_id;    // Which surface was hit (-1 if none)
        float distance;    // Distance to surface
    };
    
    std::vector<Sample> samples;  // One per ray cast
    
    // Computed properties
    float average_brightness = 0.0f;
    float max_brightness = 0.0f;
};

// Observer represents an entity that can see (can have multiple sensors)
class Observer {
public:
    // Sensors that make up this observer
    std::vector<Sensor> sensors;
    
    // How to combine multiple sensor readings
    enum CombineMode {
        SINGLE,         // Use first sensor only
        STEREO,         // Two sensors for depth perception
        AVERAGE,        // Average all sensors
        MAX,            // Use brightest from any sensor
        CUSTOM          // Use custom function
    };
    
    CombineMode combine_mode = SINGLE;
    
    // Custom combination function (if mode is CUSTOM)
    std::function<SensorReading(const std::vector<SensorReading>&)> combine_function;
    
    // Add a sensor to this observer
    void AddSensor(const Sensor& sensor) {
        sensors.push_back(sensor);
    }
    
    // Create a simple monocular observer (single sensor)
    static Observer CreateMonocular(float x, float y, float z, 
                                   float look_x, float look_y, float look_z) {
        Observer obs;
        Sensor sensor;
        sensor.x = x; sensor.y = y; sensor.z = z;
        
        // Calculate forward direction
        float len = sqrt(look_x*look_x + look_y*look_y + look_z*look_z);
        sensor.forward_x = look_x / len;
        sensor.forward_y = look_y / len;
        sensor.forward_z = look_z / len;
        
        // Default up vector
        sensor.up_x = 0; sensor.up_y = 0; sensor.up_z = 1;
        
        obs.AddSensor(sensor);
        obs.combine_mode = SINGLE;
        return obs;
    }
    
    // Create a binocular observer (two sensors for depth perception)
    static Observer CreateBinocular(float x, float y, float z,
                                   float look_x, float look_y, float look_z,
                                   float eye_separation = 0.065f) {
        Observer obs;
        
        // Calculate forward and right vectors
        float len = sqrt(look_x*look_x + look_y*look_y + look_z*look_z);
        float fx = look_x / len;
        float fy = look_y / len;
        float fz = look_z / len;
        
        // Right vector (cross product with up)
        float rx = fy * 1.0f - fz * 0.0f;
        float ry = fz * 0.0f - fx * 1.0f;
        float rz = fx * 0.0f - fy * 0.0f;
        len = sqrt(rx*rx + ry*ry + rz*rz);
        rx /= len; ry /= len; rz /= len;
        
        // Left eye
        Sensor left_eye;
        left_eye.x = x - rx * eye_separation/2;
        left_eye.y = y - ry * eye_separation/2;
        left_eye.z = z - rz * eye_separation/2;
        left_eye.forward_x = fx;
        left_eye.forward_y = fy;
        left_eye.forward_z = fz;
        left_eye.up_x = 0; left_eye.up_y = 0; left_eye.up_z = 1;
        
        // Right eye
        Sensor right_eye;
        right_eye.x = x + rx * eye_separation/2;
        right_eye.y = y + ry * eye_separation/2;
        right_eye.z = z + rz * eye_separation/2;
        right_eye.forward_x = fx;
        right_eye.forward_y = fy;
        right_eye.forward_z = fz;
        right_eye.up_x = 0; right_eye.up_y = 0; right_eye.up_z = 1;
        
        obs.AddSensor(left_eye);
        obs.AddSensor(right_eye);
        obs.combine_mode = STEREO;
        return obs;
    }
};

// ObserverSystem manages all observers and performs observation
class ObserverSystem {
private:
    std::vector<Observer> observers;
    std::vector<Surface> scene_surfaces;  // All surfaces in the scene
    
public:
    // Add an observer to the system
    int AddObserver(const Observer& observer) {
        observers.push_back(observer);
        return observers.size() - 1;
    }
    
    // Update scene surfaces (called when particles change)
    void UpdateSceneSurfaces(const std::vector<Surface>& surfaces) {
        scene_surfaces = surfaces;
    }
    
    // Perform observation - cast rays and build sensor readings
    std::vector<SensorReading> Observe(int observer_id);
    
    // Get specific observer
    Observer& GetObserver(int id) { return observers[id]; }
    const Observer& GetObserver(int id) const { return observers[id]; }
    
private:
    // Cast a single ray and find what it hits
    Surface* CastRay(float origin_x, float origin_y, float origin_z,
                     float dir_x, float dir_y, float dir_z,
                     float max_distance, float& hit_distance);
    
    // Calculate light reflected from surface to sensor
    float CalculateReflectedLight(const Surface& surface, 
                                 const Sensor& sensor,
                                 float distance);
};

#endif // OBSERVER_SYSTEM_H