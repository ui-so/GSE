#pragma once
#include "Village.h"
#include <array>
#include <string>
namespace Journey
{
enum class Stage
{
    MeetBroker,
    HuntRoad,
    ClaimHunt,
    CollectSupplies,
    DeliverSupplies,
    ClaimDelivery,
    RecordLake,
    MeetChild,
    RecordFlowers,
    CheckWarning,
    FindFamily,
    EscortFamily,
    VisitCleric,
    RecordStillness,
    CompareRecords,
    NextVillage,
    FindNextWork,
    Complete
};
enum class Site
{
    Lake,
    Flowers,
    Stillness
};
struct Reward
{
    int coins = 0, experience = 0;
};
class Quest
{
  public:
    static constexpr int ChapterSteps = int(Stage::Complete);
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
    bool HasObjective() const
    {
        return !Complete() || repeatableActive_;
    }
    bool Hunting() const
    {
        return stage_ == Stage::HuntRoad || (Complete() && repeatableActive_ && huntKills_ < 2);
    }
    bool Unlocked(int page) const;
    int PageCount() const;
    int ContractsCompleted() const
    {
        return contracts_;
    }
    int ContractKills() const
    {
        return huntKills_;
    }
    int RewardCoins() const
    {
        return rewardCoins_;
    }
    bool CarryingSupplies() const
    {
        return supplies_;
    }
    bool RepeatableActive() const
    {
        return repeatableActive_;
    }
    const char *StageKey() const;
    std::wstring Objective() const;
    const wchar_t *Hint() const;
    int TargetNpc() const;
    Scene::Position TargetPoint() const;
    Village::Phase RequiredPhase() const;
    std::wstring Dialogue(int npc, Village::Phase phase) const;
    void Talk(int npc, bool frozen, Village::Phase phase, int totalKills = 0);
    int Record(Site site, Village::Phase phase);
    void Update(float dt, Scene::Position player, Village::Phase phase, int totalKills = 0,
                bool familyResolved = false);
    Reward TakeReward();
    static const wchar_t *PageTitle(int page);
    const wchar_t *PageNote(int page) const;
    static bool RunTests(std::string &report);

  private:
    Stage stage_ = Stage::MeetBroker;
    std::array<bool, 3> pages_{};
    bool mother_ = false, child_ = false, supplies_ = false, repeatableActive_ = false;
    int killBaseline_ = 0, huntKills_ = 0, contracts_ = 0, rewardCoins_ = 0, repeatableVillage_ = 1;
    float evacuationWatch_ = 0;
    Reward pending_{};
    void Advance();
    void Award(int coins, int experience);
};
} // namespace Journey
