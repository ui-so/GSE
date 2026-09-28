#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <windows.h>
#include <gl/GL.h>
#include "DrawStats.h"
#include "RenderBatch.h"
#include "Profiler.h"
#include "TerrainBatch.h"
#include <map>
#include <queue>
#include <string>
#include <vector>
#include "Visuals.h"
#include "FirstLevel.h"
#include "Village.h"
#include "LevelView.h"
#include "AssetCache.h"
#include "SceneModels.h"
#include "SceneRender.h"

#undef NDEBUG
#include <cassert>
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
struct V
{
    float x, y;
    V operator+(V b) const
    {
        return {x + b.x, y + b.y};
    }
    V operator-(V b) const
    {
        return {x - b.x, y - b.y};
    }
    V operator*(float s) const
    {
        return {x * s, y * s};
    }
};
struct C
{
    float r, g, b, a = 1;
};
float length(V v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}
const C ink{.76f, .8f, .75f}, gold{.88f, .70f, .39f}, teal{.36f, .83f, .79f};
int width = 1280, height = 800, stage = 0;
float elapsed = 0, worldTime = 0;
bool keys[256]{}, paused = false, active = true, running = true, finished = false;
V camera{0, 2};
Scene::SceneGraph tutorialScene;
Scene::ActorId tutorialPlayer = 0, terrainActor = 0, particlesActor = 0, markerActor = 0, hudActor = 0;
Scene::ActorId sceneryGroup = 0, characterGroup = 0;
Scene::ActorId npcActors[Village::CitizenCount]{}, seedActor = 0, shrineActor = 0;
V ActorPosition(Scene::ActorId id)
{
    auto actor = tutorialScene.Find(id);
    auto p = actor ? actor->GetWorldPosition() : Scene::Position{};
    return {p.x, p.y};
}
bool ActorVisible(Scene::ActorId id)
{
    auto actor = tutorialScene.Find(id);
    return actor && actor->IsVisibleInHierarchy();
}
bool ActorActive(Scene::ActorId id)
{
    auto actor = tutorialScene.Find(id);
    return actor && actor->IsActiveInHierarchy();
}
V PlayerPosition()
{
    return ActorPosition(tutorialPlayer);
}
void SetPlayerPosition(V p)
{
    if (auto actor = tutorialScene.Find(tutorialPlayer))
        actor->SetWorldPosition({p.x, p.y});
}
constexpr int NPC_COUNT = Village::CitizenCount;
constexpr float MIN_X = Village::MinX, MAX_X = Village::MaxX, MIN_Y = Village::MinY, MAX_Y = Village::MaxY;
int facing = 0;
float actionTime = 0;
bool moving = false, postEnabled = true;
HDC dc;
HGLRC rc;
HFONT font;
HGDIOBJ oldFont;
HWND windowHandle;
std::map<wchar_t, GLuint> glyphs;
std::wstring speaker, dialog;
FirstLevel::World firstWorld;
bool firstLevelMode = false;
Village::World villageWorld;
Scene::Position villageFacing{.7071f, .7071f};
int pending = -1;
int talkingNpc = -1;
struct Object
{
    Scene::ActorId actor;
    int type;
    float scale;
    int id;
    Object(V position, int objectType, float objectScale, int dataIndex);
};
std::vector<Object> objects;
std::map<std::pair<int, int>, Scene::ActorId> tutorialChunks;
Object::Object(V position, int objectType, float objectScale, int dataIndex)
    : type(objectType), scale(objectScale), id(dataIndex)
{
    const Scene::Kind kinds[] = {Scene::Kind::Tree,   Scene::Kind::House,   Scene::Kind::Npc,
                                 Scene::Kind::Well,   Scene::Kind::Shrine,  Scene::Kind::Seed,
                                 Scene::Kind::Player, Scene::Kind::Brazier, Scene::Kind::Wildlife};
    auto parent = type == 2 || type == 6 || type == 8 ? characterGroup : sceneryGroup;
    if (type == 0 || type == 1)
    {
        auto key = std::make_pair(int(std::floor(position.x / 16)), int(std::floor(position.y / 16)));
        auto &chunk = tutorialChunks[key];
        if (!chunk)
            chunk = tutorialScene.Create("SpatialChunk", Scene::Kind::Group, sceneryGroup).GetId();
        parent = chunk;
    }
    auto &node = tutorialScene.Create("TutorialObject", kinds[type], parent);
    actor = node.GetId();
    node.SetWorldPosition({position.x, position.y});
    node.SetDataIndex(int(objects.size()));
    if (type == 2)
        npcActors[id] = actor;
    if (type == 4)
        shrineActor = actor;
    if (type == 5)
        seedActor = actor;
    if (type == 6)
        tutorialPlayer = actor;
    if (type == 7)
    {
        auto &flame = tutorialScene.Create("Flame", Scene::Kind::Flame, actor);
        flame.SetDataIndex(int(objects.size()));
        flame.SetLocalElevation(8 * objectScale);
    }
}
struct NPC
{
    V p;
    const wchar_t *name;
    const wchar_t *line;
    C color;
};
NPC npcs[] = {
    {{-1, 0},
     L"장례지기 · 마라",
     L"떠난 이를 부르는 일과, 돌아오게 하는 일은 다르지. 오늘은 그저 기억하자.",
     {.40f, .48f, .48f}},
    {{3, 2},
     L"빵 굽는 오렌",
     L"빵이 돌처럼 굳었어. 늑대가 오면 던져. 먹이는 건 권하지 않아.",
     {.67f, .43f, .26f}},
    {{-3, 4}, L"우물지기", L"숲의 씨앗이 밤에도 빛난대. 등불 기름값은 좀 내려가려나.", {.37f, .48f, .37f}},
    {{4, -1},
     L"수습 약초사",
     L"살아 있는 건 모두 숨을 쉴까? 씨앗에 귀를 대 봤는데… 아직 모르겠어.",
     {.42f, .53f, .39f}},
    {{-4, -2},
     L"늙은 파수꾼",
     L"동쪽 호수에는 길이 있어. 물속으로 들어가지는 마. 밤에는 깊이를 알 수 없거든.",
     {.43f, .42f, .51f}},
    {{1, 5}, L"여행 상인", L"불멸의 양말! 구멍은 나지만, 영수증은 영원히 남습니다.", {.58f, .39f, .40f}},
    {{-5, 2}, L"마을 아이", L"엄마는 별이 된대. 그런데 흐린 날에는 어디서 쉬는 걸까?", {.47f, .53f, .62f}},
    {{2, -4},
     L"종지기",
     L"종은 돌아오라는 소리가 아니야. 우리가 아직 기억한다는 소리지.",
     {.46f, .44f, .33f}},
    {{-7, 0},
     L"대장장이 단",
     L"칼보다 경첩을 더 많이 고치는 날이 좋은 날이지. 오늘도 좋은 날이야.",
     {.5f, .3f, .2f}},
    {{-3, -5},
     L"묘지기 연",
     L"무덤 곁에 꽃을 심어. 흙 아래의 삶과 흙 위의 삶을 함께 돌보는 거야.",
     {.3f, .4f, .3f}},
    {{6, -3}, L"목수 미르", L"다리를 오래 쓰려면 나무를 잘 알아야 해. 사람도 그럴까?", {.5f, .4f, .3f}},
    {{-7, 5}, L"재봉사 소란", L"망토 수선은 공짜. 하지만 유령에게 빌려준 옷은 직접 찾아와.", {.5f, .3f, .5f}},
    {{4, 8},
     L"양봉가",
     L"벌은 여왕을 잃으면 새 여왕을 키워. 슬픔이 없는 건지, 살아야 해서인지.",
     {.6f, .5f, .2f}},
    {{-2, 8}, L"수습 사제", L"기도문은 외웠는데 위로하는 말은 아직 배우는 중이야.", {.4f, .4f, .5f}},
    {{8, -6}, L"호수의 어부", L"물 위의 빛을 따라가지 마. 물고기도 그 빛을 피하더군.", {.3f, .4f, .5f}},
    {{-8, -7},
     L"숲지기",
     L"숲에는 사슴과 토끼, 멧돼지가 있어. 가까이 가면 놀라니 거리를 두고 지켜봐.",
     {.3f, .5f, .3f}},
    {{-12, 0},
     L"잡화상 · 루카",
     L"고장 난 나침반도 팔지. 항상 같은 곳을 가리켜서 믿음직해.",
     {.6f, .4f, .2f}},
    {{0, 10},
     L"성직자 · 세라",
     L"죽음을 되돌릴 수는 없어요. 살아 있는 사람의 손은 잡아 줄 수 있지요.",
     {.6f, .6f, .7f}},
    {{-12, 4}, L"행상 · 니코", L"짐은 싸 두었지만 이웃이 떠나기 전에는 나도 떠나지 않겠소.", {.6f, .5f, .2f}},
    {{8, 10}, L"어머니 · 엘린", L"리오가 파란 꽃을 꺾으러 갔어요. 그 애부터 찾아야 해요.", {.6f, .3f, .4f}},
    {{6, 10},
     L"꽃을 좋아하는 리오",
     L"이 꽃도 내일이면 색을 잃을까? 그림에는 파랗게 남겨 줘.",
     {.3f, .4f, .7f}},
    {{0, 18}, L"운송인 · 토르", L"수레에 사람부터 태우자. 짐은 나중 일이야.", {.5f, .4f, .3f}},
    {{-8, 18}, L"농부 · 베른", L"밭을 떠나도 올해 씨앗 한 줌은 가져갈 거야.", {.4f, .5f, .2f}},
    {{-20, 0}, L"숲 간호인 · 린", L"다친 이가 있으면 남쪽 집결지로 데려와 줘.", {.3f, .5f, .4f}},
    {{1, -16},
     L"묘목지기 · 하온",
     L"이 나무는 내 가족이 심었어. 마지막까지 옆에 있고 싶어.",
     {.4f, .5f, .3f}},
    {{12, 4},
     L"제빵 수습 · 미나",
     L"스승님은 마지막 반죽까지 굽겠대. 오늘 빵값은 안 받을 거야.",
     {.6f, .4f, .3f}},
    {{-4, 12}, L"수도자 · 에다", L"기도보다 담요가 먼저 필요한 날도 있지요.", {.5f, .5f, .6f}},
    {{-12, -4},
     L"염색 장인 · 모아",
     L"내 손의 물감은 씻기지 않는데 저 나무의 색은 사라지는구나.",
     {.6f, .3f, .6f}},
    {{-8, -10},
     L"기록 보관인 · 이안",
     L"날짜보다 이름을 적어 줘. 이곳에 누가 살았는지 남도록.",
     {.4f, .4f, .5f}},
    {{12, 0},
     L"물감 상인 · 오즈",
     L"파란 물감이 귀해. 하늘을 먼저 그리면 외상은 생각해 보지.",
     {.3f, .4f, .7f}},
    {{0, -20}, L"약초꾼 · 세린", L"잎맥이 멈췄어. 시든 것이 아니라 시간이 멎은 것 같아.", {.3f, .6f, .3f}},
    {{-20, 8}, L"경비병 · 로크", L"외곽 짐승은 마을까지 쫓아오지 못하게 하겠소.", {.4f, .4f, .5f}},
    {{24, -5}, L"어부 · 니아", L"호수가 흐르는 동안에는 물소리를 기억하고 싶어.", {.3f, .5f, .6f}},
    {{-12, 8}, L"산파 · 로엔", L"새 생명이 온 집부터 챙기고 떠나야지.", {.5f, .5f, .3f}},
    {{20, 18}, L"목동 · 파즈", L"염소보다 내 길 찾기가 더 나빠. 오늘은 녀석을 따라갈래.", {.4f, .5f, .3f}},
    {{4, 14}, L"주점 주인 · 도란", L"마지막 잔은 무료야. 잔까지 가져가지는 말고.", {.6f, .4f, .3f}}};

