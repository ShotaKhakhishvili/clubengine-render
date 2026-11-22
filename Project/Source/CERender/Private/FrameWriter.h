#pragma once

#include <Core/ClubCore.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace CER
{

// One rendered moment: the frame number it's saved as and its time in seconds
struct FShot
{
    int frame = 0;
    double time = 0.0;
};

// Downsamples supersampled frames, writes them as PNGs and/or streams them into ffmpeg as an MP4,
// and builds a contact sheet with time labels.
class FrameWriter
{
public:
    // sheetAll: every shot goes on the sheet (moments); otherwise 6 evenly spaced ones
    void Setup(std::filesystem::path outDir, int width, int height, int supersample, std::vector<FShot> shots, bool writeFrames, bool sheetAll);

    // Starts ffmpeg reading raw RGBA frames from a pipe (not our stdout: the engine logs there)
    bool OpenVideo(const std::filesystem::path& ffmpeg, const std::filesystem::path& video, int fps, const std::string& codecArgs);

    bool AddFrame(size_t shot, const CE::TArray<CE::uint8>& pixels);
    bool Finish();

private:
    struct FThumb { size_t shot; std::vector<CE::uint8> pixels; };

    std::filesystem::path outDir;
    int width = 0;
    int height = 0;
    int supersample = 1;
    std::vector<FShot> shots;
    bool writeFrames = true;
    bool sheetAll = false;

    std::FILE* video = nullptr;
    std::filesystem::path videoPath;

    std::vector<size_t> sheetShots;
    std::vector<FThumb> thumbs;
    int thumbWidth = 0;
    int thumbHeight = 0;
};

}
