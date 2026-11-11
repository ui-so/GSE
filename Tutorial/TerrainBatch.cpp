#include "TerrainBatch.h"
#include "RenderBatch.h"
#include "Visuals.h"
#include "Profiler.h"
#include "Isometric.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
namespace TerrainBatch
{
namespace
{
constexpr int WaterMaterial = 3;
constexpr int ShoreMaterial = 8;
constexpr int LakeSegments = 64;
constexpr float Turn = 6.2831853f;
constexpr float WaterUvScale = .26f;
constexpr int TileCornerX[] = {0, 1, 1, 0};
constexpr int TileCornerY[] = {0, 0, 1, 1};
constexpr int NeighborX[] = {0, 1, 0, -1};
constexpr int NeighborY[] = {-1, 0, 1, 0};
using Point = std::array<float, 2>;
using Tint = std::array<float, 4>;

Point Project(float x, float y)
{
    const auto point = Isometric::Project(x, y);
    return {point.x, point.y};
}

void AddQuad(std::vector<RenderBatch::Vertex> &mesh, const Point (&corners)[4], Tint tint)
{
    for (int corner : {0, 1, 2, 0, 2, 3})
        mesh.push_back(
            {corners[corner][0], corners[corner][1], .75f, .5f, tint[0], tint[1], tint[2], tint[3]});
}

void AddShore(std::vector<RenderBatch::Vertex> &mesh, int x, int y, int side)
{
    int end = (side + 1) % 4;
    auto first = Project(float(x + TileCornerX[side]), float(y + TileCornerY[side]));
    auto last = Project(float(x + TileCornerX[end]), float(y + TileCornerY[end]));
    const auto inward = Project(-NeighborX[side] * .22f, -NeighborY[side] * .22f);
    const float light = side < 2 ? .82f : 1.13f;
    const unsigned seed = unsigned(x) * 0x6ef3720bu ^ unsigned(y) * 0x85ebca6bu ^ unsigned(side);
    for (int slab = 0; slab < 3; ++slab)
    {
        unsigned detail = ((seed >> (slab * 5)) ^ (seed >> 17)) & 15;
        float start = slab / 3.f, stop = (slab + 1) / 3.f;
        Point a{first[0] + (last[0] - first[0]) * start, first[1] + (last[1] - first[1]) * start};
        Point b{first[0] + (last[0] - first[0]) * stop, first[1] + (last[1] - first[1]) * stop};
        float depth = .65f + detail * .037f;
        Point c{b[0] + inward[0] * depth, b[1] + inward[1] * depth},
            d{a[0] + inward[0] * depth, a[1] + inward[1] * depth};
        float shade = light * (.80f + detail * .019f);
        AddQuad(mesh, {a, b, c, d}, {.28f * shade, .34f * shade, .32f * shade, 1});
        AddQuad(mesh,
                {d,
                 c,
                 {c[0] + inward[0] * .24f, c[1] + inward[1] * .24f},
                 {d[0] + inward[0] * .24f, d[1] + inward[1] * .24f}},
                {.10f * shade, .16f * shade, .20f * shade, 1});
        if (detail > 3)
        {
            Point foamA{d[0] + inward[0] * .39f, d[1] + inward[1] * .39f};
            Point foamB{c[0] + inward[0] * .39f, c[1] + inward[1] * .39f};
            AddQuad(mesh,
                    {foamA,
                     foamB,
                     {foamB[0] + inward[0] * .19f, foamB[1] + inward[1] * .19f},
                     {foamA[0] + inward[0] * .19f, foamA[1] + inward[1] * .19f}},
                    {.43f, .80f, .74f, .52f});
        }
    }
}

void AddLake(std::vector<RenderBatch::Vertex> &surface, std::vector<RenderBatch::Vertex> &shore,
             const Lake &lake)
{
    auto waterVertex = [](float x, float y) {
        auto p = Project(x, y);
        return RenderBatch::Vertex{p[0], p[1], x * WaterUvScale, y * WaterUvScale, .99f, .99f, .99f, 1};
    };
    auto shoreline = [&lake](int step, float radius) {
        float angle = Turn * step / LakeSegments;
        // Carve shallow coves inside the collision boundary, leaving visible rock shelves.
        radius -=
            .095f - std::sin(angle * 3) * .045f - std::cos(angle * 5) * .030f - std::sin(angle * 11) * .015f;
        return Point{lake.x + std::cos(angle) * lake.radiusX * radius,
                     lake.y + std::sin(angle) * lake.radiusY * radius};
    };
    auto edge = [&shoreline](int step, float radius) {
        const auto p = shoreline(step, radius);
        return Project(p[0], p[1]);
    };
    auto bank = [&lake](int step) {
        float angle = Turn * step / LakeSegments;
        return Project(lake.x + std::cos(angle) * lake.radiusX * 1.012f,
                       lake.y + std::sin(angle) * lake.radiusY * 1.012f);
    };
    for (int i = 0; i < LakeSegments; ++i)
    {
        surface.push_back(waterVertex(lake.x, lake.y));
        for (int point = i; point <= i + 1; ++point)
        {
            const auto p = shoreline(point, 1);
            surface.push_back(waterVertex(p[0], p[1]));
        }
        unsigned bits = unsigned(i) * 0x9e3779b9u;
        float facet = float((bits >> 25) & 15) / 15;
        Point a = bank(i), b = bank(i + 1);
        Point c = edge(i + 1, .966f), d = edge(i, .966f);
        const float cliff = 5 + facet * 7;
        Point footA{d[0], d[1] + cliff}, footB{c[0], c[1] + cliff};
        const float light = .76f + facet * .30f;
        // Slate wall and dark contact edge, with a mossy cap shared with the paving.
        AddQuad(shore, {a, b, footB, footA}, {.12f * light, .20f * light, .29f * light, 1});
        AddQuad(shore, {a, b, c, d}, {.30f * light, .405f * light, .43f * light, 1});
        if (facet > .50f)
        {
            Point lipA{a[0] + (d[0] - a[0]) * .30f, a[1] + (d[1] - a[1]) * .30f};
            Point lipB{b[0] + (c[0] - b[0]) * .30f, b[1] + (c[1] - b[1]) * .30f};
            AddQuad(shore, {a, b, lipB, lipA}, {.28f, .43f, .39f, 1});
        }
        Point joint{a[0] + (b[0] - a[0]) * .08f, a[1] + (b[1] - a[1]) * .08f};
        AddQuad(shore, {a, joint, {footA[0] + 1.5f, footA[1]}, footA}, {.075f, .125f, .20f, 1});
        if (i % 3 != 0)
        {
            Point foam = edge(i, .925f);
            const float length = 5 + facet * 9;
            AddQuad(shore,
                    {{foam[0] - length, foam[1] + 6},
                     {foam[0] + length, foam[1] + 6},
                     {foam[0] + length - 3, foam[1] + 7.5f},
                     {foam[0] - length + 2, foam[1] + 7.5f}},
                    {.43f, .84f, .72f, .30f});
        }
        if (i % 8 == 0)
        {
            const auto p = bank(i);
            AddQuad(shore,
                    {{p[0] - 10, p[1]}, {p[0] - 5, p[1] - 5}, {p[0] + 8, p[1] - 4}, {p[0] + 6, p[1] + 3}},
                    {.26f, .38f, .40f, 1});
            for (int blade = 0; blade < 3; ++blade)
            {
                const float x = p[0] + blade * 4 - 5, y = p[1] + 1;
                const float tip = x - 4 + blade * 2, height = 9 + facet * 7 - blade * 2;
                AddQuad(shore, {{x, y}, {x + 2, y}, {tip + 1, y - height}, {tip, y - height}},
                        {.25f, .43f, .39f, 1});
            }
        }
    }
}
} // namespace

void Map::Clear()
{
    for (auto &chunk : chunks_)
        RenderBatch::DestroyGeometry(chunk.mesh);
    chunks_.clear();
    lake_ = {};
}
void Map::Build(const std::vector<Tile> &tiles, const Lake *lake)
{
    Profiler::Scope scope("terrain_chunk_build");
    Clear();
    std::map<std::array<int, 3>, std::vector<RenderBatch::Vertex>> grouped;
    std::set<std::pair<int, int>> water;
    for (const auto &tile : tiles)
        if (tile.material == WaterMaterial)
            water.emplace(tile.x, tile.y);
    size_t shoreEdges = 0;
    for (auto &tile : tiles)
    {
        int chunkX = int(std::floor(tile.x / 16.f)), chunkY = int(std::floor(tile.y / 16.f));
        auto &vertices = grouped[{chunkX, chunkY, tile.material}];
        RenderBatch::Vertex quad[4];
        const float textureScale = tile.material % 4 == 1 ? .25f : tile.material % 4 == 0 ? .22f : .25f;
        for (int i = 0; i < 4; ++i)
        {
            float x = float(tile.x + TileCornerX[i]), y = float(tile.y + TileCornerY[i]);
            auto p = Project(x, y);
            quad[i] = {p[0],           p[1],           x * textureScale, y * textureScale,
                       tile.variation, tile.variation, tile.variation,   1};
        }
        for (int i : {0, 1, 2, 0, 2, 3})
            vertices.push_back(quad[i]);
        if (tile.material == WaterMaterial)
            for (int side = 0; side < 4; ++side)
                if (water.count({tile.x + NeighborX[side], tile.y + NeighborY[side]}) == 0)
                {
                    AddShore(grouped[{chunkX, chunkY, ShoreMaterial}], tile.x, tile.y, side);
                    ++shoreEdges;
                }
    }
    if (lake)
    {
        lake_ = *lake;
        int chunkX = int(std::floor(lake->x / 16.f)), chunkY = int(std::floor(lake->y / 16.f));
        AddLake(grouped[{chunkX, chunkY, WaterMaterial}], grouped[{chunkX, chunkY, ShoreMaterial}], *lake);
        shoreEdges += LakeSegments;
    }
    for (auto &entry : grouped)
    {
        float left = 1e9f, top = 1e9f, right = -1e9f, bottom = -1e9f;
        for (auto &v : entry.second)
        {
            left = std::min(left, v.x);
            top = std::min(top, v.y);
            right = std::max(right, v.x);
            bottom = std::max(bottom, v.y);
        }
        int material = entry.first[2];
        unsigned texture =
            material == ShoreMaterial ? Visuals::SolidTexture() : Visuals::MaterialTexture(material);
        chunks_.push_back(
            {material, RenderBatch::CreateGeometry(entry.second, texture), left, top, right, bottom});
    }
    Profiler::Add("terrain_shore_edges_built", double(shoreEdges));
}
void Map::Draw(int width, int height, float time) const
{
    Profiler::Scope scope("terrain_submit_cpu");
    Profiler::GpuScope gpu("terrain");
    for (int pass = 0; pass < 3; ++pass)
    {
        if (pass == 1)
            Visuals::BeginWater(time, lake_.x, lake_.y, lake_.radiusX, lake_.radiusY);
        for (auto &chunk : chunks_)
        {
            int chunkPass = chunk.material == ShoreMaterial ? 2 : chunk.material == WaterMaterial ? 1 : 0;
            if (chunkPass != pass)
                continue;
            Profiler::Add("terrain_chunks_tested");
            if (!RenderBatch::VisibleRect(chunk.left, chunk.top, chunk.right, chunk.bottom, width, height))
            {
                Profiler::Add("terrain_chunks_culled");
                continue;
            }
            Profiler::Add("terrain_chunks_visible");
            RenderBatch::Geometry(chunk.mesh);
        }
        if (pass == 1)
            Visuals::EndEffect();
    }
}
} // namespace TerrainBatch
