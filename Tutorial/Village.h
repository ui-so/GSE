#pragma once
#include "SceneGraph.h"
#include "WorldLayout.h"
#include <functional>
#include <vector>
#include <string>
namespace Village
{
constexpr int CitizenCount = WorldLayout::CitizenCount;
constexpr float MinX = WorldLayout::MinX, MaxX = WorldLayout::MaxX;
constexpr float MinY = WorldLayout::MinY, MaxY = WorldLayout::MaxY;
using Position = Scene::Position;
enum class Phase
{
    Daily,
    Warning,
    Evacuation,
    Stillness
};
enum class Reaction
{
    Leave,
    Family,
    Stay,
    Help,
    CloseShop,
    ShareBread
};
enum class Service
{
    None,
    Merchant,
    Cleric,
    Baker
};
struct Citizen
{
    Scene::ActorId actor = 0;
    Position home{}, goal{};
    Reaction reaction = Reaction::Leave;
    Service service = Service::None;
    std::vector<Position> route;
    size_t step = 0;
    float timer = 0, animation = 0;
    bool walking = false, frozen = false, recorded = false, gifted = false;
    int homeVillage = 0, destination = -1, circuit = 0;
    bool courier = false;
    bool escorting = false, rescued = false;
    unsigned rumors = 0;
};
struct Monster
{
    Scene::ActorId actor = 0;
    Position home{};
    int health = 36, kind = 0;
    float cooldown = 0, flash = 0;
    float respawn = 0, weakened = 0;
};
struct Loot
{
    Scene::ActorId actor = 0;
    bool collected = false;
    float age = 0;
};
struct VillageStats
{
    int residents = 20, present = 0, walking = 0, idle = 0, talking = 0, frozen = 0;
    unsigned rumors = 0;
    int deliveries = 0;
    float calm = 75, fear = 25, trust = 70, hope = 65;
    const char *mood = "calm";
};
struct RumorReceipt
{
    int village = 0, rumor = 0;
    Scene::ActorId carrier = 0;
    float time = 0;
    Position position{};
};
struct Companion
{
    Scene::ActorId actor = 0, target = 0;
    float time = 0, cooldown = 0, pulse = 0;
    bool wary = false;
};
class World
{
  public:
    void Initialize(Scene::SceneGraph &scene, const std::vector<Scene::ActorId> &citizens,
                    Scene::ActorId player, std::function<bool(Position)> blocked);
    void Update(float dt, int talking = -1);
    void AdvanceEvent();
    float DesaturationRadius() const;
    const char *DisasterKind() const;
    bool BeginEscort(int id);
    bool FamilyResolved() const;
    int RescuedCount() const;
    int Kills() const
    {
        return kills_;
    }
    void GrantReward(int coins, int experience);
    Scene::ActorId HuntTarget() const;
    bool CastSupport();
    const Companion &Familiar() const
    {
        return companion_;
    }
    void Talk(int id);
    bool Buy(int id);
    bool Heal(int id);
    bool Attack(Position facing);
    bool Pickup();
    bool UsePotion();
    void Respawn();
    void Sketch(Position position);
    bool IsFrozen(Position position) const;
    Phase GetPhase() const
    {
        return phase_;
    }
    const std::vector<Citizen> &Citizens() const
    {
        return citizens_;
    }
    const std::vector<Monster> &Monsters() const
    {
        return monsters_;
    }
    const std::vector<Loot> &Drops() const
    {
        return loot_;
    }
    const std::wstring &Notice() const
    {
        return notice_;
    }
    std::wstring Dialogue(int id) const;
    const wchar_t *Behavior(int id) const;
    const wchar_t *PhaseName() const;
    int Health() const
    {
        return health_;
    }
    int Coins() const
    {
        return coins_;
    }
    int Potions() const
    {
        return potions_;
    }
    int Experience() const
    {
        return experience_;
    }
    int RecordCount() const;
    const std::array<VillageStats, WorldLayout::VillageCount> &Statistics() const
    {
        return statistics_;
    }
    const std::vector<RumorReceipt> &Receipts() const
    {
        return receipts_;
    }
    int CurrentVillage() const
    {
        return WorldLayout::VillageAt(GetPosition(player_));
    }
    int Respawns() const
    {
        return respawns_;
    }
    static const wchar_t *RumorName(int rumor);
    bool LandscapeRecorded() const
    {
        return landscape_;
    }
    bool StoryRecorded() const
    {
        return story_;
    }
    bool IsDead() const
    {
        return health_ <= 0;
    }
    float AttackTime() const
    {
        return attackAnimation_;
    }
    bool RunTests(std::string &report);

  private:
    Scene::SceneGraph *scene_ = nullptr;
    Scene::ActorId player_ = 0, monstersGroup_ = 0;
    std::function<bool(Position)> blocked_;
    std::vector<Citizen> citizens_;
    std::vector<Monster> monsters_;
    std::vector<Loot> loot_;
    std::vector<unsigned char> navigation_, edges_;
    std::vector<Position> spawnSites_;
    std::array<VillageStats, WorldLayout::VillageCount> statistics_{};
    std::vector<RumorReceipt> receipts_;
    float time_ = 0, statisticsTimer_ = 0;
    int respawns_ = 0, kills_ = 0;
    Companion companion_{};
    size_t spawnCursor_ = 0;
    Phase phase_ = Phase::Daily;
    float phaseTime_ = 0, attackCooldown_ = 0, attackAnimation_ = 0, immunity_ = 0;
    int health_ = 100, coins_ = 20, potions_ = 2, experience_ = 0;
    bool landscape_ = false, story_ = false;
    std::wstring notice_;
    Position GetPosition(Scene::ActorId id) const;
    bool Active(Scene::ActorId id) const;
    void SetPhase(Phase phase);
    void Plan(Citizen &citizen, Position target);
    void Move(Scene::ActorId id, Position goal, float amount, bool wilderness = false);
    bool ChooseSpawn(Position previous, Position &result, bool initial);
    void AddLoot(Position position);
    void ExchangeRumors(Citizen &citizen);
    void RebuildStatistics(int talking);
    void UpdateCompanion(float dt, int talking);
};
} // namespace Village
