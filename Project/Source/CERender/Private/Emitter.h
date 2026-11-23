#pragma once

// Emitters expand one mesh object into many instances. Placement comes from a seeded hash, and every
// instance's transform is a closed-form function of time (no frame-to-frame state), so the same seed
// always gives the same result and any frame renders the same on its own.

#include "SceneDesc.h"

#include <Core/Math/Quat.h>
#include <Core/Math/Transform.h>

#include <cstdint>
#include <vector>

namespace CER
{

class EmitterField
{
public:
    struct FInstance
    {
        uint32_t id = 0;            // instance number, keys its random values
        Vec3 start;                 // static: where it sits; moving without loop: where it starts
        CE::FQuat rotation;
        Vec3 scale;
        size_t color = 0;           // index into the palette
        double phase = 0.0;         // looping: how far through its cycle it is at t = 0 (0..1)
        Vec3 spinAxis;
        double spinRate = 0.0;      // degrees per second
        Vec3 swayDir;
        double swayPhase = 0.0;     // in turns
        double swayPeriod = 1.0;
    };

    explicit EmitterField(const FEmitterDesc& desc);

    // Fewer than desc.count when minGap/avoid leave no room for the rest
    const std::vector<FInstance>& GetInstances() const { return instances; }

    // The instance's transform in the emitter's space
    CE::FTransform Evaluate(const FInstance& instance, double time) const;

private:
    double Random(uint32_t id, uint32_t stream, uint32_t channel) const;
    Vec3 SampleArea(uint32_t id, uint32_t stream) const;
    bool IsAvoided(const Vec3& p) const;

    const FEmitterDesc& desc;
    std::vector<FInstance> instances;

    // Looping motion crosses the area along this axis in cycleTime seconds, then re-enters at the far side
    Vec3 cycleAxis{ 0.0, 0.0, 1.0 };
    double cycleTime = 0.0;         // 0 = no cycling
    Vec3 center;
};

}
