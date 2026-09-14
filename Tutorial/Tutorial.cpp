#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <windows.h>
#include <gl/GL.h>
#include <map>
#include <queue>
#include <string>
#include <vector>
#include "Visuals.h"

#undef NDEBUG
#include <cassert>
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
struct V
{
    float x, y;
    V operator+(V b) const
    {
        return {x + b.x, y + b.y};
    }
    V operator-(V b) const
    {
        return {x - b.x, y - b.y};
    }
    V operator*(float s) const
    {
        return {x * s, y * s};
    }
};
struct C
{
    float r, g, b, a = 1;
};
float length(V v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}
const C ink{.76f, .8f, .75f}, gold{.88f, .70f, .39f}, teal{.36f, .83f, .79f};
int width = 1280, height = 800, stage = 0;
float elapsed = 0, worldTime = 0;
bool keys[256]{}, paused = false, active = true, running = true, finished = false;
V player{0, 2}, camera{0, 2};
constexpr int NPC_COUNT = 16;
constexpr float MIN_X = -39.5f, MAX_X = 42.5f, MIN_Y = -39.5f, MAX_Y = 34.5f;
int facing = 0;
float actionTime = 0;
bool moving = false, postEnabled = true;
HDC dc;
HGLRC rc;
HFONT font;
HGDIOBJ oldFont;
HWND windowHandle;
std::map<wchar_t, GLuint> glyphs;
std::wstring speaker, dialog;
int pending = -1;
int talkingNpc = -1;
struct Object
{
    V p;
    int type;
    float scale;
    int id;
};
std::vector<Object> objects;
struct NPC
{
    V p;
    const wchar_t *name;
    const wchar_t *line;
    C color;
};
NPC npcs[] = {
    {{-1, 0},
     L"장례지기 · 마라",
     L"떠난 이를 부르는 일과, 돌아오게 하는 일은 다르지. 오늘은 그저 기억하자.",
     {.40f, .48f, .48f}},
    {{3, 2},
     L"빵 굽는 오렌",
     L"빵이 돌처럼 굳었어. 늑대가 오면 던져. 먹이는 건 권하지 않아.",
     {.67f, .43f, .26f}},
    {{-3, 4}, L"우물지기", L"숲의 씨앗이 밤에도 빛난대. 등불 기름값은 좀 내려가려나.", {.37f, .48f, .37f}},
    {{4, -1},
     L"수습 약초사",
     L"살아 있는 건 모두 숨을 쉴까? 씨앗에 귀를 대 봤는데… 아직 모르겠어.",
     {.42f, .53f, .39f}},
    {{-4, -2},
     L"늙은 파수꾼",
     L"동쪽 호수에는 길이 있어. 물속으로 들어가지는 마. 밤에는 깊이를 알 수 없거든.",
     {.43f, .42f, .51f}},
    {{1, 5}, L"여행 상인", L"불멸의 양말! 구멍은 나지만, 영수증은 영원히 남습니다.", {.58f, .39f, .40f}},
    {{-5, 2}, L"마을 아이", L"엄마는 별이 된대. 그런데 흐린 날에는 어디서 쉬는 걸까?", {.47f, .53f, .62f}},
    {{2, -4},
     L"종지기",
     L"종은 돌아오라는 소리가 아니야. 우리가 아직 기억한다는 소리지.",
     {.46f, .44f, .33f}},
    {{-7, 0},
     L"대장장이 단",
     L"칼보다 경첩을 더 많이 고치는 날이 좋은 날이지. 오늘도 좋은 날이야.",
     {.5f, .3f, .2f}},
    {{-3, -5},
     L"묘지기 연",
     L"무덤 곁에 꽃을 심어. 흙 아래의 삶과 흙 위의 삶을 함께 돌보는 거야.",
     {.3f, .4f, .3f}},
    {{6, -3}, L"목수 미르", L"다리를 오래 쓰려면 나무를 잘 알아야 해. 사람도 그럴까?", {.5f, .4f, .3f}},
    {{-7, 5}, L"재봉사 소란", L"망토 수선은 공짜. 하지만 유령에게 빌려준 옷은 직접 찾아와.", {.5f, .3f, .5f}},
    {{4, 8},
     L"양봉가",
     L"벌은 여왕을 잃으면 새 여왕을 키워. 슬픔이 없는 건지, 살아야 해서인지.",
     {.6f, .5f, .2f}},
    {{-2, 8}, L"수습 사제", L"기도문은 외웠는데 위로하는 말은 아직 배우는 중이야.", {.4f, .4f, .5f}},
    {{8, -6}, L"호수의 어부", L"물 위의 빛을 따라가지 마. 물고기도 그 빛을 피하더군.", {.3f, .4f, .5f}},
    {{-8, -7},
     L"숲지기",
     L"숲에는 사슴과 토끼, 멧돼지가 있어. 가까이 가면 놀라니 거리를 두고 지켜봐.",
     {.3f, .5f, .3f}}};
