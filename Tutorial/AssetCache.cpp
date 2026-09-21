#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "AssetCache.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <system_error>
namespace AssetCache
{
namespace
{
std::filesystem::path directory;
Statistics statistics;
struct Header
{
    std::array<char, 8> magic;
    std::uint32_t version;
    std::uint32_t length;
    std::uint64_t checksum;
};
constexpr std::array<char, 8> Magic = {'G', 'S', 'E', 'C', 'A', 'C', 'H', 'E'};
constexpr std::uint32_t Version = 4;
constexpr std::size_t MaximumBytes = 32 * 1024 * 1024;
std::uint64_t Hash(const Bytes &data)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (auto byte : data)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}
} // namespace
void Initialize()
{
    wchar_t executable[32768]{};
    DWORD count = GetModuleFileNameW(nullptr, executable, 32768);
    directory = count > 0 && count < 32768 ? std::filesystem::path(executable).parent_path() / L"cache"
                                           : std::filesystem::current_path() / L"cache";
    statistics = {};
}
Bytes LoadOrCreate(const std::string &name, std::size_t expectedSize, const std::function<Bytes()> &generate)
{
    if (directory.empty())
        Initialize();
    const auto path = directory / (name + "-v4.bin");
    Header header{};
    std::ifstream input(path, std::ios::binary);
    if (input.read(reinterpret_cast<char *>(&header), sizeof(header)) && header.magic == Magic &&
        header.version == Version && header.length <= MaximumBytes &&
        (expectedSize == 0 || header.length == expectedSize))
    {
        Bytes data(header.length);
        if (input.read(reinterpret_cast<char *>(data.data()), header.length) &&
            input.peek() == std::char_traits<char>::eof() && Hash(data) == header.checksum)
        {
            statistics.loaded++;
            return data;
        }
    }
    input.close();
    Bytes data = generate();
    statistics.generated++;
    if (data.size() > MaximumBytes || (expectedSize != 0 && data.size() != expectedSize))
    {
        statistics.writeFailures++;
        return data;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        statistics.writeFailures++;
        return data;
    }
    Header output{Magic, Version, static_cast<std::uint32_t>(data.size()), Hash(data)};
    const auto temporary = directory / (name + "-v4.tmp");
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(&output), sizeof(output));
    file.write(reinterpret_cast<const char *>(data.data()), data.size());
    file.flush();
    bool wrote = bool(file);
    file.close();
    if (!wrote ||
        !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        statistics.writeFailures++;
    return data;
}
const Statistics &Stats()
{
    return statistics;
}
bool RunTests(std::string &report)
{
    auto savedDirectory = directory;
    auto savedStatistics = statistics;
    directory =
        std::filesystem::current_path() / (L"asset-cache-test-" + std::to_wstring(GetCurrentProcessId()) +
                                           L"-" + std::to_wstring(GetTickCount64()));
    statistics = {};
    int calls = 0;
    auto generate = [&]() {
        calls++;
        return Bytes{1, 2, 3, 4};
    };
    auto a = LoadOrCreate("probe", 4, generate);
    auto b = LoadOrCreate("probe", 4, generate);
    bool okay = a == b && calls == 1 && statistics.loaded == 1;
    {
        std::ofstream corrupt(directory / "probe-v4.bin", std::ios::binary | std::ios::trunc);
        corrupt << "broken";
    }
    auto c = LoadOrCreate("probe", 4, generate);
    okay = okay && c == a && calls == 2 && statistics.writeFailures == 0;
    directory = savedDirectory;
    statistics = savedStatistics;
    report += okay ? "PASS: cache miss writes, second load skips generation, corrupt cache regenerates.\n"
                   : "FAIL: model/texture cache\n";
    return okay;
}
} // namespace AssetCache
