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
struct Progression
{
    bool enabled = false, supplies = false, wary = false, repeatable = false;
    int step = 0, steps = 17, contracts = 0, kills = 0, rewardCoins = 0, rescued = 0;
    std::string stage, disaster;
    std::wstring objective;
};
std::string Serialize(const std::vector<Region> &regions, const std::string &activeWorld, bool paused,
                      const std::string &session, std::uint64_t sequence,
                      const Progression &progression = {});
void Initialize(bool enabled);
bool Due();
void Publish(const std::vector<Region> &regions, const std::string &activeWorld, bool paused,
             const Progression &progression = {});
void Shutdown();
bool RunTests(std::string &report);
} // namespace Monitor