V seed{0, -13}, shrine{10, -5};
void color(C c)
{
    glColor4f(c.r, c.g, c.b, c.a);
}
void poly(std::initializer_list<V> vs, C c)
{
    color(c);
    glBegin(GL_POLYGON);
    for (auto v : vs)
        glVertex2f(v.x, v.y);
    glEnd();
}
void rect(float x, float y, float w, float h, C c)
{
    poly({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, c);
}
void ellipse(V p, float rx, float ry, C c)
{
    color(c);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(p.x, p.y);
    for (int i = 0; i <= 48; i++)
    {
        float a = i * 6.2831853f / 48;
        glVertex2f(p.x + cosf(a) * rx, p.y + sinf(a) * ry);
    }
    glEnd();
}
void line(V a, V b, C c, float w = 1)
{
    color(c);
    glLineWidth(w);
    glBegin(GL_LINES);
    glVertex2f(a.x, a.y);
    glVertex2f(b.x, b.y);
    glEnd();
    glLineWidth(1);
}
V project(V p, float z = 0)
{
    V d = p - camera;
    return {width * .5f + (d.x - d.y) * 31, height * .53f + (d.x + d.y) * 15.5f - z};
}
void text(float x, float y, const std::wstring &s, C c = ink)
{
    color(c);
    glRasterPos2f(x, y);
    for (wchar_t ch : s)
    {
        if (!glyphs.count(ch))
        {
            GLuint id = glGenLists(1);
            if (!wglUseFontBitmapsW(dc, ch, 1, id))
            {
                glDeleteLists(id, 1);
                continue;
            }
            glyphs[ch] = id;
        }
        glCallList(glyphs[ch]);
    }
}
void wrapped(float x, float y, const std::wstring &s, int columns = 58)
{
    int row = 0;
    for (size_t i = 0; i < s.size(); i += columns)
        text(x, y + row++ * 28, s.substr(i, columns));
}
void glow(V p, float r, C c)
{
    for (int i = 8; i >= 1; i--)
    {
        C a = c;
        a.a = .018f * (9 - i);
        ellipse(p, r * i / 8, r * i / 8 * .65f, a);
    }
}
void tile(int x, int y, C c)
{
    poly({project({float(x), float(y)}), project({float(x + 1), float(y)}),
          project({float(x + 1), float(y + 1)}), project({float(x), float(y + 1)})},
         c);
}
float hash(int x, int y)
{
    unsigned n = unsigned(x) * 374761393u + unsigned(y) * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (n & 65535) / 65535.f;
}
bool lake(V p)
{
    float x = (p.x - 16) / 5.5f, y = (p.y + 7) / 8;
    return x * x + y * y < 1;
}
bool blocked(V p)
{
    if (p.x < MIN_X || p.x > MAX_X || p.y < MIN_Y || p.y > MAX_Y || lake(p))
        return true;
    for (auto &o : objects)
    {
        if (o.type == 1 && fabsf(p.x - o.p.x) < 1.55f && fabsf(p.y - o.p.y) < 1.45f)
            return true;
        if (o.type == 0 && length(p - o.p) < .45f)
            return true;
    }
    return false;
}
struct Beast
{
    V p, home, velocity;
    int kind;
    float phase;
    bool fleeing = false;
};
std::vector<Beast> beasts;
void initWildlife()
{
    beasts.clear();
    for (int i = 0; i < 15; i++)
    {
        V p{i < 6    ? -12.f + float(i % 3) * 2.3f
            : i < 10 ? 8.f + float(i % 3) * 2
                     : -23.f + float(i % 4) * 2.5f,
            i < 6    ? -10.f - float(i / 3) * 3
            : i < 10 ? 10.f + float(i % 2) * 3
                     : 18.f + float(i % 3) * 3};
        for (int k = 0; k < 50 && blocked(p); k++)
            p = p + V{.35f, .27f};
        beasts.push_back({p, p, {0, 0}, i % 3, float(i), false});
    }
}
void updateWildlife(float dt)
{
    for (size_t i = 0; i < beasts.size(); i++)
    {
        auto &a = beasts[i];
        a.phase += dt;
        a.fleeing = length(a.p - player) < 3.6f;
        V direction;
        if (a.fleeing)
            direction = a.p - player;
        else if (length(a.p - a.home) > 4)
            direction = a.home - a.p;
        else
            direction = {sinf(a.phase * .33f + float(i)), cosf(a.phase * .27f + float(i) * 2)};
        float speed = a.fleeing ? (a.kind == 1 ? 3.7f : 2.7f) : (.35f + .13f * a.kind);
        if (length(direction) > .01f)
            direction = direction * (speed / length(direction));
        a.velocity = {0, 0};
        V q = a.p + V{direction.x * dt, 0};
        if (!blocked(q))
        {
            a.velocity.x = direction.x;
            a.p = q;
        }
        q = a.p + V{0, direction.y * dt};
        if (!blocked(q))
        {
            a.velocity.y = direction.y;
            a.p = q;
        }
    }
}
void fire(V p, float size)
{
    Visuals::SoftShadow(p.x, p.y + 4, 23 * size, .42f, .7f);
    glow(p + V{0, -10 * size}, 70 * size, gold);
    line(p + V{-12 * size, 3}, p + V{12 * size, -3}, {.24f, .16f, .09f}, 5);
    line(p + V{-10 * size, -3}, p + V{10 * size, 3}, {.35f, .21f, .11f}, 5);
    for (int j = 0; j < 7; j++)
    {
        float life = fmodf(worldTime * 1.7f + j * .147f, 1.f);
        float x = sinf(worldTime * 5 + j * 2) * 5 * size;
        ellipse(p + V{x, -life * 36 * size}, (1 - life) * 7 * size + 1, (1 - life) * 12 * size + 1,
                {1.f, .27f + life * .45f, .06f, .7f * (1 - life)});
    }
    for (int j = 0; j < 5; j++)
    {
        float life = fmodf(worldTime * .7f + j * .21f, 1.f);
        ellipse(p + V{sinf(life * 10 + j) * 12 * size, -25 * size - life * 45 * size}, 1, 2,
                {1, .67f, .22f, 1 - life});
    }
}
void animal(V p, const Beast &a)
{
    float d = a.velocity.x - a.velocity.y < 0 ? -1.f : 1.f;
    float gait = sinf(a.phase * (a.fleeing ? 18 : 7)) * 4;
    C coat = a.kind == 0 ? C{.48f, .31f, .18f} : a.kind == 1 ? C{.62f, .59f, .48f} : C{.26f, .24f, .22f};
    float scale = a.kind == 1 ? .7f : 1.f;
    Visuals::SoftShadow(p.x + 7, p.y + 2, 26 * scale, .35f, .8f);
    for (int j = 0; j < 4; j++)
    {
        float x = (j - 1.5f) * 7 * scale;
        line(p + V{x, -14 * scale},
             p + V{x + sinf(a.phase * 9 + j * 3) * 3, powf(-1.f, float(j)) * gait * .2f}, {.17f, .15f, .13f},
             3 * scale);
    }
    ellipse(p + V{0, -16 * scale}, 18 * scale, 10 * scale, coat);
    ellipse(p + V{-3, -20 * scale}, 12 * scale, 6 * scale, {coat.r * 1.18f, coat.g * 1.18f, coat.b * 1.18f});
    V head = p + V{d * 17 * scale, -25 * scale};
    if (a.kind == 0)
    {
        line(head + V{-d * 3, 2}, p + V{d * 10, -18}, coat, 8);
        line(head, head + V{-d * 4, -19}, {.52f, .45f, .33f}, 2);
        line(head + V{-d * 2, -10}, head + V{d * 5, -17}, {.52f, .45f, .33f}, 2);
    }
    else if (a.kind == 1)
    {
        ellipse(head + V{-3, -10}, 3, 12, coat);
        ellipse(head + V{3, -9}, 3, 11, coat);
    }
    else
    {
        poly({head + V{-6, 0}, head + V{-5, -12}, head + V{1, -4}}, coat);
    }
    ellipse(head, 8 * scale, 6 * scale, coat);
    ellipse(head + V{d * 4, -2}, 1.2f, 1.2f, {.04f, .04f, .025f});
    if (a.kind == 2)
        line(head + V{d * 6, 4}, head + V{d * 10, 0}, {.8f, .75f, .61f}, 2);
}
void initWorld()
{
    objects.clear();
    V houses[] = {{-6, -4}, {0, -6}, {5, 5}, {-6, 7}, {7, 1}};
    for (int i = 0; i < 5; i++)
        objects.push_back({houses[i], 1, 1, i});
    for (int x = -38; x <= 41; x += 2)
        for (int y = -38; y <= 33; y += 2)
        {
            V p{float(x) + hash(x, y), float(y) + hash(y, x)};
            if ((p.y < -8 || p.x < -9 || p.y > 10 || p.x > 10) && !lake(p) && fabsf(p.x) > 1.8f &&
                !(p.x > 1 && p.y > -7 && p.y < -3) && length(p - shrine) > 2 && hash(x, y) > .42f &&
                fabsf(p.y - 18) > 2 && fabsf(p.x + 20) > 2)
                objects.push_back({p, 0, .75f + hash(y, x) * .65f, 0});
        }
    for (int i = 0; i < NPC_COUNT; i++)
        objects.push_back({npcs[i].p, 2, 1, i});
    objects.push_back({{-2, 2}, 3, 1, 0});
    objects.push_back({shrine, 4, 1, 0});
    objects.push_back({seed, 5, 1, 0});
    objects.push_back({{2, 0}, 7, 1, 0});
    objects.push_back({{-5, 5}, 7, .75f, 1});
    objects.push_back({{-20, 18}, 7, 1, 2});
    initWildlife();
}
GLuint treeList = 0;
void treeGeometry(V p, float s)
{

    rect(p.x - 3 * s, p.y - 36 * s, 6 * s, 36 * s, {.16f, .15f, .12f});
    for (int i = 0; i < 3; i++)
    {
        float y = p.y - 19 * s - i * 22 * s, w = (31 - i * 5) * s;
        poly({{p.x, y - 47 * s}, {p.x + w, y}, {p.x, y - 5 * s}, {p.x - w, y}},
             i % 2 ? C{.075f, .17f, .15f} : C{.09f, .21f, .17f});
        for (int k = 0; k < 7; k++)
        {
            float q = float(k) / 7;
            float yy = y - 36 * s + q * 33 * s;
            float ww = w * q;
            line({p.x, yy - 5 * s}, {p.x - ww, yy}, {.12f, .25f, .19f, .8f}, 2);
            line({p.x, yy - 5 * s}, {p.x + ww, yy}, {.06f, .14f, .12f, .8f}, 2);
        }
        line({p.x, y - 43 * s}, {p.x - w * .85f, y - 2 * s}, {.16f, .27f, .22f, .7f});
    }
}
void tree(V p, float s)
{
    if (!treeList)
    {
        treeList = glGenLists(1);
        glNewList(treeList, GL_COMPILE);
        treeGeometry({0, 0}, 1);
        glEndList();
    }
    glPushMatrix();
    glTranslatef(p.x, p.y, 0);
    glScalef(s, s, 1);
    glCallList(treeList);
    glPopMatrix();
}
void house(V p, int id)
{

    poly({{p.x - 65, p.y - 26}, {p.x, p.y + 7}, {p.x, p.y - 66}, {p.x - 65, p.y - 96}}, {.25f, .27f, .25f});
    poly({{p.x, p.y + 7}, {p.x + 65, p.y - 26}, {p.x + 65, p.y - 96}, {p.x, p.y - 66}}, {.18f, .22f, .21f});
    poly({{p.x - 77, p.y - 92}, {p.x - 8, p.y - 142}, {p.x + 76, p.y - 92}, {p.x, p.y - 56}},
         {.26f, .25f, .23f});
    poly({{p.x - 77, p.y - 92}, {p.x - 8, p.y - 142}, {p.x - 18, p.y - 90}, {p.x, p.y - 56}},
         {.36f, .33f, .29f});
    for (int i = 0; i < 4; i++)
        line({p.x - 62 + i * 15.f, p.y - 96 - i * 7.f}, {p.x - 8 + i * 17.f, p.y - 64 - i * 8.f},
             {.12f, .16f, .15f, .65f});
    rect(p.x + 16, p.y - 40, 14, 30, {.10f, .12f, .11f});
    poly({{p.x - 44, p.y - 61}, {p.x - 27, p.y - 52}, {p.x - 27, p.y - 33}, {p.x - 44, p.y - 42}},
         {.85f, .58f, .25f});
    glow({p.x - 35, p.y - 47}, 42, gold);
    line({p.x - 36, p.y - 56}, {p.x - 36, p.y - 38}, {.23f, .20f, .14f}, 2);
    for (int j = 0; j < 4; j++)
    {
        float t = j / 3.f;
        line({p.x - 65 + t * 65, p.y - 96 + t * 30}, {p.x - 65 + t * 65, p.y - 26 + t * 33},
             {.115f, .105f, .08f}, 4);
        line({p.x + t * 65, p.y - 66 - t * 30}, {p.x + t * 65, p.y + 7 - t * 33}, {.095f, .10f, .085f}, 4);
    }
    line({p.x - 64, p.y - 44}, {p.x, p.y - 11}, {.14f, .13f, .105f}, 4);
    line({p.x, p.y - 11}, {p.x + 64, p.y - 44}, {.12f, .12f, .10f}, 4);
    for (int row = 0; row < 6; row++)
        for (int col = 0; col < 7; col++)
        {
            float u = (col + .3f * (row % 2)) / 7.f, v = (row + .2f) / 6.f;
            V a{p.x - 8 + 84 * u - 69 * v - 7 * u * v, p.y - 142 + 50 * u + 50 * v - 14 * u * v};
            line(a, a + V{8, 5}, {.42f, .39f, .30f, .5f}, 2);
        }
    for (int k = 0; k < 3; k++)
        line({p.x + 19 + k * 4.f, p.y - 39}, {p.x + 19 + k * 4.f, p.y - 11}, {.26f, .22f, .16f}, 1);
    ellipse({p.x + 27, p.y - 25}, 1.5f, 1.5f, gold);
    rect(p.x + 28, p.y - 138, 12, 29, {.24f, .25f, .23f});
    for (int row = 0; row < 4; row++)
        line({p.x + 28, p.y - 135 + row * 6.f}, {p.x + 40, p.y - 135 + row * 6.f}, {.13f, .14f, .13f});
    for (int i = 0; i < 3; i++)
    {
        float t = fmodf(worldTime * 7 + i * 17, 55);
        ellipse({p.x + 35 + sinf(t * .04f + id) * 12, p.y - 143 - t}, 7 + t * .12f, 5 + t * .08f,
                {.45f, .51f, .47f, .08f * (1 - t / 60)});
    }
}
void person(V p, C, bool hero = false, int id = 0)
{
    Visuals::SoftShadow(p.x + 6, p.y + 3, 24, .36f, .82f);
    int action = hero ? (actionTime > 0 ? 2 : moving ? 1 : 0) : (!dialog.empty() && talkingNpc == id ? 2 : 0);
    Visuals::Sprite(p.x, p.y, hero ? 0 : 1 + id % 3, hero ? facing : id % 4, action,
                    worldTime * (action == 1 ? 1.6f : .65f) + id * .17f);
    if (hero)
        glow(p + V{14, -16}, 28, gold);
}
V target()
{
    return stage == 0 || stage >= 3 ? npcs[0].p : stage == 1 ? seed : shrine;
}
const wchar_t *objective()
{
    const wchar_t *s[] = {L"마라와 이야기하기", L"숲에서 숨빛 씨앗 찾기", L"호숫가 추모석에 씨앗 심기",
                          L"마라에게 돌아가기", L"퀘스트 완료 · 남겨진 빛"};
    return s[stage];
}
void openDialog(const wchar_t *name, const wchar_t *msg, int next = -1)
{
    actionTime = 1.2f;
    speaker = name;
    dialog = msg;
    pending = next;
}
int nearest()
{
    float d = 1.8f;
    int n = -1;
    for (int i = 0; i < NPC_COUNT; i++)
    {
        float dist = length(player - npcs[i].p);
        if (dist < d)
        {
            d = dist;
            n = i;
        }
    }
    if (length(player - seed) < 1.8f && length(player - seed) < d)
        n = NPC_COUNT;
    if (length(player - shrine) < 1.8f && length(player - shrine) < d)
        n = NPC_COUNT + 1;
    return n;
}
void interact()
{
    if (!dialog.empty())
    {
        dialog.clear();
        if (pending >= 0)
        {
            stage = pending;
            if (stage == 4)
                finished = true;
        }
        pending = -1;
        return;
    }
    int n = nearest();
    talkingNpc = (n >= 0 && n < NPC_COUNT) ? n : -1;
    if (n == 0)
    {
        if (stage == 0)
            openDialog(L"마라 · 장례지기",
                       L"오늘은 돌아오지 못한 이들을 기억하는 날이야. 북쪽 숲의 숨빛 씨앗 하나를 동쪽 호수의 "
                       L"추모석에 "
                       L"심어 주겠니? 죽은 나무 곁에서도 새싹은 자란단다.",
                       1);
        else if (stage == 3)
            openDialog(
                L"마라 · 남겨진 빛",
                L"네가 심은 씨앗이 빛났다고? 떠난 이가 돌아온 건 아닐 거야. 그래도 무언가 이어진 거겠지. "
                L"고맙다. 자, 빵을 가져가렴. 오렌이 만든 거라… 이가 튼튼하면 좋겠구나.",
                4);
        else
            openDialog(npcs[0].name, npcs[0].line);
    }
    else if (n >= 1 && n < NPC_COUNT)
        openDialog(npcs[n].name, npcs[n].line);
    else if (n == NPC_COUNT)
    {
        if (stage == 1)
            openDialog(
                L"숨빛 씨앗",
                L"마른 뿌리 사이에서 작은 빛이 맥박친다. 따뜻하다. 죽은 나무가 남긴 것일까, 어딘가에서 온 "
                L"것일까? 씨앗을 조심스럽게 품에 넣었다.",
                2);
        else
            openDialog(L"오래된 뿌리", stage == 0
                                           ? L"빛나는 씨앗이 보인다. 마을 사람에게 이 나무에 대해 물어보자."
                                           : L"빛이 떠난 자리에도 가느다란 새 뿌리가 남아 있다.");
    }
    else if (n == NPC_COUNT + 1)
    {
        if (stage == 2)
            openDialog(
                L"이름 없는 추모석",
                L"씨앗을 심자 잔잔한 물결이 바깥이 아닌 안쪽으로 흐른다. 낯선 숨소리. 잠깐, 호수 아래에서 "
                L"무언가가 너를 바라본 것 같다. 마라에게 돌아가자.",
                3);
        else
            openDialog(L"이름 없는 추모석",
                       stage >= 3 ? L"작은 새싹이 흔들린다. 물 아래의 기척은 사라졌다. 답은 아직 없다."
                                  : L"이름이 닳아 사라진 돌. 작은 홈에는 무언가를 심었던 흔적이 있다.");
    }
}
void update(float dt)
{
    if (!active || paused)
        return;
    worldTime += dt;
    actionTime = std::max(0.f, actionTime - dt);
    updateWildlife(dt);
    moving = false;
    if (!finished)
        elapsed += dt;
    if (!dialog.empty())
        return;
    V d{float(keys['D'] - keys['A']), float(keys['S'] - keys['W'])};
    if (length(d) > 0)
    {
        moving = true;
        facing = fabsf(d.x) > fabsf(d.y) ? (d.x < 0 ? 1 : 2) : (d.y < 0 ? 3 : 0);
        d = d * (1 / length(d));
        V move{(d.x + d.y) * .70710678f, (d.y - d.x) * .70710678f};
        move = move * (dt * (keys[VK_SHIFT] ? 5.0f : 3.1f));
        V q = player + V{move.x, 0};
        if (!blocked(q))
            player = q;
        q = player + V{0, move.y};
        if (!blocked(q))
            player = q;
    }
    camera = camera + (player - camera) * std::min(1.f, dt * 5);
}
GLuint terrainList = 0;
V terrainOrigin{0, 0};
void drawWorld()
{
    // Static terrain batches retain world-space geometry across frames and window resizes.
    if (!terrainList)
    {
        terrainList = glGenLists(2);
        V oldCamera = camera;
        camera = {0, 0};
        terrainOrigin = {width * .5f, height * .53f};
        for (int pass = 0; pass < 2; pass++)
        {
            glNewList(terrainList + pass, GL_COMPILE);
            for (int x = -41; x < 44; x++)
                for (int y = -41; y < 36; y++)
                {
                    bool water = lake({x + .5f, y + .5f});
                    if (water != (pass == 1))
                        continue;
                    bool village = abs(x) < 8 && abs(y) < 8;
                    bool road = abs(x) < 2 || (x >= 0 && x < 14 && abs(y + 5) < 2) || abs(y - 18) < 2 ||
                                abs(x + 20) < 2;
                    int material = water ? 3 : village ? 1 : road ? 2 : 0;
                    V a = project({float(x), float(y)}), b = project({float(x + 1), float(y)}),
                      c = project({float(x + 1), float(y + 1)}), d = project({float(x), float(y + 1)});
                    float xy[] = {a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y};
                    Visuals::MaterialQuad(material, xy, .92f + hash(x / 3, y / 3) * .10f, 0);
                }
            glEndList();
        }
        camera = oldCamera;
    }
    glPushMatrix();
    glTranslatef(width * .5f - terrainOrigin.x - (camera.x - camera.y) * 31,
                 height * .53f - terrainOrigin.y - (camera.x + camera.y) * 15.5f, 0);
    glCallList(terrainList);
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();
    glTranslatef(worldTime * .024f, worldTime * .012f, 0);
    glMatrixMode(GL_MODELVIEW);
    glCallList(terrainList + 1);
    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    for (int i = 0; i < 90; i++)
    {
        float a = i * 6.2831853f / 90;
        V q{16 + cosf(a) * 5.65f, -7 + sinf(a) * 8.15f};
        V p = project(q);
        ellipse(p, 6 + hash(i, 4) * 5, 3, {.37f, .40f, .29f});
        if (i % 3 == 0)
        {
            for (int j = 0; j < 3; j++)
                line(p + V{float(j * 3), 0},
                     p + V{float(j * 3) + sinf(worldTime + i) * 2, -14 - float(j % 2) * 8},
                     {.43f, .43f, .22f}, 2);
        }
    }
    for (int i = 0; i < 5; i++)
    {
        V p = project({15.f + float(i % 2) * 2, -10.f + float(i) * 2});
        float t = fmodf(worldTime * .15f + i * .2f, 1);
        color({.46f, .72f, .73f, (1 - t) * .14f});
        glBegin(GL_LINE_LOOP);
        for (int j = 0; j < 48; j++)
        {
            float a = j * 6.2831853f / 48;
            glVertex2f(p.x + cosf(a) * (10 + t * 50), p.y + sinf(a) * (5 + t * 20));
        }
        glEnd();
    }
    for (int i = 0; i < 32; i++)
    {
        float x = 12 + hash(i, 2) * 8, y = -13 + hash(i, 3) * 12;
        if (lake({x, y}))
        {
            V p = project({x, y});
            float w = 9 + sinf(worldTime + i) * 5;
            line(p + V{-w, 0}, p + V{w, 0}, {.37f, .63f, .62f, .18f});
        }
    }
    // Village cobbles and lantern pools.
    for (int i = 0; i < 120; i++)
    {
        V q{hash(i, 9) * 12 - 6, hash(i, 8) * 12 - 6};
        if (!blocked(q))
        {
            V p = project(q);
            ellipse(p, 4 + hash(i, 3) * 3, 2, {.47f, .46f, .36f, .13f});
        }
    }
    for (auto &o : objects)
    {
        if (o.type != 0 && o.type != 1)
            continue;
        V p = project(o.p);
        if (p.x < -200 || p.x > width + 200 || p.y < -150 || p.y > height + 100)
            continue;
        float r = o.type == 1 ? 70.f : 28.f * o.scale;
        // Ground-projected caster silhouettes with layered penumbra, all before geometry.
        for (int layer = 3; layer >= 0; layer--)
        {
            float e = float(layer) * 2;
            float spread = o.type == 1 ? 48.f : 13.f * o.scale;
            poly({p + V{-spread - e, -2}, p + V{spread + e, 2}, p + V{40 + spread * .4f + e, 24 + e},
                  p + V{40 - spread * .4f - e, 24 + e}},
                 {.025f, .04f, .045f, .045f});
        }
        Visuals::SoftShadow(p.x + 25, p.y + 14, r, .48f, .7f);
        Visuals::SoftShadow(p.x, p.y, r * .6f, .35f, .8f);
    }
    std::vector<Object> sorted = objects;
    for (size_t i = 0; i < beasts.size(); i++)
        sorted.push_back({beasts[i].p, 8, 1, int(i)});
    sorted.push_back({player, 6, 1, 0});
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](Object a, Object b) { return a.p.x + a.p.y < b.p.x + b.p.y; });
    for (auto &o : sorted)
    {
        V p = project(o.p);
        if (p.x < -150 || p.x > width + 150 || p.y < -40 || p.y > height + 200)
            continue;
        switch (o.type)
        {
        case 0: {
            float a = length(o.p - player);
            if (a < 3 && o.p.x + o.p.y > player.x + player.y)
            {
                glEnable(GL_POLYGON_STIPPLE);
                GLubyte mask[128];
                for (int i = 0; i < 128; i++)
                    mask[i] = (i / 4) % 2 ? 0xAA : 0x55;
                glPolygonStipple(mask);
            }
            tree(p, o.scale);
            glDisable(GL_POLYGON_STIPPLE);
            break;
        }
        case 1:
            house(p, o.id);
            break;
        case 2:
            person(p, npcs[o.id].color, false, o.id);
            break;
        case 3:
            ellipse(p, 22, 11, {.38f, .40f, .34f});
            ellipse(p + V{0, -8}, 22, 11, {.48f, .47f, .38f});
            ellipse(p + V{0, -9}, 14, 6, {.045f, .075f, .07f});
            break;
        case 4:
            poly({{p.x - 15, p.y}, {p.x - 12, p.y - 34}, {p.x + 8, p.y - 40}, {p.x + 15, p.y - 5}},
                 {.40f, .47f, .43f});
            line(p + V{-2, -30}, p + V{-2, -10}, {.62f, .68f, .54f}, 2);
            if (stage >= 3)
            {
                glow(p + V{0, -15}, 100, teal);
                line(p, p + V{0, -22}, teal, 3);
                ellipse(p + V{6, -16}, 7, 3, teal);
            }
            break;
        case 5:
            rect(p.x - 8, p.y - 15, 16, 15, {.23f, .20f, .15f});
            if (stage < 2)
            {
                glow(p + V{0, -19}, 65, teal);
                ellipse(p + V{0, -19 + sinf(worldTime * 2) * 3}, 5, 7, teal);
            }
            break;
        case 7:
            fire(p, o.scale);
            break;
        case 8:
            animal(p, beasts[o.id]);
            break;
        case 6:
            person(p, {.3f, .5f, .6f}, true);
            break;
        }
    }
    for (int i = 0; i < 24; i++)
    {
        V q{hash(i, 19) * 34 - 13, hash(i, 21) * 30 - 18};
        V p = project(q, 15 + sinf(worldTime + i) * 8);
        ellipse(p, 1.5f, 1.5f, {.75f, .8f, .5f, .3f + .25f * sinf(worldTime + i)});
    }
    // Thin drifting mist preserves scene readability.
    for (int i = 0; i < 5; i++)
        ellipse({fmodf(worldTime * 8 + i * 330, width + 500.f) - 250, height * .55f + i * 65.f}, 280, 23,
                {.49f, .64f, .59f, .022f});
    if (stage < 4)
    {
        V p = project(target(), 55);
        float b = sinf(worldTime * 3) * 3;
        poly({{p.x - 6, p.y + b}, {p.x + 6, p.y + b}, {p.x, p.y + 8 + b}}, gold);
    }
}
void minimap()
{
    float x = width - 212.f, y = 32;
    rect(x, y, 184, 166, {.035f, .07f, .067f, .92f});
    text(x + 14, y + 23, L"주변 지도", gold);
    auto m = [&](V p) { return V{x + 90 + p.x * 1.8f, y + 90 + p.y * 1.45f}; };
    ellipse(m({16, -7}), 10, 12, {.15f, .35f, .38f});
    for (auto &o : objects)
        if (o.type == 1)
        {
            V p = m(o.p);
            rect(p.x - 3, p.y - 3, 6, 6, {.55f, .48f, .34f});
        }
    if (stage < 4)
        ellipse(m(target()), 3, 3, gold);
    ellipse(m(player), 3, 3, teal);
    text(x + 11, y + 154, L"● 나   ·   ◆ 목적지");
}
void draw()
{
    glViewport(0, 0, width, height);
    glClearColor(.035f, .065f, .065f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    drawWorld();
    Visuals::PostProcess(width, height, worldTime, postEnabled);
    rect(0, 0, float(width), 6, {.67f, .51f, .28f});
    rect(24, 26, 480, 111, {.025f, .045f, .043f, .92f});
    text(43, 54, L"잿빛 여울  ·  남겨진 빛", gold);
    text(43, 84, objective());
    int sec = int(elapsed);
    wchar_t t[100];
    swprintf_s(t, L"튜토리얼 · %d / 4     %02d:%02d  |  예상 3–5분", std::min(stage, 4), sec / 60, sec % 60);
    text(43, 115, t, {.49f, .61f, .56f});
    minimap();
    rect(24, height - 62.f, width - 48.f, 40, {.025f, .045f, .043f, .92f});
    text(42, height - 36.f,
         L"W A S D  이동     Shift  빠르게 걷기     E  대화 / 조사     Esc  일시정지   F2  후처리", ink);
    if (stage < 4)
    {
        V d = target() - player;
        float dist = length(d);
        text(42, 164, std::wstring(L"◆ ") + objective() + L"  ·  " + std::to_wstring(int(dist)) + L" m",
             gold);
    }
    for (auto &a : beasts)
        if (length(a.p - player) < 4 && dialog.empty() && !paused)
        {
            text(42, 192,
                 std::wstring(a.kind == 0   ? L"사슴"
                              : a.kind == 1 ? L"산토끼"
                                            : L"멧돼지") +
                     (a.fleeing ? L" · 놀라 달아나고 있습니다" : L" · 야생동물을 조용히 관찰하세요"),
                 ink);
            break;
        }
    if (nearest() >= 0 && dialog.empty() && !paused)
    {
        V p = project(player, 64);
        rect(p.x - 89, p.y - 24, 178, 31, {.025f, .045f, .043f, .95f});
        text(p.x - 76, p.y - 3, L"[ E ] 대화 / 조사", gold);
    }
    if (!dialog.empty())
    {
        float x = width * .12f, y = height - 240.f;
        rect(x, y, width * .76f, 157, {.025f, .043f, .041f, .98f});
        rect(x, y, 3, 157, gold);
        text(x + 23, y + 30, speaker, gold);
        wrapped(x + 23, y + 63, dialog, std::max(20, int((width * .76f - 46) / 18)));
        text(x + 23, y + 140, L"[ E ] 계속", teal);
    }
    if (finished && dialog.empty())
    {
        rect(width * .5f - 240, 185, 480, 100, {.025f, .045f, .043f, .94f});
        text(width * .5f - 212, 217, L"남겨진 빛 · 퀘스트 완료", gold);
        text(width * .5f - 212, 246, L"답은 아직 없지만, 작은 생명은 남았다.");
        text(width * .5f - 212, 270, L"자유롭게 둘러보거나 R 키로 다시 시작하세요.");
    }
    if (paused)
    {
        rect(0, 0, float(width), float(height), {0, 0, 0, .65f});
        text(width * .5f - 85, height * .5f - 30, L"잠시 쉬어가기", gold);
        text(width * .5f - 160, height * .5f + 7, L"Esc 계속   ·   R 다시 시작   ·   Q 종료");
    }
}
void reset()
{
    initWildlife();
    moving = false;
    actionTime = 0;
    facing = 0;
    stage = 0;
    elapsed = 0;
    player = {0, 2};
    camera = player;
    finished = false;
    paused = false;
    dialog.clear();
    pending = -1;
    std::fill(keys, keys + 256, false);
}
void capture(const char *path)
{
    std::vector<unsigned char> pixels(width * height * 4);
    glReadPixels(0, 0, width, height, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels.data());
    BITMAPFILEHEADER f{};
    BITMAPINFOHEADER b{};
    f.bfType = 0x4D42;
    f.bfOffBits = sizeof(f) + sizeof(b);
    f.bfSize = f.bfOffBits + DWORD(pixels.size());
    b.biSize = sizeof(b);
    b.biWidth = width;
    b.biHeight = height;
    b.biPlanes = 1;
    b.biBitCount = 32;
    b.biSizeImage = DWORD(pixels.size());
    std::ofstream out(path, std::ios::binary);
    out.write((char *)&f, sizeof(f));
    out.write((char *)&b, sizeof(b));
    out.write((char *)pixels.data(), pixels.size());
}
LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_CLOSE:
        running = false;
        return 0;
    case WM_SIZE:
        width = std::max(640, int(LOWORD(l)));
        height = std::max(480, int(HIWORD(l)));
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)l)->ptMinTrackSize = {1000, 720};
        return 0;
    case WM_ACTIVATE:
        active = LOWORD(w) != WA_INACTIVE;
        if (!active)
            std::fill(keys, keys + 256, false);
        return 0;
    case WM_KEYUP:
        if (w < 256)
            keys[w] = false;
        return 0;
    case WM_KEYDOWN:
        if (w < 256)
            keys[w] = true;
        if (l & (1 << 30))
            return 0;
        if (w == VK_ESCAPE)
        {
            paused = !paused;
            std::fill(keys, keys + 256, false);
        }
        else if (w == VK_F2)
            postEnabled = !postEnabled;
        else if (w == 'E' && !paused)
            interact();
        else if (w == 'R' && (paused || finished))
            reset();
        else if (w == 'Q' && paused)
            running = false;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