V seed{0, -13}, shrine{10, -5};
bool desaturatedGeometry = false;
void color(C c)
{
    if (desaturatedGeometry)
        c.r = c.g = c.b = c.r * .299f + c.g * .587f + c.b * .114f;
    RenderBatch::Color4f(c.r, c.g, c.b, c.a);
}
void poly(std::initializer_list<V> vs, C c)
{
    color(c);
    RenderBatch::Begin(GL_POLYGON);
    for (auto v : vs)
        RenderBatch::Vertex2f(v.x, v.y);
    RenderBatch::End();
}
void rect(float x, float y, float w, float h, C c)
{
    poly({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, c);
}
void ellipse(V p, float rx, float ry, C c)
{
    color(c);
    RenderBatch::Begin(GL_TRIANGLE_FAN);
    RenderBatch::Vertex2f(p.x, p.y);
    for (int i = 0; i <= 48; i++)
    {
        float a = i * 6.2831853f / 48;
        RenderBatch::Vertex2f(p.x + cosf(a) * rx, p.y + sinf(a) * ry);
    }
    RenderBatch::End();
}
void line(V a, V b, C c, float w = 1)
{
    color(c);
    RenderBatch::LineWidth(w);
    RenderBatch::Begin(GL_LINES);
    RenderBatch::Vertex2f(a.x, a.y);
    RenderBatch::Vertex2f(b.x, b.y);
    RenderBatch::End();
    RenderBatch::LineWidth(1);
}
V project(V p, float z = 0)
{
    V d = p - camera;
    return {width * .5f + (d.x - d.y) * 31, height * .53f + (d.x + d.y) * 15.5f - z};
}
void text(float x, float y, const std::wstring &s, C c = ink)
{
    color(c);
    RenderBatch::Text(dc, x, y, s);
}
void LevelText(float x, float y, const std::wstring &content, float r, float g, float b)
{
    text(x, y, content, {r, g, b});
}
void wrapped(float x, float y, const std::wstring &s, int columns = 58)
{
    int row = 0;
    for (size_t i = 0; i < s.size(); i += columns)
        text(x, y + row++ * 28, s.substr(i, columns));
}
void glow(V p, float r, C c)
{
    for (int i = 8; i >= 1; i--)
    {
        C a = c;
        a.a = .018f * (9 - i);
        ellipse(p, r * i / 8, r * i / 8 * .65f, a);
    }
}
void tile(int x, int y, C c)
{
    poly({project({float(x), float(y)}), project({float(x + 1), float(y)}),
          project({float(x + 1), float(y + 1)}), project({float(x), float(y + 1)})},
         c);
}
float hash(int x, int y)
{
    unsigned n = unsigned(x) * 374761393u + unsigned(y) * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (n & 65535) / 65535.f;
}
bool lake(V p)
{
    float x = (p.x - 16) / 5.5f, y = (p.y + 7) / 8;
    return x * x + y * y < 1;
}
struct CollisionShape
{
    V position;
    int type;
};
std::map<std::pair<int, int>, std::vector<CollisionShape>> collisionGrid;
bool collisionGridReady = false;
void RebuildCollisionGrid()
{
    Profiler::Scope scope("collision_grid_rebuild");
    collisionGrid.clear();
    for (const auto &object : objects)
    {
        if ((object.type != 0 && object.type != 1) || !ActorActive(object.actor))
            continue;
        V p = ActorPosition(object.actor);
        collisionGrid[{int(std::floor(p.x / 4)), int(std::floor(p.y / 4))}].push_back({p, object.type});
    }
    collisionGridReady = true;
}
bool blocked(V p)
{
    Profiler::Add("collision_queries");
    if (p.x < MIN_X || p.x > MAX_X || p.y < MIN_Y || p.y > MAX_Y || lake(p))
        return true;
    if (!collisionGridReady)
        RebuildCollisionGrid();
    int x = int(std::floor(p.x / 4)), y = int(std::floor(p.y / 4));
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            auto found = collisionGrid.find({x + dx, y + dy});
            if (found == collisionGrid.end())
                continue;
            for (const auto &shape : found->second)
            {
                Profiler::Add("collision_candidates_tested");
                if (shape.type == 1 && fabsf(p.x - shape.position.x) < 1.55f &&
                    fabsf(p.y - shape.position.y) < 1.45f)
                    return true;
                if (shape.type == 0 && length(p - shape.position) < .45f)
                    return true;
            }
        }
    return false;
}
struct Beast
{
    Scene::ActorId actor;
    V home, velocity;
    int kind;
    float phase;
    bool fleeing = false;
};
std::vector<Beast> beasts;
void initWildlife()
{
    beasts.clear();
    for (int i = 0; i < 15; i++)
    {
        V p{i < 6    ? -12.f + float(i % 3) * 2.3f
            : i < 10 ? 8.f + float(i % 3) * 2
                     : -23.f + float(i % 4) * 2.5f,
            i < 6    ? -10.f - float(i / 3) * 3
            : i < 10 ? 10.f + float(i % 2) * 3
                     : 18.f + float(i % 3) * 3};
        for (int k = 0; k < 50 && blocked(p); k++)
            p = p + V{.35f, .27f};
        objects.push_back({p, 8, 1, i});
        beasts.push_back({objects.back().actor, p, {0, 0}, i % 3, float(i), false});
    }
}
void updateWildlife(float dt)
{
    Profiler::Scope scope("wildlife_simulation");
    tutorialScene.Update(dt);
    RebuildCollisionGrid();
    for (size_t i = 0; i < beasts.size(); i++)
    {
        auto &a = beasts[i];
        if (!ActorActive(a.actor) ||
            villageWorld.IsFrozen({ActorPosition(a.actor).x, ActorPosition(a.actor).y}))
            continue;
        a.phase += dt;
        a.fleeing = length(ActorPosition(a.actor) - PlayerPosition()) < 3.6f;
        V direction;
        if (a.fleeing)
            direction = ActorPosition(a.actor) - PlayerPosition();
        else if (length(ActorPosition(a.actor) - a.home) > 4)
            direction = a.home - ActorPosition(a.actor);
        else
            direction = {sinf(a.phase * .33f + float(i)), cosf(a.phase * .27f + float(i) * 2)};
        float speed = a.fleeing ? (a.kind == 1 ? 3.7f : 2.7f) : (.35f + .13f * a.kind);
        if (length(direction) > .01f)
            direction = direction * (speed / length(direction));
        a.velocity = {0, 0};
        V q = ActorPosition(a.actor) + V{direction.x * dt, 0};
        if (!blocked(q))
        {
            a.velocity.x = direction.x;
            tutorialScene.Find(a.actor)->SetWorldPosition({q.x, q.y});
        }
        q = ActorPosition(a.actor) + V{0, direction.y * dt};
        if (!blocked(q))
        {
            a.velocity.y = direction.y;
            tutorialScene.Find(a.actor)->SetWorldPosition({q.x, q.y});
        }
    }
}
void fire(V p, float size)
{
    SceneModels::Draw(SceneModels::Kind::Brazier, p.x, p.y, size);
}
void animal(V p, const Beast &a)
{
    float d = a.velocity.x - a.velocity.y < 0 ? -1.f : 1.f;
    float gait = sinf(a.phase * (a.fleeing ? 18 : 7)) * 4;
    C coat = a.kind == 0 ? C{.48f, .31f, .18f} : a.kind == 1 ? C{.62f, .59f, .48f} : C{.26f, .24f, .22f};
    float scale = a.kind == 1 ? .7f : 1.f;
    Visuals::SoftShadow(p.x + 7, p.y + 2, 26 * scale, .35f, .8f);
    for (int j = 0; j < 4; j++)
    {
        float x = (j - 1.5f) * 7 * scale;
        line(p + V{x, -14 * scale},
             p + V{x + sinf(a.phase * 9 + j * 3) * 3, powf(-1.f, float(j)) * gait * .2f}, {.17f, .15f, .13f},
             3 * scale);
    }
    ellipse(p + V{0, -16 * scale}, 18 * scale, 10 * scale, coat);
    ellipse(p + V{-3, -20 * scale}, 12 * scale, 6 * scale, {coat.r * 1.18f, coat.g * 1.18f, coat.b * 1.18f});
    V head = p + V{d * 17 * scale, -25 * scale};
    if (a.kind == 0)
    {
        line(head + V{-d * 3, 2}, p + V{d * 10, -18}, coat, 8);
        line(head, head + V{-d * 4, -19}, {.52f, .45f, .33f}, 2);
        line(head + V{-d * 2, -10}, head + V{d * 5, -17}, {.52f, .45f, .33f}, 2);
    }
    else if (a.kind == 1)
    {
        ellipse(head + V{-3, -10}, 3, 12, coat);
        ellipse(head + V{3, -9}, 3, 11, coat);
    }
    else
    {
        poly({head + V{-6, 0}, head + V{-5, -12}, head + V{1, -4}}, coat);
    }
    ellipse(head, 8 * scale, 6 * scale, coat);
    ellipse(head + V{d * 4, -2}, 1.2f, 1.2f, {.04f, .04f, .025f});
    if (a.kind == 2)
        line(head + V{d * 6, 4}, head + V{d * 10, 0}, {.8f, .75f, .61f}, 2);
}
void initWorld()
{
    Profiler::Scope scope("tutorial_world_generate");
    collisionGridReady = false;
    tutorialScene.Clear();
    tutorialChunks.clear();
    objects.clear();
    sceneryGroup = tutorialScene.Create("Scenery", Scene::Kind::Group).GetId();
    characterGroup = tutorialScene.Create("Characters", Scene::Kind::Group).GetId();
    auto &terrain = tutorialScene.Create("TerrainAndShore", Scene::Kind::Terrain, sceneryGroup);
    terrain.SetLayer(Scene::Layer::Ground);
    terrainActor = terrain.GetId();
    auto &particles = tutorialScene.Create("Atmosphere", Scene::Kind::Particles);
    particles.SetLayer(Scene::Layer::Effects);
    particlesActor = particles.GetId();
    auto &marker = tutorialScene.Create("Objective", Scene::Kind::Marker);
    marker.SetLayer(Scene::Layer::Effects);
    markerActor = marker.GetId();
    auto &hud = tutorialScene.Create("HUD", Scene::Kind::Hud);
    hud.SetLayer(Scene::Layer::Interface);
    hudActor = hud.GetId();
    V houses[] = {{-6, -4}, {0, -6}, {5, 5},   {-6, 7},  {7, 1},   {-15, -7}, {-15, 7},
                  {-7, 14}, {8, 15}, {-24, 2}, {0, -24}, {28, -7}, {23, 22}};
    for (int i = 0; i < int(std::size(houses)); i++)
        objects.push_back({houses[i], 1, 1, i});
    for (int x = -79; x <= 82; x += 2)
        for (int y = -75; y <= 70; y += 2)
        {
            V p{float(x) + hash(x, y), float(y) + hash(y, x)};
            bool reserved = length(p - V{0, -16}) < 7 || fabsf(p.y + 18) < 2 || fabsf(p.x - 20) < 2;
            for (auto &npc : npcs)
                if (length(p - npc.p) < 3)
                    reserved = true;
            for (auto housePosition : houses)
                if (length(p - housePosition) < 4)
                    reserved = true;
            if (!reserved && (p.y < -8 || p.x < -9 || p.y > 10 || p.x > 10) && !lake(p) &&
                fabsf(p.x) > 1.8f && !(p.x > 1 && p.y > -7 && p.y < -3) && length(p - shrine) > 2 &&
                hash(x, y) > .42f && fabsf(p.y - 18) > 2 && fabsf(p.x + 20) > 2)
                objects.push_back({p, 0, .75f + hash(y, x) * .65f, 0});
        }
    objects.push_back({{3, -17}, 0, 1, 0});
    objects.push_back({{-3, -15}, 0, 1, 0});
    for (int i = 0; i < NPC_COUNT; i++)
        objects.push_back({npcs[i].p, 2, 1, i});
    objects.push_back({{-2, 2}, 3, 1, 0});
    objects.push_back({shrine, 4, 1, 0});
    objects.push_back({seed, 5, 1, 0});
    objects.push_back({{2, 0}, 7, 1, 0});
    objects.push_back({{-5, 5}, 7, .75f, 1});
    objects.push_back({{-20, 18}, 7, 1, 2});
    objects.push_back({{0, 2}, 6, 1, 0});
    RebuildCollisionGrid();
    objects.push_back({{0, -18}, 7, 1, 3});
    initWildlife();
    villageWorld.Initialize(tutorialScene, std::vector<Scene::ActorId>(npcActors, npcActors + NPC_COUNT),
                            tutorialPlayer, [](Scene::Position p) { return blocked({p.x, p.y}); });
}
GLuint treeList = 0;
void treeGeometry(V p, float s)
{

    rect(p.x - 3 * s, p.y - 36 * s, 6 * s, 36 * s, {.16f, .15f, .12f});
    for (int i = 0; i < 3; i++)
    {
        float y = p.y - 19 * s - i * 22 * s, w = (31 - i * 5) * s;
        poly({{p.x, y - 47 * s}, {p.x + w, y}, {p.x, y - 5 * s}, {p.x - w, y}},
             i % 2 ? C{.075f, .17f, .15f} : C{.09f, .21f, .17f});
        for (int k = 0; k < 7; k++)
        {
            float q = float(k) / 7;
            float yy = y - 36 * s + q * 33 * s;
            float ww = w * q;
            line({p.x, yy - 5 * s}, {p.x - ww, yy}, {.12f, .25f, .19f, .8f}, 2);
            line({p.x, yy - 5 * s}, {p.x + ww, yy}, {.06f, .14f, .12f, .8f}, 2);
        }
        line({p.x, y - 43 * s}, {p.x - w * .85f, y - 2 * s}, {.16f, .27f, .22f, .7f});
    }
}
void tree(V p, float s)
{
    if (!treeList)
    {
        treeList = glGenLists(1);
        RenderBatch::NewList(treeList, GL_COMPILE);
        treeGeometry({0, 0}, 1);
        RenderBatch::EndList();
    }
    RenderBatch::PushMatrix();
    RenderBatch::Translatef(p.x, p.y, 0);
    RenderBatch::Scalef(s, s, 1);
    RenderBatch::CallList(treeList);
    RenderBatch::PopMatrix();
}
void house(V p, int id)
{

    poly({{p.x - 65, p.y - 26}, {p.x, p.y + 7}, {p.x, p.y - 66}, {p.x - 65, p.y - 96}}, {.25f, .27f, .25f});
    poly({{p.x, p.y + 7}, {p.x + 65, p.y - 26}, {p.x + 65, p.y - 96}, {p.x, p.y - 66}}, {.18f, .22f, .21f});
    poly({{p.x - 77, p.y - 92}, {p.x - 8, p.y - 142}, {p.x + 76, p.y - 92}, {p.x, p.y - 56}},
         {.26f, .25f, .23f});
    poly({{p.x - 77, p.y - 92}, {p.x - 8, p.y - 142}, {p.x - 18, p.y - 90}, {p.x, p.y - 56}},
         {.36f, .33f, .29f});
    for (int i = 0; i < 4; i++)
        line({p.x - 62 + i * 15.f, p.y - 96 - i * 7.f}, {p.x - 8 + i * 17.f, p.y - 64 - i * 8.f},
             {.12f, .16f, .15f, .65f});
    rect(p.x + 16, p.y - 40, 14, 30, {.10f, .12f, .11f});
    poly({{p.x - 44, p.y - 61}, {p.x - 27, p.y - 52}, {p.x - 27, p.y - 33}, {p.x - 44, p.y - 42}},
         {.85f, .58f, .25f});
    glow({p.x - 35, p.y - 47}, 42, gold);
    line({p.x - 36, p.y - 56}, {p.x - 36, p.y - 38}, {.23f, .20f, .14f}, 2);
    for (int j = 0; j < 4; j++)
    {
        float t = j / 3.f;
        line({p.x - 65 + t * 65, p.y - 96 + t * 30}, {p.x - 65 + t * 65, p.y - 26 + t * 33},
             {.115f, .105f, .08f}, 4);
        line({p.x + t * 65, p.y - 66 - t * 30}, {p.x + t * 65, p.y + 7 - t * 33}, {.095f, .10f, .085f}, 4);
    }
    line({p.x - 64, p.y - 44}, {p.x, p.y - 11}, {.14f, .13f, .105f}, 4);
    line({p.x, p.y - 11}, {p.x + 64, p.y - 44}, {.12f, .12f, .10f}, 4);
    for (int row = 0; row < 6; row++)
        for (int col = 0; col < 7; col++)
        {
            float u = (col + .3f * (row % 2)) / 7.f, v = (row + .2f) / 6.f;
            V a{p.x - 8 + 84 * u - 69 * v - 7 * u * v, p.y - 142 + 50 * u + 50 * v - 14 * u * v};
            line(a, a + V{8, 5}, {.42f, .39f, .30f, .5f}, 2);
        }
    for (int k = 0; k < 3; k++)
        line({p.x + 19 + k * 4.f, p.y - 39}, {p.x + 19 + k * 4.f, p.y - 11}, {.26f, .22f, .16f}, 1);
    ellipse({p.x + 27, p.y - 25}, 1.5f, 1.5f, gold);
    rect(p.x + 28, p.y - 138, 12, 29, {.24f, .25f, .23f});
    for (int row = 0; row < 4; row++)
        line({p.x + 28, p.y - 135 + row * 6.f}, {p.x + 40, p.y - 135 + row * 6.f}, {.13f, .14f, .13f});
    for (int i = 0; i < 3; i++)
    {
        float t = fmodf(worldTime * 7 + i * 17, 55);
        ellipse({p.x + 35 + sinf(t * .04f + id) * 12, p.y - 143 - t}, 7 + t * .12f, 5 + t * .08f,
                {.45f, .51f, .47f, .08f * (1 - t / 60)});
    }
}
void person(V p, C, bool hero = false, int id = 0)
{
    Visuals::SoftShadow(p.x + 6, p.y + 3, 24, .36f, .82f);
    int action = hero ? (actionTime > 0 ? 2 : moving ? 1 : 0) : (!dialog.empty() && talkingNpc == id ? 2 : 0);
    bool frozen = !hero && villageWorld.Citizens().size() > size_t(id) && villageWorld.Citizens()[id].frozen;
    if (!hero && villageWorld.Citizens().size() > size_t(id) && villageWorld.Citizens()[id].walking)
        action = 1;
    if (hero && villageWorld.AttackTime() > 0)
        action = 3;
    float phase = !hero && villageWorld.Citizens().size() > size_t(id) ? villageWorld.Citizens()[id].animation
                                                                       : worldTime;
    Visuals::Sprite(p.x, p.y, hero ? 0 : 1 + id % 3, hero ? facing : id % 4, action,
                    phase * (action == 1 ? 1.6f : .65f) + id * .17f, frozen);
    if (!hero && length(ActorPosition(npcActors[id]) - PlayerPosition()) < 4)
        text(p.x - 45, p.y - 76, villageWorld.Behavior(id), frozen ? C{.65f, .65f, .65f} : gold);
    if (hero)
        glow(p + V{14, -16}, 28, gold);
}
V target()
{
    return stage == 0 || stage >= 3 ? ActorPosition(npcActors[0])
           : stage == 1             ? ActorPosition(seedActor)
                                    : ActorPosition(shrineActor);
}
const wchar_t *objective()
{
    const wchar_t *s[] = {L"마라와 이야기하기", L"북쪽 묘목에서 풍경 기록",
                          L"호숫가에서 사람들의 이야기 기록", L"마라에게 여행 이야기 전하기",
                          L"기록 완료 · 함께하지 못한 여행"};
    return s[stage];
}
void openDialog(const wchar_t *name, const wchar_t *msg, int next = -1)
{
    actionTime = 1.2f;
    speaker = name;
    dialog = msg;
    pending = next;
}
int nearest()
{
    float d = 1.8f;
    int n = -1;
    for (int i = 0; i < NPC_COUNT; i++)
    {
        if (!ActorActive(npcActors[i]))
            continue;
        float dist = length(PlayerPosition() - ActorPosition(npcActors[i]));
        if (dist < d)
        {
            d = dist;
            n = i;
        }
    }
    if (ActorActive(seedActor) && length(PlayerPosition() - ActorPosition(seedActor)) < 1.8f &&
        length(PlayerPosition() - ActorPosition(seedActor)) < d)
        n = NPC_COUNT;
    if (ActorActive(shrineActor) && length(PlayerPosition() - ActorPosition(shrineActor)) < 1.8f &&
        length(PlayerPosition() - ActorPosition(shrineActor)) < d)
        n = NPC_COUNT + 1;
    return n;
}
void interact()
{
    if (!ActorActive(tutorialPlayer) || villageWorld.IsDead())
        return;
    if (!dialog.empty())
    {
        dialog.clear();
        if (pending >= 0)
        {
            stage = pending;
            if (stage == 4)
                finished = true;
        }
        pending = -1;
        return;
    }
    int n = nearest();
    talkingNpc = (n >= 0 && n < NPC_COUNT) ? n : -1;
    if (n >= 0 && n < NPC_COUNT)
    {
        villageWorld.Talk(n);
        std::wstring message = villageWorld.Citizens()[n].frozen
                                   ? villageWorld.Dialogue(n)
                                   : npcs[n].line + std::wstring(L" ") + villageWorld.Dialogue(n);
        if (n == 0 && stage == 0)
            message = L"호수와 북쪽 묘목을 그려 보겠니? 네 그림 속 작은 동행도 함께. 사람들의 이름과 "
                      L"이야기도 남겨 줘.";
        if (n == 0 && stage == 3)
            message = L"그림이 떠난 이를 돌려주지는 않아. 그래도 누가 여기 살았는지 남겨 주었구나. 고맙다.";
        openDialog(npcs[n].name, message.c_str(), n == 0 ? (stage == 0 ? 1 : stage == 3 ? 4 : -1) : -1);
    }
    else if (n == NPC_COUNT)
    {
        villageWorld.Sketch({0, -13});
        openDialog(L"북쪽 묘목 · 풍경 기록",
                   L"잎과 바람을 스케치했다. 그림 한쪽에는 함께 오지 못한 작은 환수가 앉아 있다. 기록은 "
                   L"생명을 되돌리는 힘이 아니다.",
                   stage == 1 ? 2 : -1);
    }
    else if (n == NPC_COUNT + 1)
    {
        if (stage == 2 && villageWorld.RecordCount() < 3)
        {
            openDialog(L"여행 수첩", L"서로 다른 주민 세 명의 이야기를 먼저 들어보자.");
            return;
        }
        villageWorld.Sketch({10, -5});
        openDialog(L"호숫가 · 여행 수첩",
                   L"물가의 빛을 그리고 지금까지 들은 주민의 말을 적었다. 살아가는 방식도, 떠나지 못하는 "
                   L"이유도 저마다 다르다. 반려동물의 자리는 모든 그림에 남아 있다.",
                   stage == 2 ? 3 : -1);
    }
    else
        villageWorld.Pickup();
}
void update(float dt)
{
    if (firstLevelMode)
    {
        if (!active || paused)
            return;
        worldTime += dt;
        float x = float(keys['D'] - keys['A']), y = float(keys['S'] - keys['W']);
        FirstLevel::Position previous = firstWorld.GetPosition(firstWorld.Player().actor);
        firstWorld.Update(dt, {(x + y) * .70710678f, (y - x) * .70710678f}, keys[VK_SHIFT]);
        if (keys[VK_SPACE])
            firstWorld.Attack();
        auto position = firstWorld.GetPosition(firstWorld.Player().actor);
        moving = std::abs(position.x - previous.x) + std::abs(position.y - previous.y) > .0001f;
        V tracked{position.x, position.y};
        camera = camera + (tracked - camera) * std::min(1.f, dt * 5);
        return;
    }
    if (!active || paused)
        return;
    worldTime += dt;
    actionTime = std::max(0.f, actionTime - dt);
    updateWildlife(dt);
    villageWorld.Update(dt, dialog.empty() ? -1 : talkingNpc);
    moving = false;
    if (!finished)
        elapsed += dt;
    if (!dialog.empty() || villageWorld.IsDead())
        return;
    if (keys[VK_SPACE])
        villageWorld.Attack(villageFacing);
    V d{float(keys['D'] - keys['A']), float(keys['S'] - keys['W'])};
    if (ActorActive(tutorialPlayer) && length(d) > 0)
    {
        moving = true;
        facing = fabsf(d.x) > fabsf(d.y) ? (d.x < 0 ? 1 : 2) : (d.y < 0 ? 3 : 0);
        d = d * (1 / length(d));
        V move{(d.x + d.y) * .70710678f, (d.y - d.x) * .70710678f};
        villageFacing = {move.x, move.y};
        move = move * (dt * (keys[VK_SHIFT] ? 5.0f : 3.1f));
        V q = PlayerPosition() + V{move.x, 0};
        if (!blocked(q))
            SetPlayerPosition(q);
        q = PlayerPosition() + V{0, move.y};
        if (!blocked(q))
            SetPlayerPosition(q);
    }
    camera = camera + (PlayerPosition() - camera) * std::min(1.f, dt * 5);
}
TerrainBatch::Map tutorialTerrain, frozenTerrain;
void drawWorld()
{
    Profiler::Scope scope("world_build_submit");
    Profiler::GpuScope gpu("world");
    if (ActorVisible(terrainActor))
    {
        if (tutorialTerrain.Empty())
        {
            std::vector<TerrainBatch::Tile> tiles;
            for (int x = -82; x < 85; ++x)
                for (int y = -78; y < 74; ++y)
                {
                    bool water = lake({x + .5f, y + .5f}), village = abs(x) < 8 && abs(y) < 8;
                    bool road = abs(x) < 2 || (x >= 0 && x < 14 && abs(y + 5) < 2) || abs(y - 18) < 2 ||
                                abs(x + 20) < 2 || abs(x - 20) < 2 || abs(y + 18) < 2;
                    tiles.push_back({x, y,
                                     water     ? 3
                                     : village ? 1
                                     : road    ? 2
                                               : 0,
                                     .92f + hash(x / 3, y / 3) * .10f});
                }
            tutorialTerrain.Build(tiles);
        }
        auto &terrain = *tutorialScene.Find(terrainActor);
        auto terrainPosition = terrain.GetWorldPosition();
        V pivot = project({0, 0});
        RenderBatch::PushMatrix();
        RenderBatch::Translatef((terrainPosition.x - terrainPosition.y) * 31,
                                (terrainPosition.x + terrainPosition.y) * 15.5f - terrain.GetWorldElevation(),
                                0);
        SceneRender::Push(terrain, pivot.x, pivot.y);
        RenderBatch::PushMatrix();
        RenderBatch::Translatef(pivot.x, pivot.y, 0);
        tutorialTerrain.Draw(width, height, worldTime);
        if (villageWorld.GetPhase() == Village::Phase::Stillness)
        {
            if (frozenTerrain.Empty())
            {
                std::vector<TerrainBatch::Tile> tiles;
                for (int x = -6; x <= 6; ++x)
                    for (int y = -22; y <= -10; ++y)
                        if (villageWorld.IsFrozen({x + .5f, y + .5f}))
                            tiles.push_back({x, y, (abs(x) < 2 || abs(y + 18) < 2) ? 6 : 4,
                                             .92f + hash(x / 3, y / 3) * .10f});
                frozenTerrain.Build(tiles);
            }
            frozenTerrain.Draw(width, height, 0);
        }
        RenderBatch::PopMatrix();
        for (int i = 0; i < 90; i++)
        {
            float a = i * 6.2831853f / 90;
            V q{16 + cosf(a) * 5.65f, -7 + sinf(a) * 8.15f};
            V p = project(q);
            ellipse(p, 6 + hash(i, 4) * 5, 3, {.37f, .40f, .29f});
            if (i % 3 == 0)
            {
                for (int j = 0; j < 3; j++)
                    line(p + V{float(j * 3), 0},
                         p + V{float(j * 3) + sinf(worldTime + i) * 2, -14 - float(j % 2) * 8},
                         {.43f, .43f, .22f}, 2);
            }
        }
        for (int i = 0; i < 5; i++)
        {
            V p = project({15.f + float(i % 2) * 2, -10.f + float(i) * 2});
            float t = fmodf(worldTime * .15f + i * .2f, 1);
            color({.46f, .72f, .73f, (1 - t) * .14f});
            RenderBatch::Begin(GL_LINE_LOOP);
            for (int j = 0; j < 48; j++)
            {
                float a = j * 6.2831853f / 48;
                RenderBatch::Vertex2f(p.x + cosf(a) * (10 + t * 50), p.y + sinf(a) * (5 + t * 20));
            }
            RenderBatch::End();
        }
        for (int i = 0; i < 32; i++)
        {
            float x = 12 + hash(i, 2) * 8, y = -13 + hash(i, 3) * 12;
            if (lake({x, y}))
            {
                V p = project({x, y});
                float w = 9 + sinf(worldTime + i) * 5;
                line(p + V{-w, 0}, p + V{w, 0}, {.37f, .63f, .62f, .18f});
            }
        }
        // Village cobbles and lantern pools.
        for (int i = 0; i < 120; i++)
        {
            V q{hash(i, 9) * 12 - 6, hash(i, 8) * 12 - 6};
            if (!blocked(q))
            {
                V p = project(q);
                ellipse(p, 4 + hash(i, 3) * 3, 2, {.47f, .46f, .36f, .13f});
            }
        }
        SceneRender::Pop();
        RenderBatch::PopMatrix();
    }
    Scene::View view{camera.x, camera.y, width, height};
    auto visibleActors = tutorialScene.RenderQueue(Scene::Layer::World, &view);
    for (auto actor : visibleActors)
    {
        if (actor->GetKind() != Scene::Kind::Tree && actor->GetKind() != Scene::Kind::House)
            continue;
        const auto &o = objects.at(actor->GetDataIndex());
        V p = project(ActorPosition(o.actor));
        if (p.x < -200 || p.x > width + 200 || p.y < -150 || p.y > height + 100)
            continue;
        SceneRender::Push(*tutorialScene.Find(o.actor), p.x, p.y);
        float r = o.type == 1 ? 70.f : 28.f * o.scale;
        // Ground-projected caster silhouettes with layered penumbra, all before geometry.
        for (int layer = 3; layer >= 0; layer--)
        {
            float e = float(layer) * 2;
            float spread = o.type == 1 ? 48.f : 13.f * o.scale;
            poly({p + V{-spread - e, -2}, p + V{spread + e, 2}, p + V{40 + spread * .4f + e, 24 + e},
                  p + V{40 - spread * .4f - e, 24 + e}},
                 {.025f, .04f, .045f, .045f});
        }
        Visuals::SoftShadow(p.x + 25, p.y + 14, r, .48f, .7f);
        Visuals::SoftShadow(p.x, p.y, r * .6f, .35f, .8f);
        SceneRender::Pop();
    }
    for (auto actor : visibleActors)
    {
        if (actor->GetKind() == Scene::Kind::Enemy)
        {
            const auto &m = villageWorld.Monsters().at(actor->GetDataIndex());
            V p = project(ActorPosition(actor->GetId()));
            if (p.x < -100 || p.x > width + 100 || p.y < -80 || p.y > height + 120)
                continue;
            SceneRender::Push(*actor, p.x, p.y);
            Visuals::SoftShadow(p.x, p.y, 22, .4f, .8f);
            SceneModels::Draw(m.kind ? SceneModels::Kind::Wraith : SceneModels::Kind::Boar, p.x, p.y, 1,
                              worldTime, m.flash > 0);
            rect(p.x - 18, p.y - 68, 36, 4, {.15f, .07f, .06f});
            rect(p.x - 18, p.y - 68, float(m.health), 4, {.8f, .3f, .2f});
            SceneRender::Pop();
            continue;
        }
        if (actor->GetKind() == Scene::Kind::Loot)
        {
            V p = project(ActorPosition(actor->GetId()));
            SceneRender::Push(*actor, p.x, p.y);
            SceneModels::Draw(SceneModels::Kind::Coin, p.x, p.y);
            SceneRender::Pop();
            continue;
        }
        const auto &o = objects.at(actor->GetDataIndex());
        V p = project(ActorPosition(actor->GetId()), actor->GetWorldElevation());
        if (actor->GetKind() == Scene::Kind::Flame)
        {
            SceneRender::Push(*actor, p.x, p.y);
            Visuals::Flame(p.x, p.y, o.scale, worldTime, villageWorld.IsFrozen(actor->GetWorldPosition()));
            SceneRender::Pop();
            continue;
        }
        if (p.x < -150 || p.x > width + 150 || p.y < -40 || p.y > height + 200)
            continue;
        SceneRender::Push(*actor, p.x, p.y);
        desaturatedGeometry = villageWorld.IsFrozen(actor->GetWorldPosition());
        switch (o.type)
        {
        case 0: {
            float a = length(ActorPosition(o.actor) - PlayerPosition());
            if (a < 3 &&
                ActorPosition(o.actor).x + ActorPosition(o.actor).y > PlayerPosition().x + PlayerPosition().y)
            {
                RenderBatch::Enable(GL_POLYGON_STIPPLE);
                GLubyte mask[128];
                for (int i = 0; i < 128; i++)
                    mask[i] = (i / 4) % 2 ? 0xAA : 0x55;
                glPolygonStipple(mask);
            }
            if (desaturatedGeometry)
                treeGeometry(p, o.scale);
            else
                tree(p, o.scale);
            RenderBatch::Disable(GL_POLYGON_STIPPLE);
            break;
        }
        case 1:
            house(p, o.id);
            break;
        case 2:
            person(p, npcs[o.id].color, false, o.id);
            break;
        case 3:
            ellipse(p, 22, 11, {.38f, .40f, .34f});
            ellipse(p + V{0, -8}, 22, 11, {.48f, .47f, .38f});
            ellipse(p + V{0, -9}, 14, 6, {.045f, .075f, .07f});
            break;
        case 4:
            poly({{p.x - 15, p.y}, {p.x - 12, p.y - 34}, {p.x + 8, p.y - 40}, {p.x + 15, p.y - 5}},
                 {.40f, .47f, .43f});
            line(p + V{-2, -30}, p + V{-2, -10}, {.62f, .68f, .54f}, 2);
            break;
        case 5:
            line(p, p + V{0, -24}, {.3f, .24f, .15f}, 3);
            ellipse(p + V{-6, -18}, 8, 4, {.28f, .45f, .28f});
            ellipse(p + V{6, -24}, 8, 4, {.35f, .5f, .28f});
            break;
        case 7:
            fire(p, o.scale);
            break;
        case 8:
            animal(p, beasts[o.id]);
            break;
        case 6:
            person(p, {.3f, .5f, .6f}, true);
            break;
        }
        desaturatedGeometry = false;
        SceneRender::Pop();
    }
    if (ActorVisible(particlesActor))
    {
        auto &particles = *tutorialScene.Find(particlesActor);
        auto position = particles.GetWorldPosition();
        V pivot = project({0, 0});
        RenderBatch::PushMatrix();
        RenderBatch::Translatef((position.x - position.y) * 31,
                                (position.x + position.y) * 15.5f - particles.GetWorldElevation(), 0);
        SceneRender::Push(particles, pivot.x, pivot.y);
        for (int i = 0; i < 24; i++)
        {
            V q{hash(i, 19) * 34 - 13, hash(i, 21) * 30 - 18};
            V p = project(q, 15 + sinf(worldTime + i) * 8);
            ellipse(p, 1.5f, 1.5f, {.75f, .8f, .5f, .3f + .25f * sinf(worldTime + i)});
        }
        // Thin drifting mist preserves scene readability.
        for (int i = 0; i < 5; i++)
            ellipse({fmodf(worldTime * 8 + i * 330, width + 500.f) - 250, height * .55f + i * 65.f}, 280, 23,
                    {.49f, .64f, .59f, .022f});
        SceneRender::Pop();
        RenderBatch::PopMatrix();
    }
    if (stage < 4 && ActorVisible(markerActor))
    {
        auto targetActor = stage == 0 || stage >= 3 ? npcActors[0] : stage == 1 ? seedActor : shrineActor;
        if (tutorialScene.Find(markerActor)->GetParent() != targetActor && tutorialScene.Find(targetActor))
        {
            tutorialScene.Reparent(markerActor, targetActor, false);
            tutorialScene.Find(markerActor)->SetLocalPosition({0, 0});
        }
        V p = project(ActorPosition(markerActor), 55 + tutorialScene.Find(markerActor)->GetWorldElevation());
        SceneRender::Push(*tutorialScene.Find(markerActor), p.x, p.y);
        float b = sinf(worldTime * 3) * 3;
        poly({{p.x - 6, p.y + b}, {p.x + 6, p.y + b}, {p.x, p.y + 8 + b}}, gold);
        SceneRender::Pop();
    }
    RenderBatch::Flush();
}
void minimap()
{
    Profiler::Scope scope("minimap_cpu");
    float x = width - 212.f, y = 32;
    rect(x, y, 184, 166, {.035f, .07f, .067f, .92f});
    text(x + 14, y + 23, L"주변 지도", gold);
    auto m = [&](V p) { return V{x + 90 + p.x * .95f, y + 83 + p.y * .85f}; };
    ellipse(m({16, -7}), 10, 12, {.15f, .35f, .38f});
    for (auto &o : objects)
        if (o.type == 1 && ActorVisible(o.actor))
        {
            V p = m(ActorPosition(o.actor));
            rect(p.x - 3, p.y - 3, 6, 6, {.55f, .48f, .34f});
        }
    if (stage < 4)
        ellipse(m(target()), 3, 3, gold);
    for (auto id : npcActors)
        if (ActorVisible(id))
            ellipse(m(ActorPosition(id)), 1.5f, 1.5f, gold);
    for (auto &monster : villageWorld.Monsters())
        if (monster.health > 0)
            ellipse(m(ActorPosition(monster.actor)), 1.5f, 1.5f, {.8f, .3f, .2f});
    ellipse(m(PlayerPosition()), 3, 3, teal);
    text(x + 11, y + 154, L"● 나   ·   ◆ 목적지");
}
void draw()
{
    DrawStats::Frame frameStats;
    RenderBatch::Frame batchFrame;
    Profiler::Scope renderScope("render_total_cpu");
    glViewport(0, 0, width, height);
    glClearColor(.035f, .065f, .065f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    RenderBatch::MatrixMode(GL_PROJECTION);
    RenderBatch::LoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    RenderBatch::MatrixMode(GL_MODELVIEW);
    RenderBatch::LoadIdentity();
    RenderBatch::Enable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    Visuals::BeginScene(width, height, postEnabled);
    if (firstLevelMode)
    {
        LevelView::Draw(firstWorld,
                        {width, height, worldTime, camera.x, camera.y, paused, postEnabled, moving},
                        LevelText);
        return;
    }
    drawWorld();
    Visuals::PostProcess(width, height, worldTime, postEnabled);
    if (!ActorVisible(hudActor))
        return;
    Profiler::Scope hudScope("ui_cpu");
    Profiler::GpuScope hudGpu("ui");
    auto hudPosition = tutorialScene.Find(hudActor)->GetWorldPosition();
    RenderBatch::PushMatrix();
    RenderBatch::Translatef(hudPosition.x, hudPosition.y, 0);
    SceneRender::Push(*tutorialScene.Find(hudActor), 0, 0, true);
    rect(0, 0, float(width), 6, {.67f, .51f, .28f});
    rect(24, 26, 480, 111, {.025f, .045f, .043f, .92f});
    text(43, 54, L"잿빛 여울 · 함께하지 못한 여행", gold);
    text(43, 84, objective());
    int sec = int(elapsed);
    wchar_t t[100];
    swprintf_s(t, L"여행 기록 · %d / 4     %02d:%02d  |  주민 36명", std::min(stage, 4), sec / 60, sec % 60);
    text(43, 115, t, {.49f, .61f, .56f});
    text(42, 220, std::wstring(L"마을 상태: ") + villageWorld.PhaseName(), gold);
    text(42, 246,
         L"체력 " + std::to_wstring(villageWorld.Health()) + L"  동전 " +
             std::to_wstring(villageWorld.Coins()) + L"  회복약 " + std::to_wstring(villageWorld.Potions()) +
             L"  경험치 " + std::to_wstring(villageWorld.Experience()));
    text(42, 272,
         L"이야기 수집 " + std::to_wstring(villageWorld.RecordCount()) + L" / 36 · 풍경 기록 " +
             (villageWorld.LandscapeRecorded() ? L"완료" : L"대기"));
    wrapped(42, height - 112.f, villageWorld.Notice(), std::max(25, (width - 90) / 18));
    if (villageWorld.IsDead())
        text(width * .5f - 150, height * .5f, L"쓰러졌습니다 · R로 마을에서 회복", gold);
    minimap();
    rect(24, height - 62.f, width - 48.f, 40, {.025f, .045f, .043f, .92f});
    text(42, height - 36.f,
         L"WASD 이동  E 대화/획득  Space 공격  Q 회복  P 그림  F3 사건  F1 사냥터  Esc 휴식", ink);
    if (stage < 4)
    {
        V d = target() - PlayerPosition();
        float dist = length(d);
        text(42, 164, std::wstring(L"◆ ") + objective() + L"  ·  " + std::to_wstring(int(dist)) + L" m",
             gold);
    }
    for (auto &a : beasts)
        if (length(ActorPosition(a.actor) - PlayerPosition()) < 4 && dialog.empty() && !paused)
        {
            text(42, 192,
                 std::wstring(a.kind == 0   ? L"사슴"
                              : a.kind == 1 ? L"산토끼"
                                            : L"멧돼지") +
                     (a.fleeing ? L" · 놀라 달아나고 있습니다" : L" · 야생동물을 조용히 관찰하세요"),
                 ink);
            break;
        }
    if (nearest() >= 0 && dialog.empty() && !paused)
    {
        V p = project(PlayerPosition(), 64);
        rect(p.x - 89, p.y - 24, 178, 31, {.025f, .045f, .043f, .95f});
        text(p.x - 76, p.y - 3, L"[ E ] 대화 / 조사", gold);
    }
    if (!dialog.empty())
    {
        float x = width * .12f, y = height - 240.f;
        rect(x, y, width * .76f, 157, {.025f, .043f, .041f, .98f});
        rect(x, y, 3, 157, gold);
        text(x + 23, y + 30, speaker, gold);
        wrapped(x + 23, y + 63, dialog, std::max(20, int((width * .76f - 46) / 18)));
        text(x + 23, y + 140, L"[E] 닫기   상인 [1] 구입   성직자 [2] 치료", teal);
    }
    if (finished && dialog.empty())
    {
        rect(width * .5f - 240, 185, 480, 100, {.025f, .045f, .043f, .94f});
        text(width * .5f - 212, 217, L"함께하지 못한 여행 · 기록 완료", gold);
        text(width * .5f - 212, 246, L"풍경 곁에 사람들의 이야기를 남겼다.");
        text(width * .5f - 212, 270, L"자유롭게 둘러보거나 R 키로 다시 시작하세요.");
    }
    if (paused)
    {
        rect(0, 0, float(width), float(height), {0, 0, 0, .65f});
        text(width * .5f - 85, height * .5f - 30, L"잠시 쉬어가기", gold);
        text(width * .5f - 160, height * .5f + 7, L"Esc 계속   ·   R 다시 시작   ·   Q 종료");
    }
    SceneRender::Pop();
    RenderBatch::PopMatrix();
    RenderBatch::Flush();
}
void reset()
{
    if (firstLevelMode)
    {
        firstWorld.Generate(static_cast<std::uint32_t>(GetTickCount64()));
        camera = {.5f, .5f};
        paused = false;
        moving = false;
        std::fill(keys, keys + 256, false);
        return;
    }
    initWorld();
    moving = false;
    actionTime = 0;
    facing = 0;
    stage = 0;
    elapsed = 0;
    SetPlayerPosition({0, 2});
    camera = PlayerPosition();
    finished = false;
    paused = false;
    dialog.clear();
    pending = -1;
    std::fill(keys, keys + 256, false);
}
void capture(const char *path)
{
    std::vector<unsigned char> pixels(width * height * 4);
    glReadPixels(0, 0, width, height, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels.data());
    BITMAPFILEHEADER f{};
    BITMAPINFOHEADER b{};
    f.bfType = 0x4D42;
    f.bfOffBits = sizeof(f) + sizeof(b);
    f.bfSize = f.bfOffBits + DWORD(pixels.size());
    b.biSize = sizeof(b);
    b.biWidth = width;
    b.biHeight = height;
    b.biPlanes = 1;
    b.biBitCount = 32;
    b.biSizeImage = DWORD(pixels.size());
    std::ofstream out(path, std::ios::binary);
    out.write((char *)&f, sizeof(f));
    out.write((char *)&b, sizeof(b));
    out.write((char *)pixels.data(), pixels.size());
}
LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_CLOSE:
        running = false;
        return 0;
    case WM_SIZE:
        width = std::max(640, int(LOWORD(l)));
        height = std::max(480, int(HIWORD(l)));
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)l)->ptMinTrackSize = {1000, 720};
        return 0;
    case WM_ACTIVATE:
        active = LOWORD(w) != WA_INACTIVE;
        if (!active)
            std::fill(keys, keys + 256, false);
        return 0;
    case WM_KEYUP:
        if (w < 256)
            keys[w] = false;
        return 0;
    case WM_KEYDOWN:
        if (w < 256)
            keys[w] = true;
        if (l & (1 << 30))
            return 0;
        if (w == VK_F1)
        {
            firstLevelMode = !firstLevelMode;
            dialog.clear();
            paused = false;
            std::fill(keys, keys + 256, false);
            if (firstLevelMode)
            {
                auto p = firstWorld.GetPosition(firstWorld.Player().actor);
                camera = {p.x, p.y};
            }
            else
                reset();
            return 0;
        }
        if (firstLevelMode)
        {
            if (w == VK_ESCAPE)
            {
                paused = !paused;
                std::fill(keys, keys + 256, false);
            }
            else if (w == VK_F2)
                postEnabled = !postEnabled;
            else if (w == 'Q' && paused)
                running = false;
            else if (w == 'R' && firstWorld.IsDead())
            {
                firstWorld.Respawn();
                camera = {.5f, .5f};
            }
            else if (w == 'R' && paused)
                reset();
            else if (!paused)
            {
                if (w == 'E')
                    firstWorld.Pickup();
                else if (w == 'Q')
                    firstWorld.UsePotion();
                else if (w == '1')
                    firstWorld.SpendPoint(FirstLevel::Stat::Strength);
                else if (w == '2')
                    firstWorld.SpendPoint(FirstLevel::Stat::Vitality);
                else if (w == '3')
                    firstWorld.SpendPoint(FirstLevel::Stat::Guard);
            }
            return 0;
        }
        if (w == VK_ESCAPE)
        {
            paused = !paused;
            std::fill(keys, keys + 256, false);
        }
        else if (w == VK_F2)
            postEnabled = !postEnabled;
        else if (w == 'R' && villageWorld.IsDead())
        {
            villageWorld.Respawn();
            camera = PlayerPosition();
        }
        else if (w == VK_F3 && !paused)
            villageWorld.AdvanceEvent();
        else if (w == 'P' && !paused)
            villageWorld.Sketch({PlayerPosition().x, PlayerPosition().y});
        else if (w == 'Q' && !paused)
            villageWorld.UsePotion();
        else if (w == '1' && !paused && !dialog.empty())
        {
            villageWorld.Buy(talkingNpc);
        }
        else if (w == '2' && !paused && !dialog.empty())
        {
            villageWorld.Heal(talkingNpc);
        }
        else if (w == 'E' && !paused)
            interact();
        else if (w == 'R' && (paused || finished || villageWorld.GetPhase() == Village::Phase::Stillness))
            reset();
        else if (w == 'Q' && paused)
            running = false;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
