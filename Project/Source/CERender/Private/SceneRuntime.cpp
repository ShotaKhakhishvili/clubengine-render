#include "SceneRuntime.h"
#include "Easing.h"
#include "MeshShapes.h"

#include <Asset/ImportAssetBuilder.h>
#include <Asset/UMaterial.h>
#include <Asset/UShader.h>
#include <Asset/UStaticMesh.h>
#include <AssetImport/FSceneImportResult.h>
#include <AssetImport/GltfImporter.h>
#include <Core/FileIO.h>
#include <CoreUObject/AssetRegistry.h>
#include <Engine/Actors/AActor.h>
#include <Engine/Actors/ACameraActor.h>
#include <Engine/Actors/ADirectionalLight.h>
#include <Engine/Actors/APointLight.h>
#include <Engine/Actors/ASpotLight.h>
#include <Engine/Actors/AStaticMeshActor.h>
#include <Engine/Components/UPointLightComponent.h>
#include <Engine/Components/USpotLightComponent.h>
#include <Engine/Components/UStaticMeshComponent.h>
#include <Engine/World/UWorld.h>
#include <Project/ActiveProject.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <limits>

using namespace CE;

namespace CER
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

// Shader modes, must match scene.frag
constexpr int kModeLit = 0;
constexpr int kModeSky = 1;
constexpr int kModeGround = 2;
constexpr int kModeShadow = 3;
constexpr int kMaxContactShadows = 64; // must match MAX_BLOBS in scene.frag

constexpr double kSkyDiameter = 4000.0;   // well inside the camera's far plane
constexpr double kGroundSize = 1200.0;    // fog hides the edge

FVectorF ToF(const Vec3& v) { return FVectorF{ static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) }; }

Vec3 Lerp(const Vec3& a, const Vec3& b, double t) { return a + (b - a) * t; }

double& Component(Vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

FRotator RotatorToward(const Vec3& from, const Vec3& to, double roll)
{
    const Vec3 d = to - from;
    const double horizontal = std::sqrt(d.x * d.x + d.y * d.y);
    const double yaw = std::atan2(d.y, d.x) * 180.0 / kPi;
    const double pitch = std::atan2(d.z, horizontal) * 180.0 / kPi;
    return FRotator{ pitch, yaw, roll };
}

// Bounds of everything a glTF file draws, in the space the engine imports it to
void GltfBounds(const std::filesystem::path& path, Vec3& lo, Vec3& hi)
{
    lo = Vec3{ -0.5, -0.5, -0.5 };
    hi = Vec3{ 0.5, 0.5, 0.5 };
    GltfImporter importer;
    const std::optional<FSceneImportResult> result = importer.Import(path);
    if (!result)
        return;

    Vec3 a{ 1e30, 1e30, 1e30 }, b{ -1e30, -1e30, -1e30 };
    auto AddMesh = [&](const FMeshImportResult& mesh, const FTransform& transform)
    {
        for (const FVertex& v : mesh.vertices)
        {
            const Vec3 p = transform.TransformPoint(Vec3{ v.coord.x, v.coord.y, v.coord.z });
            a = Vec3{ std::min(a.x, p.x), std::min(a.y, p.y), std::min(a.z, p.z) };
            b = Vec3{ std::max(b.x, p.x), std::max(b.y, p.y), std::max(b.z, p.z) };
        }
    };
    std::function<void(int32, const FTransform&)> Walk = [&](int32 index, const FTransform& parent)
    {
        if (index < 0 || static_cast<size_t>(index) >= result->nodes.size())
            return;
        const FNodeImportDesc& node = result->nodes[index];
        const FTransform world = Combine(parent, node.localTransform);
        if (node.meshIndex >= 0 && static_cast<size_t>(node.meshIndex) < result->meshes.size())
            AddMesh(result->meshes[node.meshIndex], world);
        for (const int32 child : node.children)
            Walk(child, world);
    };
    for (const int32 root : result->rootNodes)
        Walk(root, FTransform::Identity());
    if (result->nodes.empty())
        for (const FMeshImportResult& mesh : result->meshes)
            AddMesh(mesh, FTransform::Identity());

    if (a.x <= b.x)
    {
        lo = a;
        hi = b;
    }
}

std::filesystem::path BuiltinMeshPath(const std::string& name)
{
    static const std::map<std::string, std::string> files = {
        { "cube", "Cube.glb" }, { "sphere", "Sphere.glb" }, { "cylinder", "Cylinder.glb" },
        { "cone", "Cone.glb" }, { "plane", "Plane.glb" },
    };
    return GetActiveProject().GetEnginePath() / "Engine/Source/Runtime/Engine/Public/Engine/Geometry" / files.at(name);
}

}

