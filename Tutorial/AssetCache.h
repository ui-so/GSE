#pragma once
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace AssetCache
{
using Bytes = std::vector<unsigned char>;
struct Statistics
{
    int loaded = 0;
    int generated = 0;
    int writeFailures = 0;
};
void Initialize();
Bytes LoadOrCreate(const std::string &name, std::size_t expectedSize, const std::function<Bytes()> &generate);
const Statistics &Stats();
bool RunTests(std::string &report);
} // namespace AssetCache
