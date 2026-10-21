#pragma once
#include "SceneGraph.h"
#include <array>
#include <string>
namespace WorldLayout
{
constexpr int VillageCount = 5, ResidentsPerVillage = 20, CitizenCount = 100, MonsterCount = 100;
constexpr int MapWidth = 189, MapHeight = 189;
constexpr float MinX = -94.5f, MaxX = 94.5f, MinY = -94.5f, MaxY = 94.5f;
constexpr float WalkingSpeed = 3.1f, VillageSpacing = 62.f, RespawnSeconds = 3.f;
constexpr int Mara = 0, Baker = 1, Merchant = 5, Cleric = 16, Mother = 17, Child = 18, Keeper = 19;
struct Settlement
{
    const char *id;
    const wchar_t *name;
    Scene::Position center;
};
const std::array<Settlement, VillageCount> &Settlements();
Scene::Position Plaza(int village);
int VillageAt(Scene::Position position);
float RoadDistance(Scene::Position position);
bool IsRoad(Scene::Position position);
bool RunTests(std::string &report);
} // namespace WorldLayout
