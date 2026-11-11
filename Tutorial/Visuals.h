#pragma once
#include <string>
// Procedural material textures, baked sprite sheets and a scene-only post process.
namespace Visuals
{
enum class Scenery
{
    Oldwood,
    Cottage
};
bool DrawScenery(Scenery kind, float x, float y, float scale, float opacity, bool frozen);
bool RunTests(std::string &report);
unsigned SolidTexture();
unsigned MaterialTexture(int material);
bool Initialize();
void Shutdown();
void BeginWater(float time, float x = 0, float y = 0, float radiusX = 0, float radiusY = 0);
void EndEffect();
void Flame(float x, float y, float scale, float time, bool frozen = false);
bool EffectsAvailable();
void MaterialQuad(int material, const float *xy, float variation, float time);
void Sprite(float x, float y, int palette, int facing, int action, float phase, bool frozen = false);
void SoftShadow(float x, float y, float radius, float stretch, float opacity);
void LightPool(float x, float y, float radius, float stretch, float r, float g, float b, float opacity);
void BeginScene(int width, int height, bool enabled);
void PostProcess(int width, int height, bool enabled);
bool PostAvailable();
void SaveAtlas(const char *path);
} // namespace Visuals
