#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace Monitor
{
struct Npc
{
    std::uint64_t actor = 0;
    std::wstring name, behavior;
    std::string reaction, service, state;
    float x = 0, y = 0;
    bool active = false, recorded = false;
};
struct Region
{
    std::string id, kind, phase;
    std::wstring name;
    bool simulated = false;
    int monstersAlive = 0, monstersTotal = 0, wildlife = 0;
    std::vector<Npc> npcs;
};
std::string Serialize(const std::vector<Region> &regions, const std::string &activeWorld, bool paused,
                      const std::string &session, std::uint64_t sequence);
void Initialize(bool enabled);
bool Due();
void Publish(const std::vector<Region> &regions, const std::string &activeWorld, bool paused);
void Shutdown();
bool RunTests(std::string &report);
} // namespace Monitor
