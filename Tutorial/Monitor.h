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
    std::string reaction, service, state, homeVillage, currentVillage, destination;
    bool courier = false;
    unsigned rumors = 0;
    float x = 0, y = 0;
    bool active = false, recorded = false;
};
struct Delivery
{
    std::uint64_t carrier = 0;
    int rumor = 0;
    float time = 0, x = 0, y = 0;
};
struct Region
{
    std::string id, kind, phase;
    std::wstring name;
    bool simulated = false;
    int monstersAlive = 0, monstersTotal = 0, wildlife = 0;
    std::vector<Npc> npcs;
    bool hasAtmosphere = false;
    int present = 0, walking = 0, idle = 0, talking = 0, frozen = 0, respawns = 0;
    unsigned rumors = 0;
    float calm = 0, fear = 0, trust = 0, hope = 0;
    std::string mood;
    std::vector<Delivery> deliveries;
};
std::string Serialize(const std::vector<Region> &regions, const std::string &activeWorld, bool paused,
                      const std::string &session, std::uint64_t sequence);
void Initialize(bool enabled);
bool Due();
void Publish(const std::vector<Region> &regions, const std::string &activeWorld, bool paused);
void Shutdown();
bool RunTests(std::string &report);
} // namespace Monitor
