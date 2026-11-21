#pragma once

#include "SceneDesc.h"

#include <CoreUObject/TObjectHandle.h>

#include <map>
#include <string>
#include <vector>

namespace CE
{
class UWorld;
class AActor;
class USceneComponent;
class UStaticMeshComponent;
class ULightComponent;
class UMaterial;
class UShader;
class UStaticMesh;
}

namespace CER
{

// Spawns the actors for a scene and moves them to the state at any time t.
class SceneRuntime
{
public:
    bool Build(CE::UWorld& world, const FSceneDesc& scene, std::vector<std::string>& errors);
    void Evaluate(double time);

private:
    struct FState
    {
        Vec3 position, rotation, scale, color;
        double intensity = 1.0;
        bool visible = true;
        FLookAt lookAt;
    };

    struct FRuntimeObject
    {
        const FObjectDesc* desc = nullptr;
        CE::AActor* actor = nullptr;
        CE::USceneComponent* root = nullptr;
        CE::UStaticMeshComponent* mesh = nullptr;
        CE::ULightComponent* light = nullptr;
        CE::TObjectHandle<CE::UMaterial> material;     // own instance, so color can animate per object
        FState base;
        FState current;
        int depth = 0;                                 // parent chain length, parents update first
        CE::UStaticMeshComponent* shadowMesh = nullptr; // copy of the mesh, flattened onto the ground by the shader
        size_t rootIndex = 0;                          // top of the parent chain; one contact shadow per root
    };

    CE::TObjectHandle<CE::UMaterial> MakeMaterial(const FMaterialDesc& desc, int mode);
    void ApplyEnvironment(CE::UMaterial& material) const;
    CE::TObjectHandle<CE::UStaticMesh> LoadMesh(const FObjectDesc& desc);
    CE::UStaticMeshComponent* SpawnMeshActor(CE::TObjectHandle<CE::UStaticMesh> mesh, CE::TObjectHandle<CE::UMaterial> material);

    void ApplyTrack(const FTrackDesc& track, double time);
    void UpdateContactShadows();
    Vec3 WorldPositionOf(const std::string& id) const;

    const FSceneDesc* scene = nullptr;
    CE::UWorld* world = nullptr;
    CE::TObjectHandle<CE::UShader> shader;

    std::vector<FRuntimeObject> objects;               // sorted parents first
    std::map<std::string, size_t> indexById;

    CE::UStaticMeshComponent* sky = nullptr;
    CE::UStaticMeshComponent* ground = nullptr;
    CE::TObjectHandle<CE::UMaterial> groundMaterial;
    CE::TObjectHandle<CE::UMaterial> shadowMaterial;
    FRuntimeObject* camera = nullptr;
};

}
