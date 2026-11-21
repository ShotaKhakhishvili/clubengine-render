#include "FrameWriter.h"

#include <AssetImport/Image.h>

#include <algorithm>
#include <format>
#include <iostream>

namespace CER
{

namespace
{
constexpr int kSheetFrames = 6;
constexpr int kSheetColumns = 3;
constexpr int kThumbMaxSide = 480;
constexpr int kGap = 6;
}

void FrameWriter::Setup(std::filesystem::path inOutDir, int inWidth, int inHeight, int inSupersample, int inFrameCount, bool inWriteFrames)
{
    outDir = std::move(inOutDir);
    width = inWidth;
    height = inHeight;
    supersample = std::max(1, inSupersample);
    frameCount = inFrameCount;
    writeFrames = inWriteFrames;

    // Evenly spaced frames, always including the first and the last
    const int count = std::min(kSheetFrames, frameCount);
    for (int i = 0; i < count; ++i)
        sheetFrames.push_back(count == 1 ? 0 : static_cast<int>(std::lround(i * (frameCount - 1) / double(count - 1))));

    const double thumbScale = std::min(1.0, kThumbMaxSide / double(std::max(width, height)));
    thumbWidth = std::max(1, static_cast<int>(width * thumbScale));
    thumbHeight = std::max(1, static_cast<int>(height * thumbScale));

    std::filesystem::create_directories(outDir);
}

bool FrameWriter::AddFrame(int frame, const CE::TArray<CE::uint8>& source)
{
    const int sourceWidth = width * supersample;
    const int sourceHeight = height * supersample;
    if (source.size() != static_cast<size_t>(sourceWidth) * sourceHeight * 4)
    {
        std::cerr << "CERENDER: frame " << frame << " has an unexpected size\n";
        return false;
    }

    // Box filter: each output pixel averages a supersample x supersample block
    std::vector<CE::uint8> pixels(static_cast<size_t>(width) * height * 4);
    const int area = supersample * supersample;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int sum[3] = { 0, 0, 0 };
            for (int sy = 0; sy < supersample; ++sy)
            {
                const CE::uint8* row = source.data() + (static_cast<size_t>(y * supersample + sy) * sourceWidth + x * supersample) * 4;
                for (int sx = 0; sx < supersample; ++sx)
                    for (int c = 0; c < 3; ++c)
                        sum[c] += row[sx * 4 + c];
            }
            CE::uint8* out = pixels.data() + (static_cast<size_t>(y) * width + x) * 4;
            for (int c = 0; c < 3; ++c)
                out[c] = static_cast<CE::uint8>(sum[c] / area);
            out[3] = 255;
        }
    }

    if (writeFrames)
    {
        const auto path = outDir / std::format("frame_{:04}.png", frame);
        if (!CE_STB::SavePNG(path, pixels.data(), width, height, 4))
        {
            std::cerr << "CERENDER: failed to write " << path.string() << "\n";
            return false;
        }
        ++framesWritten;
    }

    if (std::find(sheetFrames.begin(), sheetFrames.end(), frame) != sheetFrames.end())
    {
        FThumb thumb{ frame, std::vector<CE::uint8>(static_cast<size_t>(thumbWidth) * thumbHeight * 4) };
        for (int y = 0; y < thumbHeight; ++y)
            for (int x = 0; x < thumbWidth; ++x)
            {
                const int sx = std::min(width - 1, x * width / thumbWidth);
                const int sy = std::min(height - 1, y * height / thumbHeight);
                std::copy_n(pixels.data() + (static_cast<size_t>(sy) * width + sx) * 4, 4,
                            thumb.pixels.data() + (static_cast<size_t>(y) * thumbWidth + x) * 4);
            }
        thumbs.push_back(std::move(thumb));
    }

    return true;
}

bool FrameWriter::Finish()
{
    if (thumbs.size() < 2)
        return true;

    const int columns = std::min<int>(kSheetColumns, static_cast<int>(thumbs.size()));
    const int rows = (static_cast<int>(thumbs.size()) + columns - 1) / columns;
    const int sheetWidth = columns * thumbWidth + (columns + 1) * kGap;
    const int sheetHeight = rows * thumbHeight + (rows + 1) * kGap;

    std::vector<CE::uint8> sheet(static_cast<size_t>(sheetWidth) * sheetHeight * 4);
    for (size_t i = 0; i < sheet.size(); i += 4)
    {
        sheet[i] = sheet[i + 1] = sheet[i + 2] = 24;
        sheet[i + 3] = 255;
    }

    for (size_t i = 0; i < thumbs.size(); ++i)
    {
        const int ox = kGap + static_cast<int>(i % columns) * (thumbWidth + kGap);
        const int oy = kGap + static_cast<int>(i / columns) * (thumbHeight + kGap);
        for (int y = 0; y < thumbHeight; ++y)
            std::copy_n(thumbs[i].pixels.data() + static_cast<size_t>(y) * thumbWidth * 4, thumbWidth * 4,
                        sheet.data() + (static_cast<size_t>(oy + y) * sheetWidth + ox) * 4);
    }

    const auto path = outDir / "contact_sheet.png";
    if (!CE_STB::SavePNG(path, sheet.data(), sheetWidth, sheetHeight, 4))
    {
        std::cerr << "CERENDER: failed to write " << path.string() << "\n";
        return false;
    }

    std::cout << "CERENDER: contact sheet frames";
    for (const FThumb& t : thumbs) std::cout << " " << t.frame;
    std::cout << " (left to right, top to bottom)\n";
    return true;
}

}
