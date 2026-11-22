// Renders one scene file to PNG frames.
//
//   CERender <scene.json> --out <dir> [--ss <n>] [--size WxH] [--times t0,t1,...] [--sheet-only] -OfflineRender.Width=... (set by cerender.py)
//
// --times renders only those moments (seconds), in order; without it every frame of the clip.
// --sheet-only skips the frame PNGs: with --times every moment goes on the contact sheet, labeled with its time.
// --video <file.mp4> --ffmpeg <exe> --vcodec "<ffmpeg codec args>" streams the frames into ffmpeg as they render;
// --no-frames then skips the PNGs (the contact sheet is still written).
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

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
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
        if (!GetSystem<RenderSystem>()->ReadSceneColor(pixels) || !writer.AddFrame(shot, pixels))
            return 3;

        ++shot;
        if (shot < shots.size())
        {
            runtime.Evaluate(shots[shot].time);
            return 0;
        }

        // The launcher reports what was written; this only fails loudly
        return writer.Finish() ? 0 : 3;
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
        std::string times, vcodec;
        std::filesystem::path ffmpeg, video;
        bool noFrames = false;

        const auto& args = CommandLine::GetArgs();
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--out" && i + 1 < args.size())             outDir = args[++i];
            else if (args[i] == "--ss" && i + 1 < args.size())         supersample = std::max(1, std::atoi(args[++i].c_str()));
            else if (args[i] == "--sheet-only")                        sheetOnly = true;
            else if (args[i] == "--size" && i + 1 < args.size())       std::sscanf(args[++i].c_str(), "%dx%d", &sizeOverrideW, &sizeOverrideH);
            else if (args[i] == "--times" && i + 1 < args.size())      times = args[++i];
            else if (args[i] == "--video" && i + 1 < args.size())      video = args[++i];
            else if (args[i] == "--ffmpeg" && i + 1 < args.size())     ffmpeg = args[++i];
            else if (args[i] == "--vcodec" && i + 1 < args.size())     vcodec = args[++i];
            else if (args[i] == "--no-frames")                         noFrames = true;
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

        // Chosen moments, or every frame of the clip
        const int fps = scene.output.fps;
        if (!times.empty())
        {
            std::stringstream list(times);
            for (std::string item; std::getline(list, item, ',');)
            {
                const double t = std::max(0.0, std::atof(item.c_str()));
                shots.push_back({ static_cast<int>(std::lround(t * fps)), t });
            }
        }
        else
            for (int i = 0; i < scene.output.FrameCount(); ++i)
                shots.push_back({ i, i / static_cast<double>(fps) });

        if (shots.empty())
        {
            std::cerr << "CERENDER ERROR: no times to render\n";
            return false;
        }

        // Single frames always get written, animations can skip straight to the contact sheet
        const bool writeFrames = (!sheetOnly && !(noFrames && !video.empty())) || (times.empty() && shots.size() == 1);
        writer.Setup(outDir, scene.output.width, scene.output.height, supersample, shots, writeFrames, sheetOnly && !times.empty());
        if (!video.empty() && !writer.OpenVideo(ffmpeg.empty() ? std::filesystem::path("ffmpeg") : ffmpeg, video, fps, vcodec))
            return false;
        runtime.Evaluate(shots[0].time);
        return true;
    }

    CER::FSceneDesc scene;
    CER::SceneRuntime runtime;
    CER::FrameWriter writer;
    std::filesystem::path outDir;
    std::vector<CER::FShot> shots;
    size_t shot = 0;
    bool failed = false;
};

namespace CE
{
Application* CreateApplication() { return new CERenderApp(); }
}
