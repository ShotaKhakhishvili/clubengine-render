#pragma once

// Shapes generated at load time, so scenes need no extra asset files.
// All fit a 1 x 1 x 1 box centered on the origin, like the built-in .glb meshes,
// except lathe, which keeps the profile's own coordinates.

#include "SceneDesc.h"

#include <AssetImport/FMeshImportResult.h>

#include <string>

namespace CER
{

// Names that MakeShape builds (cube and plane always come from the engine's .glb files)
bool IsGeneratedShape(const std::string& name);

// True when the object needs a generated mesh: a generated shape, or a sphere/cylinder/cone with flat or detail
bool NeedsGeneratedMesh(const std::string& name, const FShapeDesc& shape);

CE::FMeshImportResult MakeShape(const std::string& name, const FShapeDesc& shape);

}
