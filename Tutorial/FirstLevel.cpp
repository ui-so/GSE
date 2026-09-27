#include "FirstLevel.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <sstream>
namespace FirstLevel
{
namespace
{
float Length(Position p)
{
    return std::sqrt(p.x * p.x + p.y * p.y);
}
Position Subtract(Position a, Position b)
{
    return {a.x - b.x, a.y - b.y};
}
Position Normalize(Position p)
{
    float length = Length(p);
    return length > .001f ? Position{p.x / length, p.y / length} : Position{};
}
int Cell(float p)
{
    return int(std::floor(p)) + World::Origin;
}
Position Center(int x, int y)
{
    return {float(x - World::Origin) + .5f, float(y - World::Origin) + .5f};
}
constexpr int Directions[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
} // namespace

Position World::GetPosition(Scene::ActorId id) const
{
    auto actor = scene_.Find(id);
    return actor ? actor->GetWorldPosition() : Position{};
}
bool World::IsActive(Scene::ActorId id) const
{
    auto actor = scene_.Find(id);
    return actor && actor->IsActiveInHierarchy();
}
Scene::ActorId World::SpawnActor(Scene::Kind kind, Position position, int index)
{
    auto parent = kind == Scene::Kind::Enemy || kind == Scene::Kind::Player ? characters_
                  : kind == Scene::Kind::Loot                               ? loot_
                                                                            : scenery_;
    if (kind == Scene::Kind::Tree || kind == Scene::Kind::Rock)
    {
        auto key = std::make_pair(int(std::floor(position.x / 16)), int(std::floor(position.y / 16)));
        auto &chunk = sceneryChunks_[key];
        if (!chunk)
            chunk = scene_.Create("SpatialChunk", Scene::Kind::Group, scenery_).GetId();
        parent = chunk;
    }
    auto &actor = scene_.Create("WorldObject", kind, parent);
    actor.SetWorldPosition(position);
    actor.SetDataIndex(index);
    if (kind == Scene::Kind::Player)
        scene_.Create("Attack", Scene::Kind::Attack, actor.GetId());
    if (kind == Scene::Kind::Brazier)
    {
        auto &flame = scene_.Create("Flame", Scene::Kind::Flame, actor.GetId());
        flame.SetLocalElevation(8);
    }
    return actor.GetId();
}
void World::AddDrop(Position position, ItemKind kind)
{
    auto id = SpawnActor(Scene::Kind::Loot, position, int(drops_.size()));
    drops_.push_back({id, kind, false});
}
void World::ClearEnemies()
{
    for (const auto &enemy : enemies_)
        scene_.Remove(enemy.actor);
    enemies_.clear();
}
Terrain World::Tile(int x, int y) const
{
    return x < 0 || y < 0 || x >= MapSize || y >= MapSize ? Terrain::Rock : tiles_[y * MapSize + x];
}
bool World::IsWalkableCell(int x, int y) const
{
    auto tile = Tile(x, y);
    return tile == Terrain::Grass || tile == Terrain::Dirt || tile == Terrain::Stone;
}
bool World::IsBlocked(Position p) const
{
    Profiler::Add("collision_queries");
    constexpr float radius = .23f;
    for (float dx : {-radius, radius})
        for (float dy : {-radius, radius})
            if (!IsWalkableCell(Cell(p.x + dx), Cell(p.y + dy)))
                return true;
    return false;
}
void World::Generate(std::uint32_t seed)
{
    Profiler::Scope profile("map_generate");
    scene_.Clear();
    sceneryChunks_.clear();
    scenery_ = scene_.Create("Scenery", Scene::Kind::Group).GetId();
    characters_ = scene_.Create("Characters", Scene::Kind::Group).GetId();
    loot_ = scene_.Create("Loot", Scene::Kind::Group).GetId();
    scene_.Create("Terrain", Scene::Kind::Terrain, scenery_).SetLayer(Scene::Layer::Ground);
    scene_.Create("HUD", Scene::Kind::Hud).SetLayer(Scene::Layer::Interface);
    SpawnActor(Scene::Kind::Brazier, {2.5f, -1.5f});
    seed_ = seed;
    std::mt19937 random(seed);
    tiles_.assign(MapSize * MapSize, Terrain::Grass);
    ClearEnemies();
    drops_.clear();
    player_ = Hero{};
    player_.actor = SpawnActor(Scene::Kind::Player, {.5f, .5f});
    scene_.Find(player_.actor)->SetWorldPosition({.5f, .5f});
    completed_ = false;
    pathTimer_ = 0;
    for (int y = 0; y < MapSize; y++)
        for (int x = 0; x < MapSize; x++)
        {
            unsigned roll = random() % 100;
            tiles_[y * MapSize + x] = (x == 0 || y == 0 || x == MapSize - 1 || y == MapSize - 1)
                                          ? Terrain::Rock
                                      : roll < 18 ? Terrain::Tree
                                      : roll < 22 ? Terrain::Rock
                                                  : Terrain::Grass;
        }
    // Small ponds are random. Corridors are carved afterward, so no walkable island is stranded.
    for (int lake = 0; lake < 6; lake++)
    {
        int cx = 5 + int(random() % 54), cy = 5 + int(random() % 54), rx = 2 + int(random() % 4),
            ry = 2 + int(random() % 4);
        for (int y = 1; y < MapSize - 1; y++)
            for (int x = 1; x < MapSize - 1; x++)
                if (float((x - cx) * (x - cx)) / (rx * rx) + float((y - cy) * (y - cy)) / (ry * ry) < 1)
                    tiles_[y * MapSize + x] = Terrain::Water;
    }
    for (int y = 1; y < MapSize - 1; y++)
        for (int x = 1; x < MapSize - 1; x++)
        {
            if (std::abs(x - Origin) <= 4 && std::abs(y - Origin) <= 4)
                tiles_[y * MapSize + x] = Terrain::Stone;
            else if (std::abs(x - Origin) <= 1 || std::abs(y - Origin) <= 1)
                tiles_[y * MapSize + x] = Terrain::Dirt;
        }
    ConnectRegions();
    for (int y = 0; y < MapSize; ++y)
        for (int x = 0; x < MapSize; ++x)
            if (Tile(x, y) == Terrain::Tree || Tile(x, y) == Terrain::Rock)
                SpawnActor(Tile(x, y) == Terrain::Tree ? Scene::Kind::Tree : Scene::Kind::Rock, Center(x, y));
    std::vector<Position> nearCells, farCells;
    for (int y = 2; y < MapSize - 2; y++)
        for (int x = 2; x < MapSize - 2; x++)
            if (IsWalkableCell(x, y))
            {
                Position p = Center(x, y);
                float distance = Length(Subtract(p, GetPosition(player_.actor)));
                if (!IsBlocked(p) && distance > 6 && distance < 13)
                    nearCells.push_back(p);
                else if (!IsBlocked(p) && distance >= 13 && distance < 29)
                    farCells.push_back(p);
            }
    std::shuffle(nearCells.begin(), nearCells.end(), random);
    std::shuffle(farCells.begin(), farCells.end(), random);
    auto Spawn = [&](std::vector<Position> &cells, int count) {
        for (auto p : cells)
        {
            bool clear = true;
            for (auto &e : enemies_)
                if (Length(Subtract(GetPosition(e.actor), p)) < 2.8f)
                    clear = false;
            if (!clear)
                continue;
            int id = int(enemies_.size());
            enemies_.push_back(
                {SpawnActor(Scene::Kind::Enemy, p, id), id % 3 == 2 ? 36 : 28, id % 2, 0, 0, true});
            if (--count == 0)
                break;
        }
    };
    Spawn(nearCells, 8);
    Spawn(farCells, 12);
    AddDrop({1.5f, .5f}, ItemKind::Potion);
    RebuildDistanceField();
    SetNotice(L"첫 사냥터 · E로 회복약을 줍고, 바깥의 짐승을 처치해 성장하세요.");
}
void World::ConnectRegions()
{
    for (;;)
    {
        std::vector<bool> seen(MapSize * MapSize, false);
        std::queue<int> open;
        int start = Origin * MapSize + Origin;
        seen[start] = true;
        open.push(start);
        while (!open.empty())
        {
            int id = open.front();
            open.pop();
            for (auto &direction : Directions)
            {
                int x = id % MapSize + direction[0], y = id / MapSize + direction[1];
                if (IsWalkableCell(x, y) && !seen[y * MapSize + x])
                {
                    seen[y * MapSize + x] = true;
                    open.push(y * MapSize + x);
                }
            }
        }
        int orphan = -1;
        for (int y = 1; y < MapSize - 1 && orphan < 0; y++)
            for (int x = 1; x < MapSize - 1; x++)
                if (IsWalkableCell(x, y) && !seen[y * MapSize + x])
                {
                    orphan = y * MapSize + x;
                    break;
                }
        if (orphan < 0)
            break;
        int x = orphan % MapSize, y = orphan / MapSize;
        while (x != Origin)
        {
            tiles_[y * MapSize + x] = Terrain::Dirt;
            x += x < Origin ? 1 : -1;
        }
        while (y != Origin)
        {
            tiles_[y * MapSize + x] = Terrain::Dirt;
            y += y < Origin ? 1 : -1;
        }
    }
}
bool World::IsConnected() const
{
    std::vector<bool> seen(MapSize * MapSize, false);
    std::queue<int> open;
    int start = Origin * MapSize + Origin;
    open.push(start);
    seen[start] = true;
    while (!open.empty())
    {
        int id = open.front();
        open.pop();
        for (auto &d : Directions)
        {
            int x = id % MapSize + d[0], y = id / MapSize + d[1];
            if (IsWalkableCell(x, y) && !seen[y * MapSize + x])
            {
                seen[y * MapSize + x] = true;
                open.push(y * MapSize + x);
            }
        }
    }
    for (int y = 0; y < MapSize; y++)
        for (int x = 0; x < MapSize; x++)
            if (IsWalkableCell(x, y) && !seen[y * MapSize + x])
                return false;
    return true;
}
void World::RebuildDistanceField()
{
    Profiler::Scope profile("ai_navigation_rebuild");
    distanceField_.assign(MapSize * MapSize, -1);
    int x = Cell(GetPosition(player_.actor).x), y = Cell(GetPosition(player_.actor).y);
    navigationX_ = x;
    navigationY_ = y;
    if (!IsWalkableCell(x, y))
        return;
    std::queue<int> open;
    open.push(y * MapSize + x);
    distanceField_[y * MapSize + x] = 0;
    while (!open.empty())
    {
        int id = open.front();
        open.pop();
        for (auto &d : Directions)
        {
            int nx = id % MapSize + d[0], ny = id / MapSize + d[1];
            if (!IsWalkableCell(nx, ny) || distanceField_[ny * MapSize + nx] >= 0)
                continue;
            distanceField_[ny * MapSize + nx] = distanceField_[id] + 1;
            open.push(ny * MapSize + nx);
        }
    }
}
void World::Move(Scene::ActorId actor, Position movement)
{
    if (!IsActive(actor))
        return;
    Position p = GetPosition(actor);
    // Substeps prevent tunneling even when a test or a slow frame supplies a large delta.
    int steps = std::max(1, int(std::ceil(Length(movement) / .1f)));
    for (int i = 0; i < steps; i++)
    {
        Position q{p.x + movement.x / steps, p.y};
        if (!IsBlocked(q))
            p = q;
        q = {p.x, p.y + movement.y / steps};
        if (!IsBlocked(q))
            p = q;
    }
    scene_.Find(actor)->SetWorldPosition(p);
}
bool World::HasLineOfSight(Position from, Position to) const
{
    Profiler::Add("line_of_sight_queries");
    Position delta = Subtract(to, from);
    int steps = std::max(1, int(std::ceil(Length(delta) * 10)));
    for (int i = 1; i <= steps; i++)
    {
        Position p{from.x + delta.x * i / steps, from.y + delta.y * i / steps};
        if (!IsWalkableCell(Cell(p.x), Cell(p.y)))
            return false;
    }
    return true;
}
void World::Update(float dt, Position input, bool sprint)
{
    Profiler::Scope profile("level_simulation");
    dt = std::clamp(dt, 0.f, .1f);
    scene_.Update(dt);
    if (!IsActive(player_.actor))
        return;
    noticeTime_ = std::max(0.f, noticeTime_ - dt);
    player_.attackCooldown = std::max(0.f, player_.attackCooldown - dt);
    player_.attackAnimation = std::max(0.f, player_.attackAnimation - dt);
    player_.invulnerability = std::max(0.f, player_.invulnerability - dt);
    if (!IsActive(player_.actor) || IsDead())
        return;
    if (Length(input) > .01f)
    {
        input = Normalize(input);
        player_.facing = input;
        float speed = sprint ? 4.6f : 3.1f;
        Move(player_.actor, {input.x * speed * dt, input.y * speed * dt});
    }
    pathTimer_ -= dt;
    if (pathTimer_ <= 0)
    {
        auto position = GetPosition(player_.actor);
        if (Cell(position.x) != navigationX_ || Cell(position.y) != navigationY_)
            RebuildDistanceField();
        pathTimer_ = .25f;
    }
    for (auto &enemy : enemies_)
    {
        Profiler::Add("ai_entities_tested");
        enemy.hitFlash = std::max(0.f, enemy.hitFlash - dt);
        if (!enemy.alive || !IsActive(enemy.actor))
            continue;
        enemy.attackCooldown = std::max(0.f, enemy.attackCooldown - dt);
        if (Length(Subtract(GetPosition(player_.actor), {.5f, .5f})) < 3.8f)
            continue;
        float distance = Length(Subtract(GetPosition(enemy.actor), GetPosition(player_.actor)));
        if (distance > 7)
            continue;
        if (distance > 1)
        {
            Position goal = GetPosition(player_.actor);
            if (!HasLineOfSight(GetPosition(enemy.actor), goal))
            {
                int x = Cell(GetPosition(enemy.actor).x), y = Cell(GetPosition(enemy.actor).y);
                if (!IsWalkableCell(x, y))
                    continue;
                int best = distanceField_[y * MapSize + x];
                goal = GetPosition(enemy.actor);
                for (auto &d : Directions)
                {
                    int nx = x + d[0], ny = y + d[1];
                    if (!IsWalkableCell(nx, ny))
                        continue;
                    int cost = distanceField_[ny * MapSize + nx];
                    if (cost >= 0 && (best < 0 || cost < best))
                    {
                        best = cost;
                        goal = Center(nx, ny);
                    }
                }
            }
            Position direction = Normalize(Subtract(goal, GetPosition(enemy.actor)));
            Move(enemy.actor, {direction.x * 1.25f * dt, direction.y * 1.25f * dt});
        }
        else if (enemy.attackCooldown <= 0 && player_.invulnerability <= 0 &&
                 HasLineOfSight(GetPosition(enemy.actor), GetPosition(player_.actor)))
        {
            player_.health = std::max(0, player_.health - std::max(1, 8 - player_.defense));
            player_.invulnerability = .65f;
            enemy.attackCooldown = 1.15f;
            if (!IsActive(player_.actor) || IsDead())
                SetNotice(L"쓰러졌습니다. R로 야영지에서 다시 일어나세요. 성장과 전리품은 유지됩니다.");
        }
    }
}
bool World::Attack()
{
    Profiler::Scope profile("combat_attack");
    if (!IsActive(player_.actor) || IsDead() || player_.attackCooldown > 0)
        return false;
    player_.attackCooldown = .42f;
    player_.attackAnimation = .28f;
    Enemy *target = nullptr;
    float best = 1.8f;
    for (auto &enemy : enemies_)
    {
        if (!enemy.alive || !IsActive(enemy.actor))
            continue;
        Position delta = Subtract(GetPosition(enemy.actor), GetPosition(player_.actor));
        float distance = Length(delta);
        Position normal = Normalize(delta);
        if (distance < best && (normal.x * player_.facing.x + normal.y * player_.facing.y >= -.05f) &&
            HasLineOfSight(GetPosition(player_.actor), GetPosition(enemy.actor)))
        {
            best = distance;
            target = &enemy;
        }
    }
    if (!target)
        return true;
    target->health -= player_.attack;
    target->hitFlash = .18f;
    if (target->health <= 0)
    {
        target->alive = false;
        scene_.Find(target->actor)->SetEnabled(false);
        target->health = 0;
        player_.kills++;
        AddDrop(GetPosition(target->actor), ItemKind::Coin);
        if (player_.kills % 2 == 0)
            AddDrop(GetPosition(target->actor), ItemKind::Potion);
        AwardExperience(20);
    }
    return true;
}
bool World::Pickup()
{
    Profiler::Scope profile("loot_pickup");
    if (!IsActive(player_.actor) || IsDead())
        return false;
    int picked = 0;
    for (auto &drop : drops_)
        if (!drop.collected && IsActive(drop.actor) &&
            Length(Subtract(GetPosition(drop.actor), GetPosition(player_.actor))) < 1.5f &&
            HasLineOfSight(GetPosition(player_.actor), GetPosition(drop.actor)))
        {
            drop.collected = true;
            scene_.Find(drop.actor)->SetEnabled(false);
            picked++;
            player_.itemsPicked++;
            if (drop.kind == ItemKind::Coin)
                player_.coins += 5;
            else
                player_.potions++;
        }
    if (picked)
    {
        SetNotice(L"전리품 획득 · 주머니에 보관했습니다.");
        return true;
    }
    return false;
}
bool World::UsePotion()
{
    if (!IsActive(player_.actor) || IsDead() || player_.potions <= 0 ||
        player_.health >= player_.maximumHealth)
        return false;
    player_.potions--;
    player_.health = std::min(player_.maximumHealth, player_.health + 40);
    SetNotice(L"회복약 사용 · 체력 40 회복");
    return true;
}
int World::NextLevelExperience() const
{
    return 60 + (player_.level - 1) * 40;
}
void World::AwardExperience(int amount)
{
    if (amount <= 0)
        return;
    player_.experience += amount;
    player_.totalExperience += amount;
    bool leveled = false;
    while (player_.experience >= NextLevelExperience())
    {
        player_.experience -= NextLevelExperience();
        player_.level++;
        player_.statPoints += 2;
        player_.maximumHealth += 10;
        player_.attack++;
        player_.health = player_.maximumHealth;
        leveled = true;
    }
    SetNotice(leveled ? L"레벨 상승! 능력치 포인트 +2 · 1 공격력 / 2 체력 / 3 방어력"
                      : L"처치 · 경험치 +20 · E로 전리품을 획득하세요.");
}
bool World::SpendPoint(Stat stat)
{
    if (!IsActive(player_.actor) || player_.statPoints <= 0 || IsDead())
        return false;
    switch (stat)
    {
    case Stat::Strength:
        player_.attack += 3;
        break;
    case Stat::Vitality:
        player_.maximumHealth += 20;
        player_.health += 20;
        break;
    case Stat::Guard:
        player_.defense++;
        break;
    default:
        return false;
    }
    player_.statPoints--;
    player_.spentPoints++;
    SetNotice(L"능력치 적용 완료 · 다음 전투부터 변경된 수치가 적용됩니다.");
    return true;
}
void World::Respawn()
{
    if (!scene_.Find(player_.actor))
        return;
    scene_.Find(player_.actor)->SetWorldPosition({.5f, .5f});
    player_.health = player_.maximumHealth;
    player_.invulnerability = 3;
    player_.attackCooldown = 0;
    player_.attackAnimation = 0;
    RebuildDistanceField();
    SetNotice(L"야영지에서 다시 일어났습니다. 경험치와 아이템은 유지됩니다.");
}
bool World::IsDead() const
{
    return player_.health <= 0;
}
bool World::IsComplete() const
{
    return player_.totalExperience >= 60 && player_.itemsPicked >= 1 && player_.spentPoints >= 1;
}
const Hero &World::Player() const
{
    return player_;
}
const std::vector<Terrain> &World::Tiles() const
{
    return tiles_;
}
const std::vector<Enemy> &World::Enemies() const
{
    return enemies_;
}
const std::vector<Drop> &World::Drops() const
{
    return drops_;
}
std::uint32_t World::Seed() const
{
    return seed_;
}
const std::wstring &World::Notice() const
{
    return notice_;
}
float World::NoticeTime() const
{
    return noticeTime_;
}
void World::SetNotice(const std::wstring &message)
{
    notice_ = message;
    noticeTime_ = 5;
}
bool World::RunTests(std::string &report)
{
    auto Check = [&](bool condition, const char *message) {
        if (!condition)
            report += std::string("FAIL: ") + message + "\n";
        return condition;
    };
    for (std::uint32_t seed = 0; seed < 128; seed++)
    {
        Generate(seed);
        if (!Check(IsConnected() && !IsBlocked(GetPosition(player_.actor)), "map connectivity"))
            return false;
        if (!Check(enemies_.size() >= 8, "enough enemies for progression"))
            return false;
        for (auto &enemy : enemies_)
            if (!Check(!IsBlocked(GetPosition(enemy.actor)), "enemy spawn"))
                return false;
    }
    Generate(42);
    auto original = tiles_;
    Generate(42);
    if (!Check(original == tiles_, "seed reproducibility"))
        return false;
    Generate(43);
    if (!Check(original != tiles_, "seed variety"))
        return false;
    Generate(7);
    ClearEnemies();
    scene_.Find(player_.actor)->SetWorldPosition({.5f, .5f});
    player_.facing = {1, 0};
    enemies_.push_back(
        {SpawnActor(Scene::Kind::Enemy, {1.5f, .5f}, int(enemies_.size())), 24, 0, 0, 0, true});
    if (!Check(Attack() && !Attack() && enemies_[0].health == 12, "attack cooldown"))
        return false;
    player_.attackCooldown = 0;
    Attack();
    if (!Check(!enemies_[0].alive && player_.totalExperience == 20 && drops_.size() == 2, "kill xp and loot"))
        return false;
    int xp = player_.totalExperience;
    player_.attackCooldown = 0;
    Attack();
    if (!Check(player_.totalExperience == xp, "no double reward"))
        return false;
    if (!Check(Pickup() && !Pickup() && player_.coins == 5 && player_.potions == 3, "pickup once"))
        return false;
    AwardExperience(40);
    if (!Check(player_.level == 2 && player_.experience == 0 && player_.statPoints == 2 &&
                   player_.attack == 13,
               "level up"))
        return false;
    if (!Check(SpendPoint(Stat::Strength) && player_.attack == 16 && SpendPoint(Stat::Vitality) &&
                   player_.maximumHealth == 130 && !SpendPoint(Stat::Guard) && IsComplete(),
               "stat allocation and completion"))
        return false;
    player_.health = 40;
    int potions = player_.potions;
    if (!Check(UsePotion() && player_.health == 80 && player_.potions == potions - 1, "healing item"))
        return false;
    player_.health = player_.maximumHealth;
    if (!Check(!UsePotion(), "no waste at full health"))
        return false;
    AwardExperience(500);
    if (!Check(player_.level > 3 && player_.experience < NextLevelExperience(), "multiple levels"))
        return false;
    int attack = player_.attack;
    player_.health = 0;
    if (!Check(!Attack() && !Pickup() && !SpendPoint(Stat::Strength), "death action lock"))
        return false;
    Respawn();
    if (!Check(player_.health == player_.maximumHealth && player_.attack == attack,
               "respawn retains progress"))
        return false;
    Generate(3);
    ClearEnemies();
    player_.facing = {1, 0};
    scene_.Find(player_.actor)->SetWorldPosition({.7f, .5f});
    enemies_.push_back(
        {SpawnActor(Scene::Kind::Enemy, {2.4f, .5f}, int(enemies_.size())), 28, 0, 0, 0, true});
    tiles_[Origin * MapSize + Origin + 1] = Terrain::Rock;
    player_.attackCooldown = 0;
    Attack();
    if (!Check(enemies_[0].health == 28, "cannot attack through wall"))
        return false;
    Generate(1);
    for (int frame = 0; frame < 300; frame++)
        Update(.05f, {1, 0}, false);
    if (!Check(!IsBlocked(GetPosition(player_.actor)), "movement collision"))
        return false;
    for (auto &enemy : enemies_)
        if (!Check(!IsBlocked(GetPosition(enemy.actor)), "AI collision"))
            return false;
    Generate(21);
    ClearEnemies();
    scene_.Find(player_.actor)->SetWorldPosition({6.5f, .5f});
    enemies_.push_back(
        {SpawnActor(Scene::Kind::Enemy, {7.1f, .5f}, int(enemies_.size())), 28, 0, 0, 0, true});
    Update(.1f, {}, false);
    if (!Check(player_.health == 94, "enemy damage uses defense"))
        return false;
    Update(.1f, {}, false);
    if (!Check(player_.health == 94, "damage invulnerability"))
        return false;
    player_.statPoints = 1;
    SpendPoint(Stat::Guard);
    player_.invulnerability = 0;
    enemies_[0].attackCooldown = 0;
    Update(.1f, {}, false);
    if (!Check(player_.health == 89, "defense allocation reduces incoming damage"))
        return false;
    Generate(22);
    ClearEnemies();
    player_.facing = {1, 0};
    for (int i = 0; i < 3; i++)
        enemies_.push_back(
            {SpawnActor(Scene::Kind::Enemy, {1.5f, .5f}, int(enemies_.size())), 24, 0, 0, 0, true});
    for (int step = 0; step < 60 && player_.kills < 3; step++)
    {
        Attack();
        Update(.1f, {}, false);
    }
    Pickup();
    if (!Check(player_.kills == 3 && player_.level == 2 && player_.coins == 15 &&
                   SpendPoint(Stat::Strength) && IsComplete(),
               "combat to loot to growth completion"))
        return false;
    Generate(73);
    auto count = scene_.Size();
    auto &group = *scene_.Find(characters_);
    auto before = GetPosition(player_.actor);
    group.SetLocalPosition({1, 2});
    auto moved = GetPosition(player_.actor);
    if (!Check(std::abs(moved.x - before.x - 1) < .001f && std::abs(moved.y - before.y - 2) < .001f,
               "gameplay inherits parent transform"))
        return false;
    group.SetLocalPosition({0, 0});
    group.SetEnabled(false);
    Update(.1f, {1, 0}, false);
    if (!Check(GetPosition(player_.actor).x == before.x && !Attack() && !Pickup(),
               "disabled group locks simulation"))
        return false;
    group.SetEnabled(true);
    for (int i = 0; i < 20; ++i)
        Update(.01f, {}, false);
    if (!Check(scene_.Size() == count && scene_.IsValid(), "persistent scene actors"))
        return false;
    auto old = player_.actor;
    scene_.Remove(characters_);
    Update(.1f, {}, false);
    Respawn();
    if (!Check(!Attack() && !scene_.Find(old), "removed actor handles are safe"))
        return false;
    Generate(73);
    if (!Check(!scene_.Find(old) && scene_.IsValid(), "map reset invalidates old actors"))
        return false;
    report += "PASS: gameplay scene ownership, parent transforms, disabled simulation, stable actor count "
              "and removed handles.\n";
    report += "PASS: 128 seeded maps, connected traversable cells, reproducible generation, enemy spawns, "
              "combat/cooldown, kill XP, one-time loot, level thresholds, stat effects, healing, "
              "death/respawn, wall and AI collision.\n";
    return true;
}
} // namespace FirstLevel
