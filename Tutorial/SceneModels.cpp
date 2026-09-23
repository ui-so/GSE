#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include "DrawStats.h"
#include "SceneModels.h"
#include "AssetCache.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace SceneModels
{
namespace
{
struct Primitive
{
    std::uint32_t shape = 0;
    float x = 0, y = 0, w = 0, h = 0, u = 0, v = 0, r = 0, g = 0, b = 0, a = 1;
    std::uint32_t animated = 0;
};
struct Model
{
    std::uint32_t count = 0;
    std::array<Primitive, 96> parts{};
};
std::array<Model, static_cast<int>(Kind::Count)> models;
Model Generate(Kind kind)
{
    Model model{};
    auto Add = [&](std::uint32_t shape, float x, float y, float w, float h, float r, float g, float b,
                   std::uint32_t animated = 0) {
        Primitive p{};
        p.shape = shape;
        p.x = x;
        p.y = y;
        p.w = w;
        p.h = h;
        p.r = r;
        p.g = g;
        p.b = b;
        p.animated = animated;
        model.parts[model.count++] = p;
    };
    auto Triangle = [&](float x, float y, float u, float v, float w, float h, float r, float g, float b) {
        Add(2, x, y, w, h, r, g, b);
        model.parts[model.count - 1].u = u;
        model.parts[model.count - 1].v = v;
    };
    switch (kind)
    {
    case Kind::Tree:
        Add(1, -4, -35, 8, 35, .23f, .17f, .11f);
        for (int i = 0; i < 4; i++)
        {
            float y = -15.f - i * 21, w = 31.f - i * 5;
            Triangle(0, y - 46, -w, y, w, y, .09f + i * .015f, .23f + i * .01f, .17f + i * .01f);
            Triangle(0, y - 46, 0, y - 3, w, y, .055f, .15f, .12f);
        }
        break;
    case Kind::Rock:
        Add(0, 0, -8, 21, 11, .26f, .29f, .27f);
        Triangle(-18, -12, -5, -26, 16, -12, .40f, .43f, .38f);
        Triangle(-5, -26, 21, -11, 5, -6, .31f, .35f, .32f);
        break;
    case Kind::Boar:
        for (int i = 0; i < 4; i++)
            Add(1, -15.f + i * 9, -16, 4, 16, .17f, .13f, .12f, 1 + std::uint32_t(i % 2));
        Add(0, 0, -20, 23, 13, .37f, .24f, .18f);
        Add(0, -2, -26, 17, 7, .46f, .31f, .23f);
        Add(0, 22, -24, 11, 9, .38f, .25f, .19f);
        Triangle(14, -28, 16, -42, 25, -31, .29f, .20f, .16f);
        Add(0, 29, -23, 6, 5, .26f, .17f, .15f);
        Add(0, 25, -28, 2, 2, .85f, .38f, .16f);
        Triangle(28, -18, 35, -15, 34, -25, .81f, .75f, .61f);
        break;
    case Kind::Wraith:
        Triangle(0, -60, -19, -3, 20, -3, .28f, .37f, .35f);
        Triangle(0, -52, 0, -4, 20, -3, .13f, .23f, .24f);
        Add(0, 0, -48, 11, 14, .42f, .48f, .42f);
        Add(0, -4, -50, 3, 3, .55f, .87f, .81f);
        Add(0, 5, -50, 3, 3, .55f, .87f, .81f);
        Add(1, -5, -39, 10, 2, .11f, .19f, .18f);
        Add(1, 16, -34, 5, 19, .39f, .36f, .29f, 1);
        break;
    case Kind::Potion:
        Add(1, -3, -22, 6, 7, .50f, .40f, .23f);
        Add(0, 0, -9, 8, 10, .19f, .48f, .39f);
        Add(0, -2, -12, 3, 5, .45f, .79f, .62f);
        Add(1, -5, -12, 10, 5, .58f, .61f, .43f);
        break;
    case Kind::Coin:
        Add(0, 0, -5, 9, 5, .71f, .50f, .18f);
        Add(0, 0, -7, 8, 4, .94f, .74f, .35f);
        Add(1, -1, -10, 2, 6, .63f, .39f, .11f);
        break;
    case Kind::Brazier:
        Add(1, -17, -6, 34, 5, .20f, .14f, .09f);
        Add(0, 0, -7, 20, 7, .25f, .23f, .21f);
        Add(0, 0, -10, 15, 5, .09f, .08f, .075f);
        break;
    default:
        break;
    }
    return model;
}
} // namespace
void Initialize()
{
    for (int i = 0; i < static_cast<int>(Kind::Count); i++)
    {
        auto bytes = AssetCache::LoadOrCreate("model-" + std::to_string(i), sizeof(Model), [i]() {
            Model model = Generate(static_cast<Kind>(i));
            AssetCache::Bytes data(sizeof(Model));
            std::memcpy(data.data(), &model, sizeof(Model));
            return data;
        });
        if (bytes.size() == sizeof(Model))
            std::memcpy(&models[i], bytes.data(), sizeof(Model));
    }
}
void Draw(Kind kind, float x, float y, float scale, float phase, bool flash)
{
    const auto &model = models[static_cast<int>(kind)];
    glPushMatrix();
    glTranslatef(x, y, 0);
    glScalef(scale, scale, 1);
    for (std::uint32_t i = 0; i < model.count && i < model.parts.size(); i++)
    {
        const auto &p = model.parts[i];
        float shift = p.animated ? std::sin(phase * 9 + p.animated * 3.14159f) * 3 : 0;
        glColor4f(flash ? 1 : p.r, flash ? .8f : p.g, flash ? .6f : p.b, p.a);
        glPushMatrix();
        glTranslatef(0, shift, 0);
        if (p.shape == 0)
        {
            DrawStats::Begin(GL_TRIANGLE_FAN);
            glVertex2f(p.x, p.y);
            for (int j = 0; j <= 32; j++)
            {
                float a = j * 6.2831853f / 32;
                glVertex2f(p.x + std::cos(a) * p.w, p.y + std::sin(a) * p.h);
            }
            glEnd();
        }
        else if (p.shape == 1)
        {
            DrawStats::Begin(GL_QUADS);
            glVertex2f(p.x, p.y);
            glVertex2f(p.x + p.w, p.y);
            glVertex2f(p.x + p.w, p.y + p.h);
            glVertex2f(p.x, p.y + p.h);
            glEnd();
        }
        else if (p.shape == 2)
        {
            DrawStats::Begin(GL_TRIANGLES);
            glVertex2f(p.x, p.y);
            glVertex2f(p.u, p.v);
            glVertex2f(p.w, p.h);
            glEnd();
        }
        glPopMatrix();
    }
    glPopMatrix();
}
} // namespace SceneModels
