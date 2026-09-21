#pragma once
#include "FirstLevel.h"
namespace LevelView
{
struct Frame
{
    int width = 1280, height = 800;
    float time = 0, cameraX = 0, cameraY = 0;
    bool paused = false, postEnabled = true, moving = false;
};
using TextRenderer = void (*)(float, float, const std::wstring &, float, float, float);
void Draw(const FirstLevel::World &world, const Frame &frame, TextRenderer text);
void Shutdown();
} // namespace LevelView
