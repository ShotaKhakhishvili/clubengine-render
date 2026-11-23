#pragma once

#include "Emitter.h"
#include "SceneDesc.h"

#include <CoreUObject/TObjectHandle.h>

#include <map>
#include <memory>
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
        CE::UStaticMeshComponent* mesh = nullptr;      // the root, or its child when the pivot moves the mesh
        CE::ULightComponent* light = nullptr;
        CE::TObjectHandle<CE::UMaterial> material;     // own instance, so color can animate per object
        FState base;
        FState current;
        int depth = 0;                                 // parent chain length, parents update first
        CE::UStaticMeshComponent* shadowMesh = nullptr; // copy of the mesh, flattened onto the ground by the shader
        size_t rootIndex = 0;                          // top of the parent chain; one contact shadow per root
        Vec3 boundsMin, boundsMax;                     // mesh bounds in the mesh's own space
        int emitter = -1;                              // index into emitters
    };

    // An emitter's instances: plain mesh components under the emitter's root, placed by its field
    struct FEmitterRuntime
    {
        size_t object = 0;
        std::unique_ptr<EmitterField> field;
        std::vector<CE::UStaticMeshComponent*> meshes;  // one per instance
        std::vector<CE::UStaticMeshComponent*> shadows; // one per instance, when they cast shadows
        Vec3 pivotOffset;
        bool visible = true;
    };

    struct FMesh
    {
        CE::TObjectHandle<CE::UStaticMesh> handle;
        Vec3 boundsMin, boundsMax;
    };

    // A path sampled into a polyline with distances, for constant-speed travel
    struct FPathCache
    {
        std::vector<Vec3> points;
        std::vector<double> distance;
    };

    CE::TObjectHandle<CE::UMaterial> MakeMaterial(const FMaterialDesc& desc, int mode);
    void ApplyEnvironment(CE::UMaterial& material) const;
    FMesh LoadMesh(const FObjectDesc& desc);
    CE::UStaticMeshComponent* SpawnMeshActor(CE::TObjectHandle<CE::UStaticMesh> mesh, CE::TObjectHandle<CE::UMaterial> material);
    void UseOwnMaterials(CE::UStaticMesh& mesh);
    void BuildEmitter(size_t index, std::vector<std::string>& errors);
    void UpdateEmitter(FEmitterRuntime& emitter, double time, bool placeAll);

    void Pose(double time, bool withFollow);
    void ApplyTrack(size_t index, double time);
    Vec3 SampleFollow(const FTrackDesc& track, double time);
    void ApplyPath(size_t index, double time, Vec3& point, Vec3* faceRotation);
    void ApplyBounce(const FTrackDesc& track, double time, FState& state);
    void ApplyWalk(const FTrackDesc& track, double time);
    void UpdateContactShadows();
    Vec3 WorldPositionOf(const std::string& id) const;

    const FSceneDesc* scene = nullptr;
    CE::UWorld* world = nullptr;
    CE::TObjectHandle<CE::UShader> shader;

    std::vector<FRuntimeObject> objects;               // sorted parents first
    std::map<std::string, size_t> indexById;
    std::map<std::string, FMesh> meshCache;
    std::vector<FPathCache> pathCaches;               // per track, filled for path tracks
    std::vector<Vec3> followValues;                    // per track, the smoothed target of follow tracks
    std::vector<FEmitterRuntime> emitters;

    CE::UStaticMeshComponent* sky = nullptr;
    CE::UStaticMeshComponent* ground = nullptr;
    CE::TObjectHandle<CE::UMaterial> groundMaterial;
    CE::TObjectHandle<CE::UMaterial> shadowMaterial;
    FRuntimeObject* camera = nullptr;
};

}
