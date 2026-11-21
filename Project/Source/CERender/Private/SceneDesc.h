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

enum class EObjectKind { Mesh, Light, Camera, Group };

struct FObjectDesc
{
    std::string id;
    EObjectKind kind = EObjectKind::Mesh;

    // Mesh
    std::string mesh;               // builtin name or file path (resolved by the parser)
    bool meshIsBuiltin = true;
    std::string materialName;       // empty = inline material or the mesh's own materials
    std::optional<FMaterialDesc> inlineMaterial;
    bool shadow = true;

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

enum class ETrackKind { Keys, Spin, Wave, Orbit };

struct FTrackDesc
{
    std::string objectId;
    EProperty property = EProperty::Position;
    int component = -1;             // -1 = whole value, 0..2 = x/y/z only
    ETrackKind kind = ETrackKind::Keys;

    std::vector<FKey> keys;

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
