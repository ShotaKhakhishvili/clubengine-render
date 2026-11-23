#include "Emitter.h"

#include <Core/Math/Rotator.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

using namespace CE;

namespace CER
{

namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kPlacementAttempts = 30;   // per instance, when minGap/avoid reject a spot

// Independent random values per instance; the stream is the placement attempt or the loop cycle
enum EChannel : uint32_t { AreaX, AreaY, AreaZ, Size, Color, RotA, RotB, RotC, Phase, SpinAxisA, SpinAxisB, SpinRate, SpinSign, SwayDir, SwayPhase, SwayPeriod };

uint64_t SplitMix(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

Vec3 UnitVector(double a, double b)
{
    const double z = 2.0 * a - 1.0;
    const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
    const double angle = 2.0 * kPi * b;
    return Vec3{ r * std::cos(angle), r * std::sin(angle), z };
}

}

EmitterField::EmitterField(const FEmitterDesc& inDesc) : desc(inDesc)
{
    const FAreaDesc& area = desc.area;
    center = area.disc ? area.center + Vec3{ 0.0, 0.0, area.height * 0.5 } : (area.min + area.max) * 0.5;

    // Looping instances cross the area vertically when they fall or rise, else along their horizontal drift
    const Vec3& v = desc.velocity;
    const double horizontal = std::sqrt(v.x * v.x + v.y * v.y);
    if (desc.moving && desc.loop)
    {
        double length = 0.0, speed = 0.0;
        if (std::abs(v.z) > 1e-9)
        {
            length = area.Height();
            speed = std::abs(v.z);
        }
        else if (horizontal > 1e-9)
        {
            cycleAxis = Vec3{ v.x / horizontal, v.y / horizontal, 0.0 };
            length = area.disc ? 2.0 * area.radius
                               : std::abs(cycleAxis.x) * (area.max.x - area.min.x) + std::abs(cycleAxis.y) * (area.max.y - area.min.y);
            speed = horizontal;
        }
        if (length > 1e-9)
            cycleTime = length / speed;
    }

    // Instances already placed, bucketed by minGap-sized cells
    const double gap = desc.moving ? 0.0 : desc.minGap;
    std::unordered_map<uint64_t, std::vector<Vec3>> grid;
    auto Cell = [&](double x, double y) { return (static_cast<uint64_t>(static_cast<int64_t>(std::floor(x / gap))) << 32) ^ static_cast<uint32_t>(static_cast<int64_t>(std::floor(y / gap))); };
    auto IsCrowded = [&](const Vec3& p)
    {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
            {
                const auto it = grid.find(Cell(p.x + dx * gap, p.y + dy * gap));
                if (it == grid.end())
                    continue;
                for (const Vec3& q : it->second)
                    if ((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) < gap * gap)
                        return true;
            }
        return false;
    };

    const bool picky = !desc.moving && (gap > 0.0 || !desc.avoid.empty());
    instances.reserve(desc.count);
    for (uint32_t id = 0; id < static_cast<uint32_t>(desc.count); ++id)
    {
        // Dart throwing: the first spot that keeps clear of avoid regions and the min gap
        Vec3 position = SampleArea(id, 0);
        bool placed = !picky;
        for (uint32_t attempt = 0; picky && attempt < kPlacementAttempts && !placed; ++attempt)
        {
            position = SampleArea(id, attempt);
            placed = !IsAvoided(position) && !(gap > 0.0 && IsCrowded(position));
        }
        if (!placed)
            continue;
        if (gap > 0.0)
            grid[Cell(position.x, position.y)].push_back(position);

        FInstance instance;
        instance.id = id;
        instance.start = position;
        instance.phase = Random(id, 0, Phase);

        const double size = desc.sizeMin + (desc.sizeMax - desc.sizeMin) * Random(id, 0, Size);
        instance.scale = desc.scale * size;
        if (!desc.palette.empty())
            instance.color = std::min(desc.palette.size() - 1, static_cast<size_t>(Random(id, 0, Color) * desc.palette.size()));

        switch (desc.rotation)
        {
        case EEmitterRotation::Fixed:
            instance.rotation = FRotator{ desc.fixedRotation.x, desc.fixedRotation.y, desc.fixedRotation.z }.ToQuat();
            break;
        case EEmitterRotation::Yaw:
            instance.rotation = FRotator{ 0.0, 360.0 * Random(id, 0, RotA), 0.0 }.ToQuat();
            break;
        case EEmitterRotation::Random:
        {
            // Uniform over all orientations
            const double a = Random(id, 0, RotA), b = 2.0 * kPi * Random(id, 0, RotB), c = 2.0 * kPi * Random(id, 0, RotC);
            instance.rotation = FQuat{ std::sqrt(1.0 - a) * std::sin(b), std::sqrt(1.0 - a) * std::cos(b), std::sqrt(a) * std::sin(c), std::sqrt(a) * std::cos(c) };
            break;
        }
        }

        if (desc.spinMax > 0.0)
        {
            instance.spinAxis = UnitVector(Random(id, 0, SpinAxisA), Random(id, 0, SpinAxisB));
            instance.spinRate = (desc.spinMin + (desc.spinMax - desc.spinMin) * Random(id, 0, SpinRate)) * (Random(id, 0, SpinSign) < 0.5 ? -1.0 : 1.0);
        }

        const double swayAngle = 2.0 * kPi * Random(id, 0, SwayDir);
        instance.swayDir = Vec3{ std::cos(swayAngle), std::sin(swayAngle), 0.0 };
        instance.swayPhase = Random(id, 0, SwayPhase);
        instance.swayPeriod = desc.swayPeriod * (0.8 + 0.4 * Random(id, 0, SwayPeriod));

        instances.push_back(instance);
    }
}

double EmitterField::Random(uint32_t id, uint32_t stream, uint32_t channel) const
{
    uint64_t h = SplitMix(static_cast<uint64_t>(static_cast<uint32_t>(desc.seed)));
    h = SplitMix(h ^ id);
    h = SplitMix(h ^ (static_cast<uint64_t>(stream) << 8 | channel));
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);   // [0, 1)
}

