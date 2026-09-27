#include "Actor.h"
#include "SceneGraph.h"
#include <cmath>
#include <utility>
namespace Scene
{
Position Matrix::Apply(Position p) const
{
    return {a * p.x + c * p.y + x, b * p.x + d * p.y + y};
}
Matrix Matrix::operator*(const Matrix &m) const
{
    return {a * m.a + c * m.b, b * m.a + d * m.b,     a * m.c + c * m.d,
            b * m.c + d * m.d, a * m.x + c * m.y + x, b * m.x + d * m.y + y};
}
bool Matrix::Inverse(Matrix &m) const
{
    float determinant = a * d - b * c;
    if (std::abs(determinant) < .000001f)
        return false;
    float r = 1 / determinant;
    m = {d * r, -b * r, -c * r, a * r, (c * y - d * x) * r, (b * x - a * y) * r};
    return true;
}
Actor::Actor(SceneGraph &graph, ActorId id, std::string name, Kind kind)
    : graph_(&graph), id_(id), name_(std::move(name)), kind_(kind)
{
}
ActorId Actor::GetId() const
{
    return id_;
}
ActorId Actor::GetParent() const
{
    return parent_;
}
const std::vector<ActorId> &Actor::GetChildren() const
{
    return children_;
}
const std::string &Actor::GetName() const
{
    return name_;
}
Kind Actor::GetKind() const
{
    return kind_;
}
int Actor::GetDataIndex() const
{
    return dataIndex_;
}
void Actor::SetDataIndex(int index)
{
    dataIndex_ = index;
}
Layer Actor::GetLayer() const
{
    return layer_;
}
void Actor::SetLayer(Layer layer)
{
    graph_->Touch();
    layer_ = layer;
}
void Actor::SetLocalPosition(Position p)
{
    graph_->Touch();
    local_.x = p.x;
    local_.y = p.y;
}
Position Actor::GetLocalPosition() const
{
    return {local_.x, local_.y};
}
Matrix Actor::GetWorldMatrix() const
{
    const Actor *parent = graph_->Find(parent_);
    if (cachedRevision_ != graph_->revision_)
    {
        cachedWorld_ = parent ? parent->GetWorldMatrix() * local_ : local_;
        cachedRevision_ = graph_->revision_;
    }
    return cachedWorld_;
}
Position Actor::GetWorldPosition() const
{
    auto world = GetWorldMatrix();
    return {world.x, world.y};
}
bool Actor::SetWorldPosition(Position p)
{
    const Actor *parent = graph_->Find(parent_);
    if (!parent)
    {
        SetLocalPosition(p);
        return true;
    }
    Matrix inverse;
    if (!parent->GetWorldMatrix().Inverse(inverse))
        return false;
    SetLocalPosition(inverse.Apply(p));
    return true;
}
void Actor::SetLocalMatrix(const Matrix &matrix)
{
    graph_->Touch();
    local_ = matrix;
}
const Matrix &Actor::GetLocalMatrix() const
{
    return local_;
}
void Actor::SetLocalElevation(float height)
{
    graph_->Touch();
    elevation_ = height;
}
float Actor::GetWorldElevation() const
{
    const Actor *parent = graph_->Find(parent_);
    return elevation_ + (parent ? parent->GetWorldElevation() : 0);
}
void Actor::SetEnabled(bool enabled)
{
    graph_->Touch();
    enabled_ = enabled;
}
void Actor::SetVisible(bool visible)
{
    graph_->Touch();
    visible_ = visible;
}
bool Actor::IsEnabled() const
{
    return enabled_;
}
bool Actor::IsVisible() const
{
    return visible_;
}
bool Actor::IsActiveInHierarchy() const
{
    if (!enabled_ || pendingRemoval_)
        return false;
    const Actor *parent = graph_->Find(parent_);
    return !parent || parent->IsActiveInHierarchy();
}
bool Actor::IsVisibleInHierarchy() const
{
    if (!visible_ || !IsActiveInHierarchy())
        return false;
    const Actor *parent = graph_->Find(parent_);
    return !parent || parent->IsVisibleInHierarchy();
}
void Actor::SetUpdate(std::function<void(Actor &, float)> update)
{
    update_ = std::move(update);
}
void Actor::Update(float dt)
{
    if (update_)
        update_(*this, dt);
}
} // namespace Scene
