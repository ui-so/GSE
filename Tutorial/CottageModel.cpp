#include "CottageModel.h"
#include "AssetCache.h"
#include "Isometric.h"
#include "Profiler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace CottageModel
{
namespace
{
struct Point
{
    float x, y, z;
};
struct Color
{
    float r, g, b;
};
using Mesh = std::vector<RenderBatch::ModelVertex>;
constexpr Color Ink{.075f, .13f, .20f}, Timber{.13f, .21f, .29f}, TimberLight{.24f, .33f, .38f};
constexpr Color Slate{.27f, .38f, .42f}, Moss{.32f, .45f, .42f}, Amber{.88f, .61f, .29f};
constexpr float HalfWidth = 1.30f, HalfDepth = 1.20f, Foundation = 11, Eaves = 64, Ridge = 101;
constexpr size_t MaximumVertices = 8192;
struct CachedMesh
{
    std::uint32_t count = 0;
    std::array<RenderBatch::ModelVertex, MaximumVertices> vertices{};
};
void Triangle(Mesh &mesh, Point a, Point b, Point c, Color color)
{
    for (auto point : {a, b, c})
    {
        const auto projected = Isometric::Project(point.x, point.y, point.z);
        mesh.push_back({projected.x, projected.y, color.r, color.g, color.b, 1, 0});
    }
}
void Quad(Mesh &mesh, Point a, Point b, Point c, Point d, Color color)
{
    Triangle(mesh, a, b, c, color);
    Triangle(mesh, a, c, d, color);
}
Point Facade(bool front, float u, float height)
{
    return front ? Point{-HalfWidth + 2 * HalfWidth * u, HalfDepth + .012f, height}
                 : Point{HalfWidth + .012f, HalfDepth - 2 * HalfDepth * u, height};
}
void Panel(Mesh &mesh, bool front, float left, float right, float bottom, float top, Color color)
{
    Quad(mesh, Facade(front, left, bottom), Facade(front, right, bottom), Facade(front, right, top),
         Facade(front, left, top), color);
}
void Beam(Mesh &mesh, bool front, float from, float to, float bottom, float top)
{
    constexpr float Thickness = .024f;
    Quad(mesh, Facade(front, from - Thickness, bottom), Facade(front, from + Thickness, bottom),
         Facade(front, to + Thickness, top), Facade(front, to - Thickness, top), Timber);
}
void Window(Mesh &mesh, bool front, float u)
{
    Panel(mesh, front, u - .018f, u + .16f, 29, 49, Ink);
    Panel(mesh, front, u, u + .14f, 32, 46, TimberLight);
    Panel(mesh, front, u + .014f, u + .126f, 34, 44, Amber);
    Panel(mesh, front, u + .066f, u + .08f, 34, 44, Timber);
    Panel(mesh, front, u + .014f, u + .126f, 38, 39.5f, Timber);
    Panel(mesh, front, u - .025f, u + .17f, 29, 32, Slate);
}
Point Roof(float side, float across, float along, float lift = 0)
{
    return {side * 1.52f * across, -1.43f + 2.86f * along, Ridge - 38 * across + lift};
}
Mesh Generate()
{
    Mesh mesh;
    // The first two faces are the foundation. Their edges are checked against the terrain axes.
    Quad(mesh, {-1.50f, 1.40f, 0}, {1.50f, 1.40f, 0}, {1.50f, 1.40f, Foundation}, {-1.50f, 1.40f, Foundation},
         Ink);
    Quad(mesh, {1.50f, 1.40f, 0}, {1.50f, -1.40f, 0}, {1.50f, -1.40f, Foundation}, {1.50f, 1.40f, Foundation},
         Timber);
    Quad(mesh, {-1.50f, -1.40f, Foundation}, {1.50f, -1.40f, Foundation}, {1.50f, 1.40f, Foundation},
         {-1.50f, 1.40f, Foundation}, Slate);
    for (bool front : {false, true})
    {
        Panel(mesh, front, 0, 1, Foundation, Eaves, Ink);
        for (int row = 0; row < 4; ++row)
            for (int column = -1; column < 5; ++column)
            {
                float left = std::max(0.f, (column + (row % 2) * .5f) / 4.f);
                float right = std::min(1.f, (column + 1 + (row % 2) * .5f) / 4.f);
                if (right <= left)
                    continue;
                float shade = .79f + ((column + row * 3 + 8) % 5) * .055f + (front ? .08f : 0);
                const Color stone{Slate.r * shade, Slate.g * shade, Slate.b * shade};
                Panel(mesh, front, left + .010f, right - .010f, Foundation + row * 12 + .8f,
                      Foundation + row * 12 + 11.6f, stone);
                Panel(mesh, front, left + .012f, right - .012f, Foundation + row * 12 + 10.7f,
                      Foundation + row * 12 + 11.6f, {.34f * shade, .45f * shade, .44f * shade});
            }
        for (float u : {.025f, .50f, .975f})
            Panel(mesh, front, u - .026f, u + .026f, Foundation, Eaves, Timber);
        Panel(mesh, front, 0, 1, 14, 17, Timber);
        Panel(mesh, front, 0, 1, 53, 56, Timber);
        Panel(mesh, front, 0, 1, 61, Eaves, Timber);
        Beam(mesh, front, .05f, .45f, 56, Eaves);
        Beam(mesh, front, .95f, .55f, 56, Eaves);
        Window(mesh, front, front ? .13f : .31f);
    }
    // Planked door and frame lie in the front wall plane.
    Panel(mesh, true, .56f, .85f, Foundation, 49, Ink);
    Panel(mesh, true, .59f, .82f, Foundation + 1, 46, Timber);
    for (int plank = 0; plank < 4; ++plank)
        Panel(mesh, true, .596f + plank * .057f, .643f + plank * .057f, Foundation + 2, 44,
              {.19f + plank * .008f, .28f, .33f});
    Panel(mesh, true, .59f, .82f, 20, 22, Ink);
    Panel(mesh, true, .59f, .82f, 37, 39, Ink);
    Panel(mesh, true, .76f, .78f, 27, 29, Amber);
    // Front gable, vertical king post and diagonal roof braces.
    Triangle(mesh, Facade(true, 0, Eaves), Facade(true, 1, Eaves), Facade(true, .5f, Ridge), Slate);
    Beam(mesh, true, .01f, .50f, Eaves, Ridge);
    Beam(mesh, true, .99f, .50f, Eaves, Ridge);
    Panel(mesh, true, .48f, .52f, Eaves, Ridge, Timber);
    for (float u : {.25f, .75f})
        Panel(mesh, true, u - .018f, u + .018f, Eaves, Ridge - 18, Timber);
    // Tile seams and bevels are generated on the roof planes, never in screen coordinates.
    for (float side : {-1.f, 1.f})
    {
        Quad(mesh, Roof(side, 0, 0), Roof(side, 1, 0), Roof(side, 1, 1), Roof(side, 0, 1), Ink);
        for (int row = 0; row < 4; ++row)
            for (int column = -1; column < 6; ++column)
            {
                float along = std::max(0.f, (column + (row % 2) * .5f) / 5.f);
                float end = std::min(1.f, (column + 1 + (row % 2) * .5f) / 5.f);
                if (end <= along)
                    continue;
                float a = row / 4.f + .009f, b = (row + 1) / 4.f - .009f;
                float shade = .80f + ((column * 3 + row + 16) % 5) * .047f;
                Quad(mesh, Roof(side, a, along + .004f), Roof(side, b, along + .004f),
                     Roof(side, b, end - .004f), Roof(side, a, end - .004f),
                     {.20f * shade, .34f * shade, .37f * shade});
                Quad(mesh, Roof(side, b - .024f, along + .004f, .25f), Roof(side, b, along + .004f, .25f),
                     Roof(side, b, end - .004f, .25f), Roof(side, b - .024f, end - .004f, .25f), Moss);
            }
    }
    // Stone chimney on the rear roof, using the same three world axes.
    constexpr float x0 = .43f, x1 = .80f, y0 = -.86f, y1 = -.48f, low = 87, high = 121;
    Quad(mesh, {x0, y1, low}, {x1, y1, low}, {x1, y1, high}, {x0, y1, high}, Slate);
    Quad(mesh, {x1, y1, low}, {x1, y0, low}, {x1, y0, high}, {x1, y1, high}, TimberLight);
    for (int row = 0; row < 5; ++row)
    {
        float z = low + row * 6;
        Quad(mesh, {x0, y1 + .005f, z}, {x1, y1 + .005f, z}, {x1, y1 + .005f, z + 1}, {x0, y1 + .005f, z + 1},
             Ink);
    }
    Quad(mesh, {x0 - .06f, y0 - .06f, high}, {x1 + .06f, y0 - .06f, high}, {x1 + .06f, y1 + .06f, high},
         {x0 - .06f, y1 + .06f, high}, Moss);
    Quad(mesh, {x0 + .05f, y0 + .05f, high + .2f}, {x1 - .05f, y0 + .05f, high + .2f},
         {x1 - .05f, y1 - .05f, high + .2f}, {x0 + .05f, y1 - .05f, high + .2f}, Ink);
    return mesh;
}
bool Valid(const CachedMesh &cache)
{
    if (cache.count < 18 || cache.count > MaximumVertices || cache.count % 3 != 0)
        return false;
    return std::all_of(cache.vertices.begin(), cache.vertices.begin() + cache.count, [](const auto &v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::abs(v.x) < 256 && std::abs(v.y) < 256 &&
               v.r >= 0 && v.r <= 1 && v.g >= 0 && v.g <= 1 && v.b >= 0 && v.b <= 1 && v.a == 1 &&
               v.animation == 0;
    });
}
bool HasEdge(const Mesh &mesh, Point from, Point to)
{
    const auto a = Isometric::Project(from.x, from.y, from.z);
    const auto b = Isometric::Project(to.x, to.y, to.z);
    auto same = [](const auto &vertex, Isometric::Point point) {
        return std::abs(vertex.x - point.x) < .0001f && std::abs(vertex.y - point.y) < .0001f;
    };
    for (size_t i = 0; i + 2 < mesh.size(); i += 3)
        for (size_t edge = 0; edge < 3; ++edge)
            if (same(mesh[i + edge], a) && same(mesh[i + (edge + 1) % 3], b))
                return true;
    return false;
}
} // namespace

std::vector<RenderBatch::ModelVertex> Load()
{
    Profiler::Scope scope("cottage_model_load");
    const auto key = "cottage-projected-v2-" + std::to_string(Isometric::Horizontal) + "-" +
                     std::to_string(Isometric::Depth);
    auto data = AssetCache::LoadOrCreate(key, sizeof(CachedMesh), []() {
        const auto mesh = Generate();
        CachedMesh cache;
        cache.count = static_cast<std::uint32_t>(mesh.size());
        if (mesh.size() > MaximumVertices)
            return AssetCache::Bytes{};
        std::copy(mesh.begin(), mesh.end(), cache.vertices.begin());
        AssetCache::Bytes bytes(sizeof(cache));
        std::memcpy(bytes.data(), &cache, sizeof(cache));
        return bytes;
    });
    CachedMesh cache;
    if (data.size() == sizeof(cache))
    {
        std::memcpy(&cache, data.data(), sizeof(cache));
        if (Valid(cache))
            return {cache.vertices.begin(), cache.vertices.begin() + cache.count};
    }
    Profiler::Event("cottage_model_cache_invalid", "regenerating");
    return Generate();
}
bool RunTests(std::string &report)
{
    const auto mesh = Generate();
    CachedMesh cache;
    if (mesh.size() < 18 || mesh.size() > MaximumVertices)
    {
        report += "FAIL: cottage mesh size.\n";
        return false;
    }
    cache.count = static_cast<std::uint32_t>(mesh.size());
    std::copy(mesh.begin(), mesh.end(), cache.vertices.begin());
    const auto &a = mesh[0], &b = mesh[1], &c = mesh[2], &side = mesh[7];
    const bool aligned = std::abs((b.y - a.y) / (b.x - a.x) - .5f) < .0001f &&
                         std::abs((side.y - b.y) / (side.x - b.x) + .5f) < .0001f &&
                         std::abs(b.x - c.x) < .0001f && std::abs(c.y - b.y + Foundation) < .0001f;
    const bool roofAligned =
        HasEdge(mesh, Roof(1, 1, 0), Roof(1, 1, 1)) && HasEdge(mesh, Roof(1, 0, 1), Roof(1, 0, 0));
    const bool windowAligned = HasEdge(mesh, Facade(true, .13f - .018f, 29), Facade(true, .13f + .16f, 29));
    if (!Valid(cache) || !aligned || !roofAligned || !windowAligned)
    {
        report += "FAIL: cottage foundation, verticals or terrain projection alignment.\n";
        return false;
    }
    report +=
        "PASS: cached cottage foundation, eaves, ridge, window sill and verticals use terrain 2:1 axes.\n";
    return true;
}
} // namespace CottageModel
