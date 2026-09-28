#include "Village.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <queue>
namespace Village
{
namespace
{
constexpr int Width = 164, Height = 148;
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
    constexpr Reaction reactions[CitizenCount] = {
        Reaction::Stay,      Reaction::ShareBread, Reaction::Leave,     Reaction::Help,
        Reaction::Help,      Reaction::CloseShop,  Reaction::Family,    Reaction::Help,
        Reaction::CloseShop, Reaction::Stay,       Reaction::Help,      Reaction::CloseShop,
        Reaction::Leave,     Reaction::Help,       Reaction::Family,    Reaction::Help,
        Reaction::CloseShop, Reaction::Help,       Reaction::CloseShop, Reaction::Family,
        Reaction::Family,    Reaction::Help,       Reaction::Leave,     Reaction::Help,
        Reaction::Stay,      Reaction::ShareBread, Reaction::Help,      Reaction::CloseShop,
        Reaction::Stay,      Reaction::CloseShop,  Reaction::Help,      Reaction::Help,
        Reaction::Leave,     Reaction::Help,       Reaction::Family,    Reaction::CloseShop};
    for (size_t i = 0; i < actors.size(); ++i)
    {
        Citizen c;
        c.actor = actors[i];
        c.home = GetPosition(c.actor);
        c.goal = c.home;
        c.timer = 3 + float(i % 7);
        c.reaction = reactions[i % CitizenCount];
        if (i == 1 || i == 25)
        {
            c.service = Service::Baker;
            c.reaction = Reaction::ShareBread;
        }
        if (i == 5 || i == 16 || i == 18 || i == 29)
        {
            c.service = Service::Merchant;
            c.reaction = Reaction::CloseShop;
        }
        if (i == 13 || i == 17 || i == 26)
        {
            c.service = Service::Cleric;
            c.reaction = Reaction::Help;
        }
        if (i == 0 || i == 9 || i == 24)
            c.reaction = Reaction::Stay;
        if (i == 6 || i == 19 || i == 20)
            c.reaction = Reaction::Family;
        citizens_.push_back(c);
    }
    monstersGroup_ = scene.Create("VillageThreats", Scene::Kind::Group).GetId();
    const Position spawns[] = {{-20, -18}, {-24, -20}, {20, 18}, {24, 20}, {-36, 18},  {-40, 19},
                               {0, -34},   {2, -38},   {38, -5}, {42, -5}, {-20, 38},  {-24, 42},
                               {20, -40},  {23, -43},  {50, 18}, {54, 19}, {-58, -18}, {-62, -18}};
    for (size_t i = 0; i < std::size(spawns); ++i)
    {
        auto p = spawns[i];
        int cell = Cell(p);
        if (cell < 0)
            continue;
        if (!navigation_[cell])
        {
            bool found = false;
            for (int radius = 1; radius <= 6 && !found; ++radius)
                for (int dy = -radius; dy <= radius && !found; ++dy)
                    for (int dx = -radius; dx <= radius && !found; ++dx)
                    {
                        Position candidate{p.x + dx, p.y + dy};
                        int id = Cell(candidate);
                        if (id >= 0 && navigation_[id])
                        {
                            p = Center(id);
                            found = true;
                        }
                    }
        }
        if (blocked_(p))
        {
            Profiler::Event("village_spawn_failed", std::to_string(i));
            continue;
        }
        auto &a = scene.Create("ForestCreature", Scene::Kind::Enemy, monstersGroup_);
        a.SetWorldPosition(p);
        a.SetDataIndex(int(monsters_.size()));
        monsters_.push_back({a.GetId(), p, 36, int(i % 2), 0, 0});
    }
    notice_ = L"36명의 주민과 대화하고 호수 풍경을 기록해 보세요. F3으로 탈색 사건을 시작합니다.";
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
    std::vector<int> previous(Width * Height, -1);
    std::queue<int> open;
    open.push(start);
    previous[start] = start;
    while (!open.empty() && previous[finish] < 0)
    {
        int id = open.front();
        open.pop();
        Profiler::Add("npc_path_cells_visited");
        int x = id % Width, y = id / Width;
        int direction = 0;
        for (auto d : {Position{1, 0}, Position{-1, 0}, Position{0, 1}, Position{0, -1}})
        {
            bool clear = (edges_[id] & (1 << direction++)) != 0;
            if (!clear)
                continue;
            int nx = x + int(d.x), ny = y + int(d.y);
            if (nx < 0 || nx >= Width || ny < 0 || ny >= Height)
                continue;
            int next = ny * Width + nx;
            if (navigation_[next] && previous[next] < 0)
            {
                previous[next] = id;
                open.push(next);
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
void World::Move(Scene::ActorId id, Position goal, float amount)
{
    if (!Active(id))
        return;
    auto p = GetPosition(id);
    float d = Distance(p, goal);
    if (d < .001f)
        return;
    float scale = std::min(1.f, amount / d);
    Position q{p.x + (goal.x - p.x) * scale, p.y + (goal.y - p.y) * scale};
    if (!blocked_(q))
        scene_->Find(id)->SetWorldPosition(q);
    else
    {
        q = {p.x + (goal.x - p.x) * scale, p.y};
        if (!blocked_(q))
            p = q;
        q = {p.x, p.y + (goal.y - p.y) * scale};
        if (!blocked_(q))
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
    for (size_t i = 0; i < citizens_.size(); ++i)
    {
        auto &c = citizens_[i];
        c.timer = 0;
        Position goal = c.home;
        if (phase == Phase::Warning && c.reaction == Reaction::Family)
            goal = citizens_[i == 6    ? 19
                             : i == 14 ? 32
                             : i == 19 ? 20
                             : i == 20 ? 19
                             : i == 34 ? 22
                                       : i]
                       .home;
        if (phase >= Phase::Evacuation && c.reaction != Reaction::Stay)
            goal = {-6.f + float(i % 8) * 1.7f, 18.f + float(i / 8) * 1.7f};
        Plan(c, goal);
    }
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
    if (phase_ == Phase::Daily)
        return c.walking ? L"생업 이동" : L"일상 · 휴식";
    if (c.reaction == Reaction::Stay)
        return L"떠나지 않음";
    if (phase_ == Phase::Warning)
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
        if (int(i) == talking)
            continue;
        auto before = GetPosition(c.actor);
        if (c.step < c.route.size())
        {
            Move(c.actor, c.route[c.step], dt * (phase_ >= Phase::Evacuation ? 2.f : 1.05f));
            if (Distance(GetPosition(c.actor), c.route[c.step]) < .12f)
                ++c.step;
        }
        else if (phase_ == Phase::Daily)
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
    }
    for (auto &m : monsters_)
    {
        Profiler::Add("village_monsters_tested");
        m.flash = std::max(0.f, m.flash - dt);
        if (m.health <= 0 || !Active(m.actor) || IsFrozen(GetPosition(m.actor)))
            continue;
        m.cooldown = std::max(0.f, m.cooldown - dt);
        auto p = GetPosition(m.actor), hero = GetPosition(player_);
        bool safe = std::abs(hero.x) < 12 && hero.y > -10 && hero.y < 29;
        if (!safe && Distance(hero, p) < 7 && Distance(hero, m.home) < 10)
        {
            if (Distance(hero, p) > 1.1f)
                Move(m.actor, hero, dt * 1.6f);
            else if (immunity_ <= 0 && m.cooldown <= 0 && talking < 0)
            {
                health_ = std::max(0, health_ - 8);
                immunity_ = 1;
                m.cooldown = 1.4f;
                if (IsDead())
                    notice_ = L"쓰러졌습니다. R로 마을에서 회복합니다.";
            }
        }
        else if (Distance(p, m.home) > .3f)
            Move(m.actor, m.home, dt * 1.2f);
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
    if (c.service == Service::Baker && phase_ == Phase::Warning && !c.gifted)
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
    std::wstring text;
    if (phase_ == Phase::Daily)
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
    if (c.service == Service::Merchant && phase_ == Phase::Warning)
        text = L"곧 문을 닫을 참이오. 길에서 쓸 물약이 필요하면 지금 챙기시오. 사람부터 보내야지.";
    if (c.service == Service::Merchant)
        text += phase_ >= Phase::Evacuation ? L" [대피 중 · 거래 중단]" : L" [1] 동전 5개로 회복약 구입";
    if (c.service == Service::Cleric)
        text += L" [2] 무료 치료 · 탈색을 되돌리는 힘은 없음";
    return text;
}
bool World::Buy(int id)
{
    Profiler::Scope scope("village_trade");
    if (id < 0 || size_t(id) >= citizens_.size() || citizens_[id].service != Service::Merchant ||
        citizens_[id].frozen || phase_ >= Phase::Evacuation || coins_ < 5 || IsDead())
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
            auto &drop = scene_->Create("VillageLoot", Scene::Kind::Loot, monstersGroup_);
            drop.SetWorldPosition(p);
            drop.SetDataIndex(int(loot_.size()));
            loot_.push_back({drop.GetId(), false});
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
bool World::RunTests(std::string &report)
{
    auto fail = [&](bool value, const char *name) {
        if (!value)
            report += std::string("FAIL: village ") + name + "\n";
        return value;
    };
    if (!fail(citizens_.size() == CitizenCount && monsters_.size() == 18, "population"))
        return false;
    auto before = GetPosition(citizens_[16].actor);
    for (int i = 0; i < 120; ++i)
        Update(.1f);
    if (!fail(Distance(before, GetPosition(citizens_[16].actor)) > .1f, "daily work movement"))
        return false;
    health_ = 60;
    scene_->Find(player_)->SetWorldPosition(GetPosition(citizens_[17].actor));
    if (!fail(Heal(17) && health_ == 100, "cleric healing"))
        return false;
    scene_->Find(player_)->SetWorldPosition(GetPosition(citizens_[16].actor));
    int money = coins_;
    if (!fail(Buy(16) && coins_ == money - 5, "merchant transaction"))
        return false;
    Talk(0);
    Talk(1);
    Talk(2);
    Sketch({10, -5});
    if (!fail(story_, "recording people and pet"))
        return false;
    AdvanceEvent();
    int pots = potions_;
    Talk(1);
    Talk(1);
    if (!fail(potions_ == pots + 1, "one-time bread sharing"))
        return false;
    AdvanceEvent();
    if (!fail(!Buy(16), "shops close on evacuation"))
        return false;
    for (int i = 0; i < 590; ++i)
        Update(.1f);
    for (const auto &c : citizens_)
        if (c.reaction != Reaction::Stay &&
            !fail(Distance(GetPosition(c.actor), c.goal) < 1.5f, "evacuation arrival"))
            return false;
    AdvanceEvent();
    Update(.1f);
    auto frozenPosition = GetPosition(citizens_[24].actor);
    float frozenTime = citizens_[24].animation;
    Update(.1f);
    if (!fail(citizens_[24].frozen && Distance(frozenPosition, GetPosition(citizens_[24].actor)) == 0 &&
                  citizens_[24].animation == frozenTime,
              "frozen animation and position"))
        return false;
    if (!fail(IsFrozen({0, -16}) && !IsFrozen({0, 18}), "local irreversible stillness"))
        return false;
    AdvanceEvent();
    if (!fail(phase_ == Phase::Stillness, "cannot undo death"))
        return false;
    auto &m = monsters_[0];
    auto pos = GetPosition(m.actor);
    scene_->Find(player_)->SetWorldPosition({pos.x - .8f, pos.y});
    for (int i = 0; i < 3; ++i)
    {
        attackCooldown_ = 0;
        Attack({1, 0});
    }
    if (!fail(m.health == 0 && experience_ == 20 && Pickup() && !Pickup(), "combat and one-time loot"))
        return false;
    report += "PASS: 36 village citizens, 18 monsters, trade, one-time bread, evacuation closure, local "
              "stillness, sketch records and combat loot.\n";
    return true;
}
} // namespace Village
