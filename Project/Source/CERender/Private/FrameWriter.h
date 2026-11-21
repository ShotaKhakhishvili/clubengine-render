#pragma once

#include <Core/ClubCore.h>

#include <filesystem>
#include <vector>

namespace CER
{

// Downsamples supersampled frames, writes them as PNGs and builds a contact sheet.
class FrameWriter
{
public:
    void Setup(std::filesystem::path outDir, int width, int height, int supersample, int frameCount, bool writeFrames);

    bool AddFrame(int frame, const CE::TArray<CE::uint8>& pixels);
    bool Finish();

    int GetFramesWritten() const { return framesWritten; }

private:
    struct FThumb { int frame; std::vector<CE::uint8> pixels; };

    std::filesystem::path outDir;
    int width = 0;
    int height = 0;
    int supersample = 1;
    int frameCount = 1;
    bool writeFrames = true;
    int framesWritten = 0;

    std::vector<int> sheetFrames;
    std::vector<FThumb> thumbs;
    int thumbWidth = 0;
    int thumbHeight = 0;
};

}
