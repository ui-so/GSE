#pragma once
// Procedural material textures, baked sprite sheets and a scene-only post process.
namespace Visuals
{
bool Initialize();
void Shutdown();
void MaterialQuad(int material, const float *xy, float variation, float time);
void Sprite(float x, float y, int palette, int facing, int action, float phase);
void SoftShadow(float x, float y, float radius, float stretch, float opacity);
void PostProcess(int width, int height, float time, bool enabled);
bool PostAvailable();
void SaveAtlas(const char *path);
} // namespace Visuals
