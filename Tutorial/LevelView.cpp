#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include "DrawStats.h"
#include "RenderBatch.h"
#include "Profiler.h"
#include "TerrainBatch.h"
#include "LevelView.h"
#include "SceneModels.h"
#include "SceneRender.h"
#include "Visuals.h"
#include <algorithm>
#include <cmath>
#include <vector>
namespace LevelView
{
namespace
{
struct Point
{
    float x, y;
};
struct Color
{
    float r, g, b, a = 1;
};
Frame frame;
TextRenderer renderText = nullptr;
TerrainBatch::Map terrainMap;
GLuint minimapTexture = 0;
std::uint32_t minimapSeed = 0;
std::uint32_t terrainSeed = 0;

constexpr Color Ink{.79f, .83f, .77f}, Gold{.9f, .73f, .40f}, Mint{.40f, .78f, .67f};
void Rectangle(float x, float y, float w, float h, Color c)
{
    RenderBatch::Color4f(c.r, c.g, c.b, c.a);
    RenderBatch::Begin(GL_QUADS);
    RenderBatch::Vertex2f(x, y);
    RenderBatch::Vertex2f(x + w, y);
    RenderBatch::Vertex2f(x + w, y + h);
    RenderBatch::Vertex2f(x, y + h);
    RenderBatch::End();
}
void Text(float x, float y, const std::wstring &s, Color c = Ink)
{
    renderText(x, y, s, c.r, c.g, c.b);
}
Point Project(FirstLevel::Position p, float elevation = 0)
{
    float x = p.x - frame.cameraX, y = p.y - frame.cameraY;
    return {frame.width * .5f + (x - y) * 31, frame.height * .53f + (x + y) * 15.5f - elevation};
}
void Line(Point a, Point b, Color c, float width = 1)
{
    RenderBatch::Color4f(c.r, c.g, c.b, c.a);
    RenderBatch::LineWidth(width);
    RenderBatch::Begin(GL_LINES);
    RenderBatch::Vertex2f(a.x, a.y);
    RenderBatch::Vertex2f(b.x, b.y);
    RenderBatch::End();
    RenderBatch::LineWidth(1);
}
float Distance(FirstLevel::Position a, FirstLevel::Position b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}
void DrawTerrain(const FirstLevel::World &world)
{
    if (terrainMap.Empty() || terrainSeed != world.Seed())
    {
        terrainSeed = world.Seed();
        std::vector<TerrainBatch::Tile> tiles;
        for (int y = 0; y < FirstLevel::World::MapSize; ++y)
            for (int x = 0; x < FirstLevel::World::MapSize; ++x)
            {
                auto tile = world.Tile(x, y);
                int material = tile == FirstLevel::Terrain::Water   ? 3
                               : tile == FirstLevel::Terrain::Stone ? 1
                               : tile == FirstLevel::Terrain::Dirt  ? 2
                                                                    : 0;
                tiles.push_back(
                    {x - FirstLevel::World::Origin, y - FirstLevel::World::Origin, material, .92f});
            }
        terrainMap.Build(tiles);
    }
    RenderBatch::PushMatrix();
    RenderBatch::Translatef(frame.width * .5f - (frame.cameraX - frame.cameraY) * 31,
                            frame.height * .53f - (frame.cameraX + frame.cameraY) * 15.5f, 0);
    terrainMap.Draw(frame.width, frame.height, frame.time);
    RenderBatch::PopMatrix();
}
void DrawScene(const FirstLevel::World &world)
{
    Profiler::Scope sceneScope("world_build_submit");
    for (auto actor : world.GetScene().RenderQueue(Scene::Layer::Ground))
        if (actor->GetKind() == Scene::Kind::Terrain)
        {
            auto position = actor->GetWorldPosition();
            auto pivot = Project({0, 0});
            RenderBatch::PushMatrix();
            RenderBatch::Translatef((position.x - position.y) * 31,
                                    (position.x + position.y) * 15.5f - actor->GetWorldElevation(), 0);
            SceneRender::Push(*actor, pivot.x, pivot.y);
            DrawTerrain(world);
            SceneRender::Pop();
            RenderBatch::PopMatrix();
        }
    Scene::View view{frame.cameraX, frame.cameraY, frame.width, frame.height};
    auto objects = world.GetScene().RenderQueue(Scene::Layer::World, &view);
    {
        Profiler::Scope scope("shadows_cpu");
        Profiler::GpuScope gpu("shadows");
        for (auto actor : objects)
        {
            Point p = Project(actor->GetWorldPosition(), actor->GetWorldElevation());
            if (p.x < -100 || p.x > frame.width + 100 || p.y < -60 || p.y > frame.height + 140)
                continue;
            if (actor->GetKind() == Scene::Kind::Flame || actor->GetKind() == Scene::Kind::Attack)
                continue;
            SceneRender::Push(*actor, p.x, p.y);
            Visuals::SoftShadow(p.x + 10, p.y + 4, actor->GetKind() == Scene::Kind::Tree ? 32.f : 24.f, .4f,
                                .8f);
            SceneRender::Pop();
        }
        RenderBatch::Flush();
    }
    Profiler::Scope objectsScope("objects_cpu");
    Profiler::GpuScope objectsGpu("objects");
    for (auto actor : objects)
    {
        Point p = Project(actor->GetWorldPosition(), actor->GetWorldElevation());
        if (p.x < -100 || p.x > frame.width + 100 || p.y < -60 || p.y > frame.height + 140)
            continue;
        SceneRender::Push(*actor, p.x, p.y);
        switch (actor->GetKind())
        {
        default:
            break;
        case Scene::Kind::Tree: {
            bool occluding =
                Distance(actor->GetWorldPosition(), world.GetPosition(world.Player().actor)) < 2.3f &&
                actor->GetWorldPosition().x + actor->GetWorldPosition().y >
                    world.GetPosition(world.Player().actor).x + world.GetPosition(world.Player().actor).y;
            if (occluding)
            {
                GLubyte mask[128];
                for (int i = 0; i < 128; i++)
                    mask[i] = (i / 4) % 2 ? 0xAA : 0x55;
                RenderBatch::Enable(GL_POLYGON_STIPPLE);
                glPolygonStipple(mask);
            }
            SceneModels::Draw(SceneModels::Kind::Tree, p.x, p.y, .85f);
            RenderBatch::Disable(GL_POLYGON_STIPPLE);
            break;
        }
        case Scene::Kind::Rock:
            SceneModels::Draw(SceneModels::Kind::Rock, p.x, p.y, .85f);
            break;
        case Scene::Kind::Enemy: {
            auto &enemy = world.Enemies()[actor->GetDataIndex()];
            SceneModels::Draw(enemy.kind == 0 ? SceneModels::Kind::Boar : SceneModels::Kind::Wraith, p.x, p.y,
                              1, frame.time, enemy.hitFlash > 0);
            int maximum = actor->GetDataIndex() % 3 == 2 ? 36 : 28;
            Rectangle(p.x - 20, p.y - 72, 40, 5, {.09f, .05f, .045f});
            Rectangle(p.x - 20, p.y - 72, 40 * std::clamp(float(enemy.health) / maximum, 0.f, 1.f), 5,
                      {.70f, .26f, .19f});
            if (enemy.hitFlash > 0)
                Text(p.x - 15, p.y - 82, L"명중", Gold);
            break;
        }
        case Scene::Kind::Loot: {
            auto &drop = world.Drops()[actor->GetDataIndex()];
            SceneModels::Draw(drop.kind == FirstLevel::ItemKind::Potion ? SceneModels::Kind::Potion
                                                                        : SceneModels::Kind::Coin,
                              p.x, p.y - std::sin(frame.time * 3) * 2);
            if (Distance(world.GetPosition(drop.actor), world.GetPosition(world.Player().actor)) < 1.5f)
                Text(p.x - 28, p.y - 29, L"E 획득", Gold);
            break;
        }
        case Scene::Kind::Brazier:
            SceneModels::Draw(SceneModels::Kind::Brazier, p.x, p.y);
            break;
        case Scene::Kind::Flame:
            Visuals::Flame(p.x, p.y, 1, frame.time);
            break;
        case Scene::Kind::Player: {
            const auto &hero = world.Player();
            float x = hero.facing.x - hero.facing.y, y = hero.facing.x + hero.facing.y;
            int facing = std::abs(x) > std::abs(y) ? (x < 0 ? 1 : 2) : (y < 0 ? 3 : 0);
            if (hero.invulnerability <= 0 || int(frame.time * 14) % 2 == 0)
                Visuals::Sprite(p.x, p.y, 0, facing,
                                hero.attackAnimation > 0 ? 3
                                : frame.moving           ? 1
                                                         : 0,
                                frame.time * (hero.attackAnimation > 0 ? 2 : 1.5f));
            break;
        }
        case Scene::Kind::Attack: {
            const auto &hero = world.Player();
            if (hero.attackAnimation > 0)
            {
                float a = std::atan2(hero.facing.y, hero.facing.x);
                RenderBatch::Color4f(.91f, .81f, .58f, hero.attackAnimation / .28f);
                RenderBatch::LineWidth(3);
                RenderBatch::Begin(GL_LINE_STRIP);
                for (int j = 0; j <= 20; j++)
                {
                    float angle = a - 1.1f + j * .11f;
                    Point edge = Project({actor->GetWorldPosition().x + std::cos(angle) * 1.6f,
                                          actor->GetWorldPosition().y + std::sin(angle) * 1.6f},
                                         12 + actor->GetWorldElevation());
                    RenderBatch::Vertex2f(edge.x, edge.y);
                }
                RenderBatch::End();
                RenderBatch::LineWidth(1);
            }
            break;
        }
        }
        SceneRender::Pop();
    }
    RenderBatch::Flush();
}
void Minimap(const FirstLevel::World &world)
{
    Profiler::Scope scope("minimap_cpu");
    float x = frame.width - 178.f, y = 222;
    Rectangle(x - 10, y - 27, 156, 183, {.025f, .05f, .045f, .93f});
    Text(x, y - 8, L"사냥터 지도", Gold);
    if (!minimapTexture || minimapSeed != world.Seed())
    {
        Profiler::Scope buildScope("minimap_texture_build");
        RenderBatch::Flush();
        if (!minimapTexture)
            glGenTextures(1, &minimapTexture);
        minimapSeed = world.Seed();
        std::vector<unsigned char> pixels(64 * 64 * 4, 255);
        for (int cy = 0; cy < 64; ++cy)
            for (int cx = 0; cx < 64; ++cx)
            {
                auto tile = world.Tile(cx, cy);
                Color color = tile == FirstLevel::Terrain::Water ? Color{.11f, .29f, .34f}
                              : world.IsWalkableCell(cx, cy)     ? Color{.28f, .35f, .25f}
                                                                 : Color{.07f, .14f, .10f};
                auto index = size_t(cy * 64 + cx) * 4;
                pixels[index] = static_cast<unsigned char>(color.r * 255);
                pixels[index + 1] = static_cast<unsigned char>(color.g * 255);
                pixels[index + 2] = static_cast<unsigned char>(color.b * 255);
            }
        glBindTexture(GL_TEXTURE_2D, minimapTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    }
    RenderBatch::Enable(GL_TEXTURE_2D);
    RenderBatch::BindTexture(GL_TEXTURE_2D, minimapTexture);
    RenderBatch::Color4f(1, 1, 1, 1);
    RenderBatch::Begin(GL_QUADS);
    RenderBatch::TexCoord2f(0, 0);
    RenderBatch::Vertex2f(x, y);
    RenderBatch::TexCoord2f(1, 0);
    RenderBatch::Vertex2f(x + 128, y);
    RenderBatch::TexCoord2f(1, 1);
    RenderBatch::Vertex2f(x + 128, y + 128);
    RenderBatch::TexCoord2f(0, 1);
    RenderBatch::Vertex2f(x, y + 128);
    RenderBatch::End();
    RenderBatch::Disable(GL_TEXTURE_2D);
    for (auto &enemy : world.Enemies())
        if (enemy.alive && world.IsActive(enemy.actor))
            Rectangle(x + (world.GetPosition(enemy.actor).x + 32) * 2 - 1,
                      y + (world.GetPosition(enemy.actor).y + 32) * 2 - 1, 3, 3, {.9f, .36f, .24f});
    Rectangle(x + (world.GetPosition(world.Player().actor).x + 32) * 2 - 2,
              y + (world.GetPosition(world.Player().actor).y + 32) * 2 - 2, 4, 4, Mint);
    Text(x, y + 147, L"나", Mint);
    Text(x + 60, y + 147, L"적", {.9f, .36f, .24f});
}
void DrawHud(const FirstLevel::World &world)
{
    const auto &hero = world.Player();
    Rectangle(22, 24, 380, 153, {.025f, .045f, .04f, .94f});
    Text(40, 51, L"첫 레벨 · 고요한 사냥터", Gold);
    Text(40, 78,
         L"성장 레벨 " + std::to_wstring(hero.level) + L"  |  체력 " + std::to_wstring(hero.health) + L" / " +
             std::to_wstring(hero.maximumHealth));
    Rectangle(40, 91, 340, 9, {.13f, .09f, .07f});
    Rectangle(40, 91, 340 * float(hero.health) / hero.maximumHealth, 9, {.50f, .68f, .43f});
    Text(40, 123,
         L"경험치 " + std::to_wstring(hero.experience) + L" / " +
             std::to_wstring(world.NextLevelExperience()) + L"  |  처치 " + std::to_wstring(hero.kills));
    Rectangle(40, 132, 340, 5, {.10f, .15f, .15f});
    Rectangle(40, 132, 340 * float(hero.experience) / world.NextLevelExperience(), 5, Mint);
    Text(40, 160,
         L"공격 " + std::to_wstring(hero.attack) + L"  방어 " + std::to_wstring(hero.defense) + L"  회복약 " +
             std::to_wstring(hero.potions) + L"  동전 " + std::to_wstring(hero.coins));
    float right = frame.width - 334.f;
    Rectangle(right, 24, 310, 176, {.025f, .045f, .04f, .94f});
    Text(right + 18, 51, L"이번 레벨의 목표", Gold);
    Text(right + 18, 84,
         (hero.totalExperience >= 60 ? L"완료 · " : L"진행 · ") + std::wstring(L"경험치 60 획득"),
         hero.totalExperience >= 60 ? Mint : Ink);
    Text(right + 18, 113, (hero.itemsPicked > 0 ? L"완료 · " : L"진행 · ") + std::wstring(L"아이템 획득"),
         hero.itemsPicked > 0 ? Mint : Ink);
    Text(right + 18, 142,
         (hero.spentPoints > 0 ? L"완료 · " : L"진행 · ") + std::wstring(L"능력치 포인트 사용"),
         hero.spentPoints > 0 ? Mint : Ink);
    Text(right + 18, 178,
         world.IsComplete() ? L"첫 성장 완료 · 자유 사냥 가능" : L"적을 처치하고 성장해 보세요",
         world.IsComplete() ? Mint : Gold);
    Minimap(world);
    if (hero.statPoints > 0)
    {
        float y = frame.height - 223.f;
        Rectangle(28, y, frame.width - 56.f, 104, {.025f, .065f, .055f, .96f});
        Text(48, y + 27, L"능력치 포인트 " + std::to_wstring(hero.statPoints) + L" · 숫자 키로 선택", Gold);
        Text(48, y + 58, L"1  공격력 +3          2  최대 체력 +20          3  방어력 +1");
        Text(48, y + 86,
             L"선택 즉시 적용됩니다. 레벨 상승 시 기본 공격력 +1, 최대 체력 +10도 자동 적용됩니다.");
    }
    if (world.NoticeTime() > 0)
    {
        float y = hero.statPoints > 0 ? frame.height - 247.f : frame.height - 127.f;
        Text(40, y, world.Notice(), Gold);
    }
    Rectangle(22, frame.height - 99.f, frame.width - 44.f, 77, {.025f, .045f, .04f, .96f});
    Text(40, frame.height - 72.f, L"WASD 이동   Shift 달리기   Space 공격   E 획득   Q 회복약");
    Text(40, frame.height - 43.f, L"1·2·3 능력치   Esc 일시정지   F1 튜토리얼 전환   F2 후처리",
         {.54f, .67f, .60f});
    if (world.IsDead())
    {
        Rectangle(0, 0, float(frame.width), float(frame.height), {.04f, .025f, .025f, .76f});
        Text(frame.width * .5f - 105, frame.height * .5f - 25, L"잠시 쓰러졌을 뿐", Gold);
        Text(frame.width * .5f - 235, frame.height * .5f + 12,
             L"R · 야영지에서 회복  |  경험치와 전리품은 유지됩니다");
    }
    if (frame.paused)
    {
        Rectangle(0, 0, float(frame.width), float(frame.height), {0, 0, 0, .73f});
        Text(frame.width * .5f - 140, frame.height * .5f - 45, L"사냥터에서 잠시 쉬기", Gold);
        Text(frame.width * .5f - 200, frame.height * .5f - 8, L"Esc 계속  ·  R 새 랜덤 맵  ·  Q 종료");
        Text(frame.width * .5f - 200, frame.height * .5f + 22, L"새 맵을 시작하면 이번 진행이 초기화됩니다.");
        Text(frame.width * .5f - 200, frame.height * .5f + 52, L"맵 시드: " + std::to_wstring(world.Seed()));
    }
}
} // namespace
void Draw(const FirstLevel::World &world, const Frame &current, TextRenderer text)
{
    frame = current;
    renderText = text;
    DrawScene(world);
    Visuals::PostProcess(frame.width, frame.height, frame.time, frame.postEnabled);
    Profiler::Scope hudScope("ui_cpu");
    Profiler::GpuScope hudGpu("ui");
    for (auto actor : world.GetScene().RenderQueue(Scene::Layer::Interface))
        if (actor->GetKind() == Scene::Kind::Hud)
        {
            auto p = actor->GetWorldPosition();
            RenderBatch::PushMatrix();
            RenderBatch::Translatef(p.x, p.y - actor->GetWorldElevation(), 0);
            SceneRender::Push(*actor, 0, 0, true);
            DrawHud(world);
            SceneRender::Pop();
            RenderBatch::PopMatrix();
        }
    RenderBatch::Flush();
}
void Shutdown()
{
    terrainMap.Clear();
    if (minimapTexture)
        glDeleteTextures(1, &minimapTexture);
    minimapTexture = 0;
}
} // namespace LevelView