// Grid search uses the actual collision query; validates all quest sites are reachable.
float routeDistance(V start, V goal)
{
    const int nx = 165, ny = 149;
    auto point = [](int x, int y) { return V{MIN_X + x * .5f, MIN_Y + y * .5f}; };
    int sx = int(std::round((start.x - MIN_X) * 2)), sy = int(std::round((start.y - MIN_Y) * 2));
    std::vector<float> distances(nx * ny, -1);
    std::queue<int> queue;
    queue.push(sy * nx + sx);
    distances[sy * nx + sx] = 0;
    while (!queue.empty())
    {
        int cell = queue.front();
        queue.pop();
        int x = cell % nx, y = cell / nx;
        if (length(point(x, y) - goal) < 1.5f)
            return distances[cell];
        const int dx[] = {1, -1, 0, 0}, dy[] = {0, 0, 1, -1};
        for (int i = 0; i < 4; i++)
        {
            int xx = x + dx[i], yy = y + dy[i];
            if (xx < 0 || xx >= nx || yy < 0 || yy >= ny)
                continue;
            int next = yy * nx + xx;
            if (distances[next] >= 0 || blocked(point(xx, yy)))
                continue;
            distances[next] = distances[cell] + .5f;
            queue.push(next);
        }
    }
    return -1;
}
int tests()
{
    initWorld();
    static_assert(sizeof(npcs) / sizeof(npcs[0]) == 16, "NPC count must be 16");
    assert((MAX_X - MIN_X) * (MAX_Y - MIN_Y) == 4 * 41 * 37);
    assert(beasts.size() == 15);
    for (int i = 0; i < NPC_COUNT; i++)
    {
        assert(!blocked(npcs[i].p));
        assert(routeDistance(player, npcs[i].p) >= 0);
        player = npcs[i].p;
        assert(nearest() == i);
    }
    reset();
    V first = beasts[0].p;
    for (int i = 0; i < 300; i++)
        updateWildlife(.02f);
    assert(length(beasts[0].p - first) > .01f);
    for (auto &a : beasts)
        assert(!blocked(a.p));
    player = beasts[0].p + V{1, 0};
    updateWildlife(.02f);
    assert(beasts[0].fleeing);
    reset();
    float route = 0;
    V stops[] = {player, npcs[0].p, seed, shrine, npcs[0].p};
    for (int i = 0; i < 4; i++)
    {
        float d = routeDistance(stops[i], stops[i + 1]);
        assert(d >= 0);
        route += d;
    }
    assert(route / 3.1f + 180 < 300);
    std::ofstream report("self-test-report.txt");
    report << "Collision-aware route distance: " << route
           << " m\nWalking plus 180-second reading/exploration allowance: " << route / 3.1f + 180
           << " seconds\n";
    assert(!blocked(shrine));
    assert(!blocked(seed));
    assert(!blocked(player));
    assert(blocked({16, -7}));
    player = npcs[0].p;
    interact();
    assert(stage == 0 && !dialog.empty());
    interact();
    assert(stage == 1);
    player = shrine;
    interact();
    interact();
    assert(stage == 1);
    player = seed;
    interact();
    interact();
    assert(stage == 2);
    player = shrine;
    interact();
    interact();
    assert(stage == 3);
    player = npcs[0].p;
    interact();
    interact();
    assert(stage == 4 && finished);
    reset();
    assert(stage == 0 && !finished && dialog.empty());
    float e = elapsed;
    paused = true;
    update(1);
    assert(elapsed == e);
    paused = false;
    keys['W'] = true;
    V before = player;
    update(.1f);
    assert(player.x < before.x && player.y < before.y);
    keys['W'] = false;
    reset();
    keys['W'] = true;
    update(.1f);
    float straight = length(player - V{0, 2});
    reset();
    keys['W'] = keys['D'] = true;
    update(.1f);
    assert(fabsf(length(player - V{0, 2}) - straight) < .001f);
    reset();
    openDialog(L"test", L"test");
    V stationary = player;
    keys['W'] = true;
    update(.1f);
    assert(length(player - stationary) < .001f && elapsed > .09f);
    reset();
    report << "PASS: 16 NPC interactions/reachability, exact 4x playable area, 15 wildlife spawns, "
              "movement/collision/flee behavior.\n";
    report << "PASS: quest order, premature interaction, reachability, collision, pause, movement, diagonal "
              "speed, dialog lock, restart.\n";
    return 0;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR command, int)
{
    if (wcsstr(command, L"--self-test"))
        return tests();
    WNDCLASSW wc{};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"GSETutorial";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    windowHandle =
        CreateWindowW(wc.lpszClassName, L"GSE · 잿빛 여울 — 튜토리얼", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                      CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, nullptr);
    dc = GetDC(windowHandle);
    PIXELFORMATDESCRIPTOR pf{};
    pf.nSize = sizeof(pf);
    pf.nVersion = 1;
    pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pf.iPixelType = PFD_TYPE_RGBA;
    pf.cColorBits = 32;
    pf.cAlphaBits = 8;
    int format = ChoosePixelFormat(dc, &pf);
    if (!format || !SetPixelFormat(dc, format, &pf))
        return 1;
    rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc))
        return 2;
    font = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, HANGUL_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
    oldFont = SelectObject(dc, font);
    Visuals::Initialize();
    initWorld();
    bool shot = wcsstr(command, L"--capture") != nullptr;
    ShowWindow(windowHandle, shot ? SW_HIDE : SW_SHOW);
    if (wcsstr(command, L"--no-post"))
        postEnabled = false;
    bool bench = wcsstr(command, L"--benchmark") != nullptr;
    int benchFrames = 0;
    double benchTotal = 0;
    if (bench)
        ShowWindow(windowHandle, SW_HIDE);
    auto last = std::chrono::steady_clock::now();
    while (running)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        auto now = std::chrono::steady_clock::now();
        float dt = std::min(.05f, std::chrono::duration<float>(now - last).count());
        last = now;
        update(dt);
        draw();
        if (shot)
        {
            std::ofstream renderReport("render-report.txt");
            renderReport << "GL: " << glGetString(GL_VERSION) << "\nRenderer: " << glGetString(GL_RENDERER)
                         << "\nPost shader: " << Visuals::PostAvailable() << "\n";
            capture("tutorial-village.bmp");
            Visuals::SaveAtlas("sprite-atlas.bmp");
            postEnabled = false;
            draw();
            capture("tutorial-post-off.bmp");
            postEnabled = true;
            moving = true;
            worldTime = .08f;
            draw();
            capture("tutorial-walk-a.bmp");
            worldTime = .4f;
            draw();
            capture("tutorial-walk-b.bmp");
            moving = false;
            player = {-11, -10};
            camera = player;
            worldTime = 2;
            draw();
            capture("tutorial-wildlife.bmp");
            player = shrine + V{-1, 0};
            camera = {10, -5};
            stage = 3;
            draw();
            capture("tutorial-lake.bmp");
            reset();
            player = npcs[0].p + V{0, 1};
            camera = player;
            interact();
            draw();
            capture("tutorial-dialog.bmp");
            width = 1000;
            height = 720;
            draw();
            capture("tutorial-small.bmp");
            running = false;
        }
        if (bench)
        {
            glFinish();
            auto end = std::chrono::steady_clock::now();
            if (benchFrames >= 20)
                benchTotal += std::chrono::duration<double, std::milli>(end - now).count();
            if (++benchFrames == 140)
            {
                std::ofstream o("benchmark-report.txt");
                o << "120 measured frames (20 warmup), 1280x800, scene + post + UI: " << benchTotal / 120
                  << " ms/frame; GPU=" << glGetString(GL_RENDERER) << "\n";
                running = false;
            }
        }
        SwapBuffers(dc);
        Sleep(8);
    }
    if (treeList)
        glDeleteLists(treeList, 1);
    if (terrainList)
        glDeleteLists(terrainList, 2);
    Visuals::Shutdown();
    for (auto g : glyphs)
        glDeleteLists(g.second, 1);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(rc);
    ReleaseDC(windowHandle, dc);
    DestroyWindow(windowHandle);
    return 0;
}
