#include "TerrainBatch.h"
#include "RenderBatch.h"
#include "Visuals.h"
#include "Profiler.h"
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
constexpr int LakeSegments = 96;
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
    return {(x - y) * 31.f, (x + y) * 15.5f};
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
        radius += std::sin(angle * 5) * .014f + std::cos(angle * 9) * .011f + std::sin(angle * 19) * .008f;
        return Point{lake.x + std::cos(angle) * lake.radiusX * radius,
                     lake.y + std::sin(angle) * lake.radiusY * radius};
    };
    auto edge = [&shoreline](int step, float radius) {
        const auto p = shoreline(step, radius);
        return Project(p[0], p[1]);
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
        Point a = edge(i, 1.024f + facet * .014f), b = edge(i + 1, 1.024f + facet * .014f);
        Point c = edge(i + 1, .977f), d = edge(i, .977f);
        Point e = edge(i, .955f), f = edge(i + 1, .955f);
        float light = .65f + facet * .51f + std::sin(i * Turn / LakeSegments) * .14f;
        const float moss = facet > .65f ? .065f : 0;
        AddQuad(shore, {a, b, c, d}, {.32f * light + moss, .38f * light + moss, .36f * light, 1});
        AddQuad(shore, {d, c, f, e}, {.14f * light, .22f * light, .26f * light, 1});
        if (facet > .54f)
        {
            Point lipA = edge(i, .990f), lipB = edge(i + 1, .990f);
            AddQuad(shore, {lipA, lipB, c, d}, {.44f * light, .49f * light, .41f * light, 1});
        }
        if (facet > .23f)
        {
            Point g = edge(i, .941f), h = edge(i + 1, .941f);
            Point j = edge(i + 1, .932f), k = edge(i, .932f);
            AddQuad(shore, {g, h, j, k}, {.43f, .83f, .76f, .24f + facet * .30f});
        }
        if (i % 7 == 0)
        {
            const auto p = edge(i, 1.032f);
            AddQuad(shore,
                    {{p[0] - 8, p[1]}, {p[0] - 3, p[1] - 4}, {p[0] + 7, p[1] - 3}, {p[0] + 4, p[1] + 3}},
                    {.29f, .34f, .30f, 1});
            for (int blade = 0; blade < 3; ++blade)
            {
                const float x = p[0] + blade * 4 - 5, y = p[1] + 1;
                const float tip = x - 5 + blade * 3, height = 10 + facet * 11 - blade * 2;
                AddQuad(shore, {{x, y}, {x + 2, y}, {tip + 1, y - height}, {tip, y - height}},
                        {.30f + facet * .19f, .42f + facet * .15f, .19f, 1});
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
        const float textureScale = tile.material % 4 == 1 ? .55f : tile.material % 4 == 0 ? .35f : .26f;
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
            Visuals::BeginWater(time);
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
