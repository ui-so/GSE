#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>
#include <fstream>
#include "Visuals.h"
namespace Visuals
{
namespace
{
GLuint materials[4]{}, sprites[4]{}, shadow = 0, scene = 0, program = 0;
int sceneW = 0, sceneH = 0;
using CreateShaderT = GLuint(APIENTRY *)(GLenum);
using ShaderSourceT = void(APIENTRY *)(GLuint, GLsizei, const char *const *, const GLint *);
using CompileShaderT = void(APIENTRY *)(GLuint);
using GetShaderivT = void(APIENTRY *)(GLuint, GLenum, GLint *);
using CreateProgramT = GLuint(APIENTRY *)();
using AttachShaderT = void(APIENTRY *)(GLuint, GLuint);
using LinkProgramT = void(APIENTRY *)(GLuint);
using GetProgramivT = void(APIENTRY *)(GLuint, GLenum, GLint *);
using UseProgramT = void(APIENTRY *)(GLuint);
using DeleteShaderT = void(APIENTRY *)(GLuint);
using DeleteProgramT = void(APIENTRY *)(GLuint);
using Uniform1iT = void(APIENTRY *)(GLint, GLint);
using Uniform1fT = void(APIENTRY *)(GLint, GLfloat);
using Uniform2fT = void(APIENTRY *)(GLint, GLfloat, GLfloat);
using GetUniformT = GLint(APIENTRY *)(GLuint, const char *);
CreateShaderT createShader;
ShaderSourceT shaderSource;
CompileShaderT compileShader;
GetShaderivT getShaderiv;
CreateProgramT createProgram;
AttachShaderT attachShader;
LinkProgramT linkProgram;
GetProgramivT getProgramiv;
UseProgramT useProgram;
DeleteShaderT deleteShader;
DeleteProgramT deleteProgram;
Uniform1iT uniform1i;
Uniform1fT uniform1f;
Uniform2fT uniform2f;
GetUniformT getUniform;
float noise(int x, int y)
{
    unsigned n = unsigned(x) * 374761393u + unsigned(y) * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return float(n & 65535) / 65535.f;
}
GLuint upload(int w, int h, const std::vector<unsigned char> &pixels, bool repeat = false)
{
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : 0x812F);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return id;
}
void quad(float x, float y, float w, float h, float u, float v, float uw, float vh)
{
    glBegin(GL_QUADS);
    glTexCoord2f(u, v);
    glVertex2f(x, y);
    glTexCoord2f(u + uw, v);
    glVertex2f(x + w, y);
    glTexCoord2f(u + uw, v + vh);
    glVertex2f(x + w, y + h);
    glTexCoord2f(u, v + vh);
    glVertex2f(x, y + h);
    glEnd();
}
struct Pixel
{
    unsigned char r, g, b, a;
};
// Rasterize actual animation frames once; gameplay selects atlas cells, not stick-figure transforms.
const int FW = 64, FH = 80, COLS = 8, ROWS = 12, AW = FW * COLS, AH = FH * ROWS;
std::vector<unsigned char> atlasPreview;
std::vector<unsigned char> bakeSprite(int palette)
{
    std::vector<unsigned char> pixels(AW * AH * 4, 0);
    const Pixel outfits[] = {
        {57, 107, 117, 255}, {115, 78, 60, 255}, {91, 104, 70, 255}, {110, 89, 123, 255}};
    for (int action = 0; action < 3; action++)
        for (int face = 0; face < 4; face++)
            for (int frame = 0; frame < 8; frame++)
            {
                int ox = frame * FW, oy = (action * 4 + face) * FH;
                auto put = [&](int x, int y, Pixel c) {
                    if (x < 0 || x >= FW || y < 0 || y >= FH)
                        return;
                    size_t i = size_t((oy + y) * AW + ox + x) * 4;
                    pixels[i] = c.r;
                    pixels[i + 1] = c.g;
                    pixels[i + 2] = c.b;
                    pixels[i + 3] = c.a;
                };
                auto box = [&](int x, int y, int w, int h, Pixel c) {
                    for (int yy = y; yy < y + h; yy++)
                        for (int xx = x; xx < x + w; xx++)
                            put(xx, yy, c);
                };
                auto oval = [&](int x, int y, int rx, int ry, Pixel c) {
                    for (int yy = -ry; yy <= ry; yy++)
                        for (int xx = -rx; xx <= rx; xx++)
                            if (float(xx * xx) / (rx * rx) + float(yy * yy) / (ry * ry) <= 1)
                                put(x + xx, y + yy, c);
                };
                float phase = frame * 6.2831853f / 8;
                int swing = action == 1 ? int(sinf(phase) * 5) : 0,
                    bob = action == 1 ? int(fabsf(sinf(phase)) * 2) : frame / 4;
                Pixel dark{26, 31, 31, 255}, leather{66, 49, 38, 255}, skin{191, 155, 117, 255},
                    cloth = outfits[palette];
                // Boots, trousers, split cloak, tunic, belt, shoulder panels, gloves, hood and facial
                // direction.
                box(23, 57 + swing, 7, 16 - swing, dark);
                box(35, 57 - swing, 7, 16 + swing, dark);
                box(21, 70 + swing / 2, 10, 5, leather);
                box(35, 70 - swing / 2, 10, 5, leather);
                for (int y = 29; y < 64; y++)
                {
                    int spread = 9 + (y - 29) / 5;
                    for (int x = 32 - spread; x <= 32 + spread; x++)
                    {
                        Pixel c = cloth;
                        float shade = .65f + float(x - (32 - spread)) / (spread * 2) * .43f;
                        c.r = static_cast<unsigned char>(c.r * shade);
                        c.g = static_cast<unsigned char>(c.g * shade);
                        c.b = static_cast<unsigned char>(c.b * shade);
                        put(x, y - bob, c);
                    }
                }
                box(23, 48 - bob, 19, 4, leather);
                box(31, 48 - bob, 4, 4, {187, 151, 82, 255});
                oval(21, 33 - bob, 5, 6, cloth);
                oval(43, 33 - bob, 5, 6, cloth);
                int hand = action == 2 ? int(9 + 5 * sinf(phase)) : swing;
                box(17, 36 - bob, 5, 13 + hand / 2, leather);
                oval(19, 50 - bob + hand / 2, 3, 4, skin);
                box(44, 36 - bob - hand, 5, 14, leather);
                oval(46, 50 - bob - hand, 3, 4, skin);
                oval(32, 22 - bob, 11, 13, dark);
                oval(32, 19 - bob, 10, 11, cloth);
                if (face != 3)
                {
                    int shift = face == 1 ? -3 : face == 2 ? 3 : 0;
                    oval(32 + shift, 25 - bob, 7, 8, skin);
                    box(26 + shift, 18 - bob, 13, 4, leather);
                    box(29 + shift, 25 - bob, 2, 2, dark);
                    if (face == 0)
                        box(35, 25 - bob, 2, 2, dark);
                    box(31 + shift, 30 - bob, 4, 1, leather);
                }
                box(29, 35 - bob, 3, 11, {156, 135, 94, 255});
                box(38, 36 - bob, 2, 10, {51, 47, 38, 255});
                if (palette == 0)
                {
                    box(48, 49 - bob - hand, 6, 9, {218, 163, 71, 255});
                    box(50, 51 - bob - hand, 2, 5, {255, 232, 162, 255});
                }
            }
    return pixels;
}
GLuint shader(GLenum type, const char *src)
{
    GLuint id = createShader(type);
    shaderSource(id, 1, &src, nullptr);
    compileShader(id);
    GLint ok = 0;
    getShaderiv(id, 0x8B81, &ok);
    if (!ok)
    {
        deleteShader(id);
        return 0;
    }
    return id;
}
void loadPost()
{
#define LOAD(v, t, n)                                                                                        \
    v = reinterpret_cast<t>(wglGetProcAddress(n));                                                           \
    if (!v || reinterpret_cast<uintptr_t>(v) <= 3 || reinterpret_cast<intptr_t>(v) == -1)                    \
    return
    LOAD(createShader, CreateShaderT, "glCreateShader");
    LOAD(shaderSource, ShaderSourceT, "glShaderSource");
    LOAD(compileShader, CompileShaderT, "glCompileShader");
    LOAD(getShaderiv, GetShaderivT, "glGetShaderiv");
    LOAD(createProgram, CreateProgramT, "glCreateProgram");
    LOAD(attachShader, AttachShaderT, "glAttachShader");
    LOAD(linkProgram, LinkProgramT, "glLinkProgram");
    LOAD(getProgramiv, GetProgramivT, "glGetProgramiv");
    LOAD(useProgram, UseProgramT, "glUseProgram");
    LOAD(deleteShader, DeleteShaderT, "glDeleteShader");
    LOAD(deleteProgram, DeleteProgramT, "glDeleteProgram");
    LOAD(uniform1i, Uniform1iT, "glUniform1i");
    LOAD(uniform1f, Uniform1fT, "glUniform1f");
    LOAD(uniform2f, Uniform2fT, "glUniform2f");
    LOAD(getUniform, GetUniformT, "glGetUniformLocation");
#undef LOAD
    const char *vs = "#version 120\nvarying vec2 uv;void "
                     "main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;uv=gl_MultiTexCoord0.xy;}";
    const char *fs = R"GLSL(#version 120
uniform sampler2D scene;uniform vec2 pixel;uniform float clock;varying vec2 uv;
vec3 bright(vec2 p){vec3 s=texture2D(scene,p).rgb;return s*smoothstep(0.48,0.93,max(max(s.r,s.g),s.b));}
void main(){vec3 c=texture2D(scene,uv).rgb;
 vec3 bloom=(bright(uv+vec2(pixel.x*6.0,0.0))+bright(uv-vec2(pixel.x*6.0,0.0))+bright(uv+vec2(0.0,pixel.y*6.0))+bright(uv-vec2(0.0,pixel.y*6.0)))*0.25;
 c=(c+bloom*0.37)*(1.10-c*0.10)*vec3(0.99,1.025,1.04);
 vec2 d=uv-0.5;c*=1.0-0.33*dot(d,d);
 float grain=fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715))+clock*0.13)-0.5;
 gl_FragColor=vec4(clamp(c+grain*0.003,0.0,1.0),1.0);})GLSL";
    GLuint a = shader(0x8B31, vs), b = shader(0x8B30, fs);
    if (!a || !b)
    {
        if (a)
            deleteShader(a);
        if (b)
            deleteShader(b);
        return;
    }
    program = createProgram();
    attachShader(program, a);
    attachShader(program, b);
    linkProgram(program);
    GLint ok = 0;
    getProgramiv(program, 0x8B82, &ok);
    deleteShader(a);
    deleteShader(b);
    if (!ok)
    {
        deleteProgram(program);
        program = 0;
    }
}
} // namespace
bool Initialize()
{
    for (int type = 0; type < 4; type++)
    {
        std::vector<unsigned char> data(128 * 128 * 4);
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 128; x++)
            {
                float n = noise(x, y), r, g, b;
                if (type == 0)
                {
                    float blade = (x % 9 == 0 && y % 17 < 8) ? .08f : 0;
                    r = .15f + n * .025f + blade * .3f;
                    g = .22f + n * .035f + blade;
                    b = .15f + n * .02f;
                }
                else if (type == 1)
                {
                    int row = y / 32, xx = (x + (row % 2) * 32) % 64, yy = y % 32;
                    bool gap = xx < 2 || yy < 2 || xx > 61;
                    float slab = noise((x + (row % 2) * 32) / 64, row);
                    float edge = (xx == 2 || yy == 2) ? .10f : 0;
                    r = gap ? .105f : .29f + slab * .10f + n * .045f + edge;
                    g = gap ? .13f : .30f + slab * .09f + n * .04f + edge;
                    b = gap ? .095f : .25f + slab * .06f + n * .04f + edge;
                }
                else if (type == 2)
                {
                    r = .26f + n * .11f;
                    g = .24f + n * .09f;
                    b = .17f + n * .07f;
                    if (n > .94f)
                    {
                        r += .07f;
                        g += .065f;
                        b += .05f;
                    }
                }
                else
                {
                    float ripple = sinf(x * .17f + sinf(y * .12f) * 2) * .015f;
                    r = .065f + n * .015f;
                    g = .20f + n * .025f + ripple;
                    b = .24f + n * .035f + ripple;
                }
                size_t i = size_t(y * 128 + x) * 4;
                data[i] = static_cast<unsigned char>(r * 255);
                data[i + 1] = static_cast<unsigned char>(g * 255);
                data[i + 2] = static_cast<unsigned char>(b * 255);
                data[i + 3] = 255;
            }
        materials[type] = upload(128, 128, data, true);
    }
    for (int i = 0; i < 4; i++)
    {
        auto pixels = bakeSprite(i);
        sprites[i] = upload(AW, AH, pixels);
        if (i == 0)
            atlasPreview = pixels;
    }
    std::vector<unsigned char> pixels(128 * 128 * 4);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
        {
            float d = std::sqrt(powf((x - 63.5f) / 64, 2) + powf((y - 63.5f) / 64, 2));
            int i = (y * 128 + x) * 4;
            pixels[i] = 7;
            pixels[i + 1] = 13;
            pixels[i + 2] = 18;
            pixels[i + 3] = static_cast<unsigned char>(std::max(0.f, 1 - d) * std::max(0.f, 1 - d) * 220);
        }
    shadow = upload(128, 128, pixels);
    glGenTextures(1, &scene);
    loadPost();
    return program != 0;
}
void MaterialQuad(int material, const float *xy, float variation, float time)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, materials[material]);
    glColor4f(variation, variation, variation, 1);
    float t = material == 3 ? time * .024f : 0;
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++)
    {
        glTexCoord2f((i == 1 || i == 2 ? 1.f : 0) + t, (i >= 2 ? 1.f : 0) + t * .5f);
        glVertex2f(xy[i * 2], xy[i * 2 + 1]);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}
