#pragma once
#include "SceneGraph.h"
#include <functional>
#include <vector>
#include <string>
namespace Village
{
constexpr int CitizenCount = 36;
constexpr float MinX = -80.5f, MaxX = 83.5f, MinY = -76.5f, MaxY = 71.5f;
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
};
struct Monster
{
    Scene::ActorId actor = 0;
    Position home{};
    int health = 36, kind = 0;
    float cooldown = 0, flash = 0;
};
struct Loot
{
    Scene::ActorId actor = 0;
    bool collected = false;
};
class World
{
  public:
    void Initialize(Scene::SceneGraph &scene, const std::vector<Scene::ActorId> &citizens,
                    Scene::ActorId player, std::function<bool(Position)> blocked);
    void Update(float dt, int talking = -1);
    void AdvanceEvent();
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
    Phase phase_ = Phase::Daily;
    float phaseTime_ = 0, attackCooldown_ = 0, attackAnimation_ = 0, immunity_ = 0;
    int health_ = 100, coins_ = 20, potions_ = 2, experience_ = 0;
    bool landscape_ = false, story_ = false;
    std::wstring notice_;
    Position GetPosition(Scene::ActorId id) const;
    bool Active(Scene::ActorId id) const;
    void SetPhase(Phase phase);
    void Plan(Citizen &citizen, Position target);
    void Move(Scene::ActorId id, Position goal, float amount);
};
} // namespace Village
