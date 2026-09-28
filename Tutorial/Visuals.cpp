#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include "DrawStats.h"
#include "RenderBatch.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include "Visuals.h"
#include "AssetCache.h"
namespace Visuals
{
namespace
{
GLuint materials[8]{}, sprites[4]{}, shadow = 0, scene = 0, program = 0, effectsProgram = 0;
int sceneW = 0, sceneH = 0;
using GenFramebuffersT = void(APIENTRY *)(GLsizei, GLuint *);
using BindFramebufferT = void(APIENTRY *)(GLenum, GLuint);
using AttachTextureT = void(APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
using CheckFramebufferT = GLenum(APIENTRY *)(GLenum);
using DeleteFramebuffersT = void(APIENTRY *)(GLsizei, const GLuint *);
using ActiveTextureT = void(APIENTRY *)(GLenum);
GenFramebuffersT genFramebuffers = nullptr;
BindFramebufferT bindFramebuffer = nullptr;
AttachTextureT attachTexture = nullptr;
CheckFramebufferT checkFramebuffer = nullptr;
DeleteFramebuffersT deleteFramebuffers = nullptr;
ActiveTextureT activeTexture = nullptr;
GLuint sceneFbo = 0, bloomFbo = 0, bloomTexture = 0;
bool sceneTarget = false;
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
GLint Uniform(GLuint id, const char *name)
{
    static std::map<std::pair<GLuint, std::string>, GLint> locations;
    auto key = std::make_pair(id, std::string(name));
    auto found = locations.find(key);
    if (found != locations.end())
        return found->second;
    return locations[key] = getUniform(id, name);
}
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
    RenderBatch::Begin(GL_QUADS);
    RenderBatch::TexCoord2f(u, v);
    RenderBatch::Vertex2f(x, y);
    RenderBatch::TexCoord2f(u + uw, v);
    RenderBatch::Vertex2f(x + w, y);
    RenderBatch::TexCoord2f(u + uw, v + vh);
    RenderBatch::Vertex2f(x + w, y + h);
    RenderBatch::TexCoord2f(u, v + vh);
    RenderBatch::Vertex2f(x, y + h);
    RenderBatch::End();
}
struct Pixel
{
    unsigned char r, g, b, a;
};
// Rasterize actual animation frames once; gameplay selects atlas cells, not stick-figure transforms.
const int FW = 64, FH = 80, COLS = 8, ROWS = 16, AW = FW * COLS, AH = FH * ROWS;
std::vector<unsigned char> atlasPreview;
std::vector<unsigned char> bakeSprite(int palette)
{
    std::vector<unsigned char> pixels(AW * AH * 4, 0);
    const Pixel outfits[] = {
        {57, 107, 117, 255}, {115, 78, 60, 255}, {91, 104, 70, 255}, {110, 89, 123, 255}};
    for (int action = 0; action < 4; action++)
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
                int hand = action == 3   ? int(12 + 12 * sinf(phase))
                           : action == 2 ? int(9 + 5 * sinf(phase))
                                         : swing;
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
                if (action == 3)
                {
                    box(46, 29 - bob - hand, 3, 25, {169, 178, 174, 255});
                    box(42, 48 - bob - hand, 11, 3, {139, 106, 52, 255});
                }
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
uniform sampler2D scene;uniform sampler2D bloomImage;uniform int pass;uniform int bloomReady;uniform vec2 pixel;uniform float clock;varying vec2 uv;
vec3 bright(vec2 p){vec3 s=texture2D(scene,p).rgb;return s*smoothstep(0.48,0.93,max(max(s.r,s.g),s.b));}
void main(){vec3 c=texture2D(scene,uv).rgb;
 vec3 bloom=vec3(0);if(pass==1||bloomReady==0)bloom=(bright(uv+vec2(pixel.x*6.0,0.0))+bright(uv-vec2(pixel.x*6.0,0.0))+bright(uv+vec2(0.0,pixel.y*6.0))+bright(uv-vec2(0.0,pixel.y*6.0)))*0.25;
 if(pass==1){gl_FragColor=vec4(bloom,1);return;}
 if(bloomReady==1)bloom=texture2D(bloomImage,uv).rgb;
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
void loadEffects()
{
    if (!program)
        return;
    const char *vertex = R"GLSL(#version 120
varying vec2 uv;varying vec2 world;varying vec4 tint;
void main(){gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;uv=gl_MultiTexCoord0.xy;world=gl_Vertex.xy;tint=gl_Color;})GLSL";
    const char *fragment = R"GLSL(#version 120
uniform sampler2D image;uniform float clock;uniform int mode;varying vec2 uv;varying vec2 world;varying vec4 tint;
void main(){
 if(mode==0){
  vec2 flow=uv+vec2(clock*0.018,clock*0.011);float wave=sin(world.x*0.065+world.y*0.1-clock*1.6)*0.008+sin(world.y*0.09+clock)*0.006;
  vec3 c=texture2D(image,flow+vec2(wave,-wave)).rgb;
  float crest=pow(max(0.0,sin(world.x*0.085+world.y*0.045+clock*1.7)),12.0);
  gl_FragColor=vec4((c+vec3(0.09,0.15,0.14)*crest)*tint.rgb,1.0);
 }else{
  float y=1.0-uv.y;float bend=sin(y*10.0-clock*6.0)*0.08*y+sin(clock*9.0+y*17.0)*0.025;
  float x=abs(uv.x-0.5-bend);float radius=(1.0-y)*0.30*(0.8+0.2*sin(clock*7.0+y*11.0));
  float shape=(1.0-smoothstep(radius*0.65,radius+0.045,x))*smoothstep(0.0,0.10,y)*(1.0-smoothstep(0.8,1.0,y));
  float core=1.0-smoothstep(0.0,radius+0.01,x);vec3 c=mix(vec3(1.0,0.16,0.025),vec3(1.0,0.85,0.28),core*(1.0-y));
  float spark=step(0.992,fract(sin(floor(uv.x*31.0)*17.13+floor((uv.y+clock*.45)*24.0)*41.71)*437.5))*smoothstep(.4,.9,y)*.45;
  if(mode==2)c=vec3(dot(c,vec3(0.299,0.587,0.114)));
  gl_FragColor=vec4(c,max(shape,spark));
 }
})GLSL";
    GLuint v = shader(0x8B31, vertex), f = shader(0x8B30, fragment);
    if (!v || !f)
    {
        if (v)
            deleteShader(v);
        if (f)
            deleteShader(f);
        return;
    }
    effectsProgram = createProgram();
    attachShader(effectsProgram, v);
    attachShader(effectsProgram, f);
    linkProgram(effectsProgram);
    GLint okay = 0;
    getProgramiv(effectsProgram, 0x8B82, &okay);
    deleteShader(v);
    deleteShader(f);
    if (!okay)
    {
        deleteProgram(effectsProgram);
        effectsProgram = 0;
    }
}
} // namespace
bool Initialize()
{
    for (int type = 0; type < 4; type++)
    {
        auto data = AssetCache::LoadOrCreate("material-" + std::to_string(type), 128 * 128 * 4, [type]() {
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
            return data;
        });
        materials[type] = upload(128, 128, data, true);
        for (size_t i = 0; i < data.size(); i += 4)
        {
            auto gray =
                static_cast<unsigned char>(data[i] * .299f + data[i + 1] * .587f + data[i + 2] * .114f);
            data[i] = data[i + 1] = data[i + 2] = gray;
        }
        materials[type + 4] = upload(128, 128, data, true);
    }
    std::vector<unsigned char> spriteAtlas(size_t(AW * 2) * AH * 2 * 4);
    for (int i = 0; i < 4; ++i)
    {
        auto pixels = AssetCache::LoadOrCreate("sprite-" + std::to_string(i), AW * AH * 4,
                                               [i]() { return bakeSprite(i); });
        for (int y = 0; y < AH; ++y)
            std::copy_n(pixels.data() + size_t(y) * AW * 4, AW * 4,
                        spriteAtlas.data() + (size_t(y + (i / 2) * AH) * AW * 2 + (i % 2) * AW) * 4);
        if (i == 0)
            atlasPreview = pixels;
    }
    sprites[0] = upload(AW * 2, AH * 2, spriteAtlas);
    for (size_t i = 0; i < spriteAtlas.size(); i += 4)
    {
        auto gray = static_cast<unsigned char>(spriteAtlas[i] * .299f + spriteAtlas[i + 1] * .587f +
                                               spriteAtlas[i + 2] * .114f);
        spriteAtlas[i] = spriteAtlas[i + 1] = spriteAtlas[i + 2] = gray;
    }
    sprites[1] = upload(AW * 2, AH * 2, spriteAtlas);
    auto pixels = AssetCache::LoadOrCreate("shadow", 128 * 128 * 4, []() {
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
        return pixels;
    });
    // Share one texture between solid-colored triangles and soft shadows, preserving draw order.
    std::vector<unsigned char> shadowAtlas(256 * 128 * 4, 255);
    for (int y = 0; y < 128; ++y)
        std::copy_n(pixels.data() + y * 128 * 4, 128 * 4, shadowAtlas.data() + y * 256 * 4);
    shadow = upload(256, 128, shadowAtlas);
    RenderBatch::SetSolidTexture(shadow, .75f, .5f);
    glGenTextures(1, &scene);
    loadPost();
    loadEffects();
    genFramebuffers = reinterpret_cast<GenFramebuffersT>(wglGetProcAddress("glGenFramebuffers"));
    bindFramebuffer = reinterpret_cast<BindFramebufferT>(wglGetProcAddress("glBindFramebuffer"));
    attachTexture = reinterpret_cast<AttachTextureT>(wglGetProcAddress("glFramebufferTexture2D"));
    checkFramebuffer = reinterpret_cast<CheckFramebufferT>(wglGetProcAddress("glCheckFramebufferStatus"));
    deleteFramebuffers = reinterpret_cast<DeleteFramebuffersT>(wglGetProcAddress("glDeleteFramebuffers"));
    activeTexture = reinterpret_cast<ActiveTextureT>(wglGetProcAddress("glActiveTexture"));
    if (genFramebuffers && bindFramebuffer && attachTexture && checkFramebuffer && deleteFramebuffers &&
        activeTexture)
    {
        genFramebuffers(1, &sceneFbo);
        genFramebuffers(1, &bloomFbo);
        glGenTextures(1, &bloomTexture);
    }
    else
        Profiler::Event("postprocess_fallback", "framebuffer_objects_unavailable");
    return program != 0;
}
unsigned MaterialTexture(int material)
{
    return materials[material];
}
void MaterialQuad(int material, const float *xy, float variation, float time)
{
    RenderBatch::Enable(GL_TEXTURE_2D);
    RenderBatch::BindTexture(GL_TEXTURE_2D, materials[material]);
    RenderBatch::Color4f(variation, variation, variation, 1);
    float t = material == 3 ? time * .024f : 0;
    RenderBatch::Begin(GL_QUADS);
    for (int i = 0; i < 4; i++)
    {
        RenderBatch::TexCoord2f((i == 1 || i == 2 ? 1.f : 0) + t, (i >= 2 ? 1.f : 0) + t * .5f);
        RenderBatch::Vertex2f(xy[i * 2], xy[i * 2 + 1]);
    }
    RenderBatch::End();
    RenderBatch::Disable(GL_TEXTURE_2D);
}
void Sprite(float x, float y, int palette, int facing, int action, float phase, bool frozen)
{
    int frame = int(phase * 8) % 8;
    int row = std::clamp(action, 0, 3) * 4 + std::clamp(facing, 0, 3);
    RenderBatch::Enable(GL_TEXTURE_2D);
    RenderBatch::BindTexture(GL_TEXTURE_2D, sprites[frozen ? 1 : 0]);
    RenderBatch::Color4f(1, 1, 1, 1);
    quad(x - 25, y - 59, 50, 62, (frame / 8.f + (palette % 2)) * .5f,
         (row / 16.f + ((palette % 4) / 2)) * .5f, 1 / 16.f, 1 / 32.f);
    RenderBatch::Disable(GL_TEXTURE_2D);
}
void SoftShadow(float x, float y, float radius, float stretch, float opacity)
{
    RenderBatch::Enable(GL_TEXTURE_2D);
    RenderBatch::BindTexture(GL_TEXTURE_2D, shadow);
    RenderBatch::Color4f(1, 1, 1, opacity);
    quad(x - radius, y - radius * stretch, radius * 2, radius * stretch * 2, 0, 0, .5f, 1);
    RenderBatch::Disable(GL_TEXTURE_2D);
}
bool PostAvailable()
{
    return program != 0;
}
void BeginScene(int width, int height, bool enabled)
{
    Profiler::Scope scope("render_target_setup");
    RenderBatch::Flush();
    sceneTarget = false;
    if (!program || !enabled)
    {
        if (bindFramebuffer)
            bindFramebuffer(0x8D40, 0);
        return;
    }
    if (width != sceneW || height != sceneH)
    {
        sceneW = width;
        sceneH = height;
        for (int i = 0; i < (sceneFbo ? 2 : 1); ++i)
        {
            GLuint textureId = i == 0 ? scene : bloomTexture;
            glBindTexture(GL_TEXTURE_2D, textureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, i == 0 ? width : std::max(1, width / 2),
                         i == 0 ? height : std::max(1, height / 2), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            if (sceneFbo)
            {
                bindFramebuffer(0x8D40, i == 0 ? sceneFbo : bloomFbo);
                attachTexture(0x8D40, 0x8CE0, GL_TEXTURE_2D, textureId, 0);
                if (checkFramebuffer(0x8D40) != 0x8CD5)
                {
                    Profiler::Event("postprocess_fallback", "framebuffer_incomplete");
                    deleteFramebuffers(1, &sceneFbo);
                    deleteFramebuffers(1, &bloomFbo);
                    sceneFbo = bloomFbo = 0;
                    break;
                }
            }
        }
    }
    if (sceneFbo)
    {
        bindFramebuffer(0x8D40, sceneFbo);
        sceneTarget = true;
        glClear(GL_COLOR_BUFFER_BIT);
    }
    else if (bindFramebuffer)
        bindFramebuffer(0x8D40, 0);
}
void PostProcess(int width, int height, float time, bool enabled)
{
    Profiler::Scope scope("postprocess_cpu");
    Profiler::GpuScope gpu("postprocess");
    RenderBatch::Flush();
    if (!program || !enabled)
        return;
    RenderBatch::Enable(GL_TEXTURE_2D);
    RenderBatch::BindTexture(GL_TEXTURE_2D, scene);
    RenderBatch::Disable(GL_BLEND);
    useProgram(program);
    uniform1i(Uniform(program, "scene"), 0);
    uniform1i(Uniform(program, "bloomImage"), 1);
    uniform2f(Uniform(program, "pixel"), 1.f / width, 1.f / height);
    uniform1f(Uniform(program, "clock"), time);
    uniform1i(Uniform(program, "bloomReady"), sceneTarget ? 1 : 0);
    if (sceneTarget)
    {
        bindFramebuffer(0x8D40, bloomFbo);
        glViewport(0, 0, std::max(1, width / 2), std::max(1, height / 2));
        uniform1i(Uniform(program, "pass"), 1);
        quad(0, 0, float(width), float(height), 0, 1, 1, -1);
        RenderBatch::Flush();
        bindFramebuffer(0x8D40, 0);
        glViewport(0, 0, width, height);
        activeTexture(0x84C1);
        glBindTexture(GL_TEXTURE_2D, bloomTexture);
        activeTexture(0x84C0);
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, scene);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
        Profiler::Add("framebuffer_copy_bytes", double(width) * height * 4);
    }
    uniform1i(Uniform(program, "pass"), 0);
    RenderBatch::Color4f(1, 1, 1, 1);
    quad(0, 0, float(width), float(height), 0, 1, 1, -1);
    RenderBatch::Flush();
    useProgram(0);
    RenderBatch::Disable(GL_TEXTURE_2D);
    RenderBatch::Enable(GL_BLEND);
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
void BeginWater(float time)
{
    if (!effectsProgram)
        return;
    RenderBatch::Flush();
    useProgram(effectsProgram);
    uniform1i(Uniform(effectsProgram, "image"), 0);
    uniform1i(Uniform(effectsProgram, "mode"), 0);
    uniform1f(Uniform(effectsProgram, "clock"), time);
}
void EndEffect()
{
    if (effectsProgram)
        RenderBatch::Flush();
    useProgram(0);
}
bool EffectsAvailable()
{
    return effectsProgram != 0;
}
void Flame(float x, float y, float scale, float time, bool frozen)
{
    if (!effectsProgram)
        return;
    RenderBatch::Flush();
    useProgram(effectsProgram);
    uniform1i(Uniform(effectsProgram, "mode"), frozen ? 2 : 1);
    uniform1f(Uniform(effectsProgram, "clock"), frozen ? 0.f : time);
    // The procedural flame consumes UV even though it does not sample this texture.
    RenderBatch::Enable(GL_TEXTURE_2D);
    RenderBatch::BindTexture(GL_TEXTURE_2D, shadow);
    RenderBatch::Color4f(1, 1, 1, 1);
    quad(x - 30 * scale, y - 66 * scale, 60 * scale, 72 * scale, 0, 0, 1, 1);
    RenderBatch::Flush();
    useProgram(0);
    RenderBatch::Disable(GL_TEXTURE_2D);
}
void Shutdown()
{
    if (sceneFbo)
        deleteFramebuffers(1, &sceneFbo);
    if (bloomFbo)
        deleteFramebuffers(1, &bloomFbo);
    if (bloomTexture)
        glDeleteTextures(1, &bloomTexture);
    glDeleteTextures(8, materials);
    glDeleteTextures(4, sprites);
    glDeleteTextures(1, &shadow);
    glDeleteTextures(1, &scene);
    if (program)
        deleteProgram(program);
    if (effectsProgram)
        deleteProgram(effectsProgram);
    effectsProgram = 0;
    program = 0;
    atlasPreview.clear();
}
} // namespace Visuals
