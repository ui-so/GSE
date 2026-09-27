#pragma once
#include "Actor.h"
#include "RenderBatch.h"
#include <gl/GL.h>
namespace SceneRender
{
// Convert an actor's world XY basis to the existing isometric screen basis.
inline void Push(const Scene::Actor &actor, float pivotX, float pivotY, bool interfaceLayer = false)
{
    auto m = actor.GetWorldMatrix();
    float a = (m.a - m.b - m.c + m.d) * .5f;
    float b = (m.a + m.b - m.c - m.d) * .25f;
    float c = m.a - m.b + m.c - m.d;
    float d = (m.a + m.b + m.c + m.d) * .5f;
    if (interfaceLayer)
    {
        a = m.a;
        b = m.b;
        c = m.c;
        d = m.d;
    }
    const GLfloat matrix[16] = {a, b, 0, 0, c, d, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    RenderBatch::PushMatrix();
    RenderBatch::Translatef(pivotX, pivotY, 0);
    RenderBatch::MultMatrixf(matrix);
    RenderBatch::Translatef(-pivotX, -pivotY, 0);
}
inline void Pop()
{
    RenderBatch::PopMatrix();
}
} // namespace SceneRender
