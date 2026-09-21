#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace FirstLevel
{
struct Position
{
    float x = 0;
    float y = 0;
};
enum class Terrain : std::uint8_t
{
    Grass,
    Stone,
    Dirt,
    Water,
    Tree,
    Rock
};
enum class Stat
{
    Strength,
    Vitality,
    Guard
};
enum class ItemKind
{
    Coin,
    Potion
};
struct Enemy
{
    Position position;
    int health = 28;
    int kind = 0;
    float attackCooldown = 0;
    float hitFlash = 0;
    bool alive = true;
};
struct Drop
{
    Position position;
    ItemKind kind = ItemKind::Coin;
    bool collected = false;
};
struct Hero
{
    Position position;
    Position facing{0.7071f, 0.7071f};
    int health = 100;
    int maximumHealth = 100;
    int attack = 12;
    int defense = 2;
    int level = 1;
    int experience = 0;
    int totalExperience = 0;
    int statPoints = 0;
    int spentPoints = 0;
    int potions = 2;
    int coins = 0;
    int itemsPicked = 0;
    int kills = 0;
    float attackCooldown = 0;
    float attackAnimation = 0;
    float invulnerability = 0;
};
class World
{
  public:
    static constexpr int MapSize = 64;
    static constexpr int Origin = MapSize / 2;
    void Generate(std::uint32_t seed);
    void Update(float dt, Position input, bool sprint);
    bool Attack();
    bool Pickup();
    bool UsePotion();
    bool SpendPoint(Stat stat);
    void AwardExperience(int amount);
    bool IsBlocked(Position position) const;
    bool IsWalkableCell(int x, int y) const;
    bool IsConnected() const;
    bool IsComplete() const;
    bool IsDead() const;
    int NextLevelExperience() const;
    Terrain Tile(int x, int y) const;
    const std::vector<Terrain> &Tiles() const;
    const std::vector<Enemy> &Enemies() const;
    const std::vector<Drop> &Drops() const;
    const Hero &Player() const;
    std::uint32_t Seed() const;
    const std::wstring &Notice() const;
    float NoticeTime() const;
    void Respawn();
    bool RunTests(std::string &report);

  private:
    std::vector<Terrain> tiles_;
    std::vector<Enemy> enemies_;
    std::vector<Drop> drops_;
    std::vector<int> distanceField_;
    Hero player_;
    std::uint32_t seed_ = 0;
    std::wstring notice_;
    float noticeTime_ = 0;
    float pathTimer_ = 0;
    bool completed_ = false;
    void ConnectRegions();
    void RebuildDistanceField();
    void Move(Position &position, Position movement);
    void SetNotice(const std::wstring &message);
    bool HasLineOfSight(Position from, Position to) const;
};
} // namespace FirstLevel
