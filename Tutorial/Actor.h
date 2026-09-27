#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace Scene
{
using ActorId = std::uint64_t;
constexpr ActorId InvalidActor = 0;
struct Position
{
    float x = 0;
    float y = 0;
};
// Affine transform: [a c x; b d y; 0 0 1]. Keep-world reparenting also preserves shear.
struct Matrix
{
    float a = 1, b = 0, c = 0, d = 1, x = 0, y = 0;
    Position Apply(Position point) const;
    Matrix operator*(const Matrix &other) const;
    bool Inverse(Matrix &result) const;
};
enum class Kind
{
    Group,
    Terrain,
    Tree,
    Rock,
    House,
    Player,
    Npc,
    Enemy,
    Loot,
    Well,
    Shrine,
    Seed,
    Wildlife,
    Brazier,
    Flame,
    Particles,
    Marker,
    Attack,
    Hud
};
enum class Layer
{
    Ground,
    World,
    Effects,
    Interface
};
class SceneGraph;
class Actor
{
  public:
    virtual ~Actor() = default;
    Actor(const Actor &) = delete;
    Actor &operator=(const Actor &) = delete;
    ActorId GetId() const;
    ActorId GetParent() const;
    const std::vector<ActorId> &GetChildren() const;
    const std::string &GetName() const;
    Kind GetKind() const;
    int GetDataIndex() const;
    void SetDataIndex(int index);
    Layer GetLayer() const;
    void SetLayer(Layer layer);
    void SetLocalPosition(Position position);
    Position GetLocalPosition() const;
    bool SetWorldPosition(Position position);
    Position GetWorldPosition() const;
    void SetLocalMatrix(const Matrix &matrix);
    const Matrix &GetLocalMatrix() const;
    Matrix GetWorldMatrix() const;
    void SetLocalElevation(float elevation);
    float GetWorldElevation() const;
    void SetEnabled(bool enabled);
    void SetVisible(bool visible);
    bool IsEnabled() const;
    bool IsVisible() const;
    bool IsActiveInHierarchy() const;
    bool IsVisibleInHierarchy() const;
    void SetUpdate(std::function<void(Actor &, float)> update);
    virtual void Update(float dt);

  protected:
    Actor(SceneGraph &graph, ActorId id, std::string name, Kind kind);

  private:
    friend class SceneGraph;
    SceneGraph *graph_;
    ActorId id_, parent_ = InvalidActor;
    std::string name_;
    Kind kind_;
    Layer layer_ = Layer::World;
    Matrix local_;
    mutable Matrix cachedWorld_;
    mutable std::uint64_t cachedRevision_ = 0;
    float elevation_ = 0;
    int dataIndex_ = -1;
    bool enabled_ = true, visible_ = true, pendingRemoval_ = false;
    std::vector<ActorId> children_;
    std::function<void(Actor &, float)> update_;
};
} // namespace Scene
