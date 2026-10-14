#pragma once
#include "Village.h"
#include <array>
#include <string>

namespace Journey
{
enum class Stage
{
    MeetMara,
    RecordLake,
    MeetChild,
    RecordFlowers,
    ShareBread,
    FindFamily,
    VisitCleric,
    WatchEvacuation,
    RecordStillness,
    ReturnMara,
    LeaveVillage,
    Complete
};
enum class Site
{
    Lake,
    Flowers,
    Stillness
};
class Quest
{
  public:
    Stage Current() const
    {
        return stage_;
    }
    int Step() const
    {
        return int(stage_);
    }
    bool Complete() const
    {
        return stage_ == Stage::Complete;
    }
    bool Unlocked(int page) const;
    int PageCount() const;
    const wchar_t *Objective() const;
    const wchar_t *Hint() const;
    int TargetNpc() const;
    Scene::Position TargetPoint() const;
    Village::Phase RequiredPhase() const;
    std::wstring Dialogue(int npc, Village::Phase phase) const;
    void Talk(int npc, bool frozen, Village::Phase phase);
    int Record(Site site, Village::Phase phase);
    void Update(float dt, Scene::Position player, Village::Phase phase);
    static const wchar_t *PageTitle(int page);
    static const wchar_t *PageNote(int page);
    static bool RunTests(std::string &report);

  private:
    Stage stage_ = Stage::MeetMara;
    std::array<bool, 3> pages_{};
    bool mother_ = false, child_ = false;
    float evacuationWatch_ = 0;
    void Advance();
};
} // namespace Journey
