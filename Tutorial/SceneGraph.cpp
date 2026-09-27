#include "SceneGraph.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace Scene
{
SceneGraph::SceneGraph()
{
    Clear();
}
Actor &SceneGraph::Create(const std::string &name, Kind kind, ActorId parent)
{
    Touch();
    if (parent == InvalidActor)
        parent = root_;
    if (parent != InvalidActor && (!Find(parent) || Find(parent)->pendingRemoval_))
        throw std::invalid_argument("Invalid actor parent");
    ActorId id = nextId_++;
    auto owned = std::unique_ptr<Actor>(new Actor(*this, id, name, kind));
    owned->parent_ = parent;
    Actor &actor = *owned;
    actors_.emplace(id, std::move(owned));
    if (auto node = Find(parent))
        node->children_.push_back(id);
    return actor;
}
Actor *SceneGraph::Find(ActorId id)
{
    auto found = actors_.find(id);
    return found == actors_.end() ? nullptr : found->second.get();
}
const Actor *SceneGraph::Find(ActorId id) const
{
    auto found = actors_.find(id);
    return found == actors_.end() ? nullptr : found->second.get();
}
Actor &SceneGraph::Root()
{
    return *Find(root_);
}
const Actor &SceneGraph::Root() const
{
    return *Find(root_);
}
bool SceneGraph::Reparent(ActorId child, ActorId parent, bool keepWorld)
{
    Touch();
    Actor *node = Find(child);
    Actor *destination = Find(parent);
    if (!node || !destination || child == root_ || node->pendingRemoval_ || destination->pendingRemoval_)
        return false;
    for (auto ancestor = destination; ancestor; ancestor = Find(ancestor->parent_))
        if (ancestor == node)
            return false;
    Matrix local = node->local_;
    float elevation = node->elevation_;
    if (keepWorld)
    {
        Matrix inverse;
        if (!destination->GetWorldMatrix().Inverse(inverse))
            return false;
        local = inverse * node->GetWorldMatrix();
        elevation = node->GetWorldElevation() - destination->GetWorldElevation();
    }
    auto old = Find(node->parent_);
    if (old)
    {
        auto &list = old->children_;
        list.erase(std::remove(list.begin(), list.end(), child), list.end());
    }
    node->parent_ = parent;
    node->local_ = local;
    node->elevation_ = elevation;
    destination->children_.push_back(child);
    return true;
}
void SceneGraph::EraseSubtree(ActorId id)
{
    auto node = Find(id);
    if (!node)
        return;
    auto children = node->children_;
    for (auto child : children)
        EraseSubtree(child);
    auto parent = Find(node->parent_);
    if (parent)
    {
        auto &list = parent->children_;
        list.erase(std::remove(list.begin(), list.end(), id), list.end());
    }
    actors_.erase(id);
}
bool SceneGraph::Remove(ActorId id)
{
    Touch();
    auto node = Find(id);
    if (!node || id == root_ || node->pendingRemoval_)
        return false;
    if (updating_)
    {
        node->pendingRemoval_ = true;
        removals_.push_back(id);
    }
    else
        EraseSubtree(id);
    return true;
}
void SceneGraph::Clear()
{
    Touch();
    if (updating_)
        throw std::logic_error("Clear during scene update is not supported; remove a subtree instead");
    actors_.clear();
    removals_.clear();
    root_ = InvalidActor;
    root_ = Create("Root", Kind::Group).GetId();
}
void SceneGraph::Visit(ActorId id, std::vector<const Actor *> &result) const
{
    auto node = Find(id);
    if (!node)
        return;
    result.push_back(node);
    for (auto child : node->children_)
        Visit(child, result);
}
std::vector<const Actor *> SceneGraph::Traverse() const
{
    std::vector<const Actor *> result;
    result.reserve(actors_.size());
    Visit(root_, result);
    return result;
}
void SceneGraph::Update(float dt)
{
    Profiler::Scope scope("scene_update");
    if (updating_)
        throw std::logic_error("Nested scene update");
    std::vector<ActorId> snapshot;
    for (auto actor : Traverse())
        snapshot.push_back(actor->GetId());
    updating_ = true;
    try
    {
        for (auto id : snapshot)
        {
            auto actor = Find(id);
            if (actor && actor->IsActiveInHierarchy())
                actor->Update(std::max(0.f, dt));
        }
    }
    catch (...)
    {
        updating_ = false;
        for (auto id : removals_)
            EraseSubtree(id);
        removals_.clear();
        throw;
    }
    updating_ = false;
    for (auto id : removals_)
        EraseSubtree(id);
    removals_.clear();
}
SceneGraph::Bounds SceneGraph::BuildBounds(ActorId id) const
{
    Bounds bounds;
    auto node = Find(id);
    if (!node || !node->IsVisibleInHierarchy())
        return bounds_[id] = bounds;
    if (node->GetKind() != Kind::Group && node->GetLayer() == Layer::World)
    {
        auto m = node->GetWorldMatrix();
        float x = (m.x - m.y) * 31, y = (m.x + m.y) * 15.5f - node->GetWorldElevation();
        float radius = 320 * std::max({1.f, std::abs(m.a) + std::abs(m.c), std::abs(m.b) + std::abs(m.d)});
        bounds = {x - radius, y - radius, x + radius, y + radius};
    }
    for (auto child : node->GetChildren())
    {
        auto b = BuildBounds(child);
        bounds.left = std::min(bounds.left, b.left);
        bounds.top = std::min(bounds.top, b.top);
        bounds.right = std::max(bounds.right, b.right);
        bounds.bottom = std::max(bounds.bottom, b.bottom);
    }
    return bounds_[id] = bounds;
}
void SceneGraph::VisibleVisit(ActorId id, const Bounds &view, std::vector<const Actor *> &result) const
{
    Profiler::Add("scene_nodes_tested");
    auto found = bounds_.find(id);
    if (found == bounds_.end())
        return;
    const auto &b = found->second;
    if (b.right < view.left || b.left > view.right || b.bottom < view.top || b.top > view.bottom)
    {
        Profiler::Add("scene_subtrees_culled");
        return;
    }
    auto actor = Find(id);
    if (actor->GetKind() != Kind::Group && actor->GetLayer() == Layer::World && actor->IsVisibleInHierarchy())
        result.push_back(actor);
    for (auto child : actor->GetChildren())
        VisibleVisit(child, view, result);
}
std::vector<const Actor *> SceneGraph::RenderQueue(Layer layer, const View *view) const
{
    Profiler::Scope scope("render_queue_build_sort");
    std::vector<const Actor *> result;
    if (view && layer == Layer::World)
    {
        if (boundsRevision_ != revision_)
        {
            Profiler::Scope timing("scene_bounds_rebuild");
            bounds_.clear();
            BuildBounds(root_);
            boundsRevision_ = revision_;
        }
        float x = (view->cameraX - view->cameraY) * 31, y = (view->cameraX + view->cameraY) * 15.5f;
        VisibleVisit(
            root_,
            {x - view->width * .5f, y - view->height * .53f, x + view->width * .5f, y + view->height * .47f},
            result);
    }
    else
        for (auto actor : Traverse())
            if (actor->GetKind() != Kind::Group && actor->GetLayer() == layer &&
                actor->IsVisibleInHierarchy())
                result.push_back(actor);
    std::stable_sort(result.begin(), result.end(), [](const Actor *a, const Actor *b) {
        auto x = a->GetWorldPosition(), y = b->GetWorldPosition();
        float first = x.x + x.y, second = y.x + y.y;
        return first == second ? a->GetId() < b->GetId() : first < second;
    });
    Profiler::Add("queued_actors", double(result.size()));
    return result;
}
std::size_t SceneGraph::Size() const
{
    return actors_.size();
}
bool SceneGraph::IsValid() const
{
    std::unordered_set<ActorId> seen;
    for (auto actor : Traverse())
    {
        if (!seen.insert(actor->GetId()).second)
            return false;
        for (auto id : actor->GetChildren())
            if (!Find(id) || Find(id)->GetParent() != actor->GetId())
                return false;
    }
    return seen.size() == actors_.size();
}
bool SceneGraph::RunTests(std::string &report)
{
    auto Check = [&](bool okay, const char *name) {
        if (!okay)
            report += std::string("FAIL: scene graph ") + name + "\n";
        return okay;
    };
    SceneGraph graph;
    auto &parent = graph.Create("Parent", Kind::Group);
    parent.SetLocalMatrix({0, 2, -2, 0, 10, 20});
    auto &child = graph.Create("Child", Kind::Player, parent.GetId());
    child.SetLocalPosition({3, 4});
    auto p = child.GetWorldPosition();
    if (!Check(std::abs(p.x - 2) < .001f && std::abs(p.y - 26) < .001f, "parent matrix"))
        return false;
    if (!Check(child.SetWorldPosition({4, 24}), "set world position"))
        return false;
    auto &other = graph.Create("Other", Kind::Group);
    other.SetLocalPosition({-4, 7});
    if (!Check(graph.Reparent(child.GetId(), other.GetId(), true) &&
                   std::abs(child.GetWorldPosition().x - 4) < .001f,
               "keep-world reparent"))
        return false;
    if (!Check(!graph.Reparent(other.GetId(), child.GetId()), "cycle rejection"))
        return false;
    other.SetVisible(false);
    if (!Check(graph.RenderQueue(Layer::World).empty(), "inherited visibility"))
        return false;
    other.SetVisible(true);
    int ticks = 0;
    child.SetUpdate([&](Actor &, float) { ticks++; });
    other.SetEnabled(false);
    graph.Update(.1f);
    if (!Check(ticks == 0, "inherited activity"))
        return false;
    other.SetEnabled(true);
    graph.Update(.1f);
    if (!Check(ticks == 1, "update traversal"))
        return false;
    ActorId id = child.GetId();
    child.SetUpdate([&](Actor &actor, float) { graph.Remove(actor.GetId()); });
    graph.Update(.1f);
    if (!Check(!graph.Find(id) && graph.IsValid(), "deferred self removal"))
        return false;
    auto &a = graph.Create("a", Kind::Tree);
    auto &b = graph.Create("b", Kind::Tree);
    a.SetLocalPosition({8, 0});
    b.SetLocalPosition({2, 0});
    auto queue = graph.RenderQueue(Layer::World);
    if (!Check(queue[0]->GetId() == b.GetId(), "depth order"))
        return false;
    auto &nested = graph.Create("nested", Kind::Flame, a.GetId());
    ActorId nestedId = nested.GetId();
    graph.Remove(a.GetId());
    if (!Check(!graph.Find(nestedId), "subtree destruction"))
        return false;
    id = b.GetId();
    graph.Clear();
    if (!Check(!graph.Find(id) && graph.Size() == 1, "stale ids after clear"))
        return false;
    SceneGraph visibility;
    auto &nearGroup = visibility.Create("near", Kind::Group);
    auto &near = visibility.Create("visible", Kind::Tree, nearGroup.GetId());
    auto &farGroup = visibility.Create("far", Kind::Group);
    farGroup.SetLocalPosition({1000, 1000});
    auto &far = visibility.Create("hidden", Kind::Tree, farGroup.GetId());
    View view{0, 0, 1280, 800};
    auto visible = visibility.RenderQueue(Layer::World, &view);
    if (!Check(visible.size() == 1 && visible[0]->GetId() == near.GetId(), "subtree camera culling"))
        return false;
    farGroup.SetLocalPosition({0, 0});
    if (!Check(visibility.RenderQueue(Layer::World, &view).size() == 2,
               "bounds invalidate after parent move"))
        return false;
    nearGroup.SetVisible(false);
    if (!Check(visibility.RenderQueue(Layer::World, &view).size() == 1 &&
                   visibility.RenderQueue(Layer::World, &view)[0]->GetId() == far.GetId(),
               "bounds invalidate after hide"))
        return false;
    report += "PASS: spatial subtree culling and transform/visibility cache invalidation.\n";
    report += "PASS: actor transforms, keep-world parent changes, cycle rejection, inherited state, "
              "traversal, deferred destruction, stable depth order and stale handles.\n";
    return true;
}
} // namespace Scene
