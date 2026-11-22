#include "FrameWriter.h"

#include <AssetImport/Image.h>

#include <algorithm>
#include <csignal>
#include <format>
#include <iostream>
#include <string>

namespace CER
{

namespace
{
constexpr int kSheetFrames = 6;
constexpr int kGap = 4;
constexpr int kFontScale = 2;

// 5x7 bitmap glyphs for the time labels, one row per byte (bit 4 = left column)
struct FGlyph { char c; CE::uint8 rows[7]; };
constexpr FGlyph kFont[] = {
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { 's', { 0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
};

// White text on a dark box in the top-left corner of an RGBA image
void DrawLabel(std::vector<CE::uint8>& image, int imageWidth, int imageHeight, const std::string& text)
{
    const int advance = 6 * kFontScale, pad = 3;
    const int boxW = std::min(imageWidth, static_cast<int>(text.size()) * advance - kFontScale + 2 * pad);
    const int boxH = std::min(imageHeight, 7 * kFontScale + 2 * pad);

    for (int y = 0; y < boxH; ++y)
        for (int x = 0; x < boxW; ++x)
        {
            CE::uint8* p = image.data() + (static_cast<size_t>(y) * imageWidth + x) * 4;
            for (int c = 0; c < 3; ++c) p[c] = static_cast<CE::uint8>(p[c] * 0.35);
        }

    for (size_t i = 0; i < text.size(); ++i)
    {
        const auto glyph = std::find_if(std::begin(kFont), std::end(kFont), [&](const FGlyph& g) { return g.c == text[i]; });
        if (glyph == std::end(kFont))
            continue;
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
            {
                if (!(glyph->rows[row] & (0x10 >> col)))
                    continue;
                for (int sy = 0; sy < kFontScale; ++sy)
                    for (int sx = 0; sx < kFontScale; ++sx)
                    {
                        const int x = pad + static_cast<int>(i) * advance + col * kFontScale + sx;
                        const int y = pad + row * kFontScale + sy;
                        if (x >= imageWidth || y >= imageHeight) continue;
                        CE::uint8* p = image.data() + (static_cast<size_t>(y) * imageWidth + x) * 4;
                        p[0] = p[1] = p[2] = 255;
                    }
            }
    }
}
}

namespace
{
#ifdef _WIN32
std::FILE* OpenPipe(const std::string& command) { return _popen(command.c_str(), "wb"); }
int ClosePipe(std::FILE* pipe) { return _pclose(pipe); }
#else
std::FILE* OpenPipe(const std::string& command) { return popen(command.c_str(), "w"); }
int ClosePipe(std::FILE* pipe) { return pclose(pipe); }
#endif

std::string Quote(const std::filesystem::path& path) { return "\"" + path.string() + "\""; }
}

bool FrameWriter::OpenVideo(const std::filesystem::path& ffmpeg, const std::filesystem::path& path, int fps, const std::string& codecArgs)
{
#ifndef _WIN32
    // If ffmpeg dies, writes fail with an error instead of killing us with SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::remove(path, ec);

    std::string command = Quote(ffmpeg) + " -y -loglevel error -f rawvideo -pix_fmt rgba"
        + std::format(" -s {}x{} -r {} -i -", width, height, fps)
        + " -vf \"pad=ceil(iw/2)*2:ceil(ih/2)*2\" " + codecArgs
        + " -pix_fmt yuv420p -movflags +faststart " + Quote(path);
#ifdef _WIN32
    command = "\"" + command + "\"";   // cmd.exe strips one outer pair of quotes
#endif

    video = OpenPipe(command);
    if (!video)
    {
        std::cerr << "CERENDER ERROR: couldn't start ffmpeg (" << ffmpeg.string() << ")\n";
        return false;
    }
    videoPath = path;
    return true;
}

void FrameWriter::Setup(std::filesystem::path inOutDir, int inWidth, int inHeight, int inSupersample, std::vector<FShot> inShots, bool inWriteFrames, bool inSheetAll)
{
    outDir = std::move(inOutDir);
    width = inWidth;
    height = inHeight;
    supersample = std::max(1, inSupersample);
    shots = std::move(inShots);
    writeFrames = inWriteFrames;
    sheetAll = inSheetAll;

    // All moments, or evenly spaced shots that always include the first and the last
    const int total = static_cast<int>(shots.size());
    const int count = sheetAll ? total : std::min(kSheetFrames, total);
    for (int i = 0; i < count; ++i)
        sheetShots.push_back(sheetAll || count == 1 ? i : static_cast<size_t>(std::lround(i * (total - 1) / double(count - 1))));

    // Small tiles keep the sheet cheap to look at; render a single frame (--frame) to see details
    const int maxSide = count <= kSheetFrames ? 240 : (count <= 16 ? 200 : 160);
    const double thumbScale = std::min(1.0, maxSide / double(std::max(width, height)));
    thumbWidth = std::max(1, static_cast<int>(width * thumbScale));
    thumbHeight = std::max(1, static_cast<int>(height * thumbScale));

    std::filesystem::create_directories(outDir);
}

bool FrameWriter::AddFrame(size_t shot, const CE::TArray<CE::uint8>& source)
{
    const int sourceWidth = width * supersample;
    const int sourceHeight = height * supersample;
    if (source.size() != static_cast<size_t>(sourceWidth) * sourceHeight * 4)
    {
        std::cerr << "CERENDER ERROR: frame " << shots[shot].frame << " has an unexpected size\n";
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

    if (video && std::fwrite(pixels.data(), 1, pixels.size(), video) != pixels.size())
    {
        std::cerr << "CERENDER ERROR: ffmpeg stopped taking frames at frame " << shots[shot].frame << " (see its message above)\n";
        ClosePipe(video);
        video = nullptr;
        return false;
    }

    if (writeFrames)
    {
        const auto path = outDir / std::format("frame_{:04}.png", shots[shot].frame);
        if (!CE_STB::SavePNG(path, pixels.data(), width, height, 4))
        {
            std::cerr << "CERENDER ERROR: failed to write " << path.string() << "\n";
            return false;
        }
    }

    if (std::find(sheetShots.begin(), sheetShots.end(), shot) != sheetShots.end())
    {
        // Area average, so small tiles stay smooth
        FThumb thumb{ shot, std::vector<CE::uint8>(static_cast<size_t>(thumbWidth) * thumbHeight * 4) };
        for (int y = 0; y < thumbHeight; ++y)
        {
            const int y0 = y * height / thumbHeight, y1 = std::max(y0 + 1, (y + 1) * height / thumbHeight);
            for (int x = 0; x < thumbWidth; ++x)
            {
                const int x0 = x * width / thumbWidth, x1 = std::max(x0 + 1, (x + 1) * width / thumbWidth);
                int sum[3] = { 0, 0, 0 };
                for (int sy = y0; sy < y1; ++sy)
                    for (int sx = x0; sx < x1; ++sx)
                        for (int c = 0; c < 3; ++c)
                            sum[c] += pixels[(static_cast<size_t>(sy) * width + sx) * 4 + c];
                CE::uint8* out = thumb.pixels.data() + (static_cast<size_t>(y) * thumbWidth + x) * 4;
                for (int c = 0; c < 3; ++c) out[c] = static_cast<CE::uint8>(sum[c] / ((y1 - y0) * (x1 - x0)));
                out[3] = 255;
            }
        }
        DrawLabel(thumb.pixels, thumbWidth, thumbHeight, std::format("{:.2f}s", shots[shot].time));
        thumbs.push_back(std::move(thumb));
    }

    return true;
}

bool FrameWriter::Finish()
{
    if (video)
    {
        const int status = ClosePipe(video);
        video = nullptr;
        if (status != 0 || !std::filesystem::exists(videoPath))
        {
            std::cerr << "CERENDER ERROR: ffmpeg couldn't write " << videoPath.string() << "\n";
            return false;
        }
    }

    if (thumbs.size() < (sheetAll ? 1u : 2u))
        return true;

    const int n = static_cast<int>(thumbs.size());
    const int columns = n <= 3 ? n : (n == 4 ? 2 : (n <= 9 ? 3 : (n <= 16 ? 4 : 6)));
    const int rows = (n + columns - 1) / columns;
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
        std::cerr << "CERENDER ERROR: failed to write " << path.string() << "\n";
        return false;
    }
    return true;
}

}
