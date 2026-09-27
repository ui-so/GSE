#include "TerrainBatch.h"
#include "RenderBatch.h"
#include "Visuals.h"
#include "Profiler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
namespace TerrainBatch
{
void Map::Clear()
{
    for (auto &chunk : chunks_)
        RenderBatch::DestroyGeometry(chunk.mesh);
    chunks_.clear();
}
void Map::Build(const std::vector<Tile> &tiles)
{
    Profiler::Scope scope("terrain_chunk_build");
    Clear();
    std::map<std::array<int, 3>, std::vector<RenderBatch::Vertex>> grouped;
    for (auto &tile : tiles)
    {
        auto &vertices =
            grouped[{int(std::floor(tile.x / 16.f)), int(std::floor(tile.y / 16.f)), tile.material}];
        RenderBatch::Vertex quad[4];
        int dx[] = {0, 1, 1, 0}, dy[] = {0, 0, 1, 1};
        for (int i = 0; i < 4; ++i)
        {
            float x = float(tile.x + dx[i]), y = float(tile.y + dy[i]);
            quad[i] = {(x - y) * 31,   (x + y) * 15.5f, float(dx[i]),   float(dy[i]),
                       tile.variation, tile.variation,  tile.variation, 1};
        }
        for (int i : {0, 1, 2, 0, 2, 3})
            vertices.push_back(quad[i]);
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
        chunks_.push_back({material,
                           RenderBatch::CreateGeometry(entry.second, Visuals::MaterialTexture(material)),
                           left, top, right, bottom});
    }
}
void Map::Draw(int width, int height, float time) const
{
    Profiler::Scope scope("terrain_submit_cpu");
    Profiler::GpuScope gpu("terrain");
    for (int pass = 0; pass < 2; ++pass)
    {
        if (pass == 1)
            Visuals::BeginWater(time);
        for (auto &chunk : chunks_)
        {
            if ((chunk.material == 3) != (pass == 1))
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
