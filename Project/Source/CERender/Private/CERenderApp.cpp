// Renders one scene file to PNG frames.
//
//   CERender <scene.json> --out <dir> [--ss <n>] [--size WxH] [--sheet-only] -OfflineRender.Width=... (set by cerender.py)
//
// The engine's offline loop drives the frames. This app moves the scene to each frame's time,
// reads the rendered image back and writes it (downsampled when supersampling).

#include "FrameWriter.h"
#include "SceneParser.h"
#include "SceneRuntime.h"

#include <Core/CommandLine.h>
#include <Engine/Application.h>
#include <Engine/Systems/RenderSystem.h>
#include <Engine/Systems/WorldSystem.h>

#include <cstdio>
#include <iostream>

using namespace CE;

class CERenderApp final : public Application
{
public:
    int Update(float) override
    {
        if (failed)
            return 2;

        TArray<uint8> pixels;
        if (!GetSystem<RenderSystem>()->ReadSceneColor(pixels) || !writer.AddFrame(frame, pixels))
            return 3;

        ++frame;
        if (frame < scene.output.FrameCount())
        {
            runtime.Evaluate(frame / static_cast<double>(scene.output.fps));
            return 0;
        }

        if (!writer.Finish())
            return 3;

        std::cout << "CERENDER: done, " << writer.GetFramesWritten() << " frame(s) in " << outDir.string() << "\n";
        return 0;
    }

protected:
    void PostInit() override
    {
        failed = !Load();
    }

private:
    bool Load()
    {
        std::filesystem::path scenePath;
        int supersample = 1;
        bool sheetOnly = false;
        int sizeOverrideW = 0, sizeOverrideH = 0;

        const auto& args = CommandLine::GetArgs();
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--out" && i + 1 < args.size())             outDir = args[++i];
            else if (args[i] == "--ss" && i + 1 < args.size())         supersample = std::max(1, std::atoi(args[++i].c_str()));
            else if (args[i] == "--sheet-only")                        sheetOnly = true;
            else if (args[i] == "--size" && i + 1 < args.size())       std::sscanf(args[++i].c_str(), "%dx%d", &sizeOverrideW, &sizeOverrideH);
            else if (!args[i].empty() && args[i][0] != '-' && scenePath.empty()) scenePath = args[i];
        }

        if (scenePath.empty() || outDir.empty())
        {
            std::cerr << "CERENDER ERROR: usage: CERender <scene.json> --out <dir> (run it through cerender.py)\n";
            return false;
        }

        CER::SceneParser parser;
        if (!parser.Parse(scenePath, scene))
        {
            std::cerr << "CERENDER ERROR: " << parser.GetErrors().size() << " problem(s) in " << scenePath.string() << "\n";
            for (const auto& e : parser.GetErrors())
                std::cerr << "  - " << e << "\n";
            return false;
        }

        if (sizeOverrideW > 0 && sizeOverrideH > 0)
        {
            scene.output.width = sizeOverrideW;
            scene.output.height = sizeOverrideH;
        }

        std::vector<std::string> errors;
        if (!runtime.Build(GetSystem<WorldSystem>()->GetWorld(), scene, errors))
        {
            std::cerr << "CERENDER ERROR: failed to build the scene\n";
            for (const auto& e : errors)
                std::cerr << "  - " << e << "\n";
            return false;
        }

        // Single frames always get written, animations can skip straight to the contact sheet
        const int frames = scene.output.FrameCount();
        writer.Setup(outDir, scene.output.width, scene.output.height, supersample, frames, !sheetOnly || frames == 1);
        runtime.Evaluate(0.0);
        return true;
    }

    CER::FSceneDesc scene;
    CER::SceneRuntime runtime;
    CER::FrameWriter writer;
    std::filesystem::path outDir;
    int frame = 0;
    bool failed = false;
};

namespace CE
{
Application* CreateApplication() { return new CERenderApp(); }
}