void Sprite(float x, float y, int palette, int facing, int action, float phase)
{
    int frame = int(phase * 8) % 8;
    int row = std::clamp(action, 0, 2) * 4 + std::clamp(facing, 0, 3);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sprites[palette % 4]);
    glColor4f(1, 1, 1, 1);
    quad(x - 25, y - 59, 50, 62, frame / 8.f, row / 12.f, 1 / 8.f, 1 / 12.f);
    glDisable(GL_TEXTURE_2D);
}
void SoftShadow(float x, float y, float radius, float stretch, float opacity)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, shadow);
    glColor4f(1, 1, 1, opacity);
    quad(x - radius, y - radius * stretch, radius * 2, radius * stretch * 2, 0, 0, 1, 1);
    glDisable(GL_TEXTURE_2D);
}
bool PostAvailable()
{
    return program != 0;
}
void PostProcess(int width, int height, float time, bool enabled)
{
    if (!program || !enabled)
        return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, scene);
    if (width != sceneW || height != sceneH)
    {
        sceneW = width;
        sceneH = height;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    glDisable(GL_BLEND);
    useProgram(program);
    uniform1i(getUniform(program, "scene"), 0);
    uniform2f(getUniform(program, "pixel"), 1.f / width, 1.f / height);
    uniform1f(getUniform(program, "clock"), time);
    quad(0, 0, float(width), float(height), 0, 1, 1, -1);
    useProgram(0);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
}
void SaveAtlas(const char *path)
{
    BITMAPFILEHEADER f{};
    BITMAPINFOHEADER b{};
    f.bfType = 0x4D42;
    f.bfOffBits = sizeof(f) + sizeof(b);
    f.bfSize = f.bfOffBits + AW * AH * 4;
    b.biSize = sizeof(b);
    b.biWidth = AW;
    b.biHeight = -AH;
    b.biPlanes = 1;
    b.biBitCount = 32;
    auto data = atlasPreview;
    for (size_t i = 0; i < data.size(); i += 4)
        std::swap(data[i], data[i + 2]);
    std::ofstream o(path, std::ios::binary);
    o.write((char *)&f, sizeof(f));
    o.write((char *)&b, sizeof(b));
    o.write((char *)data.data(), data.size());
}
void Shutdown()
{
    glDeleteTextures(4, materials);
    glDeleteTextures(4, sprites);
    glDeleteTextures(1, &shadow);
    glDeleteTextures(1, &scene);
    if (program)
        deleteProgram(program);
    program = 0;
    atlasPreview.clear();
}
} // namespace Visuals