bool SceneRuntime::Build(UWorld& inWorld, const FSceneDesc& inScene, std::vector<std::string>& errors)
{
    world = &inWorld;
    scene = &inScene;
    AssetRegistry& assets = GetAssetRegistry();

    // Our shader: sky, soft ambient, fog, tonemapping and projected shadows
    {
        const auto shaders = GetActiveProject().GetContentPath() / "Shaders";
        const auto frag = shaders / "scene.frag";

        RHI::ShaderDesc desc;
        desc.stages = {
            { RHI::ShaderStage::Vertex,   FileIO::ReadFileWithIncludes(shaders / "scene.vert") },
            { RHI::ShaderStage::Fragment, FileIO::ReadFileWithIncludes(frag) },
        };
        shader = assets.Create<UShader>(desc);
        UShader* s = assets.Resolve(shader);
        if (!s || !s->Load())
        {
            errors.push_back("internal: scene shader failed to compile (" + frag.string() + ")");
            return false;
        }
    }

    const FEnvironmentDesc& env = scene->environment;

    // Sky dome first, ground second, so they sit behind everything in draw order
    {
        FMaterialDesc skyMaterial;
        skyMaterial.unlit = true;
        sky = SpawnMeshActor(ImportStaticMesh(BuiltinMeshPath("sphere")), MakeMaterial(skyMaterial, kModeSky));
    }

    if (env.hasGround)
    {
        FMaterialDesc groundMaterial;
        groundMaterial.color = env.groundColor;
        groundMaterial.roughness = 0.95;
        this->groundMaterial = MakeMaterial(groundMaterial, kModeGround);
        // Shadow copies are drawn opaque in the ground's own color, so overlaps never double-darken
        // and draw order doesn't matter. Back faces count too, since the copy is flat.
        this->shadowMaterial = MakeMaterial(groundMaterial, kModeShadow);
        if (UMaterial* shadow = assets.Resolve(this->shadowMaterial))
        {
            RHI::FMaterialSettings settings = shadow->GetSettings();
            settings.renderState.cullMode = RHI::CullMode::None;
            shadow->SetSettings(settings);
            shadow->SetRenderState(settings.renderState);
            shadow->SetFloat("u_ShadowStrength", 0.62f);
        }
        ground = SpawnMeshActor(ImportStaticMesh(BuiltinMeshPath("plane")), this->groundMaterial);
    }

    // Spawn in parent-first order
    for (const FObjectDesc& desc : scene->objects)
    {
        FRuntimeObject object;
        object.desc = &desc;
        for (std::string p = desc.parent; !p.empty();)
        {
            ++object.depth;
            const auto it = std::find_if(scene->objects.begin(), scene->objects.end(), [&](const FObjectDesc& o) { return o.id == p; });
            p = it != scene->objects.end() ? it->parent : std::string{};
        }
        objects.push_back(object);
    }
    std::stable_sort(objects.begin(), objects.end(), [](const FRuntimeObject& a, const FRuntimeObject& b) { return a.depth < b.depth; });

    for (size_t i = 0; i < objects.size(); ++i)
        indexById[objects[i].desc->id] = i;

    const FTransform identity;

    for (FRuntimeObject& object : objects)
    {
        const FObjectDesc& desc = *object.desc;

        object.base.position = desc.position;
        object.base.rotation = desc.rotation;
        object.base.scale = desc.scale;
        object.base.visible = desc.visible;
        object.base.lookAt = desc.lookAt;

        switch (desc.kind)
        {
        case EObjectKind::Mesh:
        {
            const FMesh loaded = LoadMesh(desc);
            const TObjectHandle<UStaticMesh> mesh = loaded.handle;
            UStaticMesh* meshAsset = assets.Resolve(mesh);
            if (!meshAsset)
            {
                errors.push_back("objects \"" + desc.id + "\": failed to load mesh " + desc.mesh);
                continue;
            }
            object.boundsMin = loaded.boundsMin;
            object.boundsMax = loaded.boundsMax;

            // The pivot moves the mesh under a plain root, so scale and rotation happen around the pivot
            EPivot pivot = desc.pivot;
            if (pivot == EPivot::Default)
                pivot = desc.meshIsBuiltin && desc.mesh != "lathe" ? EPivot::Center : EPivot::Origin;
            const Vec3 center = (loaded.boundsMin + loaded.boundsMax) * 0.5;
            Vec3 offset{};
            if (pivot == EPivot::Center) offset = center * -1.0;
            if (pivot == EPivot::Bottom) offset = Vec3{ -center.x, -center.y, -loaded.boundsMin.z };

            auto* actor = world->SpawnActor<AStaticMeshActor>(identity);
            object.mesh = actor->GetStaticMeshComponent();
            object.mesh->SetStaticMesh(mesh);
            if (std::abs(offset.x) + std::abs(offset.y) + std::abs(offset.z) > 1e-6)
            {
                object.actor = world->SpawnActor<AActor>(identity);
                object.mesh->SetParentComponent(object.actor->GetRootComponent());
                object.mesh->SetRelativeTransform(FTransform(offset, FQuat::Identity(), Vec3{ 1.0, 1.0, 1.0 }));
            }
            else
                object.actor = actor;

            // Built-ins and objects with a material get their own instance on every slot.
            // Imported meshes without one keep their glTF materials, switched to our shader.
            if (desc.shadow && ground)
            {
                object.shadowMesh = SpawnMeshActor(mesh, shadowMaterial);
                const uint32 slots = std::max<uint32>(1, static_cast<uint32>(meshAsset->GetSubMeshes().size()));
                for (uint32 slot = 0; slot < slots; ++slot)
                    object.shadowMesh->SetMaterial(slot, shadowMaterial);
            }

            const bool ownMaterial = desc.meshIsBuiltin || desc.inlineMaterial || !desc.materialName.empty();
            if (ownMaterial)
            {
                const FMaterialDesc m = desc.inlineMaterial ? *desc.inlineMaterial
                                      : (!desc.materialName.empty() ? scene->materials.at(desc.materialName) : FMaterialDesc{});
                object.base.color = m.color;
                object.material = MakeMaterial(m, kModeLit);

                const uint32 slots = std::max<uint32>(1, static_cast<uint32>(meshAsset->GetSubMeshes().size()));
                for (uint32 slot = 0; slot < slots; ++slot)
                    object.mesh->SetMaterial(slot, object.material);
            }
            else
            {
                for (const TObjectHandle<UMaterial>& handle : meshAsset->GetMaterials())
                {
                    if (UMaterial* material = assets.Resolve(handle))
                    {
                        material->SetShader(shader);
                        material->SetInt("u_Mode", kModeLit);
                        ApplyEnvironment(*material);
                    }
                }
            }
            break;
        }
        case EObjectKind::Light:
        {
            const FLightDesc& l = desc.light;
            if (l.kind == ELightKind::Directional)
            {
                auto* actor = world->SpawnActor<ADirectionalLight>(identity);
                object.actor = actor;
                object.light = actor->GetLightComponent();
            }
            else if (l.kind == ELightKind::Point)
            {
                auto* actor = world->SpawnActor<APointLight>(identity);
                object.actor = actor;
                object.light = actor->GetLightComponent();
                actor->GetPointLightComponent()->SetAttenuationRadius(static_cast<float>(l.radius));
            }
            else
            {
                auto* actor = world->SpawnActor<ASpotLight>(identity);
                object.actor = actor;
                object.light = actor->GetLightComponent();
                USpotLightComponent* spot = actor->GetSpotLightComponent();
                spot->SetAttenuationRadius(static_cast<float>(l.radius));
                spot->SetInnerConeAngle(static_cast<float>(l.innerAngle));
                spot->SetOuterConeAngle(static_cast<float>(l.outerAngle));
            }
            object.base.color = l.color;
            object.base.intensity = l.intensity;
            break;
        }
        case EObjectKind::Camera:
            object.actor = world->SpawnActor<ACameraActor>(identity);
            camera = &object;
            break;
        case EObjectKind::Group:
            object.actor = world->SpawnActor<AActor>(identity);   // gets a plain scene component as root
            break;
        }

        if (object.actor)
            object.root = object.actor->GetRootComponent();
    }

    if (!errors.empty())
        return false;

    for (FRuntimeObject& object : objects)
    {
        std::string top = object.desc->id;
        while (!objects[indexById.at(top)].desc->parent.empty())
            top = objects[indexById.at(top)].desc->parent;
        object.rootIndex = indexById.at(top);

        if (object.desc->parent.empty())
            continue;
        FRuntimeObject& parent = objects[indexById.at(object.desc->parent)];
        object.root->SetParentComponent(parent.root);
    }

    // Paths become polylines with distances, so objects travel at an even speed
    pathCaches.resize(scene->tracks.size());
    followValues.resize(scene->tracks.size());
    for (size_t i = 0; i < scene->tracks.size(); ++i)
    {
        const FTrackDesc& track = scene->tracks[i];
        if (track.kind != ETrackKind::Path)
            continue;

        const std::vector<Vec3>& p = track.pathPoints;
        const size_t n = p.size();
        FPathCache& cache = pathCaches[i];
        constexpr int kSteps = 48;

        if (track.pathCurve == ECurve::Bezier)
        {
            const size_t segments = track.pathLoop ? n / 3 : (n - 1) / 3;
            for (size_t s = 0; s < segments; ++s)
            {
                const Vec3 a = p[3 * s], b = p[3 * s + 1], c = p[3 * s + 2], d = p[(3 * s + 3) % n];
                for (int k = (s == 0 ? 0 : 1); k <= kSteps; ++k)
                {
                    const double t = k / double(kSteps), u = 1.0 - t;
                    cache.points.push_back(a * (u * u * u) + b * (3 * u * u * t) + c * (3 * u * t * t) + d * (t * t * t));
                }
            }
        }
        else
        {
            const size_t segments = track.pathLoop ? n : n - 1;
            auto At = [&](long long k) -> Vec3
            {
                if (track.pathLoop) return p[static_cast<size_t>(((k % (long long)n) + n) % n)];
                return p[static_cast<size_t>(std::clamp<long long>(k, 0, (long long)n - 1))];
            };
            for (size_t s = 0; s < segments; ++s)
            {
                const Vec3 p0 = At((long long)s - 1), p1 = At(s), p2 = At(s + 1), p3 = At(s + 2);
                const int steps = track.pathCurve == ECurve::Linear ? 1 : kSteps;
                for (int k = (s == 0 ? 0 : 1); k <= steps; ++k)
                {
                    const double t = k / double(steps), t2 = t * t, t3 = t2 * t;
                    // Catmull-Rom: passes through every point
                    cache.points.push_back(track.pathCurve == ECurve::Linear ? Lerp(p1, p2, t)
                        : (p1 * 2.0 + (p2 - p0) * t + (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * t2 + (p1 * 3.0 - p0 - p2 * 3.0 + p3) * t3) * 0.5);
                }
            }
        }

        cache.distance.push_back(0.0);
        for (size_t k = 1; k < cache.points.size(); ++k)
            cache.distance.push_back(cache.distance.back() + Length(cache.points[k] - cache.points[k - 1]));
    }

    return true;
}

SceneRuntime::FMesh SceneRuntime::LoadMesh(const FObjectDesc& desc)
{
    if (desc.meshIsBuiltin && NeedsGeneratedMesh(desc.mesh, desc.shape))
    {
        const FShapeDesc& s = desc.shape;
        std::string key = std::format("{}|{}|{}|{}|{}|{}|", desc.mesh, s.detail, s.flat ? int(*s.flat) : -1, s.thickness, s.bevel, s.seed);
        for (const auto& point : s.profile)
            key += std::format("{},{};", point[0], point[1]);
        if (const auto it = meshCache.find(key); it != meshCache.end())
            return it->second;

        const FMeshImportResult data = MakeShape(desc.mesh, s);
        FMesh out;
        out.boundsMin = Vec3{ 1e30, 1e30, 1e30 };
        out.boundsMax = Vec3{ -1e30, -1e30, -1e30 };
        for (const FVertex& v : data.vertices)
        {
            out.boundsMin = Vec3{ std::min<double>(out.boundsMin.x, v.coord.x), std::min<double>(out.boundsMin.y, v.coord.y), std::min<double>(out.boundsMin.z, v.coord.z) };
            out.boundsMax = Vec3{ std::max<double>(out.boundsMax.x, v.coord.x), std::max<double>(out.boundsMax.y, v.coord.y), std::max<double>(out.boundsMax.z, v.coord.z) };
        }
        if (!data.IsValid())
            return {};

        AssetRegistry& assets = GetAssetRegistry();
        out.handle = assets.Create<UStaticMesh>();
        if (UStaticMesh* mesh = assets.Resolve(out.handle))
        {
            mesh->SetName(desc.mesh.c_str());
            mesh->Build(data);
            mesh->SetMaterial(0, UMaterial::GetDefault());
        }
        return meshCache[key] = out;
    }

    const std::filesystem::path path = desc.meshIsBuiltin ? BuiltinMeshPath(desc.mesh) : std::filesystem::path(desc.mesh);
    const std::string key = path.string();
    if (const auto it = meshCache.find(key); it != meshCache.end())
        return it->second;

    FMesh out;
    out.handle = ImportStaticMesh(path);
    GltfBounds(path, out.boundsMin, out.boundsMax);
    return meshCache[key] = out;
}

UStaticMeshComponent* SceneRuntime::SpawnMeshActor(TObjectHandle<UStaticMesh> mesh, TObjectHandle<UMaterial> material)
{
    auto* actor = world->SpawnActor<AStaticMeshActor>(FTransform{});
    UStaticMeshComponent* component = actor->GetStaticMeshComponent();
    component->SetStaticMesh(mesh);
    component->SetMaterial(0, material);
    return component;
}

TObjectHandle<UMaterial> SceneRuntime::MakeMaterial(const FMaterialDesc& desc, int mode)
{
    AssetRegistry& assets = GetAssetRegistry();
    const TObjectHandle<UMaterial> handle = assets.Create<UMaterial>();
    UMaterial* material = assets.Resolve(handle);

    RHI::FMaterialSettings settings;
    settings.baseColor = FVector4F{ static_cast<float>(desc.color.x), static_cast<float>(desc.color.y), static_cast<float>(desc.color.z), 1.0f };
    settings.metallic = static_cast<float>(std::clamp(desc.metallic, 0.0, 1.0));
    settings.roughness = static_cast<float>(std::clamp(desc.roughness, 0.0, 1.0));
    settings.emissive = ToF(desc.emissive);
    settings.unlit = desc.unlit;
    settings.alphaCutoff = 0.0f;

    if (mode == kModeSky)
        settings.renderState.cullMode = RHI::CullMode::None;

    material->SetShader(shader);
    material->SetSettings(settings);
    material->SetRenderState(settings.renderState);
    material->SetInt("u_Mode", mode);
    material->SetInt("u_BlobCount", 0);
    material->SetFloat("u_GroundHeight", static_cast<float>(scene->environment.groundHeight));
    material->SetVec3("u_ShadowDir", 0.0f, 0.0f, -1.0f);
    material->SetFloat("u_ShadowStrength", 0.0f);
    ApplyEnvironment(*material);
    return handle;
}

void SceneRuntime::ApplyEnvironment(UMaterial& material) const
{
    const FEnvironmentDesc& env = scene->environment;
    material.SetVec3("u_SkyTop", static_cast<float>(env.skyTop.x), static_cast<float>(env.skyTop.y), static_cast<float>(env.skyTop.z));
    material.SetVec3("u_SkyHorizon", static_cast<float>(env.skyHorizon.x), static_cast<float>(env.skyHorizon.y), static_cast<float>(env.skyHorizon.z));
    material.SetVec3("u_SkyBottom", static_cast<float>(env.skyBottom.x), static_cast<float>(env.skyBottom.y), static_cast<float>(env.skyBottom.z));
    material.SetVec3("u_GroundColor", static_cast<float>(env.groundColor.x), static_cast<float>(env.groundColor.y), static_cast<float>(env.groundColor.z));
    material.SetFloat("u_Ambient", static_cast<float>(env.ambient));
    material.SetFloat("u_Exposure", static_cast<float>(env.exposure));
    material.SetBool("u_FogEnabled", env.fog);
    material.SetFloat("u_FogStart", static_cast<float>(env.fogStart));
    material.SetFloat("u_FogEnd", static_cast<float>(std::max(env.fogEnd, env.fogStart + 0.01)));
}

void SceneRuntime::ApplyTrack(size_t index, double time)
{
    const FTrackDesc& track = scene->tracks[index];
    FState& state = objects[indexById.at(track.objectId)].current;

    if (track.kind == ETrackKind::Walk)
    {
        ApplyWalk(track, time);
        return;
    }
    if (track.kind == ETrackKind::Path)
    {
        ApplyPath(index, time, state);
        return;
    }
    if (track.kind == ETrackKind::Bounce)
    {
        ApplyBounce(track, time, state);
        return;
    }

    Vec3* vec = nullptr;
    double* scalar = nullptr;
    switch (track.property)
    {
    case EProperty::Position:  vec = &state.position; break;
    case EProperty::Rotation:  vec = &state.rotation; break;
    case EProperty::Scale:     vec = &state.scale; break;
    case EProperty::Color:     vec = &state.color; break;
    case EProperty::Intensity: scalar = &state.intensity; break;
    case EProperty::Visible:   break;
    case EProperty::LookAt:
        // Animating lookAt replaces an object target with a point that starts where that target was
        if (!state.lookAt.point)
            state.lookAt.point = state.lookAt.objectId.empty() ? Vec3{} : WorldPositionOf(state.lookAt.objectId);
        state.lookAt.objectId.clear();
        vec = &*state.lookAt.point;
        break;
    }

    switch (track.kind)
    {
    case ETrackKind::Keys:
    {
        // A keyed track only takes over from its first key on, so tracks for later beats can follow earlier ones
        const auto& keys = track.keys;
        if (time < keys.front().time)
            break;

        // Repeating tracks wrap time back into the key range; the rest of each period holds the last key
        if (track.repeatPeriod > 0.0)
            time = keys.front().time + std::fmod(time - keys.front().time, track.repeatPeriod);

        Vec3 value = keys.front().value;

        if (time >= keys.back().time)
            value = keys.back().value;
        else if (time > keys.front().time)
        {
            for (size_t i = 0; i + 1 < keys.size(); ++i)
            {
                const FKey& a = keys[i];
                const FKey& b = keys[i + 1];
                if (time < b.time)
                {
                    const double span = b.time - a.time;
                    const double t = span > 0.0 ? (time - a.time) / span : 1.0;
                    value = Lerp(a.value, b.value, Ease(a.ease, t));
                    break;
                }
            }
        }

        if (track.property == EProperty::Visible)
            state.visible = value.x > 0.5;
        else if (scalar)
            *scalar = value.x;
        else if (track.component >= 0)
            Component(*vec, track.component) = value.x;
        else
            *vec = value;
        break;
    }
    case ETrackKind::Spin:
        *vec += track.spinRate * time;
        break;
    case ETrackKind::Wave:
    {
        const double s = std::sin(2.0 * kPi * (time / track.wavePeriod + track.wavePhase));
        if (scalar)
            *scalar += track.waveAmplitude * s;
        else if (track.component >= 0)
            Component(*vec, track.component) += track.waveAmplitude * s;
        else
            *vec += track.waveAmplitudeVec * s;
        break;
    }
    case ETrackKind::Orbit:
    {
        const double angle = (track.orbitStartAngle + 360.0 * time / track.orbitPeriod) * kPi / 180.0;
        *vec = track.orbitCenter + Vec3{ track.orbitRadius * std::cos(angle), track.orbitRadius * std::sin(angle), track.orbitHeight };
        break;
    }
    case ETrackKind::Follow:
        *vec = followValues[index] + track.followOffset;
        break;
    default:
        break;
    }
}

// Where the followed object was, averaged over the last few moments (newest weighs most).
// Computed from the scene at earlier times rather than frame to frame, so any frame renders the same on its own.
Vec3 SceneRuntime::SampleFollow(const FTrackDesc& track, double time)
{
    if (track.followSmoothing <= 0.0)
    {
        Pose(time, false);
        return WorldPositionOf(track.followId);
    }

    constexpr int kSamples = 16;
    const double window = 3.0 * track.followSmoothing;
    Vec3 sum{};
    double weights = 0.0;
    for (int k = 0; k < kSamples; ++k)
    {
        const double back = window * k / (kSamples - 1);
        const double w = std::exp(-back / track.followSmoothing);
        Pose(std::max(0.0, time - back), false);
        sum += WorldPositionOf(track.followId) * w;
        weights += w;
    }
    return sum * (1.0 / weights);
}

void SceneRuntime::ApplyPath(size_t index, double time, FState& state)
{
    const FTrackDesc& track = scene->tracks[index];
    const FPathCache& cache = pathCaches[index];
    if (cache.points.size() < 2)
        return;

    double duration = track.pathDuration;
    if (duration <= 0.0)
        duration = std::max(0.1, scene->output.duration - track.pathStart);

    double u = (time - track.pathStart) / duration;
    u = track.pathLoop ? (u < 0.0 ? 0.0 : u - std::floor(u)) : std::clamp(u, 0.0, 1.0);
    const double target = Ease(track.pathEase, u) * cache.distance.back();

    const size_t hi = std::clamp<size_t>(std::lower_bound(cache.distance.begin(), cache.distance.end(), target) - cache.distance.begin(), 1, cache.points.size() - 1);
    const double span = cache.distance[hi] - cache.distance[hi - 1];
    const double t = span > 0.0 ? (target - cache.distance[hi - 1]) / span : 0.0;
    state.position = Lerp(cache.points[hi - 1], cache.points[hi], t);

    if (track.pathFace)
    {
        // Turn toward the direction of travel; the object's own yaw is added, to correct models that don't face +X
        Vec3 d = cache.points[hi] - cache.points[hi - 1];
        for (size_t k = hi; std::abs(d.x) + std::abs(d.y) < 1e-9 && k + 1 < cache.points.size(); ++k)
            d = cache.points[k + 1] - cache.points[k];
        if (std::abs(d.x) + std::abs(d.y) > 1e-9)
            state.rotation.y += std::atan2(d.y, d.x) * 180.0 / kPi;
    }
}

void SceneRuntime::ApplyBounce(const FTrackDesc& track, double time, FState& state)
{
    double u = time - track.bounceStart;
    if (u < 0.0)
        return;

    // Each hop is a parabola. With restitution below 1 every hop is lower (e^2) and shorter (e) until it rests.
    double period = track.bouncePeriod, height = track.bounceHeight;
    const double e = track.bounceRestitution;
    if (e >= 1.0)
        u = std::fmod(u, period);
    else
        while (u >= period)
        {
            u -= period;
            period *= e;
            height *= e * e;
            if (height < 0.002 * track.bounceHeight)
                return;
        }

    const double phase = u / period;
    state.position.z += 4.0 * height * phase * (1.0 - phase);

    if (track.bounceSquash > 0.0)
    {
        // Squash right at the contact, stretch while moving fast, round at the top. Volume stays about the same.
        const double amount = track.bounceSquash * height / track.bounceHeight;
        double contact = std::max(0.0, 1.0 - std::min(phase, 1.0 - phase) / 0.12);
        contact *= contact;
        const double speed = std::abs(1.0 - 2.0 * phase);
        const double sz = 1.0 - amount * contact + 0.5 * amount * speed * (1.0 - contact);
        const double sxy = 1.0 / std::sqrt(sz);
        state.scale = Vec3{ state.scale.x * sxy, state.scale.y * sxy, state.scale.z * sz };
    }
}

void SceneRuntime::ApplyWalk(const FTrackDesc& track, double time)
{
    // Fades in and out over a quarter second, so a character can start and stop walking
    const double end = track.walkEnd < 0.0 ? std::numeric_limits<double>::infinity() : track.walkEnd;
    const double weight = std::clamp((time - track.walkStart) / 0.25, 0.0, 1.0) * std::clamp((end - time) / 0.25, 0.0, 1.0);
    if (weight <= 0.0)
        return;

    const double phase = 2.0 * kPi * (time - track.walkStart) / track.walkPeriod;
    auto Pitch = [&](const std::string& id, double degrees)
    {
        if (!id.empty())
            objects[indexById.at(id)].current.rotation.x += degrees * weight;
    };

    for (int side = 0; side < 2; ++side)
    {
        const double p = phase + side * kPi;
        // Positive pitch swings a hanging limb forward (+X). The knee bends while its leg swings forward.
        const double hip = track.walkSwing * std::sin(p);
        const double knee = -track.walkKneeBend * std::max(0.0, std::cos(p));
        Pitch(track.walkLegs[side], hip);
        Pitch(track.walkKnees[side], knee);
        Pitch(track.walkFeet[side], -(hip + knee));
        Pitch(track.walkArms[side], -track.walkArmSwing * std::sin(p));
    }

    // Dips at full stride, when angled legs reach less far down, so planted feet stay on the floor
    objects[indexById.at(track.objectId)].current.position.z -= track.walkBob * weight * (1.0 - std::abs(std::cos(phase)));
}

Vec3 SceneRuntime::WorldPositionOf(const std::string& id) const
{
    const auto it = indexById.find(id);
    if (it == indexById.end() || !objects[it->second].root)
        return Vec3{};
    return objects[it->second].root->GetWorldLocation();
}

// Moves every object to its state at a time: tracks, then transforms parents first, then look-ats
void SceneRuntime::Pose(double time, bool withFollow)
{
    for (FRuntimeObject& object : objects)
        object.current = object.base;

    for (size_t i = 0; i < scene->tracks.size(); ++i)
        if (withFollow || scene->tracks[i].kind != ETrackKind::Follow)
            ApplyTrack(i, time);

    // Transforms, parents first
    for (FRuntimeObject& object : objects)
    {
        if (!object.root)
            continue;

        const FState& s = object.current;
        object.root->SetRelativeTransform(FTransform(s.position, FRotator{ s.rotation.x, s.rotation.y, s.rotation.z }.ToQuat(), s.scale));
    }

    // Look-ats after every position is known
    for (FRuntimeObject& object : objects)
    {
        const FState& s = object.current;
        if (!object.root || !s.lookAt.IsSet())
            continue;

        const Vec3 target = s.lookAt.point ? *s.lookAt.point : WorldPositionOf(s.lookAt.objectId);
        const Vec3 from = object.root->GetWorldLocation();
        if (std::abs(target.x - from.x) + std::abs(target.y - from.y) + std::abs(target.z - from.z) < 1e-6)
            continue;

        object.root->SetWorldRotation(RotatorToward(from, target, s.rotation.z));
    }
}

void SceneRuntime::Evaluate(double time)
{
    // Follow targets come from the scene at earlier times, so they're worked out before the real pose
    for (size_t i = 0; i < scene->tracks.size(); ++i)
        if (scene->tracks[i].kind == ETrackKind::Follow)
            followValues[i] = SampleFollow(scene->tracks[i], time);

    Pose(time, true);

    AssetRegistry& assets = GetAssetRegistry();

    // Color, light, visibility
    for (FRuntimeObject& object : objects)
    {
        const FState& s = object.current;

        if (object.shadowMesh)
        {
            object.shadowMesh->SetVisible(s.visible);
            object.shadowMesh->SetWorldTransform(object.mesh->GetWorldTransform());
        }

        if (object.mesh)
        {
            object.mesh->SetVisible(s.visible);
            if (UMaterial* material = assets.Resolve(object.material))
            {
                RHI::FMaterialSettings settings = material->GetSettings();
                settings.baseColor = FVector4F{ static_cast<float>(s.color.x), static_cast<float>(s.color.y), static_cast<float>(s.color.z), 1.0f };
                material->SetSettings(settings);
            }
        }

        if (object.light)
        {
            object.light->SetVisible(s.visible);
            object.light->SetLightColor(ToF(s.color));
            object.light->SetIntensity(static_cast<float>(std::max(0.0, s.intensity)));
        }
    }

    UpdateContactShadows();

    // Sky and ground follow the camera so they never end
    if (camera && camera->root)
    {
        const Vec3 eye = camera->root->GetWorldLocation();
        sky->SetWorldTransform(FTransform(eye, FQuat::Identity(), Vec3{ kSkyDiameter, kSkyDiameter, kSkyDiameter }));
        if (ground)
        {
            const Vec3 at{ std::round(eye.x), std::round(eye.y), scene->environment.groundHeight };
            ground->SetWorldTransform(FTransform(at, FQuat::Identity(), Vec3{ kGroundSize, kGroundSize, 1.0 }));
        }
    }
}

void SceneRuntime::UpdateContactShadows()
{
    AssetRegistry& assets = GetAssetRegistry();
    UMaterial* groundMat = assets.Resolve(groundMaterial);
    UMaterial* shadowMat = assets.Resolve(shadowMaterial);
    if (!groundMat || !shadowMat)
        return;

    // Projected shadows follow the first visible directional light; without one there's nothing to cast
    bool hasSun = false;
    for (const FRuntimeObject& object : objects)
    {
        if (object.light && object.desc->light.kind == ELightKind::Directional && object.current.visible)
        {
            Vec3 dir = object.root->GetWorldRotationInQuat().GetForward();
            dir.z = std::min(dir.z, -0.25);   // a sun at the horizon would throw endless shadows
            const double len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            shadowMat->SetVec3("u_ShadowDir", static_cast<float>(dir.x / len), static_cast<float>(dir.y / len), static_cast<float>(dir.z / len));
            hasSun = true;
            break;
        }
    }
    shadowMat->SetFloat("u_ShadowStrength", hasSun ? 0.62f : 0.0f);

    const double groundHeight = scene->environment.groundHeight;

    // Soft occlusion right under each hierarchy (one per top-level object), on the ground and its shadows
    struct FFootprint { double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30, bottom = 1e30; };
    std::map<size_t, FFootprint> footprints;

    for (const FRuntimeObject& object : objects)
    {
        if (!object.mesh || !object.desc->shadow || !object.current.visible)
            continue;

        // World-space box around the mesh's bounds
        const FTransform& world = object.mesh->GetWorldTransform();
        FFootprint& f = footprints[object.rootIndex];
        for (int corner = 0; corner < 8; ++corner)
        {
            const Vec3 local{ corner & 1 ? object.boundsMax.x : object.boundsMin.x,
                              corner & 2 ? object.boundsMax.y : object.boundsMin.y,
                              corner & 4 ? object.boundsMax.z : object.boundsMin.z };
            const Vec3 p = world.TransformPoint(local);
            f.minX = std::min(f.minX, p.x);
            f.maxX = std::max(f.maxX, p.x);
            f.minY = std::min(f.minY, p.y);
            f.maxY = std::max(f.maxY, p.y);
            f.bottom = std::min(f.bottom, p.z);
        }
    }

    int count = 0;
    for (const auto& [root, f] : footprints)
    {
        if (count >= kMaxContactShadows)
            break;

        const double radius = 0.5 * std::max(f.maxX - f.minX, f.maxY - f.minY);
        const double height = std::max(0.0, f.bottom - groundHeight);
        const double fade = std::clamp(1.0 - height / 0.35, 0.0, 1.0);   // only things touching the ground
        if (fade <= 0.0)
            continue;

        const std::string name = "u_Blobs[" + std::to_string(count) + "]";
        const float cx = static_cast<float>(0.5 * (f.minX + f.maxX));
        const float cy = static_cast<float>(0.5 * (f.minY + f.maxY));
        const float size = static_cast<float>(radius * 1.0);
        const float strength = static_cast<float>(0.3 * fade);
        groundMat->SetVec4(name, cx, cy, size, strength);
        shadowMat->SetVec4(name, cx, cy, size, strength);
        ++count;
    }
    groundMat->SetInt("u_BlobCount", count);
    shadowMat->SetInt("u_BlobCount", count);
}

}
