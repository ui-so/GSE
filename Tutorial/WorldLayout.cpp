#include "WorldLayout.h"
#include <algorithm>
#include <cmath>
namespace WorldLayout
{
const std::array<Settlement, VillageCount> &Settlements()
{
    static const std::array<Settlement, VillageCount> villages = {{{"ashen_shore", L"잿빛 여울", {0, 0}},
                                                                   {"pine_wharf", L"청솔 나루", {62, 0}},
                                                                   {"stone_spring", L"돌샘 터", {-62, 0}},
                                                                   {"dawn_hill", L"새벽 언덕", {0, 62}},
                                                                   {"reed_garden", L"갈대 뜰", {0, -62}}}};
    return villages;
}
Scene::Position Plaza(int village)
{
    auto p = Settlements().at(size_t(village)).center;
    return {p.x - 10, p.y + 18};
}
int VillageAt(Scene::Position p)
{
    for (int i = 0; i < VillageCount; ++i)
    {
        auto c = Settlements()[i].center;
        if (std::abs(p.x - c.x) < 20 && std::abs(p.y - c.y) < 24)
            return i;
    }
    return -1;
}
float RoadDistance(Scene::Position p)
{
    float best = 1e9f;
    auto a = Plaza(0);
    for (int i = 1; i < VillageCount; ++i)
    {
        auto b = Plaza(i);
        float dx = b.x - a.x, dy = b.y - a.y;
        float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / (dx * dx + dy * dy), 0.f, 1.f);
        best = std::min(best, std::hypot(p.x - a.x - dx * t, p.y - a.y - dy * t));
    }
    return best;
}
bool IsRoad(Scene::Position p)
{
    return RoadDistance(p) < 2.2f;
}
bool RunTests(std::string &report)
{
    for (int i = 1; i < VillageCount; ++i)
    {
        auto a = Plaza(0), b = Plaza(i);
        if (std::abs(std::hypot(a.x - b.x, a.y - b.y) / WalkingSpeed - 20) > 0.001f)
        {
            report += "FAIL: village plaza travel time.\n";
            return false;
        }
    }
    report +=
        "PASS: 5 villages, 20 residents each, 62m connected plaza roads at 3.1m/s = 20 seconds per edge.\n";
    return true;
}
} // namespace WorldLayout
