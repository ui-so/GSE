#pragma once
#include "Actor.h"
#include <memory>
#include <unordered_map>
namespace Scene
{
struct View
{
    float cameraX, cameraY;
    int width, height;
};
class SceneGraph
{
  public:
    SceneGraph();
    SceneGraph(const SceneGraph &) = delete;
    SceneGraph &operator=(const SceneGraph &) = delete;
    Actor &Create(const std::string &name, Kind kind, ActorId parent = InvalidActor);
    Actor *Find(ActorId id);
    const Actor *Find(ActorId id) const;
    Actor &Root();
    const Actor &Root() const;
    bool Reparent(ActorId child, ActorId parent, bool keepWorld = true);
    bool Remove(ActorId id);
    void Clear();
    void Update(float dt);
    std::vector<const Actor *> RenderQueue(Layer layer, const View *view = nullptr) const;
    std::vector<const Actor *> Traverse() const;
    std::size_t Size() const;
    bool IsValid() const;
    static bool RunTests(std::string &report);

  private:
    friend class Actor;
    void Touch()
    {
        ++revision_;
    }
    std::uint64_t revision_ = 1;
    struct Bounds
    {
        float left = 1e30f, top = 1e30f, right = -1e30f, bottom = -1e30f;
    };
    mutable std::uint64_t boundsRevision_ = 0;
    mutable std::unordered_map<ActorId, Bounds> bounds_;
    Bounds BuildBounds(ActorId id) const;
    void VisibleVisit(ActorId id, const Bounds &view, std::vector<const Actor *> &result) const;
    std::unordered_map<ActorId, std::unique_ptr<Actor>> actors_;
    ActorId nextId_ = 1, root_ = InvalidActor;
    bool updating_ = false;
    std::vector<ActorId> removals_;
    void EraseSubtree(ActorId id);
    void Visit(ActorId id, std::vector<const Actor *> &result) const;
};
} // namespace Scene