Vec3 EmitterField::SampleArea(uint32_t id, uint32_t stream) const
{
    const FAreaDesc& area = desc.area;
    const double a = Random(id, stream, AreaX), b = Random(id, stream, AreaY), c = Random(id, stream, AreaZ);
    if (!area.disc)
        return Vec3{ area.min.x + (area.max.x - area.min.x) * a, area.min.y + (area.max.y - area.min.y) * b, area.min.z + (area.max.z - area.min.z) * c };

    // Even density over the disc or ring
    const double r = std::sqrt(area.inner * area.inner + a * (area.radius * area.radius - area.inner * area.inner));
    const double angle = 2.0 * kPi * b;
    return area.center + Vec3{ r * std::cos(angle), r * std::sin(angle), area.height * c };
}

bool EmitterField::IsAvoided(const Vec3& p) const
{
    for (const FAvoidDesc& a : desc.avoid)
    {
        if (a.circle ? (p.x - a.center.x) * (p.x - a.center.x) + (p.y - a.center.y) * (p.y - a.center.y) < a.radius * a.radius
                     : p.x >= a.min.x && p.x <= a.max.x && p.y >= a.min.y && p.y <= a.max.y)
            return true;
    }
    return false;
}

FTransform EmitterField::Evaluate(const FInstance& instance, double time) const
{
    Vec3 position = instance.start;
    double t = time;

    if (desc.moving)
    {
        const Vec3& v = desc.velocity;
        if (cycleTime > 0.0)
        {
            // Each pass across the area starts from a fresh random spot, centered on the area along the travel axis
            const double u = time / cycleTime + instance.phase;
            const double cycle = std::floor(u);
            Vec3 spot = SampleArea(instance.id, static_cast<uint32_t>(cycle));
            spot -= cycleAxis * Dot(cycleAxis, spot - center);
            position = spot + v * ((u - cycle - 0.5) * cycleTime);
        }
        else
        {
            // Without loop, falling instances settle at the bottom of the area (rising ones at the top) and stop there
            const FAreaDesc& area = desc.area;
            if (v.z < 0.0)
                t = std::min(t, (instance.start.z - area.Lowest().z) / -v.z);
            else if (v.z > 0.0)
                t = std::min(t, (area.Lowest().z + area.Height() - instance.start.z) / v.z);
            position = instance.start + v * t;
        }

        if (desc.swayAmplitude > 0.0)
        {
            // Side to side, with a smaller flutter across it
            const double p = 2.0 * kPi * (t / instance.swayPeriod + instance.swayPhase);
            const Vec3 across{ -instance.swayDir.y, instance.swayDir.x, 0.0 };
            position += instance.swayDir * (desc.swayAmplitude * std::sin(p)) + across * (0.35 * desc.swayAmplitude * std::sin(2.0 * p + 1.3));
        }
    }

    FQuat rotation = instance.rotation;
    if (instance.spinRate != 0.0)
        rotation = FQuat::FromAxisAngle(instance.spinAxis, instance.spinRate * t * kPi / 180.0) * rotation;

    return FTransform(position, rotation, instance.scale);
}

}