// Grid search uses the actual collision query; validates all quest sites are reachable.
float routeDistance(V start, V goal)
{
    const int nx = int((MAX_X - MIN_X) * 2) + 1, ny = int((MAX_Y - MIN_Y) * 2) + 1;
    auto point = [](int x, int y) { return V{MIN_X + x * .5f, MIN_Y + y * .5f}; };
    int sx = int(std::round((start.x - MIN_X) * 2)), sy = int(std::round((start.y - MIN_Y) * 2));
    std::vector<float> distances(nx * ny, -1);
    std::queue<int> queue;
    queue.push(sy * nx + sx);
    distances[sy * nx + sx] = 0;
    while (!queue.empty())
    {
        int cell = queue.front();
        queue.pop();
        int x = cell % nx, y = cell / nx;
        if (length(point(x, y) - goal) < 1.5f)
            return distances[cell];
        const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
        for (int i = 0; i < 4; i++)
        {
            int xx = x + dx[i], yy = y + dy[i];
            if (xx < 0 || xx >= nx || yy < 0 || yy >= ny)
                continue;
            int next = yy * nx + xx;
            if (distances[next] >= 0 || blocked(point(xx, yy)))
                continue;
            distances[next] = distances[cell] + .5f;
            queue.push(next);
        }
    }
    return -1;
}
int tests()
{
    firstLevelMode = false;
    initWorld();
    auto bruteBlocked = [](V point) {
        if (point.x < MIN_X || point.x > MAX_X || point.y < MIN_Y || point.y > MAX_Y || lake(point))
            return true;
        for (const auto &object : objects)
        {
            if (!ActorActive(object.actor))
                continue;
            auto p = ActorPosition(object.actor);
            if (object.type == 1 && fabsf(point.x - p.x) < 1.55f && fabsf(point.y - p.y) < 1.45f)
                return true;
            if (object.type == 0 && length(point - p) < .45f)
                return true;
        }
        return false;
    };
    for (int y = -39; y < 35; y += 3)
        for (int x = -39; x < 43; x += 3)
            assert(blocked({float(x), float(y)}) == bruteBlocked({float(x), float(y)}));
    auto &house = *tutorialScene.Find(objects[0].actor);
    auto original = house.GetWorldPosition();
    house.SetWorldPosition({12, 12});
    RebuildCollisionGrid();
    assert(blocked({12, 12}));
    house.SetEnabled(false);
    RebuildCollisionGrid();
    assert(blocked({12, 12}) == bruteBlocked({12, 12}));
    house.SetEnabled(true);
    house.SetWorldPosition(original);
    RebuildCollisionGrid();
    static_assert(sizeof(npcs) / sizeof(npcs[0]) == 36, "NPC count must be 36");
    assert((MAX_X - MIN_X) * (MAX_Y - MIN_Y) == 16 * 41 * 37);
    assert(beasts.size() == 15);
    for (int i = 0; i < NPC_COUNT; i++)
    {
        assert(!blocked(ActorPosition(npcActors[i])));
        assert(routeDistance(PlayerPosition(), ActorPosition(npcActors[i])) >= 0);
        SetPlayerPosition(ActorPosition(npcActors[i]));
        assert(nearest() == i);
    }
    reset();
    assert(tutorialScene.IsValid());
    auto count = tutorialScene.Size();
    auto &group = *tutorialScene.Find(characterGroup);
    V beforeGroup = PlayerPosition();
    group.SetLocalPosition({1, 2});
    assert(length(PlayerPosition() - beforeGroup - V{1, 2}) < .001f);
    group.SetLocalPosition({0, 0});
    group.SetEnabled(false);
    V beforeDisabled = ActorPosition(beasts[0].actor);
    updateWildlife(.1f);
    assert(length(ActorPosition(beasts[0].actor) - beforeDisabled) < .001f);
    group.SetEnabled(true);
    assert(tutorialScene.Size() == count);
    V first = ActorPosition(beasts[0].actor);
    for (int i = 0; i < 300; i++)
        updateWildlife(.02f);
    assert(length(ActorPosition(beasts[0].actor) - first) > .01f);
    for (auto &a : beasts)
        assert(!blocked(ActorPosition(a.actor)));
    SetPlayerPosition(ActorPosition(beasts[0].actor) + V{1, 0});
    updateWildlife(.02f);
    assert(beasts[0].fleeing);
    reset();
    float route = 0;
    V stops[] = {PlayerPosition(), ActorPosition(npcActors[0]), ActorPosition(seedActor),
                 ActorPosition(shrineActor), ActorPosition(npcActors[0])};
    for (int i = 0; i < 4; i++)
    {
        float d = routeDistance(stops[i], stops[i + 1]);
        assert(d >= 0);
        route += d;
    }
    assert(route / 3.1f + 180 < 300);
    std::ofstream report("self-test-report.txt");
    report << "Collision-aware route distance: " << route
           << " m\nWalking plus 180-second reading/exploration allowance: " << route / 3.1f + 180
           << " seconds\n";
    assert(!blocked(ActorPosition(shrineActor)));
    assert(!blocked(ActorPosition(seedActor)));
    assert(!blocked(PlayerPosition()));
    assert(blocked({16, -7}));
    SetPlayerPosition(ActorPosition(npcActors[0]));
    interact();
    assert(stage == 0 && !dialog.empty());
    interact();
    assert(stage == 1);
    SetPlayerPosition(ActorPosition(shrineActor));
    interact();
    interact();
    assert(stage == 1);
    SetPlayerPosition(ActorPosition(seedActor));
    interact();
    interact();
    assert(stage == 2);
    for (int i = 1; i <= 2; ++i)
    {
        SetPlayerPosition(ActorPosition(npcActors[i]));
        interact();
        interact();
    }
    SetPlayerPosition(ActorPosition(shrineActor));
    interact();
    interact();
    assert(stage == 3);
    SetPlayerPosition(ActorPosition(npcActors[0]));
    interact();
    interact();
    assert(stage == 4 && finished);
    reset();
    assert(stage == 0 && !finished && dialog.empty());
    float e = elapsed;
    paused = true;
    update(1);
    assert(elapsed == e);
    paused = false;
    keys['W'] = true;
    V before = PlayerPosition();
    update(.1f);
    assert(PlayerPosition().x < before.x && PlayerPosition().y < before.y);
    keys['W'] = false;
    reset();
    keys['W'] = true;
    update(.1f);
    float straight = length(PlayerPosition() - V{0, 2});
    reset();
    keys['W'] = keys['D'] = true;
    update(.1f);
    assert(fabsf(length(PlayerPosition() - V{0, 2}) - straight) < .001f);
    reset();
    openDialog(L"test", L"test");
    V stationary = PlayerPosition();
    keys['W'] = true;
    update(.1f);
    assert(length(PlayerPosition() - stationary) < .001f && elapsed > .09f);
    reset();
    report << "PASS: 36 NPC interactions/reachability, expanded 4x village area, 15 wildlife spawns, "
              "movement/collision/flee behavior.\n";
    report << "PASS: quest order, premature interaction, reachability, collision, pause, movement, diagonal "
              "speed, dialog lock, restart.\n";
    std::string villageReport;
    bool villageOkay = villageWorld.RunTests(villageReport);
    report << villageReport;
    report.flush();
    assert(villageOkay);
    return 0;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command, int)
{
    if (wcsstr(command, L"--self-test"))
    {
        int legacy = tests();
        std::string report;
        FirstLevel::World tested;
        bool okay = tested.RunTests(report);
        okay = AssetCache::RunTests(report) && okay;
        okay = Scene::SceneGraph::RunTests(report) && okay;
        okay = DrawStats::RunTests(report) && okay;
        okay = RenderBatch::RunTests(report) && okay;
        okay = Profiler::RunTests(report) && okay;
        std::ofstream file("first-level-test-report.txt");
        file << report;
        return legacy == 0 && okay ? 0 : 3;
    }
    DrawStats::InitializeConsole(wcsstr(command, L"--capture") || wcsstr(command, L"--benchmark"));
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"GSETutorial";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    windowHandle =
        CreateWindowW(wc.lpszClassName, L"GSE · 잿빛 여울 — 튜토리얼", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                      CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, nullptr);
    dc = GetDC(windowHandle);
    PIXELFORMATDESCRIPTOR pf{};
    pf.nSize = sizeof(pf);
    pf.nVersion = 1;
    pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pf.iPixelType = PFD_TYPE_RGBA;
    pf.cColorBits = 32;
    pf.cAlphaBits = 8;
    int format = ChoosePixelFormat(dc, &pf);
    if (!format || !SetPixelFormat(dc, format, &pf))
        return 1;
    rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc))
        return 2;
    font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, HANGUL_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
    oldFont = SelectObject(dc, font);
    bool benchmarkMode = wcsstr(command, L"--benchmark") != nullptr;
    bool profileEnabled = wcsstr(command, L"--no-profile") == nullptr;
    bool profileConsole = wcsstr(command, L"--quiet-profile") == nullptr;
    unsigned profileSeed = (benchmarkMode || wcsstr(command, L"--capture-level"))
                               ? 42u
                               : static_cast<unsigned>(GetTickCount64());
    Profiler::Initialize(
        profileEnabled, profileConsole,
        (wcsstr(command, L"--tutorial") ||
         (!benchmarkMode && !wcsstr(command, L"--level1") && !wcsstr(command, L"--capture-level")) ||
         (wcsstr(command, L"--capture") && !wcsstr(command, L"--capture-level")))
            ? "tutorial"
            : "level1",
        profileSeed, width, height);
    Profiler::Event("launch_arguments",
                    std::string(profileConsole ? "console=on" : "console=off") +
                        (benchmarkMode ? ";benchmark=on;fixed_step=1/60" : ";benchmark=off") +
                        (wcsstr(command, L"--no-post") ? ";postprocess=off" : ";postprocess=on"));
    if (!RenderBatch::Initialize())
    {
        Profiler::Event("renderer_initialization_failed", "VBO, instancing, or shaders unavailable");
        Profiler::Shutdown();
        MessageBoxW(nullptr, L"GPU 배칭 렌더러를 초기화하지 못했습니다. OpenGL 드라이버를 확인해 주세요.",
                    L"GSE", MB_ICONERROR);
        return 4;
    }
    AssetCache::Initialize();
    Visuals::Initialize();
    SceneModels::Initialize();
    initWorld();
    bool levelShot = wcsstr(command, L"--capture-level") != nullptr;
    bool shot = wcsstr(command, L"--capture") != nullptr;
    firstLevelMode = levelShot || (!shot && !wcsstr(command, L"--tutorial") &&
                                   (benchmarkMode || wcsstr(command, L"--level1")));
    firstWorld.Generate(profileSeed);
    if (firstLevelMode)
    {
        camera = {.5f, .5f};
    }
    ShowWindow(windowHandle, shot ? SW_HIDE : SW_SHOW);
    if (wcsstr(command, L"--no-post"))
        postEnabled = false;
    bool bench = wcsstr(command, L"--benchmark") != nullptr;
    int benchFrames = 0;
    double benchTotal = 0;
    if (bench)
        ShowWindow(windowHandle, SW_HIDE);
    auto last = std::chrono::steady_clock::now();
    while (running)
    {
        Profiler::BeginFrame();
        MSG msg;
        auto inputStart = Profiler::Clock::now();
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Profiler::Time(
            "input_dispatch",
            std::chrono::duration<double, std::milli>(Profiler::Clock::now() - inputStart).count());
        auto now = std::chrono::steady_clock::now();
        float rawDt = std::chrono::duration<float>(now - last).count();
        Profiler::Add("elapsed_since_update_ms", double(rawDt) * 1000);
        Profiler::Add("simulation_dt_clamped", rawDt > .05f ? 1 : 0);
        float dt = std::min(.05f, rawDt);
        last = now;
        {
            Profiler::Scope scope("game_update");
            update(benchmarkMode ? 1.f / 60 : dt);
        }
        {
            Profiler::GpuScope gpu("render_total");
            draw();
        }
        if (levelShot)
        {
            capture("level1-start.bmp");
            firstWorld.Attack();
            worldTime = .12f;
            draw();
            capture("level1-attack.bmp");
            firstWorld.Pickup();
            firstWorld.AwardExperience(60);
            draw();
            capture("level1-growth.bmp");
            firstWorld.SpendPoint(FirstLevel::Stat::Strength);
            draw();
            capture("level1-complete.bmp");
            width = 1000;
            height = 720;
            draw();
            capture("level1-small.bmp");
            const auto &cache = AssetCache::Stats();
            std::ofstream report("level1-render-report.txt");
            report << "Post=" << Visuals::PostAvailable() << " Water/Fire=" << Visuals::EffectsAvailable()
                   << "\nCache loaded=" << cache.loaded << " generated=" << cache.generated
                   << " writeFailures=" << cache.writeFailures << "\n";
            running = false;
        }
        else if (shot)
        {
            std::ofstream renderReport("render-report.txt");
            renderReport << "GL: " << glGetString(GL_VERSION) << "\nRenderer: " << glGetString(GL_RENDERER)
                         << "\nPost shader: " << Visuals::PostAvailable() << "\n";
            capture("tutorial-village.bmp");
            Visuals::SaveAtlas("sprite-atlas.bmp");
            postEnabled = false;
            draw();
            capture("tutorial-post-off.bmp");
            postEnabled = true;
            moving = true;
            worldTime = .08f;
            draw();
            capture("tutorial-walk-a.bmp");
            worldTime = .4f;
            draw();
            capture("tutorial-walk-b.bmp");
            moving = false;
            SetPlayerPosition({-11, -10});
            camera = PlayerPosition();
            worldTime = 2;
            draw();
            capture("tutorial-wildlife.bmp");
            SetPlayerPosition(ActorPosition(shrineActor) + V{-1, 0});
            camera = {10, -5};
            stage = 3;
            draw();
            capture("tutorial-lake.bmp");
            reset();
            SetPlayerPosition(ActorPosition(npcActors[0]) + V{0, 1});
            camera = PlayerPosition();
            interact();
            draw();
            capture("tutorial-dialog.bmp");
            reset();
            villageWorld.AdvanceEvent();
            for (int i = 0; i < 100; ++i)
                villageWorld.Update(.1f);
            SetPlayerPosition(ActorPosition(npcActors[16]));
            camera = PlayerPosition();
            interact();
            draw();
            capture("village-merchant.bmp");
            dialog.clear();
            talkingNpc = -1;
            villageWorld.AdvanceEvent();
            for (int i = 0; i < 550; ++i)
                villageWorld.Update(.1f);
            camera = {0, 18};
            draw();
            capture("village-evacuation.bmp");
            villageWorld.AdvanceEvent();
            villageWorld.Update(.1f);
            SetPlayerPosition({0, -13});
            camera = {0, -16};
            draw();
            capture("village-stillness.bmp");
            reset();
            width = 1000;
            height = 720;
            draw();
            capture("tutorial-small.bmp");
            running = false;
        }
        if (bench)
        {
            {
                Profiler::Scope wait("benchmark_gpu_wait");
                glFinish();
            }
            auto end = std::chrono::steady_clock::now();
            if (benchFrames >= 20)
                benchTotal += std::chrono::duration<double, std::milli>(end - now).count();
            if (++benchFrames == 140)
            {
                std::ofstream o("benchmark-report.txt");
                o << "120 measured frames (20 warmup), 1280x800, scene + post + UI: " << benchTotal / 120
                  << " ms/frame; GPU=" << glGetString(GL_RENDERER) << "\n";
                running = false;
            }
        }
        {
            Profiler::Scope scope("present_wait");
            SwapBuffers(dc);
        }
        if (wcsstr(command, L"--validate-render"))
        {
            unsigned errors = 0;
            for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
            {
                ++errors;
                Profiler::Event("opengl_error", std::to_string(error));
            }
            Profiler::Add("opengl_errors", errors);
        }
        Profiler::Add("viewport_width", width);
        Profiler::Add("viewport_height", height);
        Profiler::Add("postprocess_enabled", postEnabled ? 1 : 0);
        Profiler::Add("map_seed", firstWorld.Seed());
        Profiler::Add("scene_mode", firstLevelMode ? 1 : 0);
        Profiler::Add("actor_count",
                      double(firstLevelMode ? firstWorld.GetScene().Size() : tutorialScene.Size()));
        Profiler::EndFrame();
    }
    if (treeList)
        glDeleteLists(treeList, 1);
    tutorialTerrain.Clear();
    frozenTerrain.Clear();
    Profiler::Shutdown();
    LevelView::Shutdown();
    RenderBatch::Shutdown();
    Visuals::Shutdown();
    for (auto g : glyphs)
        glDeleteLists(g.second, 1);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(windowHandle, dc);
    DestroyWindow(windowHandle);
    return 0;
}
