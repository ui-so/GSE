#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include "RenderBatch.h"
#include "DrawStats.h"
#include "Profiler.h"
#include "Actor.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>
namespace RenderBatch
{
namespace
{
constexpr GLenum ArrayBuffer = 0x8892, StreamDraw = 0x88E0, StaticDraw = 0x88E4;
using GenBuffers = void(APIENTRY *)(GLsizei, GLuint *);
using DeleteBuffers = void(APIENTRY *)(GLsizei, const GLuint *);
using BindBuffer = void(APIENTRY *)(GLenum, GLuint);
using BufferData = void(APIENTRY *)(GLenum, std::ptrdiff_t, const void *, GLenum);
using DrawInstanced = void(APIENTRY *)(GLenum, GLint, GLsizei, GLsizei);
using Divisor = void(APIENTRY *)(GLuint, GLuint);
using AttribPointer = void(APIENTRY *)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
using AttribEnable = void(APIENTRY *)(GLuint);
using CreateShader = GLuint(APIENTRY *)(GLenum);
using ShaderSource = void(APIENTRY *)(GLuint, GLsizei, const char *const *, const GLint *);
using ShaderAction = void(APIENTRY *)(GLuint);
using GetStatus = void(APIENTRY *)(GLuint, GLenum, GLint *);
using CreateProgram = GLuint(APIENTRY *)();
using Attach = void(APIENTRY *)(GLuint, GLuint);
using BindAttrib = void(APIENTRY *)(GLuint, GLuint, const char *);
GenBuffers genBuffers;
DeleteBuffers deleteBuffers;
BindBuffer bindBuffer;
BufferData bufferData;
DrawInstanced drawInstanced;
Divisor divisor;
AttribPointer attribPointer;
AttribEnable enableAttrib, disableAttrib;
ShaderAction useProgram, deleteShader, deleteProgram;
GLuint stream = 0, instanceBuffer = 0, meshProgram = 0;
Scene::Matrix matrix;
std::vector<Scene::Matrix> stack;
GLenum matrixMode = GL_MODELVIEW, primitiveMode = GL_TRIANGLES;
bool textured = false;
GLuint texture = 0, solidTexture = 0;
float solidU = 0, solidV = 0;
Vertex current{};
std::vector<Vertex> primitive, pending;
GLenum pendingMode = GL_TRIANGLES;
GLuint pendingTexture = 0;
struct Command
{
    GLenum mode;
    GLuint texture;
    std::vector<Vertex> vertices;
};
GLuint compiling = 0;
Scene::Matrix savedCompileMatrix;
float lineWidth = 1;
std::unordered_map<GLuint, unsigned> listMeshes;
std::unordered_map<unsigned, bool> enabledStates;
std::unordered_map<GLuint, std::vector<Command>> lists;
struct MeshData
{
    GLuint buffer;
    GLsizei count;
};
std::vector<MeshData> meshes;
struct GeometryData
{
    GLuint buffer, texture;
    GLsizei count;
};
std::unordered_map<unsigned, GeometryData> geometries;
unsigned nextGeometry = 1;
struct Instance
{
    float a, b, c, d, x, y, phase, flash;
};
std::vector<Instance> instances;
unsigned pendingMesh = 0;
struct Glyph
{
    unsigned texture = 0;
    float u = 0, v = 0, uw = 0, vh = 0;
    int width = 0, height = 0, left = 0, top = 0, advance = 0;
};
std::unordered_map<wchar_t, Glyph> glyphs;
std::vector<GLuint> fontPages;
int fontX = 0, fontY = 0, fontRow = 0;
constexpr int FontSize = 1024;
void FlushGeometry()
{
    if (pending.empty())
        return;
    Profiler::Scope timing("render_batch_upload_submit");
    if (pendingTexture)
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, pendingTexture);
    }
    else
        glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    bindBuffer(ArrayBuffer, stream);
    bufferData(ArrayBuffer, std::ptrdiff_t(pending.size() * sizeof(Vertex)), pending.data(), StreamDraw);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(Vertex), nullptr);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, u)));
    glColorPointer(4, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, r)));
    Profiler::Add("texture_bind_calls", pendingTexture ? 1 : 0);
    Profiler::Add("dynamic_batches");
    glDrawArrays(pendingMode, 0, GLsizei(pending.size()));
    DrawStats::RecordDraw(pending.size(), 1);
    Profiler::Add("dynamic_upload_bytes", double(pending.size() * sizeof(Vertex)));
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    bindBuffer(ArrayBuffer, 0);
    glMatrixMode(matrixMode);
    pending.clear();
}
void FlushInstances()
{
    if (instances.empty())
        return;
    Profiler::Scope timing("render_instance_upload_submit");
    auto &mesh = meshes[pendingMesh - 1];
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    useProgram(meshProgram);
    bindBuffer(ArrayBuffer, mesh.buffer);
    for (GLuint i = 0; i < 5; ++i)
        enableAttrib(i);
    attribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex), nullptr);
    attribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                  reinterpret_cast<void *>(offsetof(ModelVertex, r)));
    attribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                  reinterpret_cast<void *>(offsetof(ModelVertex, animation)));
    bindBuffer(ArrayBuffer, instanceBuffer);
    bufferData(ArrayBuffer, std::ptrdiff_t(instances.size() * sizeof(Instance)), instances.data(),
               StreamDraw);
    attribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Instance), nullptr);
    attribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                  reinterpret_cast<void *>(offsetof(Instance, x)));
    divisor(3, 1);
    divisor(4, 1);
    Profiler::Add("program_bind_calls", 2);
    drawInstanced(GL_TRIANGLES, 0, mesh.count, GLsizei(instances.size()));
    DrawStats::RecordDraw(size_t(mesh.count), instances.size());
    Profiler::Add("instanced_draw_calls");
    Profiler::Add("model_instances", double(instances.size()));
    Profiler::Add("dynamic_upload_bytes", double(instances.size() * sizeof(Instance)));
    divisor(3, 0);
    divisor(4, 0);
    for (GLuint i = 0; i < 5; ++i)
        disableAttrib(i);
    bindBuffer(ArrayBuffer, 0);
    useProgram(0);
    glMatrixMode(matrixMode);
    instances.clear();
}
void Submit(GLenum mode, GLuint tex, const std::vector<Vertex> &vertices, bool transform)
{
    if (vertices.empty())
        return;
    if (compiling)
    {
        lists[compiling].push_back({mode, tex, vertices});
        return;
    }
    FlushInstances();
    bool solid = tex == 0 && solidTexture != 0;
    if (solid)
        tex = solidTexture;
    if (!pending.empty() &&
        (pendingMode != mode || pendingTexture != tex || pending.size() + vertices.size() > 65536))
    {
        Profiler::Add(pending.size() + vertices.size() > 65536 ? "batch_break_capacity"
                                                               : "batch_break_texture");
        FlushGeometry();
    }
    pendingMode = mode;
    pendingTexture = tex;
    for (auto v : vertices)
    {
        if (solid)
        {
            v.u = solidU;
            v.v = solidV;
        }
        if (transform)
        {
            auto p = matrix.Apply({v.x, v.y});
            v.x = p.x;
            v.y = p.y;
        }
        pending.push_back(v);
    }
    Profiler::Add("submitted_batches");
}
std::vector<Vertex> Convert(const std::vector<Vertex> &input, GLenum mode)
{
    std::vector<Vertex> out;
    out.reserve(input.size() * 3);
    auto triangle = [&](size_t a, size_t b, size_t c) {
        out.push_back(input[a]);
        out.push_back(input[b]);
        out.push_back(input[c]);
    };
    if (mode == GL_QUADS)
    {
        for (size_t i = 0; i + 3 < input.size(); i += 4)
        {
            triangle(i, i + 1, i + 2);
            triangle(i, i + 2, i + 3);
        }
    }
    else if (mode == GL_TRIANGLE_FAN || mode == GL_POLYGON)
    {
        for (size_t i = 1; i + 1 < input.size(); ++i)
            triangle(0, i, i + 1);
    }
    else if (mode == GL_LINE_STRIP || mode == GL_LINE_LOOP)
    {
        for (size_t i = 1; i < input.size(); ++i)
        {
            out.push_back(input[i - 1]);
            out.push_back(input[i]);
        }
        if (mode == GL_LINE_LOOP && input.size() > 1)
        {
            out.push_back(input.back());
            out.push_back(input.front());
        }
    }
    else
        out = input;
    if (mode == GL_LINES || mode == GL_LINE_STRIP || mode == GL_LINE_LOOP)
    {
        auto lines = out;
        out.clear();
        for (size_t i = 0; i + 1 < lines.size(); i += 2)
        {
            auto a = lines[i], b = lines[i + 1];
            float dx = b.x - a.x, dy = b.y - a.y;
            float distance = std::sqrt(dx * dx + dy * dy);
            if (distance < .0001f)
                continue;
            float nx = -dy / distance * lineWidth * .5f, ny = dx / distance * lineWidth * .5f;
            auto c = a, d = b;
            a.x += nx;
            a.y += ny;
            b.x += nx;
            b.y += ny;
            c.x -= nx;
            c.y -= ny;
            d.x -= nx;
            d.y -= ny;
            out.insert(out.end(), {a, b, d, a, d, c});
        }
    }
    return out;
}
bool MakeProgram()
{
    auto createShader = reinterpret_cast<CreateShader>(wglGetProcAddress("glCreateShader"));
    auto source = reinterpret_cast<ShaderSource>(wglGetProcAddress("glShaderSource"));
    auto compile = reinterpret_cast<ShaderAction>(wglGetProcAddress("glCompileShader"));
    auto status = reinterpret_cast<GetStatus>(wglGetProcAddress("glGetShaderiv"));
    auto createProgram = reinterpret_cast<CreateProgram>(wglGetProcAddress("glCreateProgram"));
    auto attach = reinterpret_cast<Attach>(wglGetProcAddress("glAttachShader"));
    auto link = reinterpret_cast<ShaderAction>(wglGetProcAddress("glLinkProgram"));
    auto programStatus = reinterpret_cast<GetStatus>(wglGetProcAddress("glGetProgramiv"));
    auto bindAttrib = reinterpret_cast<BindAttrib>(wglGetProcAddress("glBindAttribLocation"));
    if (!createShader || !source || !compile || !status || !createProgram || !attach || !link ||
        !programStatus || !bindAttrib)
        return false;
    const char *vs = R"GLSL(#version 120
attribute vec2 position;attribute vec4 color;attribute float animation;
attribute vec4 basis;attribute vec4 offset;varying vec4 tint;
void main(){vec2 p=position;if(animation>0.0)p.y+=sin(offset.z*9.0+animation*3.14159)*3.0;
vec2 q=vec2(basis.x*p.x+basis.z*p.y,basis.y*p.x+basis.w*p.y)+offset.xy;
gl_Position=gl_ProjectionMatrix*vec4(q,0,1);tint=mix(color,vec4(1,.8,.6,color.a),offset.w);})GLSL";
    const char *fs = "#version 120\nvarying vec4 tint;void main(){gl_FragColor=tint;}";
    GLuint shaders[2] = {createShader(0x8B31), createShader(0x8B30)};
    bool okay = true;
    for (int i = 0; i < 2; ++i)
    {
        const char *text = i == 0 ? vs : fs;
        source(shaders[i], 1, &text, nullptr);
        compile(shaders[i]);
        GLint success = 0;
        status(shaders[i], 0x8B81, &success);
        okay = okay && success != 0;
    }
    meshProgram = createProgram();
    for (auto s : shaders)
        attach(meshProgram, s);
    const char *names[] = {"position", "color", "animation", "basis", "offset"};
    for (GLuint i = 0; i < 5; ++i)
        bindAttrib(meshProgram, i, names[i]);
    link(meshProgram);
    GLint success = 0;
    programStatus(meshProgram, 0x8B82, &success);
    okay = okay && success != 0;
    for (auto s : shaders)
        deleteShader(s);
    return okay;
}
Glyph LoadGlyph(HDC dc, wchar_t ch)
{
    Profiler::Scope scope("font_glyph_cache_miss");
    Profiler::Add("font_glyph_cache_misses");
    Glyph glyph;
    GLYPHMETRICS metrics{};
    MAT2 identity{};
    identity.eM11.value = identity.eM22.value = 1;
    DWORD bytes = GetGlyphOutlineW(dc, ch, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &identity);
    if (bytes == GDI_ERROR)
    {
        SIZE size{};
        GetTextExtentPoint32W(dc, &ch, 1, &size);
        glyph.advance = size.cx;
        return glyph;
    }
    glyph.advance = metrics.gmCellIncX;
    glyph.width = int(metrics.gmBlackBoxX);
    glyph.height = int(metrics.gmBlackBoxY);
    glyph.left = metrics.gmptGlyphOrigin.x;
    glyph.top = metrics.gmptGlyphOrigin.y;
    if (!bytes || !glyph.width || !glyph.height)
    {
        glyph.width = glyph.height = 0;
        return glyph;
    }
    std::vector<unsigned char> bitmap(bytes);
    GetGlyphOutlineW(dc, ch, GGO_GRAY8_BITMAP, &metrics, bytes, bitmap.data(), &identity);
    Flush();
    if (fontX + glyph.width + 2 > FontSize)
    {
        fontX = 0;
        fontY += fontRow;
        fontRow = 0;
    }
    if (fontPages.empty() || fontY + glyph.height + 2 > FontSize)
    {
        GLuint page;
        glGenTextures(1, &page);
        fontPages.push_back(page);
        fontX = fontY = fontRow = 0;
        glBindTexture(GL_TEXTURE_2D, page);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
        std::vector<unsigned char> empty(FontSize * FontSize * 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, FontSize, FontSize, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     empty.data());
    }
    std::vector<unsigned char> pixels(size_t(glyph.width * glyph.height) * 4, 255);
    int stride = (glyph.width + 3) & ~3;
    for (int y = 0; y < glyph.height; ++y)
        for (int x = 0; x < glyph.width; ++x)
            pixels[size_t(y * glyph.width + x) * 4 + 3] =
                static_cast<unsigned char>(bitmap[size_t(y * stride + x)] * 255 / 64);
    glyph.texture = fontPages.back();
    glBindTexture(GL_TEXTURE_2D, glyph.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, fontX + 1, fontY + 1, glyph.width, glyph.height, GL_RGBA,
                    GL_UNSIGNED_BYTE, pixels.data());
    glyph.u = float(fontX + 1) / FontSize;
    glyph.v = float(fontY + 1) / FontSize;
    glyph.uw = float(glyph.width) / FontSize;
    glyph.vh = float(glyph.height) / FontSize;
    fontX += glyph.width + 2;
    fontRow = std::max(fontRow, glyph.height + 2);
    return glyph;
}
} // namespace
bool Initialize()
{
#define LOAD(v, t, n)                                                                                        \
    v = reinterpret_cast<t>(wglGetProcAddress(n));                                                           \
    if (!v)                                                                                                  \
    return false
    LOAD(genBuffers, GenBuffers, "glGenBuffers");
    LOAD(deleteBuffers, DeleteBuffers, "glDeleteBuffers");
    LOAD(bindBuffer, BindBuffer, "glBindBuffer");
    LOAD(bufferData, BufferData, "glBufferData");
    LOAD(drawInstanced, DrawInstanced, "glDrawArraysInstanced");
    LOAD(divisor, Divisor, "glVertexAttribDivisor");
    LOAD(attribPointer, AttribPointer, "glVertexAttribPointer");
    LOAD(enableAttrib, AttribEnable, "glEnableVertexAttribArray");
    LOAD(disableAttrib, AttribEnable, "glDisableVertexAttribArray");
    LOAD(useProgram, ShaderAction, "glUseProgram");
    LOAD(deleteShader, ShaderAction, "glDeleteShader");
    LOAD(deleteProgram, ShaderAction, "glDeleteProgram");
#undef LOAD
    if (!MakeProgram())
        return false;
    genBuffers(1, &stream);
    genBuffers(1, &instanceBuffer);
    pending.reserve(65536);
    primitive.reserve(128);
    instances.reserve(1024);
    return true;
}
void SetSolidTexture(unsigned id, float u, float v)
{
    Flush();
    solidTexture = id;
    solidU = u;
    solidV = v;
}
void Shutdown()
{
    Flush();
    for (auto &g : geometries)
        deleteBuffers(1, &g.second.buffer);
    geometries.clear();
    for (auto &m : meshes)
        deleteBuffers(1, &m.buffer);
    meshes.clear();
    if (stream)
        deleteBuffers(1, &stream);
    if (instanceBuffer)
        deleteBuffers(1, &instanceBuffer);
    if (meshProgram)
        deleteProgram(meshProgram);
    for (auto page : fontPages)
        glDeleteTextures(1, &page);
    fontPages.clear();
    glyphs.clear();
    lists.clear();
}
void Flush()
{
    FlushGeometry();
    FlushInstances();
}
Frame::Frame()
{
    matrix = {};
    stack.clear();
    matrixMode = GL_MODELVIEW;
    textured = false;
    texture = 0;
    current = {};
}
Frame::~Frame()
{
    Flush();
}
void Begin(unsigned mode)
{
    primitiveMode = mode;
    primitive.clear();
}
void End()
{
    auto vertices = Convert(primitive, primitiveMode);
    Submit(GL_TRIANGLES, textured ? texture : 0, vertices, false);
}
void Vertex2f(float x, float y)
{
    auto p = matrix.Apply({x, y});
    auto v = current;
    v.x = p.x;
    v.y = p.y;
    primitive.push_back(v);
}
void TexCoord2f(float u, float v)
{
    current.u = u;
    current.v = v;
}
void Color4f(float r, float g, float b, float a)
{
    current.r = r;
    current.g = g;
    current.b = b;
    current.a = a;
}
void Color3f(float r, float g, float b)
{
    Color4f(r, g, b, 1);
}
void Enable(unsigned state)
{
    if (state == GL_TEXTURE_2D)
    {
        textured = true;
        return;
    }
    if (enabledStates[state])
        return;
    Flush();
    glEnable(state);
    enabledStates[state] = true;
}
void Disable(unsigned state)
{
    if (state == GL_TEXTURE_2D)
    {
        textured = false;
        return;
    }
    if (!enabledStates[state])
        return;
    Flush();
    glDisable(state);
    enabledStates[state] = false;
}
void BindTexture(unsigned target, unsigned id)
{
    if (target == GL_TEXTURE_2D)
    {
        texture = id;
        return;
    }
    Flush();
    glBindTexture(target, id);
}
void MatrixMode(unsigned mode)
{
    Flush();
    matrixMode = mode;
    glMatrixMode(mode);
}
void LoadIdentity()
{
    if (matrixMode == GL_MODELVIEW)
        matrix = {};
    else
    {
        Flush();
        glLoadIdentity();
    }
}
void PushMatrix()
{
    if (matrixMode == GL_MODELVIEW)
        stack.push_back(matrix);
    else
    {
        Flush();
        glPushMatrix();
    }
}
void PopMatrix()
{
    if (matrixMode == GL_MODELVIEW)
    {
        if (!stack.empty())
        {
            matrix = stack.back();
            stack.pop_back();
        }
    }
    else
    {
        Flush();
        glPopMatrix();
    }
}
void Translatef(float x, float y, float z)
{
    if (matrixMode == GL_MODELVIEW)
        matrix = matrix * Scene::Matrix{1, 0, 0, 1, x, y};
    else
    {
        Flush();
        glTranslatef(x, y, z);
    }
}
void Scalef(float x, float y, float z)
{
    if (matrixMode == GL_MODELVIEW)
        matrix = matrix * Scene::Matrix{x, 0, 0, y, 0, 0};
    else
    {
        Flush();
        glScalef(x, y, z);
    }
}
void MultMatrixf(const float *m)
{
    if (matrixMode == GL_MODELVIEW)
        matrix = matrix * Scene::Matrix{m[0], m[1], m[4], m[5], m[12], m[13]};
    else
    {
        Flush();
        glMultMatrixf(m);
    }
}
void LineWidth(float width)
{
    lineWidth = width;
}
void NewList(unsigned id, unsigned)
{
    Flush();
    compiling = id;
    lists[id].clear();
    savedCompileMatrix = matrix;
    matrix = {};
}
void EndList()
{
    unsigned id = compiling;
    compiling = 0;
    matrix = savedCompileMatrix;
    bool untextured = true;
    std::vector<ModelVertex> vertices;
    for (auto &c : lists[id])
    {
        if (c.texture || c.mode != GL_TRIANGLES)
        {
            untextured = false;
            break;
        }
        for (auto &v : c.vertices)
            vertices.push_back({v.x, v.y, v.r, v.g, v.b, v.a, 0});
    }
    if (untextured && !vertices.empty())
    {
        listMeshes[id] = CreateMesh(vertices);
        lists[id].clear();
    }
}
void CallList(unsigned id)
{
    auto mesh = listMeshes.find(id);
    if (mesh != listMeshes.end())
    {
        Mesh(mesh->second, 0, 0, 1, 0, false);
        return;
    }
    for (auto &c : lists[id])
        Submit(c.mode, c.texture, c.vertices, true);
}
unsigned CreateGeometry(const std::vector<Vertex> &vertices, unsigned tex)
{
    Flush();
    GLuint buffer;
    genBuffers(1, &buffer);
    bindBuffer(ArrayBuffer, buffer);
    bufferData(ArrayBuffer, std::ptrdiff_t(vertices.size() * sizeof(Vertex)), vertices.data(), StaticDraw);
    bindBuffer(ArrayBuffer, 0);
    unsigned id = nextGeometry++;
    geometries[id] = {buffer, tex, GLsizei(vertices.size())};
    Profiler::Add("static_upload_bytes", double(vertices.size() * sizeof(Vertex)));
    return id;
}
void DestroyGeometry(unsigned id)
{
    auto it = geometries.find(id);
    if (it != geometries.end())
    {
        deleteBuffers(1, &it->second.buffer);
        geometries.erase(it);
    }
}
void Geometry(unsigned id)
{
    auto it = geometries.find(id);
    if (it == geometries.end())
        return;
    Flush();
    auto &g = it->second;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g.texture);
    GLfloat m[] = {matrix.a, matrix.b, 0, 0, matrix.c, matrix.d, 0, 0, 0, 0, 1, 0, matrix.x, matrix.y, 0, 1};
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
    bindBuffer(ArrayBuffer, g.buffer);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, sizeof(Vertex), nullptr);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, u)));
    glColorPointer(4, GL_FLOAT, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, r)));
    Profiler::Add("texture_bind_calls");
    glDrawArrays(GL_TRIANGLES, 0, g.count);
    DrawStats::RecordDraw(size_t(g.count), 1);
    Profiler::Add("static_draw_calls");
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    bindBuffer(ArrayBuffer, 0);
    glLoadIdentity();
    glMatrixMode(matrixMode);
}
bool VisibleRect(float left, float top, float right, float bottom, int width, int height)
{
    auto a = matrix.Apply({left, top}), b = matrix.Apply({right, top}), c = matrix.Apply({right, bottom}),
         d = matrix.Apply({left, bottom});
    return std::max({a.x, b.x, c.x, d.x}) >= 0 && std::min({a.x, b.x, c.x, d.x}) <= width &&
           std::max({a.y, b.y, c.y, d.y}) >= 0 && std::min({a.y, b.y, c.y, d.y}) <= height;
}
unsigned CreateMesh(const std::vector<ModelVertex> &vertices)
{
    GLuint buffer;
    genBuffers(1, &buffer);
    bindBuffer(ArrayBuffer, buffer);
    bufferData(ArrayBuffer, std::ptrdiff_t(vertices.size() * sizeof(ModelVertex)), vertices.data(),
               StaticDraw);
    bindBuffer(ArrayBuffer, 0);
    meshes.push_back({buffer, GLsizei(vertices.size())});
    return unsigned(meshes.size());
}
void Mesh(unsigned mesh, float x, float y, float scale, float phase, bool flash)
{
    if (mesh == 0 || mesh > meshes.size())
        return;
    FlushGeometry();
    if (!instances.empty() && (mesh != pendingMesh || instances.size() >= 4096))
    {
        Profiler::Add(mesh != pendingMesh ? "batch_break_mesh" : "batch_break_capacity");
        FlushInstances();
    }
    pendingMesh = mesh;
    auto m = matrix * Scene::Matrix{scale, 0, 0, scale, x, y};
    instances.push_back({m.a, m.b, m.c, m.d, m.x, m.y, phase, flash ? 1.f : 0.f});
}
void Text(void *context, float x, float baseline, const std::wstring &text)
{
    Profiler::Scope scope("ui_text_build");
    for (auto ch : text)
    {
        auto found = glyphs.find(ch);
        if (found == glyphs.end())
            found = glyphs.emplace(ch, LoadGlyph(static_cast<HDC>(context), ch)).first;
        const auto &g = found->second;
        if (g.width && g.height)
        {
            textured = true;
            texture = g.texture;
            float px = x + g.left, py = baseline - g.top;
            Begin(GL_QUADS);
            TexCoord2f(g.u, g.v);
            Vertex2f(px, py);
            TexCoord2f(g.u + g.uw, g.v);
            Vertex2f(px + g.width, py);
            TexCoord2f(g.u + g.uw, g.v + g.vh);
            Vertex2f(px + g.width, py + g.height);
            TexCoord2f(g.u, g.v + g.vh);
            Vertex2f(px, py + g.height);
            End();
        }
        x += g.advance;
    }
    textured = false;
}
bool RunTests(std::string &report)
{
    std::vector<Vertex> quad(4);
    quad[0].x = 1;
    quad[1].x = 2;
    quad[2].x = 3;
    quad[3].x = 4;
    auto triangles = Convert(quad, GL_QUADS);
    auto line = Convert(quad, GL_LINE_LOOP);
    bool okay = triangles.size() == 6 && triangles[3].x == 1 && triangles[5].x == 4 && line.size() == 24;
    report +=
        okay ? "PASS: render batching preserves triangle and line order.\n" : "FAIL: batch triangulation.\n";
    return okay;
}
} // namespace RenderBatch
