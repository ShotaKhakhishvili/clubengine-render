#pragma once

// Plain description of a scene file. The parser fills it, SceneRuntime turns it into actors.

#include <Core/Math/Vector.h>

#include <array>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace CER
{

using Vec3 = CE::FVector;

struct FOutputDesc
{
    int width = 1080;
    int height = 1080;
    int fps = 30;
    double duration = 0.0; // seconds, 0 = still image

    int FrameCount() const;
};

struct FEnvironmentDesc
{
    Vec3 skyTop{0.55, 0.72, 0.95};       // linear
    Vec3 skyHorizon{0.92, 0.88, 0.85};
    Vec3 skyBottom{0.80, 0.76, 0.74};

    bool hasGround = true;
    Vec3 groundColor{0.82, 0.80, 0.78};
    double groundHeight = 0.0;

    double ambient = 0.4;
    bool fog = true;
    double fogStart = 25.0;
    double fogEnd = 90.0;
    double exposure = 1.0;
};

struct FMaterialDesc
{
    Vec3 color{0.8, 0.8, 0.8};
    double roughness = 0.6;
    double metallic = 0.0;
    Vec3 emissive{0.0, 0.0, 0.0};
    bool unlit = false;
};

enum class ELightKind { Directional, Point, Spot };

struct FLightDesc
{
    ELightKind kind = ELightKind::Directional;
    Vec3 color{1.0, 1.0, 1.0};
    double intensity = 1.0;
    double radius = 20.0;       // point/spot
    double innerAngle = 20.0;   // spot, degrees
    double outerAngle = 35.0;   // spot, degrees
};

struct FLookAt
{
    std::optional<Vec3> point;
    std::string objectId;

    bool IsSet() const { return point.has_value() || !objectId.empty(); }
};

enum class EObjectKind { Mesh, Light, Camera, Group, Emitter };

// Where a mesh's origin sits. Default: centered for built-ins, as authored for glTF files and lathe profiles.
enum class EPivot { Default, Center, Bottom, Origin };

// Options for generated shapes (see MeshShapes.h)
struct FShapeDesc
{
    int detail = -1;                // -1 = the shape's default
    std::optional<bool> flat;       // faceted: one normal per triangle (unset = the shape's default)
    double thickness = -1.0;        // torus/ring tube diameter, -1 = default
    double bevel = 0.15;            // roundbox edge radius
    int seed = 1;                   // rock
    std::vector<std::array<double, 2>> profile; // lathe: [radius, z] points
};

// Where emitter instances live, in the emitter's own space: a box, or a disc/ring around a point
struct FAreaDesc
{
    bool disc = false;
    Vec3 min{}, max{};              // box
    Vec3 center{};                  // disc: center of its base
    double radius = 1.0;
    double inner = 0.0;             // > 0 makes a ring
    double height = 0.0;            // disc: extends this far up from the center

    Vec3 Lowest() const { return disc ? center : min; }
    double Height() const { return disc ? height : max.z - min.z; }
};

// A horizontal region that static scatter keeps clear: a circle, or a box
struct FAvoidDesc
{
    bool circle = true;
    Vec3 center{};
    double radius = 0.0;
    Vec3 min{}, max{};
};

enum class EEmitterRotation { Fixed, Yaw, Random };

// Expands one mesh object into many instances (see Emitter.h)
struct FEmitterDesc
{
    int count = 100;
    FAreaDesc area;
    std::vector<Vec3> palette;      // one color per instance, picked at random; empty = the object's material
    double sizeMin = 1.0, sizeMax = 1.0;
    Vec3 scale{1.0, 1.0, 1.0};      // per instance, before size
    EEmitterRotation rotation = EEmitterRotation::Fixed;
    Vec3 fixedRotation{};
    int seed = 1;

    // Motion (none = static scatter)
    bool moving = false;
    Vec3 velocity{};                // drift with the fall subtracted from z
    double swayAmplitude = 0.0;
    double swayPeriod = 2.0;
    double spinMin = 0.0, spinMax = 0.0; // degrees per second, around a random axis
    bool loop = true;               // re-enter at the far side of the area; false: settle at its bottom (or top)

    // Static scatter
    double minGap = 0.0;            // horizontal distance between instance centers
    std::vector<FAvoidDesc> avoid;
};

struct FObjectDesc
{
    std::string id;
    EObjectKind kind = EObjectKind::Mesh;

    // Mesh (and emitter instances)
    std::string mesh;               // builtin name or file path (resolved by the parser)
    bool meshIsBuiltin = true;
    std::string materialName;       // empty = inline material or the mesh's own materials
    std::optional<FMaterialDesc> inlineMaterial;
    bool shadow = true;
    EPivot pivot = EPivot::Default;
    FShapeDesc shape;
    FEmitterDesc emitter;

    FLightDesc light;

    Vec3 position{0.0, 0.0, 0.0};
    Vec3 rotation{0.0, 0.0, 0.0};   // pitch, yaw, roll in degrees
    Vec3 scale{1.0, 1.0, 1.0};
    std::string parent;
    FLookAt lookAt;
    bool visible = true;
};

enum class EProperty { Position, Rotation, Scale, Color, Intensity, Visible, LookAt };

enum class EEase
{
    Linear, Step,
    InQuad, OutQuad, InOutQuad,
    InCubic, OutCubic, InOutCubic,
    InSine, OutSine, InOutSine,
    OutBack, InBack, InOutBack,
    OutBounce, OutElastic
};

struct FKey
{
    double time = 0.0;
    Vec3 value{};
    EEase ease = EEase::InOutCubic; // easing of the segment that starts at this key
};

enum class ETrackKind { Keys, Spin, Wave, Orbit, Follow, Path, Bounce, Walk };

enum class ECurve { Smooth, Linear, Bezier };

struct FTrackDesc
{
    std::string objectId;
    EProperty property = EProperty::Position;
    int component = -1;             // -1 = whole value, 0..2 = x/y/z only
    ETrackKind kind = ETrackKind::Keys;

    std::vector<FKey> keys;
    double repeatPeriod = 0.0;      // > 0: the key range repeats every this many seconds

    Vec3 spinRate{};                // degrees per second, added to rotation

    double waveAmplitude = 0.0;     // added: amplitude * sin(2pi * (t / period) + phase)
    Vec3 waveAmplitudeVec{};        // per component when the target is a vector
    bool waveIsVector = false;
    double wavePeriod = 1.0;
    double wavePhase = 0.0;         // in turns (0..1)

    Vec3 orbitCenter{};
    double orbitRadius = 5.0;
    double orbitHeight = 2.0;
    double orbitPeriod = 8.0;       // seconds per turn, negative = clockwise
    double orbitStartAngle = 0.0;   // degrees

    // follow: the target's world position (smoothed) plus an offset
    std::string followId;
    Vec3 followOffset{};
    double followSmoothing = 0.0;   // seconds of lag

    // path: travel through points at constant speed
    std::vector<Vec3> pathPoints;
    ECurve pathCurve = ECurve::Smooth;
    double pathStart = 0.0;
    double pathDuration = -1.0;     // -1 = until the end of the clip
    bool pathLoop = false;
    bool pathFace = false;          // turn (yaw) along the direction of travel
    EEase pathEase = EEase::Linear;

    // bounce: hops added to position.z
    double bounceHeight = 1.0;
    double bouncePeriod = 1.0;
    double bounceRestitution = 1.0; // < 1: each hop is lower and shorter until it rests
    double bounceSquash = 0.0;
    double bounceStart = 0.0;       // time of the first ground contact

    // walk: limb swing on named joints, plus a bob on the target (the rig root)
    std::array<std::string, 2> walkLegs, walkKnees, walkFeet, walkArms;
    double walkPeriod = 1.0;        // seconds per full stride (left + right step)
    double walkSwing = 25.0;        // degrees
    double walkKneeBend = 40.0;
    double walkArmSwing = 20.0;
    double walkBob = 0.03;
    double walkStart = 0.0;
    double walkEnd = -1.0;          // -1 = until the end of the clip
};

struct FSceneDesc
{
    FOutputDesc output;
    FEnvironmentDesc environment;
    std::map<std::string, FMaterialDesc> materials;
    std::vector<FObjectDesc> objects;
    std::vector<FTrackDesc> tracks;
};

}
