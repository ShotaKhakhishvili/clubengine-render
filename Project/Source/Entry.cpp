// The project root is resolved at runtime (the folder above the exe's bin/),
// so the same binary works from a build folder or from the unpacked release zip.
#include <filesystem>
#include <iterator>
#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

namespace
{
std::filesystem::path ResolveProjectRoot()
{
    std::error_code ec;
#ifdef _WIN32
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    const std::filesystem::path exe(std::wstring(buffer, length));
#else
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
#endif
    if (!ec && exe.has_parent_path())
        return exe.parent_path().parent_path();

    return std::filesystem::current_path(ec);
}
}

#define CE_PROJECT_ROOT ResolveProjectRoot()
#include <Launch/EntryPoint.h>
