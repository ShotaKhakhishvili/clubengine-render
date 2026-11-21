#include "SceneRuntime.h"
#include "Easing.h"

#include <Asset/ImportAssetBuilder.h>
#include <Asset/UMaterial.h>
#include <Asset/UShader.h>
#include <Asset/UStaticMesh.h>
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
            const TObjectHandle<UStaticMesh> mesh = LoadMesh(desc);
            UStaticMesh* meshAsset = assets.Resolve(mesh);
            if (!meshAsset)
            {
                errors.push_back("objects \"" + desc.id + "\": failed to load mesh " + desc.mesh);
                continue;
            }

            auto* actor = world->SpawnActor<AStaticMeshActor>(identity);
            object.actor = actor;
            object.mesh = actor->GetStaticMeshComponent();
            object.mesh->SetStaticMesh(mesh);

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

    return true;
}

TObjectHandle<UStaticMesh> SceneRuntime::LoadMesh(const FObjectDesc& desc)
{
    return ImportStaticMesh(desc.meshIsBuiltin ? BuiltinMeshPath(desc.mesh) : std::filesystem::path(desc.mesh));
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

void SceneRuntime::ApplyTrack(const FTrackDesc& track, double time)
{
    FState& state = objects[indexById.at(track.objectId)].current;

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
    }
}

Vec3 SceneRuntime::WorldPositionOf(const std::string& id) const
{
    const auto it = indexById.find(id);
    if (it == indexById.end() || !objects[it->second].root)
        return Vec3{};
    return objects[it->second].root->GetWorldLocation();
}

void SceneRuntime::Evaluate(double time)
{
    for (FRuntimeObject& object : objects)
        object.current = object.base;

    for (const FTrackDesc& track : scene->tracks)
        ApplyTrack(track, time);

    AssetRegistry& assets = GetAssetRegistry();

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

    // Color, light, visibility
    for (FRuntimeObject& object : objects)
    {
        const FState& s = object.current;

        if (object.shadowMesh)
        {
            object.shadowMesh->SetVisible(s.visible);
            object.shadowMesh->SetWorldTransform(object.root->GetWorldTransform());
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

        const Vec3 p = object.root->GetWorldLocation();
        const Vec3 sc = object.root->GetWorldScale();
        const double half = 0.5 * std::max(std::abs(sc.x), std::abs(sc.y));

        FFootprint& f = footprints[object.rootIndex];
        f.minX = std::min(f.minX, p.x - half);
        f.maxX = std::max(f.maxX, p.x + half);
        f.minY = std::min(f.minY, p.y - half);
        f.maxY = std::max(f.maxY, p.y + half);
        f.bottom = std::min(f.bottom, p.z - 0.5 * std::abs(sc.z));
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
