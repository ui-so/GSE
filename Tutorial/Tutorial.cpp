#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <windows.h>
#include <gl/GL.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "DrawStats.h"
#include "RenderBatch.h"
#include "Profiler.h"
#include "Monitor.h"
#include "Journey.h"
#include "JournalArt.h"
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
#include "Isometric.h"
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
int width = 1280, height = 800;
float elapsed = 0, worldTime = 0;
bool keys[GLFW_KEY_LAST + 1]{}, paused = false, active = true, running = true, finished = false;
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
bool iconified = false;
std::wstring speaker, dialog;
FirstLevel::World firstWorld;
bool firstLevelMode = false;
Village::World villageWorld;
Scene::Position villageFacing{.7071f, .7071f};
Journey::Quest journey;
bool journalOpen = false;
int journalPage = 0;
Scene::ActorId journalActor = 0, assemblyActor = 0, exitActor = 0;
int talkingNpc = -1;
int frozenTerrainStep = -1;
int validationExit = 0;
bool scenarioMonitoring = false;
void PublishMonitoring();
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
    std::wstring name;
    std::wstring line;
    C color;
};
const NPC npcTemplates[] = {
    {{-1, 0},
     L"의뢰 중개인 · 마라",
     L"일거리를 찾는다면 잘 왔어. 숲길 정리부터 보급품 배달까지, 오늘도 사람 사는 일은 계속되지.",
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

std::vector<NPC> npcs;
void BuildRegionalRoster()
{
    npcs.clear();
    constexpr int selection[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 20, 24};
    const wchar_t *names[] = {L"가온", L"누리", L"다온", L"라온", L"마루", L"보라", L"소담",
                              L"아람", L"여울", L"우림", L"유나", L"이솔", L"지안", L"초아",
                              L"태오", L"하람", L"해온", L"나린", L"도윤", L"루아"};
    const wchar_t *roles[] = {L"의뢰 중개인", L"제빵사", L"우물지기", L"약초사",       L"파수꾼",
                              L"상인",        L"아이",   L"종지기",   L"대장장이",     L"묘지기",
                              L"목수",        L"재봉사", L"양봉가",   L"수습 사제",    L"길잡이",
                              L"순회 상인",   L"성직자", L"직조공",   L"꽃 심는 아이", L"숲지기"};
    const wchar_t *lines[] = {L"이웃의 이름부터 적어 두고 있어요. 한 사람의 하루도 긴 이야기지요.",
                              L"빵을 너무 오래 구웠나 봐. 방패로 팔면 더 비쌀까?",
                              L"오늘 물을 길었나요? 우물 곁에서는 잠시 쉬어 가세요.",
                              L"작은 새싹도 뿌리를 내리려고 애쓰네요. 다친 잎도 함부로 버리진 않아요.",
                              L"숲길엔 짐승이 있으니 나갈 때 물약부터 챙기시오.",
                              L"회복약을 팔아요. 여행자가 돌아와 이야기를 들려주면 그게 가장 반갑지요.",
                              L"저 구름은 토끼 같아. 해가 지면 토끼도 집으로 갈까?",
                              L"종은 시간을 알려 주지. 기다리는 사람에겐 조금 다르게 들리겠지만.",
                              L"오늘은 칼보다 냄비를 많이 고쳤어. 저녁이 평안하겠군.",
                              L"무덤 곁의 꽃도 물을 줘야지. 기억하는 일에도 손이 필요하거든.",
                              L"나무결을 따라 다듬고 있어. 서두르면 오래 못 써.",
                              L"찢어진 망토는 꿰매 드려요. 유령의 망토라면 바늘부터 빌려 주세요.",
                              L"벌집을 지켜보면 작은 생명도 서로 기대어 산다는 걸 알게 돼.",
                              L"오늘은 기도보다 따뜻한 물을 먼저 건넸어요. 그 편이 필요한 사람도 있더군요.",
                              L"두 마을 광장을 오가며 직접 본 소식을 전해요.",
                              L"걷고 물건을 팔고 소식을 나릅니다. 신발 밑창이 제 장부만큼 얇아졌네요.",
                              L"상처는 돌볼 수 있어요. 살아 있는 이의 손을 잡는 것이 제 일이에요.",
                              L"아이가 돌아오면 함께 천을 걷어야겠어요. 오늘 저녁은 같이 먹고 싶네요.",
                              L"꺾은 꽃보다 심은 꽃을 보러 오는 게 좋아. 내일도 여기 있을까?",
                              L"이 나무가 다시 잎을 내는 걸 여러 번 봤어요. 기다리는 일은 익숙해요."};
    for (int village = 0; village < WorldLayout::VillageCount; ++village)
        for (int local = 0; local < WorldLayout::ResidentsPerVillage; ++local)
        {
            NPC npc = npcTemplates[selection[local]];
            auto base = WorldLayout::Settlements()[village].center;
            npc.p = npc.p + V{base.x, base.y};
            if (village > 0)
            {
                npc.name = std::wstring(roles[local]) + L" · " + names[local];
                npc.line = lines[local];
            }
            if (local == 14 || local == 15)
            {
                auto plaza = WorldLayout::Plaza(village);
                npc.p = {plaza.x + (local == 14 ? -1.f : 1.f), plaza.y};
                npc.name = std::wstring(WorldLayout::Settlements()[village].name) +
                           (local == 14 ? L" 길잡이" : L" 순회 상인");
                npc.line = lines[local];
            }
            npcs.push_back(std::move(npc));
        }
}

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
    const auto offset = Isometric::Project(d.x, d.y, z);
    return {width * .5f + offset.x, height * .53f + offset.y};
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
    if (desaturatedGeometry)
        c.r = c.g = c.b = c.r * .299f + c.g * .587f + c.b * .114f;
    Visuals::LightPool(p.x, p.y, r, .65f, c.r, c.g, c.b, c.a);
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
constexpr TerrainBatch::Lake VillageLake{16, -7, 5.5f, 8};
bool lake(V p)
{
    float x = (p.x - VillageLake.x) / VillageLake.radiusX, y = (p.y - VillageLake.y) / VillageLake.radiusY;
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
    frozenTerrainStep = -1;
    BuildRegionalRoster();
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
    auto &journal = tutorialScene.Create("TravelJournal", Scene::Kind::Hud);
    journal.SetLayer(Scene::Layer::Interface);
    journal.SetVisible(false);
    journalActor = journal.GetId();
    auto &assembly = tutorialScene.Create("AssemblyWaypoint", Scene::Kind::Marker, sceneryGroup);
    assembly.SetWorldPosition({0, 18});
    assembly.SetLayer(Scene::Layer::Effects);
    assemblyActor = assembly.GetId();
    auto &exit = tutorialScene.Create("SouthRoadWaypoint", Scene::Kind::Marker, sceneryGroup);
    exit.SetWorldPosition(WorldLayout::Plaza(1));
    exit.SetLayer(Scene::Layer::Effects);
    exitActor = exit.GetId();
    V houses[] = {{-6, -4}, {11, -9}, {5, 5}, {-6, 7}, {7, 1}, {-15, -7}, {-15, 7}, {8, 15}};
    for (int village = 0; village < WorldLayout::VillageCount; ++village)
    {
        auto base = WorldLayout::Settlements()[village].center;
        for (int i = 0; i < int(std::size(houses)); ++i)
            objects.push_back({houses[i] + V{base.x, base.y}, 1, 1, i});
    }
    for (int x = -93; x <= 93; x += 4)
        for (int y = -93; y <= 93; y += 4)
        {
            V p{float(x) + hash(x, y), float(y) + hash(y, x)};
            bool reserved =
                WorldLayout::VillageAt({p.x, p.y}) >= 0 || WorldLayout::IsRoad({p.x, p.y}) || lake(p);
            if (!reserved && hash(x, y) > .32f)
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
void house(V p)
{
    Visuals::DrawScenery(Visuals::Scenery::Cottage, p.x, p.y, 1, 1, desaturatedGeometry);
    if (!desaturatedGeometry)
    {
        auto frontWindow = Isometric::Project(-.78f, 1.212f, 39);
        auto sideWindow = Isometric::Project(1.312f, .288f, 39);
        glow(p + V{frontWindow.x, frontWindow.y}, 25, {.90f, .61f, .29f, .28f});
        glow(p + V{sideWindow.x, sideWindow.y}, 20, {.90f, .61f, .29f, .26f});
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
    if (hero)
        SceneModels::Draw(SceneModels::Kind::MercenaryKit, p.x, p.y);
    Visuals::Sprite(p.x, p.y, hero ? 0 : 1 + id % 3, hero ? facing : id % 4, action,
                    phase * (action == 1 ? 1.6f : .65f) + id * .17f, frozen);
    if (!hero && length(ActorPosition(npcActors[id]) - PlayerPosition()) < 4)
        text(p.x - 45, p.y - 76, villageWorld.Behavior(id), frozen ? C{.65f, .65f, .65f} : gold);
    if (hero)
        glow(p + V{14, -16}, 28, gold);
}
Scene::ActorId JourneyTargetActor()
{
    if (journey.Hunting())
        return villageWorld.HuntTarget();
    int npc = journey.TargetNpc();
    if (npc >= 0)
        return npcActors[npc];
    switch (journey.Current())
    {
    case Journey::Stage::RecordLake:
        return shrineActor;
    case Journey::Stage::RecordFlowers:
    case Journey::Stage::RecordStillness:
        return seedActor;
    case Journey::Stage::EscortFamily:
        return assemblyActor;
    case Journey::Stage::NextVillage:
        return exitActor;
    default:
        return tutorialPlayer;
    }
}
V target()
{
    return ActorPosition(JourneyTargetActor());
}
std::wstring objective()
{
    return journey.Objective();
}
void openDialog(const wchar_t *name, const wchar_t *msg)
{
    actionTime = 1.2f;
    speaker = name;
    dialog = msg;
}
void CloseJournal()
{
    journalOpen = false;
    if (auto actor = tutorialScene.Find(journalActor))
        actor->SetVisible(false);
    std::fill(keys, std::end(keys), false);
}
void OpenJournal(int page)
{
    journalPage = std::clamp(page, 0, 2);
    journalOpen = true;
    if (auto actor = tutorialScene.Find(journalActor))
        actor->SetVisible(true);
    moving = false;
    std::fill(keys, std::end(keys), false);
}
void SyncJourneyPhase()
{
    while (villageWorld.GetPhase() < journey.RequiredPhase())
        villageWorld.AdvanceEvent();
}
void RecordLandscape()
{
    if (villageWorld.IsDead())
        return;
    V p = PlayerPosition();
    int page = -1;
    if (ActorActive(shrineActor) && length(p - ActorPosition(shrineActor)) < 2.6f)
        page = journey.Record(Journey::Site::Lake, villageWorld.GetPhase());
    else if (ActorActive(seedActor) && length(p - ActorPosition(seedActor)) < 2.6f)
        page = journey.Record(journey.Current() == Journey::Stage::RecordStillness ? Journey::Site::Stillness
                                                                                   : Journey::Site::Flowers,
                              villageWorld.GetPhase());
    if (page >= 0)
    {
        villageWorld.Sketch({p.x, p.y});
        OpenJournal(page);
    }
    else
        openDialog(L"여행 수첩", journey.Hint());
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
    if (journalOpen)
    {
        CloseJournal();
        return;
    }
    if (!dialog.empty())
    {
        dialog.clear();
        talkingNpc = -1;
        return;
    }
    int npc = nearest();
    talkingNpc = npc >= 0 && npc < NPC_COUNT ? npc : -1;
    if (talkingNpc >= 0)
    {
        const auto &citizen = villageWorld.Citizens()[size_t(npc)];
        auto story = journey.Dialogue(npc, villageWorld.GetPhase());
        if (citizen.frozen)
            story.clear();
        villageWorld.Talk(npc);
        if (journey.Current() == Journey::Stage::FindFamily)
            villageWorld.BeginEscort(npc);
        journey.Talk(npc, citizen.frozen, villageWorld.GetPhase(), villageWorld.Kills());
        auto reward = journey.TakeReward();
        if (reward.coins || reward.experience)
            villageWorld.GrantReward(reward.coins, reward.experience);
        std::wstring message =
            story.empty()
                ? (citizen.frozen ? villageWorld.Dialogue(npc)
                                  : std::wstring(npcs[npc].line) + L" " + villageWorld.Dialogue(npc))
                : story;
        openDialog(npcs[npc].name.c_str(), message.c_str());
    }
    else if (npc == NPC_COUNT || npc == NPC_COUNT + 1)
        RecordLandscape();
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
        firstWorld.Update(dt, {(x + y) * .70710678f, (y - x) * .70710678f},
                          (keys[GLFW_KEY_LEFT_SHIFT] || keys[GLFW_KEY_RIGHT_SHIFT]));
        if (keys[GLFW_KEY_SPACE])
            firstWorld.Attack();
        auto position = firstWorld.GetPosition(firstWorld.Player().actor);
        moving = std::abs(position.x - previous.x) + std::abs(position.y - previous.y) > .0001f;
        V tracked{position.x, position.y};
        camera = camera + (tracked - camera) * std::min(1.f, dt * 5);
        return;
    }
    if (!active || paused || journalOpen)
        return;
    worldTime += dt;
    actionTime = std::max(0.f, actionTime - dt);
    updateWildlife(dt);
    villageWorld.Update(dt, dialog.empty() ? -1 : talkingNpc);
    if (dialog.empty() && !villageWorld.IsDead())
    {
        SyncJourneyPhase();
        auto player = PlayerPosition();
        journey.Update(dt, {player.x, player.y}, villageWorld.GetPhase(), villageWorld.Kills(),
                       villageWorld.FamilyResolved());
        SyncJourneyPhase();
        finished = journey.Complete();
    }
    moving = false;
    if (!finished)
        elapsed += dt;
    if (!dialog.empty() || villageWorld.IsDead())
        return;
    if (keys[GLFW_KEY_SPACE])
        villageWorld.Attack(villageFacing);
    V d{float(keys['D'] - keys['A']), float(keys['S'] - keys['W'])};
    if (ActorActive(tutorialPlayer) && length(d) > 0)
    {
        moving = true;
        facing = fabsf(d.x) > fabsf(d.y) ? (d.x < 0 ? 1 : 2) : (d.y < 0 ? 3 : 0);
        d = d * (1 / length(d));
        V move{(d.x + d.y) * .70710678f, (d.y - d.x) * .70710678f};
        villageFacing = {move.x, move.y};
        move = move * (dt * ((keys[GLFW_KEY_LEFT_SHIFT] || keys[GLFW_KEY_RIGHT_SHIFT]) ? 5.0f : 3.1f));
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
            for (int x = int(MIN_X) - 2; x < int(MAX_X) + 2; ++x)
                for (int y = int(MIN_Y) - 2; y < int(MAX_Y) + 2; ++y)
                {
                    Scene::Position p{x + .5f, y + .5f};
                    int region = WorldLayout::VillageAt(p);
                    bool village = false, localRoad = false;
                    if (region >= 0)
                    {
                        auto base = WorldLayout::Settlements()[region].center;
                        float dx = std::abs(p.x - base.x), dy = std::abs(p.y - base.y);
                        village = dx < 8 && dy < 8 && dx + dy < 12.5f + hash(x, y);
                        localRoad = std::abs(p.x - base.x) < 1.8f || std::abs(p.y - base.y - 18) < 1.8f;
                    }
                    bool road =
                        WorldLayout::IsRoad(p) || localRoad || (x >= 0 && x < 14 && std::abs(y + 5) < 2);
                    tiles.push_back({x, y, village ? 1 : road ? 2 : 0, .97f + hash(x / 6, y / 6) * .025f});
                }
            tutorialTerrain.Build(tiles, &VillageLake);
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
        if (villageWorld.DesaturationRadius() > 0)
        {
            int step = int(villageWorld.DesaturationRadius() * 4);
            if (frozenTerrain.Empty() || frozenTerrainStep != step)
            {
                Profiler::Scope overlayScope("desaturation_overlay_rebuild");
                std::vector<TerrainBatch::Tile> tiles;
                for (int x = -6; x <= 6; ++x)
                    for (int y = -22; y <= -10; ++y)
                        if (villageWorld.IsFrozen({x + .5f, y + .5f}))
                            tiles.push_back({x, y, (abs(x) < 2 || abs(y + 18) < 2) ? 6 : 4,
                                             .92f + hash(x / 3, y / 3) * .10f});
                frozenTerrain.Build(tiles);
                frozenTerrainStep = step;
                Profiler::Add("desaturation_overlay_tiles", double(tiles.size()));
            }
            frozenTerrain.Draw(width, height, 0);
        }
        RenderBatch::PopMatrix();
        SceneRender::Pop();
        RenderBatch::PopMatrix();
    }
    Scene::View view{camera.x, camera.y, width, height};
    auto visibleActors = tutorialScene.RenderQueue(Scene::Layer::World, &view);
    for (auto actor : visibleActors)
    {
        if (actor->GetKind() == Scene::Kind::Brazier && !villageWorld.IsFrozen(actor->GetWorldPosition()))
        {
            V p = project(ActorPosition(actor->GetId()));
            float pulse = .66f + .10f * sinf(worldTime * 5 + actor->GetWorldPosition().x);
            Visuals::LightPool(p.x, p.y, 67, .53f, .92f, .60f, .25f, pulse * .65f);
        }
        if (actor->GetKind() != Scene::Kind::Tree && actor->GetKind() != Scene::Kind::House)
            continue;
        const auto &o = objects.at(actor->GetDataIndex());
        V p = project(ActorPosition(o.actor));
        if (p.x < -200 || p.x > width + 200 || p.y < -150 || p.y > height + 100)
            continue;
        SceneRender::Push(*tutorialScene.Find(o.actor), p.x, p.y);
        float r = o.type == 1 ? 80.f : 54.f * o.scale;
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
    const V playerScreen = project(PlayerPosition());
    for (auto actor : visibleActors)
    {
        if (actor->GetKind() == Scene::Kind::Companion)
        {
            Profiler::Scope companionDraw("companion_render_submit");
            const auto &familiar = villageWorld.Familiar();
            auto position = actor->GetWorldPosition();
            V ground = project({position.x, position.y}),
              p = project({position.x, position.y}, actor->GetWorldElevation());
            SceneRender::Push(*actor, p.x, p.y);
            Visuals::SoftShadow(ground.x, ground.y, 15, .5f, .18f);
            glow(p + V{0, -6}, 32, {.18f, .63f, .9f, .55f});
            SceneModels::Draw(SceneModels::Kind::FoxCompanion, p.x, p.y, .72f,
                              familiar.time * (familiar.wary ? .15f : 1.7f));
            if (familiar.pulse > 0 && ActorActive(familiar.target))
            {
                V goal = project(ActorPosition(familiar.target), 25);
                line(p, goal, {.32f, .8f, 1.f, familiar.pulse * 1.7f}, 3);
                ellipse(goal, 23, 10, {.3f, .8f, 1.f, familiar.pulse * .5f});
            }
            SceneRender::Pop();
            Profiler::Add("companion_models_drawn");
            continue;
        }
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
            if (m.weakened > 0)
            {
                ellipse(p, 25, 11, {.2f, .65f, 1.f, .35f});
                text(p.x - 25, p.y - 77, L"감속 · 약화", teal);
            }
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
        if (p.x < -150 || p.x > width + 150 || p.y < -80 || p.y > height + 200)
            continue;
        SceneRender::Push(*actor, p.x, p.y);
        desaturatedGeometry = villageWorld.IsFrozen(actor->GetWorldPosition());
        switch (o.type)
        {
        case 0: {
            const V offset = p - playerScreen;
            const bool occluding =
                std::abs(offset.x) < 60 * o.scale && offset.y > 0 && offset.y < 110 * o.scale;
            if (!Visuals::DrawScenery(Visuals::Scenery::Oldwood, p.x, p.y, o.scale, occluding ? .42f : 1.f,
                                      desaturatedGeometry))
                treeGeometry(p, o.scale);
            break;
        }
        case 1:
            house(p);
            break;
        case 2:
            person(p, npcs[o.id].color, false, o.id);
            break;
        case 3:
            Visuals::SoftShadow(p.x + 8, p.y + 6, 41, .45f, .9f);
            ellipse(p, 26, 12, {.14f, .23f, .31f});
            ellipse(p + V{0, -10}, 26, 13, {.35f, .455f, .46f});
            ellipse(p + V{0, -10}, 19, 9, {.07f, .17f, .20f});
            ellipse(p + V{0, -8}, 14, 5, {.15f, .39f, .40f});
            for (int block = 0; block < 6; ++block)
            {
                float angle = (block + .5f) * .523599f;
                V joint{cosf(angle) * 25, -10 + sinf(angle) * 12};
                line(p + joint, p + V{joint.x * .78f, joint.y - 3}, {.23f, .28f, .27f});
            }
            break;
        case 4:
            Visuals::SoftShadow(p.x + 9, p.y + 6, 45, .43f, .82f);
            poly({p + V{-31, 3}, p + V{2, -12}, p + V{31, 1}, p + V{0, 17}}, {.14f, .23f, .31f});
            poly({p + V{-26, -2}, p + V{1, -16}, p + V{27, -3}, p + V{0, 11}}, {.35f, .455f, .46f});
            poly({p + V{-14, -3}, p + V{-12, -38}, p + V{0, -52}, p + V{10, -42}, p + V{6, 4}},
                 {.285f, .39f, .43f});
            poly({p + V{10, -42}, p + V{18, -36}, p + V{16, -3}, p + V{6, 4}}, {.14f, .23f, .31f});
            poly({p + V{-12, -38}, p + V{0, -52}, p + V{10, -42}, p + V{-1, -40}}, {.40f, .51f, .50f});
            glow(p + V{-2, -27}, 28, {.20f, .76f, .69f, .62f});
            poly({p + V{-1, -35}, p + V{3, -27}, p + V{-3, -20}, p + V{-6, -27}}, {.52f, .90f, .78f});
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
            ellipse(p, 1.5f, 1.5f, {.34f, .76f, .70f, .20f + .18f * sinf(worldTime + i)});
        }
        // Thin drifting mist preserves scene readability.
        for (int i = 0; i < 5; i++)
            ellipse({fmodf(worldTime * 8 + i * 330, width + 500.f) - 250, height * .55f + i * 65.f}, 280, 23,
                    {.28f, .46f, .54f, .014f});
        SceneRender::Pop();
        RenderBatch::PopMatrix();
    }
    if (journey.HasObjective() && ActorVisible(markerActor))
    {
        auto targetActor = JourneyTargetActor();
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
    auto m = [&](V p) { return V{x + 90 + p.x * .78f, y + 83 + p.y * .67f}; };
    for (int village = 1; village < WorldLayout::VillageCount; ++village)
    {
        auto a = WorldLayout::Plaza(0), b = WorldLayout::Plaza(village);
        line(m({a.x, a.y}), m({b.x, b.y}), {.42f, .36f, .24f}, 2);
    }
    for (int village = 0; village < WorldLayout::VillageCount; ++village)
    {
        auto base = WorldLayout::Settlements()[village].center;
        V p = m({base.x, base.y});
        text(p.x - 6, p.y - 9, std::to_wstring(village + 1), gold);
    }
    ellipse(m({16, -7}), 10, 12, {.15f, .35f, .38f});
    for (auto &o : objects)
        if (o.type == 1 && ActorVisible(o.actor))
        {
            V p = m(ActorPosition(o.actor));
            rect(p.x - 3, p.y - 3, 6, 6, {.55f, .48f, .34f});
        }
    if (journey.HasObjective())
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
void DrawJournal()
{
    if (!journalOpen || !ActorVisible(journalActor))
        return;
    Profiler::Scope scope("journal_ui_cpu");
    Profiler::GpuScope gpu("journal_ui");
    auto &actor = *tutorialScene.Find(journalActor);
    auto position = actor.GetWorldPosition();
    RenderBatch::PushMatrix();
    RenderBatch::Translatef(position.x, position.y, 0);
    SceneRender::Push(actor, 0, 0, true);
    rect(0, 0, float(width), float(height), {.015f, .025f, .028f, .9f});
    float x = 24, y = 24, w = width - 48.f, h = height - 48.f;
    rect(x, y, w, h, {.075f, .09f, .09f, 1});
    rect(x, y, w, 3, gold);
    text(x + 28, y + 34, L"용병의 여행 수첩 · 꼬리 자국 주의", gold);
    text(x + 28, y + 67,
         std::to_wstring(journalPage + 1) + L" / 3  ·  " + Journey::Quest::PageTitle(journalPage), ink);
    float imageX = x + 28, imageY = y + 103;
    float imageW = std::min(w * .64f, (h - 188) * 1.5f), imageH = imageW / 1.5f;
    rect(imageX - 2, imageY - 2, imageW + 4, imageH + 4, {.7f, .65f, .51f});
    bool unlocked = journey.Unlocked(journalPage);
    if (!unlocked || !JournalArt::Draw(journalPage, imageX, imageY, imageW, imageH))
    {
        rect(imageX, imageY, imageW, imageH, {.12f, .16f, .16f});
        text(imageX + 24, imageY + imageH * .5f,
             unlocked ? L"그림을 불러오지 못했습니다" : L"아직 기록하지 않은 풍경", gold);
    }
    float notesX = imageX + imageW + 24;
    int columns = std::max(10, int((x + w - 24 - notesX) / 17));
    text(notesX, imageY + 22, L"풍경에 남긴 말", gold);
    wrapped(notesX, imageY + 58,
            unlocked ? journey.PageNote(journalPage) : L"목적지의 풍경을 P로 기록하면 이 페이지가 열립니다.",
            columns);
    text(notesX, y + h - 154, L"기록한 그림 " + std::to_wstring(journey.PageCount()) + L" / 3", teal);
    wrapped(notesX, y + h - 122, L"어릴 적부터 함께한 푸른 여우 동료.", columns);
    text(x + 28, y + h - 53,
         L"그림에는 마법이 없다. 지나온 풍경을 남기던 습관에, 이제 사람들의 이야기를 덧붙인다.", ink);
    text(x + 28, y + h - 22,
         L"← → 그림 넘기기   1~3 페이지 선택   J / P / E / Esc 닫기 · 읽는 동안 시간 정지", gold);
    SceneRender::Pop();
    RenderBatch::PopMatrix();
    RenderBatch::Flush();
}
void draw()
{
    DrawStats::Frame frameStats;
    RenderBatch::Frame batchFrame;
    Profiler::Scope renderScope("render_total_cpu");
    glViewport(0, 0, width, height);
    glClearColor(.024f, .039f, .062f, 1);
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
    Visuals::PostProcess(width, height, postEnabled);
    if (!ActorVisible(hudActor))
        return;
    Profiler::Scope hudScope("ui_cpu");
    Profiler::GpuScope hudGpu("ui");
    auto hudPosition = tutorialScene.Find(hudActor)->GetWorldPosition();
    RenderBatch::PushMatrix();
    RenderBatch::Translatef(hudPosition.x, hudPosition.y, 0);
    SceneRender::Push(*tutorialScene.Find(hudActor), 0, 0, true);
    rect(0, 0, float(width), 6, {.67f, .51f, .28f});
    rect(24, 26, std::min(width - 270.f, 650.f), 111, {.025f, .045f, .043f, .92f});
    int region = villageWorld.CurrentVillage();
    text(43, 54, region >= 0 ? WorldLayout::Settlements()[region].name : L"마을 사이 숲길", gold);
    text(43, 84, objective());
    int sec = int(elapsed);
    wchar_t t[100];
    swprintf_s(t, L"서장 · %d / 17     %02d:%02d  |  5개 마을 · 주민 100명", journey.Step(), sec / 60,
               sec % 60);
    text(43, 115, t, {.49f, .61f, .56f});
    if (region >= 0)
    {
        const auto &mood = villageWorld.Statistics()[region];
        text(42, 220,
             std::wstring(region == 0 ? villageWorld.PhaseName() : L"현지 일상") + L" · 평온 " +
                 std::to_wstring(int(mood.calm)) + L" · 불안 " + std::to_wstring(int(mood.fear)),
             gold);
    }
    else
        text(42, 220, L"마을 사이 숲길 · 몬스터를 경계하세요", gold);
    text(42, 246,
         L"체력 " + std::to_wstring(villageWorld.Health()) + L"  동전 " +
             std::to_wstring(villageWorld.Coins()) + L"  회복약 " + std::to_wstring(villageWorld.Potions()) +
             L"  경험치 " + std::to_wstring(villageWorld.Experience()));
    text(42, 272,
         L"의뢰 완료 " + std::to_wstring(journey.ContractsCompleted()) + L" · 풍경 " +
             std::to_wstring(journey.PageCount()) + L" / 3 · " +
             (journey.CarryingSupplies() ? L"보급품 소지" : L"보급품 없음"));
    text(42, 298,
         villageWorld.Familiar().wary ? L"여우: 곁에 바짝 붙음 · 평소와 다른 마력"
                                      : L"여우: 오래된 동료 · 자동 감속/약화 지원",
         teal);
    wrapped(42, height - 112.f, journey.Hint(), std::max(25, (width - 90) / 18));
    if (villageWorld.IsDead())
        text(width * .5f - 150, height * .5f, L"쓰러졌습니다 · R로 마을에서 회복", gold);
    minimap();
    rect(24, height - 62.f, width - 48.f, 40, {.025f, .045f, .043f, .92f});
    text(42, height - 36.f,
         L"WASD 이동  E 대화/획득  P 기록  J 수첩  Space 검  F 동료  Q 회복  F1 사냥터  Esc 휴식", ink);
    if (journey.HasObjective())
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
        float x = width * .12f, y = height - 280.f;
        rect(x, y, width * .76f, 202, {.025f, .043f, .041f, .98f});
        rect(x, y, 3, 202, gold);
        text(x + 23, y + 30, speaker, gold);
        wrapped(x + 23, y + 63, dialog, std::max(20, int((width * .76f - 46) / 18)));
        text(x + 23, y + 181, L"[E] 닫기   상인 [1] 구입   성직자 [2] 치료", teal);
    }
    if (finished && !journey.HasObjective() && dialog.empty())
    {
        rect(width * .5f - 240, 185, 480, 100, {.025f, .045f, .043f, .94f});
        text(width * .5f - 212, 217, L"서장 완료 · 용병의 여행", gold);
        text(width * .5f - 212, 246, L"두 의뢰와 급속 탈색을 겪고 사람들의 이야기를 남겼다.");
        text(width * .5f - 212, 270, L"J로 수첩 · 다섯 마을 중개인에게 다음 일감 받기");
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
    DrawJournal();
}
void reset()
{
    if (firstLevelMode)
    {
        firstWorld.Generate(static_cast<std::uint32_t>(GetTickCount64()));
        camera = {.5f, .5f};
        paused = false;
        moving = false;
        std::fill(keys, std::end(keys), false);
        return;
    }
    initWorld();
    moving = false;
    actionTime = 0;
    facing = 0;
    journey = Journey::Quest{};
    CloseJournal();
    journalPage = 0;
    elapsed = 0;
    SetPlayerPosition({0, 2});
    camera = PlayerPosition();
    finished = false;
    paused = false;
    dialog.clear();
    talkingNpc = -1;
    std::fill(keys, std::end(keys), false);
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
bool RunStoryScenario(bool captureFrames)
{
    std::ofstream report("story-scenario-report.txt");
    auto check = [&](bool okay, const char *name) {
        report << (okay ? "PASS: " : "FAIL: ") << name << " stage=" << journey.StageKey()
               << " kills=" << villageWorld.Kills() << " coins=" << villageWorld.Coins()
               << " phase=" << int(villageWorld.GetPhase()) << "\n";
        report.flush();
        if (scenarioMonitoring)
        {
            PublishMonitoring();
            Sleep(600);
        }
        return okay;
    };
    auto shot = [&](const char *name) {
        if (captureFrames)
        {
            draw();
            capture(name);
        }
    };
    auto talk = [&](int npc) {
        SetPlayerPosition(ActorPosition(npcActors[npc]));
        camera = PlayerPosition();
        interact();
        interact();
        SyncJourneyPhase();
    };
    auto hunt = [&]() {
        int targetKills = villageWorld.Kills() + 2;
        for (int victim = 0; victim < 2; ++victim)
        {
            auto id = villageWorld.HuntTarget();
            if (!id)
                return false;
            auto position = ActorPosition(id);
            V hero{};
            bool found = false;
            for (auto d : {V{1, 0}, V{-1, 0}, V{0, 1}, V{0, -1}})
            {
                auto candidate = position + d * .7f;
                bool clear = true;
                for (int step = 1; step <= 7; ++step)
                    if (blocked(position + d * (step * .1f)))
                        clear = false;
                if (clear)
                {
                    hero = candidate;
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
            SetPlayerPosition(hero);
            camera = hero;
            villageFacing = {position.x - hero.x, position.y - hero.y};
            int before = villageWorld.Kills();
            keys[GLFW_KEY_SPACE] = true;
            for (int frame = 0; frame < 100 && villageWorld.Kills() == before; ++frame)
            {
                update(.1f);
                if (victim == 0 && frame == 0)
                    shot(targetKills <= 2 ? "story-v2-companion-combat.bmp" : "story-v2-repeat-combat.bmp");
            }
            keys[GLFW_KEY_SPACE] = false;
            if (villageWorld.Kills() == before)
                return false;
        }
        update(.1f);
        return villageWorld.Kills() >= targetKills;
    };
    reset();
    if (!check(villageWorld.Coins() == 3 && ActorActive(villageWorld.Familiar().actor),
               "mercenary and living companion"))
        return false;
    shot("story-v2-arrival.bmp");
    OpenJournal(0);
    shot("journal-locked.bmp");
    CloseJournal();
    talk(0);
    if (!check(journey.Current() == Journey::Stage::HuntRoad &&
                   villageWorld.GetPhase() == Village::Phase::Daily,
               "ordinary first contract"))
        return false;
    if (!check(hunt(), "actual sword combat and companion support"))
        return false;
    if (!check(journey.Current() == Journey::Stage::ClaimHunt &&
                   villageWorld.GetPhase() == Village::Phase::Daily,
               "no disaster before daily work"))
        return false;
    talk(0);
    int firstPay = villageWorld.Coins();
    talk(0);
    if (!check(firstPay == 18 && villageWorld.Coins() == firstPay && journey.ContractsCompleted() == 1,
               "one-time hunt pay"))
        return false;
    talk(WorldLayout::Baker);
    if (!check(journey.CarryingSupplies(), "physical parcel pickup"))
        return false;
    shot("story-v2-delivery.bmp");
    talk(WorldLayout::Cleric);
    talk(0);
    if (!check(!journey.CarryingSupplies() && journey.ContractsCompleted() == 2 &&
                   villageWorld.Coins() == 28 && villageWorld.GetPhase() == Village::Phase::Daily,
               "two normal jobs and 25 coins pay"))
        return false;
    SetPlayerPosition(ActorPosition(seedActor));
    RecordLandscape();
    if (!check(!journalOpen, "wrong sketch location cannot advance"))
        return false;
    dialog.clear();
    talkingNpc = -1;
    SetPlayerPosition(ActorPosition(shrineActor));
    camera = {10, -5};
    RecordLandscape();
    auto beforeNote = std::wstring(journey.PageNote(0));
    if (!check(journalOpen && journey.Unlocked(0) && !villageWorld.StoryRecorded(),
               "ordinary evening notebook"))
        return false;
    float beforeTime = worldTime;
    auto beforePlayer = PlayerPosition();
    update(.1f);
    if (!check(worldTime == beforeTime && length(PlayerPosition() - beforePlayer) < .001f,
               "notebook pauses simulation"))
        return false;
    shot("journal-lake.bmp");
    CloseJournal();
    talk(WorldLayout::Child);
    SetPlayerPosition(ActorPosition(seedActor));
    camera = {0, -13};
    RecordLandscape();
    shot("journal-flowers.bmp");
    CloseJournal();
    SyncJourneyPhase();
    update(.1f);
    if (!check(villageWorld.GetPhase() == Village::Phase::Warning && !villageWorld.Familiar().wary &&
                   !villageWorld.Citizens()[WorldLayout::Keeper].frozen,
               "ordinary signs and familiar evacuation protocol"))
        return false;
    shot("story-v2-ordinary-warning.bmp");
    talk(4);
    for (int i = 0; i < 25; ++i)
        update(.1f);
    camera = {0, -13};
    shot("story-v2-rapid.bmp");
    camera = PlayerPosition();
    shot("story-v2-wary-companion.bmp");
    if (!check(villageWorld.GetPhase() == Village::Phase::Evacuation && villageWorld.Familiar().wary &&
                   villageWorld.Citizens()[WorldLayout::Keeper].frozen,
               "rapid acceleration and wary companion"))
        return false;
    talk(WorldLayout::Mother);
    talk(WorldLayout::Child);
    if (!check(journey.Current() == Journey::Stage::EscortFamily && !villageWorld.FamilyResolved(),
               "dialog alone is not rescue"))
        return false;
    SetPlayerPosition(ActorPosition(assemblyActor));
    camera = PlayerPosition();
    for (int i = 0; i < 400 && journey.Current() == Journey::Stage::EscortFamily; ++i)
    {
        update(.1f);
        if (i == 20)
            shot("story-v2-escort.bmp");
    }
    if (!check(villageWorld.RescuedCount() == 2 && journey.Current() == Journey::Stage::VisitCleric,
               "two actual NPC arrivals"))
        return false;
    talk(WorldLayout::Cleric);
    SetPlayerPosition(ActorPosition(seedActor));
    camera = {0, -13};
    RecordLandscape();
    shot("journal-stillness.bmp");
    CloseJournal();
    talk(0);
    if (!check(journey.PageCount() == 3 && beforeNote != journey.PageNote(0) &&
                   villageWorld.StoryRecorded() && villageWorld.Citizens()[WorldLayout::Keeper].frozen,
               "life stories added without resurrection"))
        return false;
    auto next = WorldLayout::Plaza(1);
    SetPlayerPosition({next.x, next.y});
    camera = PlayerPosition();
    update(.1f);
    talk(20);
    if (!check(journey.Complete() && journey.RepeatableActive() && journey.ContractsCompleted() == 2,
               "next village offers paid work"))
        return false;
    shot("story-v2-next-village.bmp");
    if (!check(hunt(), "next-village repeatable combat"))
        return false;
    talk(20);
    if (!check(journey.ContractsCompleted() == 3 && villageWorld.Coins() == 40 && !journey.RepeatableActive(),
               "repeatable payout is one-time"))
        return false;
    shot("story-v2-chapter-complete.bmp");
    if (captureFrames)
    {
        OpenJournal(2);
        width = 1000;
        height = 720;
        draw();
        capture("journal-small.bmp");
        CloseJournal();
        std::ofstream renderReport("story-render-report.txt");
        renderReport << "Chapter complete=" << journey.Complete() << " pages=" << journey.PageCount()
                     << " contracts=" << journey.ContractsCompleted() << " rewards=" << journey.RewardCoins()
                     << " rescued=" << villageWorld.RescuedCount()
                     << " phase=" << int(villageWorld.GetPhase())
                     << " living_companion=1 embedded_png_v2=3\n";
    }
    report << "PASS: full v2 story, 2 ordinary contracts, rapid disaster, physical escort, 3 records, "
              "next-village contract.\n";
    return true;
}
void CaptureStory()
{
    if (!RunStoryScenario(true))
        validationExit = 5;
    running = false;
}
void OnKey(GLFWwindow *, int w, int, int action, int)
{
    if (w < 0 || w > GLFW_KEY_LAST)
        return;
    if (action == GLFW_RELEASE)
    {
        keys[w] = false;
        return;
    }
    if (action != GLFW_PRESS)
        return;
    keys[w] = true;
    if (journalOpen)
    {
        if (w == 'J' || w == 'P' || w == 'E' || w == GLFW_KEY_ESCAPE)
            CloseJournal();
        else if (w == GLFW_KEY_LEFT)
            journalPage = (journalPage + 2) % 3;
        else if (w == GLFW_KEY_RIGHT)
            journalPage = (journalPage + 1) % 3;
        else if (w >= '1' && w <= '3')
            journalPage = int(w - '1');
        return;
    }
    if (w == GLFW_KEY_F1)
    {
        firstLevelMode = !firstLevelMode;
        dialog.clear();
        paused = false;
        std::fill(keys, std::end(keys), false);
        if (firstLevelMode)
        {
            auto p = firstWorld.GetPosition(firstWorld.Player().actor);
            camera = {p.x, p.y};
        }
        else
            reset();
        return;
    }
    if (firstLevelMode)
    {
        if (w == GLFW_KEY_ESCAPE)
        {
            paused = !paused;
            std::fill(keys, std::end(keys), false);
        }
        else if (w == GLFW_KEY_F2)
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
        return;
    }
    if (w == GLFW_KEY_ESCAPE)
    {
        paused = !paused;
        std::fill(keys, std::end(keys), false);
    }
    else if (w == GLFW_KEY_F2)
        postEnabled = !postEnabled;
    else if (w == 'R' && villageWorld.IsDead())
    {
        villageWorld.Respawn();
        camera = PlayerPosition();
    }
    else if (w == GLFW_KEY_F3 && !paused)
    {
        if (journey.RequiredPhase() >= Village::Phase::Warning)
            villageWorld.AdvanceEvent();
        else
            openDialog(L"의뢰 수첩",
                       L"먼저 일상 의뢰 두 건을 마치고 풍경을 기록하세요. 사건은 그 뒤에 시작됩니다.");
    }
    else if (w == 'F' && !paused && dialog.empty())
        villageWorld.CastSupport();
    else if (w == 'J' && !paused && !villageWorld.IsDead())
        OpenJournal(journalPage);
    else if (w == 'P' && !paused && dialog.empty())
        RecordLandscape();
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
}

void OnFramebufferSize(GLFWwindow *, int framebufferWidth, int framebufferHeight)
{
    if (framebufferWidth > 0 && framebufferHeight > 0)
    {
        width = framebufferWidth;
        height = framebufferHeight;
    }
}

void OnFocus(GLFWwindow *, int focused)
{
    active = focused == GLFW_TRUE;
    if (!active)
        std::fill(std::begin(keys), std::end(keys), false);
}

void OnIconify(GLFWwindow *, int minimized)
{
    iconified = minimized == GLFW_TRUE;
}

namespace
{
struct DesktopResources
{
    GLFWwindow *window = nullptr;
    HFONT font = nullptr;
    HGDIOBJ previousFont = nullptr;
    bool initialized = false;

    ~DesktopResources()
    {
        if (dc && previousFont && previousFont != HGDI_ERROR)
            SelectObject(dc, previousFont);
        if (font)
            DeleteObject(font);
        if (dc)
        {
            DeleteDC(dc);
            dc = nullptr;
        }
        if (window)
            glfwDestroyWindow(window);
        if (initialized)
            glfwTerminate();
    }
};
} // namespace

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
    assert(npcs.size() == WorldLayout::CitizenCount);
    assert((MAX_X - MIN_X) * (MAX_Y - MIN_Y) == WorldLayout::MapWidth * WorldLayout::MapHeight);
    assert(beasts.size() == 15);
    for (int i = 0; i < NPC_COUNT; i++)
    {
        assert(!blocked(ActorPosition(npcActors[i])));
        assert(routeDistance(PlayerPosition(), ActorPosition(npcActors[i])) >= 0);
        SetPlayerPosition(ActorPosition(npcActors[i]));
        assert(nearest() == i);
    }
    for (int village = 1; village < WorldLayout::VillageCount; ++village)
    {
        auto a = WorldLayout::Plaza(0), b = WorldLayout::Plaza(village);
        for (int step = 0; step <= 248; ++step)
        {
            float t = step / 248.f;
            assert(!blocked({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}));
        }
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
    V stops[] = {PlayerPosition(),
                 ActorPosition(npcActors[0]),
                 {-10, 27},
                 ActorPosition(npcActors[0]),
                 ActorPosition(npcActors[1]),
                 ActorPosition(npcActors[WorldLayout::Cleric]),
                 ActorPosition(npcActors[0]),
                 ActorPosition(shrineActor),
                 ActorPosition(npcActors[WorldLayout::Child]),
                 ActorPosition(seedActor),
                 ActorPosition(npcActors[4]),
                 ActorPosition(npcActors[WorldLayout::Mother]),
                 ActorPosition(npcActors[WorldLayout::Child]),
                 ActorPosition(assemblyActor),
                 ActorPosition(npcActors[WorldLayout::Cleric]),
                 ActorPosition(seedActor),
                 ActorPosition(npcActors[0]),
                 ActorPosition(exitActor),
                 ActorPosition(npcActors[20])};
    for (size_t i = 0; i + 1 < std::size(stops); i++)
    {
        float d = routeDistance(stops[i], stops[i + 1]);
        assert(d >= 0);
        route += d;
    }
    assert(route / 3.1f + 180 < 300);
    std::ofstream report("self-test-report.txt");
    report << "Collision-aware route distance: " << route
           << " m\nWalking plus 150s reading, 15s combat and 15s escort allowance: " << route / 3.1f + 180
           << " seconds\n";
    assert(!blocked(ActorPosition(shrineActor)));
    assert(!blocked(ActorPosition(seedActor)));
    assert(!blocked(PlayerPosition()));
    assert(blocked({16, -7}));
    assert(RunStoryScenario(false));
    reset();
    assert(journey.Step() == 0 && !finished && dialog.empty() && !journalOpen);
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
    // Exercise the actual WASD update at normal speed along all four road edges.
    for (int village = 1; village < WorldLayout::VillageCount; ++village)
    {
        reset();
        for (const auto &monster : villageWorld.Monsters())
            tutorialScene.Find(monster.actor)->SetEnabled(false);
        auto start = WorldLayout::Plaza(0), goal = WorldLayout::Plaza(village);
        SetPlayerPosition({start.x, start.y});
        keys['D'] = village == 1 || village == 4;
        keys['S'] = village == 1 || village == 3;
        keys['A'] = village == 2 || village == 3;
        keys['W'] = village == 2 || village == 4;
        for (int step = 0; step < 200; ++step)
            update(.1f);
        assert(length(PlayerPosition() - V{goal.x, goal.y}) < .02f);
    }
    reset();
    report
        << "PASS: four clear plaza roads, actual WASD normal-speed travel=20 seconds per connected edge.\n";
    report << "PASS: 100 NPC interactions/reachability, five connected villages, 15 wildlife spawns, "
              "movement/collision/flee behavior.\n";
    report << "PASS: quest order, premature interaction, reachability, collision, pause, movement, diagonal "
              "speed, dialog lock, restart.\n";
    report << "PASS: v2 prologue through real Actors, paid work, physical rescue, three records and "
              "next-village contracts.\n";
    std::string villageReport;
    bool villageOkay = villageWorld.RunTests(villageReport);
    report << villageReport;
    report.flush();
    assert(villageOkay);
    return 0;
}
void PublishMonitoring()
{
    Profiler::Scope scope("monitor_collect");
    const char *phases[] = {"daily", "warning", "evacuation", "stillness"};
    const char *reactions[] = {"leave", "family", "stay", "help", "close_shop", "share_bread"};
    const char *services[] = {"none", "merchant", "cleric", "baker"};
    std::vector<Monitor::Region> regions;
    bool simulated = !firstLevelMode && !paused && !journalOpen && active && !villageWorld.IsDead();
    for (int index = 0; index < WorldLayout::VillageCount; ++index)
    {
        Monitor::Region village;
        village.id = WorldLayout::Settlements()[index].id;
        village.name = WorldLayout::Settlements()[index].name;
        village.kind = "village";
        village.phase = index == 0 ? phases[int(villageWorld.GetPhase())] : "daily";
        village.simulated = simulated;
        const auto &stats = villageWorld.Statistics()[index];
        village.hasAtmosphere = true;
        village.present = stats.present;
        village.rumors = stats.rumors;
        village.calm = stats.calm;
        village.fear = stats.fear;
        village.trust = stats.trust;
        village.hope = stats.hope;
        village.mood = stats.mood;
        village.walking = stats.walking;
        village.idle = stats.idle;
        village.talking = stats.talking;
        village.frozen = stats.frozen;
        for (const auto &receipt : villageWorld.Receipts())
            if (receipt.village == index)
                village.deliveries.push_back(
                    {receipt.carrier, receipt.rumor, receipt.time, receipt.position.x, receipt.position.y});
        for (size_t i = 0; i < villageWorld.Citizens().size(); ++i)
        {
            const auto &citizen = villageWorld.Citizens()[i];
            if (citizen.homeVillage != index)
                continue;
            Monitor::Npc npc;
            npc.actor = citizen.actor;
            npc.name = npcs[i].name;
            auto position = ActorPosition(citizen.actor);
            npc.x = position.x;
            npc.y = position.y;
            npc.active = ActorActive(citizen.actor);
            npc.recorded = citizen.recorded;
            npc.reaction = reactions[int(citizen.reaction)];
            npc.service = services[int(citizen.service)];
            npc.state = !npc.active                                                  ? "inactive"
                        : citizen.frozen                                             ? "frozen"
                        : !firstLevelMode && !dialog.empty() && talkingNpc == int(i) ? "talking"
                        : citizen.walking                                            ? "walking"
                                                                                     : "idle";
            npc.behavior = villageWorld.Behavior(int(i));
            npc.courier = citizen.courier;
            npc.rumors = citizen.rumors;
            npc.homeVillage = village.id;
            int here = WorldLayout::VillageAt({position.x, position.y});
            npc.currentVillage = here >= 0 ? WorldLayout::Settlements()[here].id : "wilderness";
            npc.destination =
                citizen.destination >= 0 ? WorldLayout::Settlements()[citizen.destination].id : "";
            village.npcs.push_back(std::move(npc));
        }
        regions.push_back(std::move(village));
    }
    Monitor::Region wilderness;
    wilderness.id = "wilderness";
    wilderness.kind = "world";
    wilderness.name = L"마을 사이 숲길";
    wilderness.phase = "hunting";
    wilderness.simulated = simulated;
    wilderness.monstersTotal = int(villageWorld.Monsters().size());
    wilderness.respawns = villageWorld.Respawns();
    for (const auto &monster : villageWorld.Monsters())
        if (monster.health > 0 && ActorActive(monster.actor))
            ++wilderness.monstersAlive;
    for (const auto &beast : beasts)
        if (ActorActive(beast.actor))
            ++wilderness.wildlife;
    regions.push_back(std::move(wilderness));
    Monitor::Region hunting;
    hunting.id = "first_level";
    hunting.kind = "hunting_ground";
    hunting.name = L"첫 사냥터";
    hunting.phase = firstWorld.IsComplete() ? "complete" : "hunting";
    hunting.simulated = firstLevelMode && !paused && active && !firstWorld.IsDead();
    hunting.monstersTotal = int(firstWorld.Enemies().size());
    for (const auto &enemy : firstWorld.Enemies())
        if (enemy.alive && firstWorld.IsActive(enemy.actor))
            ++hunting.monstersAlive;
    regions.push_back(std::move(hunting));
    int current = villageWorld.CurrentVillage();
    Monitor::Progression progress;
    progress.enabled = true;
    progress.step = journey.Step();
    progress.steps = Journey::Quest::ChapterSteps;
    progress.stage = journey.StageKey();
    progress.objective = journey.Objective();
    progress.contracts = journey.ContractsCompleted();
    progress.kills = journey.ContractKills();
    progress.rewardCoins = journey.RewardCoins();
    progress.supplies = journey.CarryingSupplies();
    progress.rescued = villageWorld.RescuedCount();
    progress.repeatable = journey.RepeatableActive();
    progress.wary = villageWorld.Familiar().wary;
    progress.disaster = villageWorld.DisasterKind();
    Monitor::Publish(regions,
                     firstLevelMode ? "first_level"
                     : current >= 0 ? WorldLayout::Settlements()[current].id
                                    : "wilderness",
                     paused || journalOpen, progress);
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR command, int)
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
        okay = Monitor::RunTests(report) && okay;
        okay = Journey::Quest::RunTests(report) && okay;
        okay = WorldLayout::RunTests(report) && okay;
        okay = JournalArt::RunTests(report) && okay;
        okay = Visuals::RunTests(report) && okay;
        std::ofstream file("first-level-test-report.txt");
        file << report;
        return legacy == 0 && okay ? 0 : 3;
    }
    if (wcsstr(command, L"--story-monitor-test"))
    {
        firstWorld.Generate(42);
        Monitor::Initialize(true);
        scenarioMonitoring = true;
        bool okay = RunStoryScenario(false);
        Monitor::Shutdown();
        return okay ? 0 : 5;
    }
    if (wcsstr(command, L"--monitor-test"))
    {
        initWorld();
        firstWorld.Generate(42);
        Monitor::Initialize(true);
        for (int i = 0; i < 10; ++i)
        {
            if (i == 2 || i == 4 || i == 6)
                villageWorld.AdvanceEvent();
            if (i == 8)
                firstLevelMode = true;
            for (int step = 0; step < 60; ++step)
                villageWorld.Update(.1f);
            PublishMonitoring();
            Sleep(600);
        }
        Monitor::Shutdown();
        return 0;
    }
    const bool benchmarkMode = wcsstr(command, L"--benchmark") != nullptr;
    const bool shot = wcsstr(command, L"--capture") != nullptr;
    const bool windowTest = wcsstr(command, L"--window-test") != nullptr;
    const bool hidden = benchmarkMode || shot || windowTest;
    DrawStats::InitializeConsole(hidden);
    auto windowError = [hidden](const wchar_t *message, int status) {
        if (!hidden)
            MessageBoxW(nullptr, message, L"GSE", MB_ICONERROR);
        return status;
    };
    glfwSetErrorCallback([](int code, const char *description) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
    });
    DesktopResources desktop;
    desktop.initialized = glfwInit() == GLFW_TRUE;
    if (!desktop.initialized)
        return windowError(L"GLFW를 초기화하지 못했습니다.", 1);

    // Request a legacy-compatible context: the renderer uses GLSL 120 and fixed-function GL.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DEPTH_BITS, 0);
    glfwWindowHint(GLFW_STENCIL_BITS, 0);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    desktop.window = glfwCreateWindow(width, height, u8"GSE · 잿빛 여울 — 튜토리얼", nullptr, nullptr);
    if (!desktop.window)
        return windowError(L"GLFW에서 호환 OpenGL 창을 만들지 못했습니다. 그래픽 드라이버를 확인하세요.", 2);
    auto *window = desktop.window;
    glfwMakeContextCurrent(window);
    if (glfwGetCurrentContext() != window)
        return windowError(L"OpenGL 컨텍스트를 활성화하지 못했습니다.", 2);
    glfwSetWindowSizeLimits(window, 1000, 720, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetFramebufferSizeCallback(window, OnFramebufferSize);
    glfwSetWindowFocusCallback(window, OnFocus);
    glfwSetWindowIconifyCallback(window, OnIconify);
    glfwSetKeyCallback(window, OnKey);
    int framebufferWidth = 0, framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    OnFramebufferSize(window, framebufferWidth, framebufferHeight);

    // GDI is only the Korean glyph rasterizer; it no longer owns the window or the GL context.
    dc = CreateCompatibleDC(nullptr);
    desktop.font =
        CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, HANGUL_CHARSET, OUT_DEFAULT_PRECIS,
                    CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
    if (!dc || !desktop.font)
        return windowError(L"한글 글꼴을 초기화하지 못했습니다.", 2);
    desktop.previousFont = SelectObject(dc, desktop.font);
    if (!desktop.previousFont || desktop.previousFont == HGDI_ERROR)
        return windowError(L"한글 글꼴을 선택하지 못했습니다.", 2);

    if (windowTest)
    {
        glfwSetWindowSize(window, 1100, 740);
        glfwPollEvents();
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        OnKey(window, GLFW_KEY_W, 0, GLFW_PRESS, 0);
        const bool pressed = keys[GLFW_KEY_W];
        OnKey(window, GLFW_KEY_W, 0, GLFW_RELEASE, 0);
        const bool released = !keys[GLFW_KEY_W];
        OnKey(window, GLFW_KEY_LEFT_SHIFT, 0, GLFW_PRESS, 0);
        OnFocus(window, GLFW_FALSE);
        const bool cleared = !active && !keys[GLFW_KEY_LEFT_SHIFT];
        OnFocus(window, GLFW_TRUE);
        OnKey(window, GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
        OnKey(window, GLFW_KEY_ESCAPE, 0, GLFW_REPEAT, 0);
        const bool resized = framebufferWidth == 1100 && framebufferHeight == 740 &&
                             width == framebufferWidth && height == framebufferHeight;
        const bool okay = pressed && released && cleared && paused && resized;
        std::ofstream report("glfw-window-test.txt");
        report << (okay ? "PASS" : "FAIL") << " GLFW " << glfwGetVersionString() << "; framebuffer=" << width
               << "x" << height << "; input=" << (pressed && released && cleared && paused) << "\n";
        return okay ? 0 : 6;
    }
    bool profileEnabled = wcsstr(command, L"--no-profile") == nullptr;
    bool profileConsole = wcsstr(command, L"--quiet-profile") == nullptr;
    unsigned profileSeed = (benchmarkMode || wcsstr(command, L"--capture-level"))
                               ? 42u
                               : static_cast<unsigned>(GetTickCount64());
    Profiler::Initialize(
        profileEnabled, profileConsole,
        (wcsstr(command, L"--tutorial") || wcsstr(command, L"--benchmark-region") ||
         (!benchmarkMode && !wcsstr(command, L"--level1") && !wcsstr(command, L"--capture-level")) ||
         (wcsstr(command, L"--capture") && !wcsstr(command, L"--capture-level")))
            ? "tutorial"
            : "level1",
        profileSeed, width, height);
    Profiler::Event("launch_arguments",
                    std::string(profileConsole ? "console=on" : "console=off") +
                        (benchmarkMode ? ";benchmark=on;fixed_step=1/60" : ";benchmark=off") +
                        (wcsstr(command, L"--no-post") ? ";postprocess=off" : ";postprocess=on"));
    Profiler::Event("window_backend", "glfw");
    if (!RenderBatch::Initialize())
    {
        Profiler::Event("renderer_initialization_failed", "VBO, instancing, or shaders unavailable");
        Profiler::Shutdown();
        return windowError(L"GPU 배칭 렌더러를 초기화하지 못했습니다. OpenGL 드라이버를 확인해 주세요.", 4);
    }
    AssetCache::Initialize();
    Visuals::Initialize();
    SceneModels::Initialize();
    JournalArt::Initialize();
    initWorld();
    bool regionalShot = wcsstr(command, L"--capture-region") != nullptr;
    bool storyShot = wcsstr(command, L"--capture-story") != nullptr;
    bool levelShot = wcsstr(command, L"--capture-level") != nullptr;
    firstLevelMode = levelShot || (!shot && !wcsstr(command, L"--tutorial") &&
                                   (benchmarkMode || wcsstr(command, L"--level1")));
    firstWorld.Generate(profileSeed);
    if (wcsstr(command, L"--benchmark-region"))
    {
        firstLevelMode = false;
        SetPlayerPosition({0, 2});
        camera = PlayerPosition();
        for (int i = 0; i < 600; ++i)
            villageWorld.Update(.1f);
        villageWorld.Respawn();
        SetPlayerPosition({-10, 31});
        camera = PlayerPosition();
    }
    if (wcsstr(command, L"--benchmark-rapid"))
    {
        firstLevelMode = false;
        villageWorld.AdvanceEvent();
        villageWorld.AdvanceEvent();
        SetPlayerPosition({0, -13});
        camera = PlayerPosition();
        for (int i = 0; i < 20; ++i)
            villageWorld.Update(.1f);
        Profiler::Event("benchmark_scenario", "story_v2_rapid_expansion");
    }
    if (firstLevelMode)
    {
        camera = {.5f, .5f};
    }
    Monitor::Initialize(!wcsstr(command, L"--no-monitor") && !shot &&
                        (!benchmarkMode || wcsstr(command, L"--monitor")));
    if (!hidden)
    {
        glfwShowWindow(window);
        OnFocus(window, glfwGetWindowAttrib(window, GLFW_FOCUSED));
    }
    if (wcsstr(command, L"--no-post"))
        postEnabled = false;
    const bool bench = benchmarkMode;
    int benchFrames = 0;
    double benchTotal = 0;
    auto last = std::chrono::steady_clock::now();
    while (running && !glfwWindowShouldClose(window))
    {
        if (iconified && !hidden)
        {
            glfwWaitEventsTimeout(.1);
            last = std::chrono::steady_clock::now();
            continue;
        }
        Profiler::BeginFrame();
        auto inputStart = Profiler::Clock::now();
        glfwPollEvents();
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
        if (regionalShot)
        {
            for (int village = 0; village < WorldLayout::VillageCount; ++village)
            {
                auto base = WorldLayout::Settlements()[village].center;
                SetPlayerPosition({base.x, base.y + 2});
                camera = PlayerPosition();
                draw();
                capture(("region-village-" + std::to_string(village + 1) + ".bmp").c_str());
            }
            SetPlayerPosition({-10, 31});
            camera = PlayerPosition();
            draw();
            capture("region-forest-road.bmp");
            SetPlayerPosition({0, 2});
            for (int i = 0; i < 600; ++i)
                villageWorld.Update(.1f);
            auto plaza = WorldLayout::Plaza(0);
            SetPlayerPosition({plaza.x, plaza.y});
            camera = PlayerPosition();
            draw();
            capture("region-couriers.bmp");
            std::ofstream report("regional-render-report.txt");
            report << "villages=5 residents=" << villageWorld.Citizens().size()
                   << " monsters=" << villageWorld.Monsters().size()
                   << " physical_rumor_receipts=" << villageWorld.Receipts().size()
                   << "\nGL=" << glGetString(GL_VERSION) << "\nRenderer=" << glGetString(GL_RENDERER) << "\n";
            running = false;
        }
        else if (storyShot)
            CaptureStory();
        else if (levelShot)
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
            SetPlayerPosition(ActorPosition(npcActors[WorldLayout::Merchant]));
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
            glfwSwapBuffers(window);
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
        Profiler::Add("journal_open", journalOpen ? 1 : 0);
        Profiler::Add("journey_stage", journey.Step());
        Profiler::Add("story_contracts_completed", journey.ContractsCompleted());
        Profiler::Add("story_family_rescued", villageWorld.RescuedCount());
        Profiler::Add("companion_wary", villageWorld.Familiar().wary ? 1 : 0);
        Profiler::Add("desaturation_radius_m", villageWorld.DesaturationRadius());
        Profiler::Add("journal_pages_unlocked", journey.PageCount());
        Profiler::Add("map_seed", firstWorld.Seed());
        Profiler::Add("scene_mode", firstLevelMode ? 1 : 0);
        Profiler::Add("actor_count",
                      double(firstLevelMode ? firstWorld.GetScene().Size() : tutorialScene.Size()));
        if (Monitor::Due())
            PublishMonitoring();
        Profiler::EndFrame();
    }
    tutorialTerrain.Clear();
    frozenTerrain.Clear();
    Monitor::Shutdown();
    Profiler::Shutdown();
    JournalArt::Shutdown();
    LevelView::Shutdown();
    RenderBatch::Shutdown();
    Visuals::Shutdown();
    return validationExit;
}
