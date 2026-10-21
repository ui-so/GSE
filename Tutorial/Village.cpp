#include "Village.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <limits>
namespace Village
{
namespace
{
constexpr int Width = WorldLayout::MapWidth, Height = WorldLayout::MapHeight;
float Distance(Position a, Position b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}
int Cell(Position p)
{
    int x = int(std::floor(p.x - MinX)), y = int(std::floor(p.y - MinY));
    return x < 0 || x >= Width || y < 0 || y >= Height ? -1 : y * Width + x;
}
Position Center(int id)
{
    return {MinX + float(id % Width) + .5f, MinY + float(id / Width) + .5f};
}
} // namespace
Position World::GetPosition(Scene::ActorId id) const
{
    auto actor = scene_->Find(id);
    return actor ? actor->GetWorldPosition() : Position{};
}
bool World::Active(Scene::ActorId id) const
{
    auto actor = scene_->Find(id);
    return actor && actor->IsActiveInHierarchy();
}
void World::Initialize(Scene::SceneGraph &scene, const std::vector<Scene::ActorId> &actors,
                       Scene::ActorId player, std::function<bool(Position)> blocked)
{
    Profiler::Scope scope("village_initialize");
    scene_ = &scene;
    player_ = player;
    blocked_ = std::move(blocked);
    citizens_.clear();
    monsters_.clear();
    loot_.clear();
    phase_ = Phase::Daily;
    phaseTime_ = 0;
    health_ = 100;
    coins_ = 20;
    potions_ = 2;
    experience_ = 0;
    landscape_ = story_ = false;
    attackCooldown_ = attackAnimation_ = immunity_ = 0;
    statistics_ = {};
    statistics_[0].rumors = 1;
    receipts_.clear();
    spawnSites_.clear();
    time_ = statisticsTimer_ = 0;
    respawns_ = 0;
    spawnCursor_ = 0;
    navigation_.assign(Width * Height, 0);
    for (int i = 0; i < Width * Height; ++i)
        navigation_[i] = !blocked_(Center(i));
    // Validate edges, not just cells: thin tree trunks can lie between walkable centers.
    edges_.assign(Width * Height, 0);
    for (int id = 0; id < Width * Height; ++id)
    {
        if (!navigation_[id])
            continue;
        int direction = 0;
        for (auto d : {Position{1, 0}, Position{-1, 0}, Position{0, 1}, Position{0, -1}})
        {
            auto p = Center(id);
            bool clear = true;
            for (int step = 1; step <= 4; ++step)
                if (blocked_({p.x + d.x * step * .25f, p.y + d.y * step * .25f}))
                    clear = false;
            if (clear)
                edges_[id] |= static_cast<unsigned char>(1 << direction);
            ++direction;
        }
    }
    constexpr Reaction reactions[20] = {
        Reaction::Stay,      Reaction::ShareBread, Reaction::Leave,  Reaction::Help,      Reaction::Help,
        Reaction::CloseShop, Reaction::Family,     Reaction::Help,   Reaction::CloseShop, Reaction::Stay,
        Reaction::Help,      Reaction::CloseShop,  Reaction::Leave,  Reaction::Help,      Reaction::Leave,
        Reaction::CloseShop, Reaction::Help,       Reaction::Family, Reaction::Family,    Reaction::Stay};
    for (size_t i = 0; i < actors.size(); ++i)
    {
        Citizen c;
        c.actor = actors[i];
        c.home = GetPosition(c.actor);
        c.goal = c.home;
        int local = int(i) % WorldLayout::ResidentsPerVillage;
        c.homeVillage = int(i) / WorldLayout::ResidentsPerVillage;
        c.reaction = reactions[local];
        c.timer = 2 + float(i) * .11f;
        c.courier = local == 14 || local == 15;
        if (c.courier)
            c.timer = 1 + float(i % 7) * .2f;
        if (local == 1)
            c.service = Service::Baker;
        if (local == 5 || local == 15)
            c.service = Service::Merchant;
        if (local == 13 || local == 16)
            c.service = Service::Cleric;
        citizens_.push_back(c);
    }
    for (int id = 0; id < Width * Height; ++id)
    {
        auto p = Center(id);
        if (navigation_[id] && WorldLayout::VillageAt(p) < 0 && WorldLayout::RoadDistance(p) < 10)
            spawnSites_.push_back(p);
    }
    std::shuffle(spawnSites_.begin(), spawnSites_.end(), std::mt19937(42));
    monstersGroup_ = scene.Create("RegionalThreats", Scene::Kind::Group).GetId();
    for (int i = 0; i < WorldLayout::MonsterCount; ++i)
    {
        Position p;
        if (!ChooseSpawn({}, p, true))
        {
            Profiler::Event("village_spawn_failed", std::to_string(i));
            continue;
        }
        auto &actor = scene.Create("ForestCreature", Scene::Kind::Enemy, monstersGroup_);
        actor.SetWorldPosition(p);
        actor.SetDataIndex(int(monsters_.size()));
        monsters_.push_back({actor.GetId(), p, 36, i % 2, 0, 0, 0});
    }
    RebuildStatistics(-1);
    notice_ = L"다섯 마을의 주민 100명과 길 위의 소문을 만나 보세요. 순회자가 도착해야 소문이 전해집니다.";
}

void World::Plan(Citizen &c, Position target)
{
    Profiler::Scope scope("npc_pathfinding");
    Profiler::Add("npc_path_requests");
    c.route.clear();
    c.step = 0;
    c.goal = target;
    int start = Cell(GetPosition(c.actor)), finish = Cell(target);
    if (start < 0 || finish < 0)
        return;
    if (!navigation_[finish])
    {
        float best = 1e9f;
        for (int i = 0; i < Width * Height; ++i)
            if (navigation_[i] && Distance(Center(i), target) < best)
            {
                best = Distance(Center(i), target);
                finish = i;
            }
    }
    std::vector<int> previous(Width * Height, -1), cost(Width * Height, std::numeric_limits<int>::max());
    using Entry = std::pair<int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    auto heuristic = [&](int id) {
        return std::abs(id % Width - finish % Width) + std::abs(id / Width - finish / Width);
    };
    previous[start] = start;
    cost[start] = 0;
    open.push({heuristic(start), start});
    while (!open.empty())
    {
        auto entry = open.top();
        open.pop();
        int id = entry.second;
        if (entry.first != cost[id] + heuristic(id))
            continue;
        if (id == finish)
            break;
        Profiler::Add("npc_path_cells_visited");
        int direction = 0;
        for (int offset : {1, -1, Width, -Width})
        {
            bool clear = (edges_[id] & (1 << direction++)) != 0;
            if (!clear)
                continue;
            int next = id + offset;
            if (next < 0 || next >= Width * Height || !navigation_[next])
                continue;
            if (cost[id] + 1 < cost[next])
            {
                cost[next] = cost[id] + 1;
                previous[next] = id;
                open.push({cost[next] + heuristic(next), next});
            }
        }
    }
    if (previous[finish] < 0)
    {
        Profiler::Add("npc_path_failures");
        return;
    }
    for (int at = finish; at != start; at = previous[at])
        c.route.push_back(Center(at));
    std::reverse(c.route.begin(), c.route.end());
}
void World::Move(Scene::ActorId id, Position goal, float amount, bool wilderness)
{
    if (!Active(id))
        return;
    auto p = GetPosition(id);
    float d = Distance(p, goal);
    if (d < .001f)
        return;
    float scale = std::min(1.f, amount / d);
    Position q{p.x + (goal.x - p.x) * scale, p.y + (goal.y - p.y) * scale};
    auto blocked = [&](Position p) { return blocked_(p) || (wilderness && WorldLayout::VillageAt(p) >= 0); };
    if (!blocked(q))
        scene_->Find(id)->SetWorldPosition(q);
    else
    {
        q = {p.x + (goal.x - p.x) * scale, p.y};
        if (!blocked(q))
            p = q;
        q = {p.x, p.y + (goal.y - p.y) * scale};
        if (!blocked(q))
            p = q;
        scene_->Find(id)->SetWorldPosition(p);
    }
}
bool World::IsFrozen(Position p) const
{
    return phase_ == Phase::Stillness && Distance(p, {0, -16}) < 6;
}
void World::SetPhase(Phase phase)
{
    phase_ = phase;
    phaseTime_ = 0;
    Profiler::Event("village_story_phase", std::to_string(int(phase)));
    statistics_[0].rumors |= 1u << unsigned(phase);
    for (size_t i = 0; i < citizens_.size(); ++i)
    {
        auto &c = citizens_[i];
        if (c.homeVillage != 0 || c.courier || c.frozen)
            continue;
        c.timer = 0;
        Position goal = c.home;
        if (phase == Phase::Warning && c.reaction == Reaction::Family)
            goal = citizens_[i == WorldLayout::Mother  ? WorldLayout::Child
                             : i == WorldLayout::Child ? WorldLayout::Mother
                                                       : i]
                       .home;
        if (phase >= Phase::Evacuation && c.reaction != Reaction::Stay)
            goal = {-6.f + float(i % 8) * 1.7f, 18.f + float(i / 8) * 1.7f};
        Plan(c, goal);
    }
    RebuildStatistics(-1);
    notice_ = phase == Phase::Warning
                  ? L"북쪽 묘목의 색이 옅어집니다. 상인은 짐을 싸고 가족은 서로를 찾습니다."
              : phase == Phase::Evacuation
                  ? L"대피 종이 울립니다. 남쪽 집결지로 향하는 주민들의 이야기를 들어보세요."
                  : L"북쪽 숲 일부가 무채색으로 멈췄습니다. 되돌릴 수 없습니다. 남아 있는 삶을 기록하세요.";
}
void World::AdvanceEvent()
{
    if (phase_ == Phase::Stillness)
    {
        notice_ = L"탈색은 되돌리지 않습니다. R로 테스트 전체를 새로 시작할 수 있습니다.";
        return;
    }
    SetPhase(static_cast<Phase>(int(phase_) + 1));
}
const wchar_t *World::PhaseName() const
{
    const wchar_t *names[] = {L"평온한 일상", L"탈색의 징후", L"주민 대피", L"정지한 북쪽 숲"};
    return names[int(phase_)];
}
const wchar_t *World::Behavior(int id) const
{
    const auto &c = citizens_.at(size_t(id));
    if (c.frozen)
        return L"무채색 · 정지";
    if (c.courier)
        return c.destination >= 0 ? L"마을 사이 이동 · 소문 운반" : L"광장에서 소식 교환";
    Phase local = c.homeVillage == 0 ? phase_ : Phase::Daily;
    if (local == Phase::Daily)
        return c.walking ? L"생업 이동" : (c.rumors > 1 ? L"소문을 들으며 경계" : L"일상 · 휴식");
    if (c.reaction == Reaction::Stay)
        return L"떠나지 않음";
    if (local == Phase::Warning)
    {
        const wchar_t *names[] = {L"대피 준비",   L"가족 찾기", L"남아 있기",
                                  L"이웃 돌보기", L"가게 정리", L"남은 빵 나누기"};
        return names[int(c.reaction)];
    }
    return c.walking ? L"집결지로 이동" : L"집결지에서 기다림";
}
void World::Update(float dt, int talking)
{
    Profiler::Scope scope("village_simulation");
    dt = std::clamp(dt, 0.f, .1f);
    attackCooldown_ = std::max(0.f, attackCooldown_ - dt);
    attackAnimation_ = std::max(0.f, attackAnimation_ - dt);
    immunity_ = std::max(0.f, immunity_ - dt);
    if (IsDead())
        return;
    time_ += dt;
    if (phase_ != Phase::Daily && phase_ != Phase::Stillness)
    {
        phaseTime_ += dt;
        if (phaseTime_ > (phase_ == Phase::Warning ? 30.f : 60.f))
            AdvanceEvent();
    }
    for (size_t i = 0; i < citizens_.size(); ++i)
    {
        auto &c = citizens_[i];
        Profiler::Add("npc_entities_tested");
        c.walking = false;
        if (!Active(c.actor))
            continue;
        if (IsFrozen(GetPosition(c.actor)))
            c.frozen = true;
        if (c.frozen)
        {
            Profiler::Add("npc_frozen");
            continue;
        }
        ExchangeRumors(c);
        if (int(i) == talking)
            continue;
        auto before = GetPosition(c.actor);
        Phase local = c.homeVillage == 0 ? phase_ : Phase::Daily;
        if (c.step < c.route.size())
        {
            Move(c.actor, c.route[c.step],
                 dt * (c.courier                    ? WorldLayout::WalkingSpeed
                       : local >= Phase::Evacuation ? 2.f
                                                    : 1.05f));
            if (Distance(GetPosition(c.actor), c.route[c.step]) < .08f)
                ++c.step;
        }
        else if (c.courier)
        {
            if (c.destination >= 0)
            {
                ExchangeRumors(c);
                c.destination = -1;
                c.timer = 4;
            }
            c.timer -= dt;
            if (c.timer <= 0)
            {
                int current = WorldLayout::VillageAt(GetPosition(c.actor));
                if (current < 0)
                    current = c.homeVillage;
                c.destination =
                    current == 0 ? (c.homeVillage == 0 ? 1 + (c.circuit++ % 4) : c.homeVillage) : 0;
                Plan(c, WorldLayout::Plaza(c.destination));
                if (c.route.empty())
                {
                    c.destination = -1;
                    c.timer = 1;
                }
            }
        }
        else if (local == Phase::Daily)
        {
            c.timer -= dt;
            if (c.timer <= 0)
            {
                c.timer = 8 + float(i % 9);
                Plan(c, Distance(before, c.home) > .7f ? c.home : Position{c.home.x + 1.5f, c.home.y + .5f});
            }
        }
        c.walking = Distance(before, GetPosition(c.actor)) > .001f;
        c.animation += dt;
        ExchangeRumors(c);
    }
    int budget = 4;
    for (auto &m : monsters_)
    {
        Profiler::Add("village_monsters_tested");
        m.flash = std::max(0.f, m.flash - dt);
        if (m.health <= 0)
        {
            m.respawn = std::max(0.f, m.respawn - dt);
            if (m.respawn <= 0 && budget > 0)
            {
                Position point;
                if (ChooseSpawn(m.home, point, false))
                {
                    m.home = point;
                    m.health = 36;
                    m.cooldown = 1;
                    m.flash = 0;
                    auto actor = scene_->Find(m.actor);
                    actor->SetWorldPosition(point);
                    actor->SetEnabled(true);
                    ++respawns_;
                    --budget;
                    Profiler::Add("monster_respawns");
                }
                else
                {
                    m.respawn = .25f;
                    Profiler::Add("monster_respawn_retries");
                }
            }
            continue;
        }
        if (!Active(m.actor) || IsFrozen(GetPosition(m.actor)))
            continue;
        m.cooldown = std::max(0.f, m.cooldown - dt);
        auto p = GetPosition(m.actor), hero = GetPosition(player_);
        bool safe = WorldLayout::VillageAt(hero) >= 0;
        if (!safe && Distance(hero, p) < 6 && Distance(hero, m.home) < 10)
        {
            if (Distance(hero, p) > 1.1f)
                Move(m.actor, hero, dt * 1.6f, true);
            else if (immunity_ <= 0 && m.cooldown <= 0 && talking < 0)
            {
                health_ = std::max(0, health_ - 8);
                immunity_ = 1;
                m.cooldown = 1.4f;
            }
        }
        else if (Distance(p, m.home) > .3f)
            Move(m.actor, m.home, dt * 1.2f, true);
    }
    for (auto &drop : loot_)
        if (!drop.collected)
        {
            drop.age += dt;
            if (drop.age >= 60)
            {
                drop.collected = true;
                scene_->Find(drop.actor)->SetEnabled(false);
            }
        }
    statisticsTimer_ -= dt;
    if (statisticsTimer_ <= 0)
    {
        RebuildStatistics(talking);
        statisticsTimer_ = .5f;
    }
}
void World::Talk(int id)
{
    Profiler::Scope scope("village_interaction");
    if (id < 0 || size_t(id) >= citizens_.size())
        return;
    auto &c = citizens_[size_t(id)];
    if (c.frozen)
        return;
    c.recorded = true;
    if (c.service == Service::Baker && c.homeVillage == 0 && phase_ == Phase::Warning && !c.gifted)
    {
        c.gifted = true;
        health_ = std::min(100, health_ + 20);
        ++potions_;
        notice_ = L"제빵사가 마지막 빵을 나누었습니다. 회복약 +1, 체력 +20.";
    }
}
std::wstring World::Dialogue(int id) const
{
    const auto &c = citizens_.at(size_t(id));
    if (c.frozen)
        return L"말을 건네도 대답하지 않는다. 마지막 자세와 색을 잃은 몸이 남았다. 기록이 이 죽음을 "
               L"되돌리지는 못한다.";
    Phase local = c.homeVillage == 0 ? phase_ : Phase::Daily;
    std::wstring text;
    if (local == Phase::Daily)
    {
        const wchar_t *lines[] = {L"오늘 장작부터 옮겨야지. 여행자, 우리 동네도 한 장 그려 주겠어?",
                                  L"내 가족이 저 길로 오기로 했어. 기다리는 동안 이야기를 들려줄까?",
                                  L"익숙한 이 자리가 좋아. 사라질까 두려워도 오늘 할 일은 해야지.",
                                  L"위로할 말이 없어도 곁에 앉아 있을 수는 있어.",
                                  L"여행 물품은 여기 있소. 외상 장부만큼은 영원히 남길 생각이오."};
        text = lines[int(c.reaction) % 5];
    }
    else
    {
        const wchar_t *lines[] = {L"짐은 가벼워도 기억은 무겁네. 남쪽에서 다시 만나세.",
                                  L"물건은 두고 왔어. 가족만 찾으면 함께 떠날 거야.",
                                  L"나는 조금 더 남겠어. 여기에 살던 사람들을 누군가는 기억해야지.",
                                  L"부활을 약속할 수는 없어. 지금 살아 있는 사람 곁을 지킬 뿐이야.",
                                  L"가게는 닫았소. 물건보다 사람을 먼저 보내야겠지.",
                                  L"마지막 빵이야. 돈은 됐어. 조금 딱딱해도 길 위에서 나눠 먹어."};
        text = lines[int(c.reaction)];
    }
    if (c.service == Service::Merchant && local == Phase::Warning)
        text = L"곧 문을 닫을 참이오. 길에서 쓸 물약이 필요하면 지금 챙기시오. 사람부터 보내야지.";
    if (c.service == Service::Merchant)
        text += local >= Phase::Evacuation ? L" [대피 중 · 거래 중단]" : L" [1] 동전 5개로 회복약 구입";
    if (c.service == Service::Cleric)
        text += L" [2] 무료 치료 · 탈색을 되돌리는 힘은 없음";
    if (c.rumors)
        for (int rumor = 3; rumor >= 0; --rumor)
            if (c.rumors & (1u << rumor))
            {
                text += L" 들은 소식: " + std::wstring(RumorName(rumor));
                break;
            }
    return text;
}
bool World::Buy(int id)
{
    Profiler::Scope scope("village_trade");
    if (id < 0 || size_t(id) >= citizens_.size() || citizens_[id].service != Service::Merchant ||
        citizens_[id].frozen || (citizens_[id].homeVillage == 0 && phase_ >= Phase::Evacuation) ||
        coins_ < 5 || IsDead())
        return false;
    if (!Active(citizens_[id].actor) || Distance(GetPosition(player_), GetPosition(citizens_[id].actor)) > 2)
        return false;
    coins_ -= 5;
    ++potions_;
    notice_ = L"회복약 구입 · 동전 -5, 회복약 +1";
    return true;
}
bool World::Heal(int id)
{
    if (id < 0 || size_t(id) >= citizens_.size() || citizens_[id].service != Service::Cleric ||
        citizens_[id].frozen || IsDead())
        return false;
    if (!Active(citizens_[id].actor) || Distance(GetPosition(player_), GetPosition(citizens_[id].actor)) > 2)
        return false;
    health_ = 100;
    notice_ = L"성직자가 상처를 치료했습니다. 죽음이나 탈색을 되돌리지는 못합니다.";
    return true;
}
bool World::Attack(Position facing)
{
    Profiler::Scope scope("village_combat");
    if (IsDead() || attackCooldown_ > 0)
        return false;
    attackCooldown_ = .42f;
    attackAnimation_ = .25f;
    for (auto &m : monsters_)
    {
        auto p = GetPosition(m.actor), hero = GetPosition(player_);
        float dx = p.x - hero.x, dy = p.y - hero.y;
        if (m.health <= 0 || !Active(m.actor) || IsFrozen(p) || Distance(p, hero) > 1.8f ||
            dx * facing.x + dy * facing.y < -.1f)
            continue;
        bool clear = true;
        for (int i = 1; i <= 10; ++i)
            if (blocked_({hero.x + dx * i / 10, hero.y + dy * i / 10}))
                clear = false;
        if (!clear)
            continue;
        m.health -= 12;
        m.flash = .15f;
        if (m.health <= 0)
        {
            scene_->Find(m.actor)->SetEnabled(false);
            experience_ += 20;
            m.home = p; // Dead slots retain the actual death position for displaced respawn.
            m.respawn = WorldLayout::RespawnSeconds;
            AddLoot(p);
            notice_ = L"외곽의 위협을 물리쳤습니다. 경험치 +20 · E로 전리품 획득.";
        }
        break;
    }
    return true;
}
bool World::Pickup()
{
    Profiler::Scope scope("village_loot");
    if (IsDead())
        return false;
    bool picked = false;
    for (auto &drop : loot_)
        if (!drop.collected && Active(drop.actor) &&
            Distance(GetPosition(drop.actor), GetPosition(player_)) < 1.6f)
        {
            drop.collected = true;
            scene_->Find(drop.actor)->SetEnabled(false);
            coins_ += 5;
            picked = true;
        }
    if (picked)
        notice_ = L"전리품 획득 · 동전 +5";
    return picked;
}
bool World::UsePotion()
{
    if (IsDead() || health_ == 100 || potions_ <= 0)
        return false;
    --potions_;
    health_ = std::min(100, health_ + 40);
    notice_ = L"회복약 사용 · 체력 +40";
    return true;
}
void World::Respawn()
{
    health_ = 100;
    immunity_ = 3;
    scene_->Find(player_)->SetWorldPosition({0, 2});
    notice_ = L"마을에서 다시 일어났습니다.";
}
void World::Sketch(Position p)
{
    Profiler::Scope scope("village_record");
    if (IsDead())
        return;
    if (Distance(p, {10, -5}) < 3 || Distance(p, {0, -13}) < 3)
    {
        landscape_ = true;
        if (RecordCount() >= 3)
            story_ = true;
        notice_ = story_ ? L"풍경 옆에 주민들의 이야기를 적었다. 그림 속에는 언제나 반려동물이 함께 있다."
                         : L"풍경 속에 함께 오지 못한 작은 환수를 그렸다. 사람들의 이야기도 들어보자.";
        Profiler::Event("village_sketch_recorded", story_ ? "landscape_and_people" : "landscape_with_pet");
    }
    else
        notice_ = L"호숫가 추모석 또는 북쪽 묘목 옆에서 P로 그림을 기록하세요.";
}
int World::RecordCount() const
{
    return int(
        std::count_if(citizens_.begin(), citizens_.end(), [](const Citizen &c) { return c.recorded; }));
}
bool World::ChooseSpawn(Position previous, Position &result, bool initial)
{
    Profiler::Scope scope("monster_spawn_select");
    for (size_t attempt = 0; attempt < spawnSites_.size(); ++attempt)
    {
        Profiler::Add("monster_spawn_candidates");
        auto p = spawnSites_[(spawnCursor_ + attempt) % spawnSites_.size()];
        if ((!initial && Distance(p, previous) < 12) || Distance(p, GetPosition(player_)) < 14)
            continue;
        bool occupied = false;
        for (const auto &m : monsters_)
            if (m.health > 0 && Distance(p, GetPosition(m.actor)) < 1.6f)
            {
                occupied = true;
                break;
            }
        if (occupied)
            continue;
        spawnCursor_ = (spawnCursor_ + attempt + 73) % spawnSites_.size();
        result = p;
        return true;
    }
    return false;
}
void World::AddLoot(Position p)
{
    for (auto &drop : loot_)
        if (drop.collected)
        {
            drop.collected = false;
            drop.age = 0;
            auto actor = scene_->Find(drop.actor);
            actor->SetWorldPosition(p);
            actor->SetEnabled(true);
            return;
        }
    if (loot_.size() >= 128)
    {
        Profiler::Add("loot_pool_full");
        return;
    }
    auto &actor = scene_->Create("RegionalLoot", Scene::Kind::Loot, monstersGroup_);
    actor.SetWorldPosition(p);
    actor.SetDataIndex(int(loot_.size()));
    loot_.push_back({actor.GetId(), false, 0});
}
const wchar_t *World::RumorName(int rumor)
{
    const wchar_t *names[] = {L"마을 사이 숲길에 짐승이 늘었다", L"잿빛 여울 북쪽에서 탈색 징후를 보았다",
                              L"잿빛 여울 주민들이 남쪽으로 대피한다", L"잿빛 여울의 숲 일부가 멈췄다"};
    return names[std::clamp(rumor, 0, 3)];
}
void World::ExchangeRumors(Citizen &c)
{
    Profiler::Add("rumor_contacts_tested");
    auto p = GetPosition(c.actor);
    int village = WorldLayout::VillageAt(p);
    if (village < 0 || (c.courier && Distance(p, WorldLayout::Plaza(village)) > 1.8f))
        return;
    auto &stats = statistics_[village];
    if (c.rumors == stats.rumors)
        return;
    Profiler::Scope scope("rumor_contact");
    unsigned learned = c.rumors & ~stats.rumors;
    for (int rumor = 0; rumor < 4; ++rumor)
        if (learned & (1u << rumor))
        {
            ++stats.deliveries;
            receipts_.push_back({village, rumor, c.actor, time_, p});
            Profiler::Add("rumor_deliveries");
            Profiler::Event("rumor_delivered", "village=" + std::to_string(village) +
                                                   ";rumor=" + std::to_string(rumor) +
                                                   ";carrier=" + std::to_string(c.actor));
        }
    stats.rumors |= c.rumors;
    c.rumors |= stats.rumors;
}
void World::RebuildStatistics(int talking)
{
    Profiler::Scope scope("village_atmosphere");
    for (auto &v : statistics_)
    {
        v.present = v.walking = v.idle = v.talking = v.frozen = 0;
    }
    int records[5]{}, bread[5]{}, threats[5]{};
    for (size_t i = 0; i < citizens_.size(); ++i)
    {
        const auto &c = citizens_[i];
        auto &v = statistics_[c.homeVillage];
        if (!Active(c.actor))
            continue;
        int here = WorldLayout::VillageAt(GetPosition(c.actor));
        if (here >= 0)
            ++statistics_[here].present;
        if (c.frozen)
            ++v.frozen;
        else if (int(i) == talking)
            ++v.talking;
        else if (c.walking)
            ++v.walking;
        else
            ++v.idle;
        records[c.homeVillage] += c.recorded ? 1 : 0;
        bread[c.homeVillage] += c.gifted ? 1 : 0;
    }
    for (const auto &m : monsters_)
        if (m.health > 0 && Active(m.actor))
            for (int i = 0; i < 5; ++i)
                if (Distance(GetPosition(m.actor), WorldLayout::Settlements()[i].center) < 34)
                    ++threats[i];
    for (int i = 0; i < 5; ++i)
    {
        auto &v = statistics_[i];
        float event = i == 0 ? float(int(phase_)) * 14 : 0;
        float news = (v.rumors & 1 ? 4.f : 0) + (v.rumors & 2 ? 5.f : 0) + (v.rumors & 4 ? 7.f : 0) +
                     (v.rumors & 8 ? 10.f : 0);
        v.fear = std::clamp(10.f + i * 2 + event + news + std::min(threats[i], 16) * .5f + v.frozen * 3.f,
                            0.f, 100.f);
        v.calm = 100 - v.fear;
        v.trust = std::clamp(65.f + i * 2 + records[i] + std::min(v.deliveries, 8) * 1.5f, 0.f, 100.f);
        v.hope = std::clamp(72.f - i * 2 - event * .5f - v.frozen * 4 + records[i] * .7f + bread[i] * 3, 0.f,
                            100.f);
        v.mood = v.frozen ? "mourning" : v.fear >= 50 ? "anxious" : v.fear >= 25 ? "alert" : "calm";
    }
}
bool World::RunTests(std::string &report)
{
    auto check = [&](bool okay, const char *name) {
        if (!okay)
            report += std::string("FAIL: regional ") + name + "\n";
        return okay;
    };
    if (!check(citizens_.size() == 100 && monsters_.size() == 100, "population"))
        return false;
    for (int village = 0; village < 5; ++village)
        if (!check(std::count_if(citizens_.begin(), citizens_.end(),
                                 [&](const Citizen &c) { return c.homeVillage == village; }) == 20,
                   "20 home residents"))
            return false;
    if (!check(statistics_[1].rumors == 0 && statistics_[0].rumors == 1, "no initial remote knowledge"))
        return false;
    AdvanceEvent();
    for (int i = 0; i < 100; ++i)
        Update(.1f);
    if (!check(statistics_[1].rumors == 0, "no remote broadcast before travel"))
        return false;
    for (int i = 0; i < 500; ++i)
        Update(.1f);
    if (!check((statistics_[1].rumors & 1) != 0 && !receipts_.empty(), "physical courier delivery"))
        return false;
    for (const auto &r : receipts_)
        if (!check(Distance(r.position, WorldLayout::Plaza(r.village)) < 1.8f, "delivery position evidence"))
            return false;
    for (int i = 0; i < 500; ++i)
        Update(.1f);
    for (int village = 1; village < 5; ++village)
        if (!check((statistics_[village].rumors & 1) != 0, "connected rumor routes"))
            return false;
    scene_->Find(player_)->SetWorldPosition(GetPosition(citizens_[WorldLayout::Cleric].actor));
    health_ = 60;
    if (!check(Heal(WorldLayout::Cleric) && health_ == 100, "cleric healing"))
        return false;
    scene_->Find(player_)->SetWorldPosition(GetPosition(citizens_[25].actor));
    int money = coins_;
    if (!check(Buy(25) && coins_ == money - 5, "remote merchant remains open"))
        return false;
    AdvanceEvent();
    Update(.1f);
    if (!check(citizens_[WorldLayout::Keeper].frozen, "localized stillness"))
        return false;
    auto &m = monsters_[0];
    auto old = GetPosition(m.actor);
    auto actorId = m.actor;
    Position hero{};
    bool found = false;
    for (auto d : {Position{1, 0}, Position{-1, 0}, Position{0, 1}, Position{0, -1}})
    {
        Position p{old.x + d.x * .8f, old.y + d.y * .8f};
        bool clear = true;
        for (int j = 0; j <= 8; ++j)
            if (blocked_({old.x + d.x * j * .1f, old.y + d.y * j * .1f}))
                clear = false;
        if (clear)
        {
            hero = p;
            found = true;
            break;
        }
    }
    if (!check(found, "combat approach"))
        return false;
    scene_->Find(player_)->SetWorldPosition(hero);
    for (int i = 0; i < 3; ++i)
    {
        attackCooldown_ = 0;
        Attack({old.x - hero.x, old.y - hero.y});
    }
    if (!check(m.health == 0 && experience_ == 20 && Pickup() && !Pickup(), "kill and one-time loot"))
        return false;
    scene_->Find(player_)->SetWorldPosition({0, 2});
    for (int i = 0; i < 40; ++i)
        Update(.1f);
    if (!check(m.health == 36 && m.actor == actorId && respawns_ >= 1 &&
                   Distance(old, GetPosition(m.actor)) >= 12 &&
                   WorldLayout::VillageAt(GetPosition(m.actor)) < 0,
               "fast displaced respawn with actor reuse"))
        return false;
    size_t actors = scene_->Size();
    for (int i = 0; i < 200; ++i)
    {
        AddLoot({0, 2});
        Pickup();
    }
    if (!check(loot_.size() <= 128 && scene_->Size() == actors, "bounded reusable loot pool"))
        return false;
    for (const auto &v : statistics_)
        if (!check(v.calm >= 0 && v.calm <= 100 && v.fear >= 0 && v.fear <= 100 && v.trust >= 0 &&
                       v.trust <= 100 && v.hope >= 0 && v.hope <= 100,
                   "mood score range"))
            return false;
    report +=
        "PASS: 5x20 residents, 100 corridor monsters, physical-only rumor propagation, arrival receipts, "
        "local mood, remote trade, 3-second displaced respawn, stable actor and loot pools.\n";
    return true;
}
} // namespace Village
