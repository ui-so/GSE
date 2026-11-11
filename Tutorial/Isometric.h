#pragma once

namespace Isometric
{
constexpr float Horizontal = 31.f;
constexpr float Depth = Horizontal * .5f;
struct Point
{
    float x, y;
};
constexpr Point Project(float x, float y, float elevation = 0)
{
    return {(x - y) * Horizontal, (x + y) * Depth - elevation};
}
} // namespace Isometric
