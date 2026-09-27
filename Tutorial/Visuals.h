#pragma once
// Procedural material textures, baked sprite sheets and a scene-only post process.
namespace Visuals
{
unsigned MaterialTexture(int material);
bool Initialize();
void Shutdown();
void BeginWater(float time);
void EndEffect();
void Flame(float x, float y, float scale, float time);
bool EffectsAvailable();
void MaterialQuad(int material, const float *xy, float variation, float time);
void Sprite(float x, float y, int palette, int facing, int action, float phase);
void SoftShadow(float x, float y, float radius, float stretch, float opacity);
void BeginScene(int width, int height, bool enabled);
void PostProcess(int width, int height, float time, bool enabled);
bool PostAvailable();
void SaveAtlas(const char *path);
} // namespace Visuals
