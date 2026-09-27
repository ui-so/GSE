#pragma once
#include <string>
#include <vector>
namespace RenderBatch
{
struct Vertex
{
    float x, y, u = 0, v = 0, r = 1, g = 1, b = 1, a = 1;
};
struct ModelVertex
{
    float x, y, r, g, b, a, animation;
};
bool Initialize();
void Shutdown();
void SetSolidTexture(unsigned texture, float u, float v);
void Flush();
void Begin(unsigned mode);
void End();
void Vertex2f(float x, float y);
void TexCoord2f(float u, float v);
void Color4f(float r, float g, float b, float a);
void Color3f(float r, float g, float b);
void Enable(unsigned state);
void Disable(unsigned state);
void BindTexture(unsigned target, unsigned texture);
void MatrixMode(unsigned mode);
void LoadIdentity();
void PushMatrix();
void PopMatrix();
void Translatef(float x, float y, float z);
void Scalef(float x, float y, float z);
void MultMatrixf(const float *matrix);
void LineWidth(float width);
void NewList(unsigned list, unsigned mode);
void EndList();
void CallList(unsigned list);
unsigned CreateGeometry(const std::vector<Vertex> &vertices, unsigned texture);
void DestroyGeometry(unsigned id);
void Geometry(unsigned id);
bool VisibleRect(float left, float top, float right, float bottom, int width, int height);
unsigned CreateMesh(const std::vector<ModelVertex> &vertices);
void Mesh(unsigned mesh, float x, float y, float scale, float phase, bool flash);
void Text(void *dc, float x, float baseline, const std::wstring &text);
bool RunTests(std::string &report);
class Frame
{
  public:
    Frame();
    ~Frame();
};
} // namespace RenderBatch
