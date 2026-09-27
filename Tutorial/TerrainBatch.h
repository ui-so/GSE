#pragma once
#include <vector>
namespace TerrainBatch
{
struct Tile
{
    int x, y, material;
    float variation = 1;
};
struct Chunk
{
    int material;
    unsigned mesh;
    float left, top, right, bottom;
};
class Map
{
  public:
    void Build(const std::vector<Tile> &tiles);
    void Draw(int width, int height, float time) const;
    void Clear();
    bool Empty() const
    {
        return chunks_.empty();
    }

  private:
    std::vector<Chunk> chunks_;
};
} // namespace TerrainBatch
