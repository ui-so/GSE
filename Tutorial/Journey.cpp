#include "Journey.h"
#include "Profiler.h"
#include <algorithm>
#include <cmath>
namespace Journey
{
bool Quest::Unlocked(int page) const
{
    return page >= 0 && page < int(pages_.size()) && pages_[size_t(page)];
}
int Quest::PageCount() const
{
    return int(std::count(pages_.begin(), pages_.end(), true));
}
void Quest::Advance()
{
    if (!Complete())
    {
        stage_ = Stage(Step() + 1);
        Profiler::Event("journey_stage_changed", StageKey());
    }
}
void Quest::Award(int coins, int experience)
{
    pending_.coins += coins;
    pending_.experience += experience;
    rewardCoins_ += coins;
    ++contracts_;
    Profiler::Add("contract_rewards_claimed");
    Profiler::Event("contract_reward_claimed",
                    "contracts=" + std::to_string(contracts_) + ";coins=" + std::to_string(coins));
}
Reward Quest::TakeReward()
{
    auto reward = pending_;
    pending_ = {};
    return reward;
}
const char *Quest::StageKey() const
{
    const char *keys[] = {"meet_broker",      "hunt_road",        "claim_hunt",      "collect_supplies",
                          "deliver_supplies", "claim_delivery",   "record_lake",     "meet_child",
                          "record_flowers",   "check_warning",    "find_family",     "escort_family",
                          "visit_cleric",     "record_stillness", "compare_records", "next_village",
                          "find_next_work",   "complete"};
    return keys[Step()];
}
std::wstring Quest::Objective() const
{
    if (Hunting())
        return L"용병 의뢰 · 숲길 마물 처치 " + std::to_wstring(huntKills_) + L" / 2";
    if (Complete() && repeatableActive_)
        return L"다음 마을 의뢰인에게 보고하고 보수 받기";
    const wchar_t *steps[] = {L"마라에게 일거리를 묻기",
                              L"숲길 마물 두 마리 처치",
                              L"마라에게 토벌 보수 받기 · 동전 15",
                              L"오렌에게 구호소 보급품 받기",
                              L"세라에게 빵과 붕대 배달",
                              L"마라에게 배달 보수 받기 · 동전 10",
                              L"호숫가에서 P로 저녁 풍경 그리기",
                              L"리오와 여우 동료 이야기 나누기",
                              L"북쪽 꽃길에서 P로 두 번째 풍경 그리기",
                              L"파수꾼에게 탈색 징후와 대피 절차 묻기",
                              L"엘린과 리오를 만나 대피 안내",
                              L"가족을 남쪽 집결지로 안내하기",
                              L"세라와 살아 있는 이의 돌봄 이야기",
                              L"북쪽 경계에서 P로 멈춘 숲 기록",
                              L"마라와 예전 그림을 보며 주민들의 이야기 남기기",
                              L"청솔 나루 광장으로 이동",
                              L"청솔 나루 의뢰 중개인에게 다음 일감 받기",
                              L"서장 완료 · 의뢰와 여행은 계속된다"};
    return steps[Step()];
}
const wchar_t *Quest::Hint() const
{
    if (Hunting())
        return L"금빛 표시가 마물을 가리킵니다. Space 검 공격 · F 동료 지원. 푸른 표식의 적은 느려지고 검 "
               L"피해가 증가합니다.";
    if (stage_ == Stage::CollectSupplies || stage_ == Stage::DeliverSupplies)
        return L"돈이 필요하면 일을 받는다. 첫 배달은 빵과 붕대다. E로 물건을 받고 의뢰 대상에게 직접 "
               L"건네자.";
    if (stage_ == Stage::FindFamily)
        return L"엘린과 리오에게 각각 E로 말을 걸면 따라옵니다. 같은 사람에게 반복해서 말해도 완료되지 "
               L"않습니다.";
    if (stage_ == Stage::EscortFamily)
        return L"살아 있는 두 사람이 따라올 때까지 기다리며 남쪽 집결지로 이동하세요. 완전 탈색된 주민은 "
               L"되돌릴 수 없습니다.";
    if (stage_ == Stage::CheckWarning)
        return L"탈색은 익숙한 재해다. 파수꾼에게 기존 감시와 대피 절차를 듣자. 여우가 평소보다 가까이 "
               L"붙는다.";
    if (stage_ == Stage::RecordStillness || stage_ == Stage::CompareRecords)
        return L"그림은 마법이 아니다. 되돌리는 대신, 풍경 옆에 그곳에서 살던 사람들의 이야기를 남긴다.";
    if (stage_ == Stage::NextVillage)
        return L"급속 탈색의 원인을 알아내라는 의뢰는 없다. 당장 다음 식비를 벌러 청솔 나루로 향하자.";
    if (Complete())
        return repeatableActive_ ? L"다음 의뢰는 반복 토벌입니다. 해결 후 해당 중개인에게 돌아오세요. 다른 "
                                   L"마을에서도 새 일을 받을 수 있습니다."
                                 : L"서장이 끝났습니다. 마을의 중개인에게 E로 새 의뢰를 받아 "
                                   L"탐험·전투·보수·기록의 여행을 계속하세요.";
    return L"WASD 이동 · E 대화/보급품 · Space 검 공격 · F 여우 지원 · P 풍경 기록 · J 그림 수첩";
}
int Quest::TargetNpc() const
{
    switch (stage_)
    {
    case Stage::MeetBroker:
    case Stage::ClaimHunt:
    case Stage::ClaimDelivery:
    case Stage::CompareRecords:
        return WorldLayout::Mara;
    case Stage::CollectSupplies:
        return WorldLayout::Baker;
    case Stage::DeliverSupplies:
    case Stage::VisitCleric:
        return WorldLayout::Cleric;
    case Stage::MeetChild:
        return WorldLayout::Child;
    case Stage::CheckWarning:
        return 4;
    case Stage::FindFamily:
        return mother_ ? WorldLayout::Child : WorldLayout::Mother;
    case Stage::FindNextWork:
        return WorldLayout::ResidentsPerVillage;
    case Stage::Complete:
        return repeatableActive_ && huntKills_ >= 2 ? repeatableVillage_ * WorldLayout::ResidentsPerVillage
                                                    : -1;
    default:
        return -1;
    }
}
Scene::Position Quest::TargetPoint() const
{
    switch (stage_)
    {
    case Stage::HuntRoad:
        return {-10, 27};
    case Stage::RecordLake:
        return {10, -5};
    case Stage::RecordFlowers:
    case Stage::RecordStillness:
        return {0, -13};
    case Stage::EscortFamily:
        return {0, 18};
    case Stage::NextVillage:
        return WorldLayout::Plaza(1);
    default:
        return {0, 2};
    }
}
Village::Phase Quest::RequiredPhase() const
{
    if (stage_ >= Stage::RecordStillness)
        return Village::Phase::Stillness;
    if (stage_ >= Stage::FindFamily)
        return Village::Phase::Evacuation;
    if (stage_ >= Stage::CheckWarning)
        return Village::Phase::Warning;
    return Village::Phase::Daily;
}
std::wstring Quest::Dialogue(int npc, Village::Phase phase) const
{
    if (npc == WorldLayout::Mara)
    {
        if (stage_ == Stage::MeetBroker)
            return L"마라: 새 용병이군. 남쪽 숲길 마물 두 마리를 정리하면 동전 열다섯이야. 조사 의뢰는 "
                   L"아니고, 짐수레가 다닐 길을 만드는 일이지. / 용병: 위험수당까지 세어 주신 거죠? 이 녀석 "
                   L"간식값도 만만치 않아서.";
        if (stage_ == Stage::ClaimHunt)
            return L"마라: 길이 좀 조용해졌어. 약속한 동전 열다섯이야. 오렌의 빵과 붕대를 세라에게 옮기는 "
                   L"일도 있어. 배달 보수는 열 닢. / 용병: 칼보다 빵을 나르는 날이 수지가 좋으면 좋겠네요.";
        if (stage_ == Stage::ClaimDelivery)
            return L"마라: 구호소에 잘 도착했대. 배달 보수 열 닢 받아. / 용병: 오늘 밥값은 벌었네. 해 지기 "
                   L"전에 호수나 한 장 그리고 가자. 야, 붓은 간식이 아니야.";
        if (stage_ == Stage::CompareRecords)
            return L"마라: 수첩 속 북쪽 길엔 아직 색이 있구나. 평소 절차대로라면 대피할 시간이 있었어야 "
                   L"했는데. / 용병: 적어도 여기에는 아직 남아 있네요. 오늘은 사람들 이야기도 적어 둘게요. "
                   L"공짜는 아니고요. 다음에 만나면 술 한 잔 사세요.";
    }
    if (npc == WorldLayout::Baker && stage_ == Stage::CollectSupplies)
        return L"오렌: 구호소에 가져갈 빵과 붕대야. 무거워도 돈은 네 몫이지, 옆의 여우 몫은 아니야. / 용병: "
               L"들었지? 꼬리로 가방 열지 말란 뜻이야.";
    if (npc == WorldLayout::Cleric && stage_ == Stage::DeliverSupplies)
        return L"세라: 빵과 붕대가 왔네요. 오래전부터 탈색을 감시하며 대피소를 준비해 왔어요. 오늘도 그저 "
               L"평소의 준비예요. / 용병: 그럼 배달 영수증도 평소처럼 부탁합니다. 이 친구 발도장은 빼고요.";
    if (npc == WorldLayout::Child && stage_ == Stage::MeetChild)
        return L"리오: 여우가 어깨에 기대네! 어릴 때부터 같이 다녔다고? 북쪽 파란 꽃도 그려 줘. / 용병: 그림 "
               L"그리는 손을 방해하는 재주는 어릴 때부터였지. 가만, 그 꽃은 먹는 게 아니야.";
    if (npc == 4 && stage_ == Stage::CheckWarning)
        return phase > Village::Phase::Warning
                   ? L"파수꾼: 너무 빨라! 잎이 바랜다는 보고가 온 지 얼마 안 됐는데 숲이 벌써 멈춰. 정해진 "
                     L"대피 순서를 기다릴 수 없겠어. / 용병: 위험수당 얘기는 나중에 하죠. 지금은 사람부터 "
                     L"빼요."
                   : L"파수꾼: 북쪽 묘목에 탈색 징후가 있어. 늘 하던 대로 감시하고 주민을 모으면 돼. 보통은 "
                     L"시간이 있지. ...잠깐, 저쪽 불꽃이 왜 벌써 멎었지? / 용병: 이 녀석도 이상하다는군요. "
                     L"평소 속도가 아니에요. 사람부터 빼죠.";
    if (npc == WorldLayout::Mother && stage_ == Stage::FindFamily)
        return L"엘린: 짐은 두고 갈게요. 리오와 함께 집결지까지 가 주세요. / 용병: 앞장설게요. 이 여우가 "
               L"길을 새면 무시하세요. 오늘은 간식보다 사람이 먼저야.";
    if (npc == WorldLayout::Child && stage_ == Stage::FindFamily)
        return L"리오: 꽃도 불도 그대로 멈췄어. 엄마랑 같이 가고 싶어. / 용병: 내 망토 보이지? 그것만 "
               L"따라와. 꽃은 내가 그려 뒀으니까 꺾지 않아도 돼.";
    if (npc == WorldLayout::Cleric && stage_ == Stage::VisitCleric)
        return L"세라: 살아 있는 분들의 상처부터 돌보겠어요. 완전히 무채색이 된 사람을 되돌릴 수는 없어요. / "
               L"용병: 동전부터 챙길 줄 알았던 상인이 고양이를 먼저 안더군요. 그런 건 그림 옆에 적어 "
               L"둬야겠어요.";
    if (npc == WorldLayout::ResidentsPerVillage && stage_ == Stage::FindNextWork)
        return L"중개인: 청솔 나루에 온 걸 환영하오. 숲길 마물 두 마리를 정리하면 동전 열둘이오. / 용병: "
               L"이번엔 보통 숲길 일이죠? ...좋아요. 어차피 내일 밥값도 벌어야 하니까.";
    if (Complete() && npc % WorldLayout::ResidentsPerVillage == 0)
    {
        if (repeatableActive_ && npc != repeatableVillage_ * WorldLayout::ResidentsPerVillage)
            return L"중개인: 그 의뢰는 받은 마을의 중개인에게 보고하시오. 같은 장부를 쓰는 사이는 아니라서 "
                   L"말이오.";
        return repeatableActive_
                   ? (huntKills_ >= 2
                          ? L"중개인: 처리했군. 약속한 동전 열둘이오. / 용병: 다음에도 살아서 장부에 이름 "
                            L"올릴게요."
                          : L"중개인: 의뢰는 아직 진행 중이오. 마물 두 마리를 정리하고 돌아오시오.")
                   : L"중개인: 새 길 정리 의뢰가 있소. 마물 두 마리에 동전 열둘이오. / 용병: 간식값 포함이면 "
                     L"받아들이죠.";
    }
    return {};
}
void Quest::Talk(int npc, bool frozen, Village::Phase phase, int totalKills)
{
    Profiler::Scope scope("journey_interaction");
    if (frozen)
        return;
    switch (stage_)
    {
    case Stage::MeetBroker:
        if (npc == WorldLayout::Mara)
        {
            killBaseline_ = totalKills;
            huntKills_ = 0;
            Advance();
        }
        break;
    case Stage::ClaimHunt:
        if (npc == WorldLayout::Mara)
        {
            Award(15, 30);
            Advance();
        }
        break;
    case Stage::CollectSupplies:
        if (npc == WorldLayout::Baker)
        {
            supplies_ = true;
            Advance();
        }
        break;
    case Stage::DeliverSupplies:
        if (npc == WorldLayout::Cleric && supplies_)
        {
            supplies_ = false;
            Advance();
        }
        break;
    case Stage::ClaimDelivery:
        if (npc == WorldLayout::Mara)
        {
            Award(10, 20);
            Advance();
        }
        break;
    case Stage::MeetChild:
        if (npc == WorldLayout::Child)
            Advance();
        break;
    case Stage::CheckWarning:
        if (npc == 4 && phase >= Village::Phase::Warning)
            Advance();
        break;
    case Stage::FindFamily:
        mother_ |= npc == WorldLayout::Mother;
        child_ |= npc == WorldLayout::Child;
        if (mother_ && child_)
            Advance();
        break;
    case Stage::VisitCleric:
        if (npc == WorldLayout::Cleric)
            Advance();
        break;
    case Stage::CompareRecords:
        if (npc == WorldLayout::Mara)
            Advance();
        break;
    case Stage::FindNextWork:
        if (npc == WorldLayout::ResidentsPerVillage)
        {
            repeatableActive_ = true;
            killBaseline_ = totalKills;
            huntKills_ = 0;
            Advance();
        }
        break;
    case Stage::Complete:
        if (npc % WorldLayout::ResidentsPerVillage != 0)
            break;
        if (repeatableActive_)
        {
            if (npc == repeatableVillage_ * WorldLayout::ResidentsPerVillage && huntKills_ >= 2)
            {
                Award(12, 20);
                repeatableActive_ = false;
            }
        }
        else
        {
            repeatableVillage_ = npc / WorldLayout::ResidentsPerVillage;
            repeatableActive_ = true;
            killBaseline_ = totalKills;
            huntKills_ = 0;
        }
        break;
    default:
        break;
    }
}
int Quest::Record(Site site, Village::Phase phase)
{
    Profiler::Scope scope("journey_record");
    int page = int(site);
    bool expected =
        (site == Site::Lake && stage_ == Stage::RecordLake) ||
        (site == Site::Flowers && stage_ == Stage::RecordFlowers) ||
        (site == Site::Stillness && stage_ == Stage::RecordStillness && phase == Village::Phase::Stillness);
    if (expected)
    {
        pages_[size_t(page)] = true;
        Advance();
        Profiler::Event("journey_page_unlocked", std::to_string(page));
    }
    return Unlocked(page) ? page : -1;
}
void Quest::Update(float dt, Scene::Position player, Village::Phase phase, int totalKills,
                   bool familyResolved)
{
    Profiler::Scope scope("journey_update");
    if (Hunting())
    {
        huntKills_ = std::clamp(totalKills - killBaseline_, 0, 2);
        if (stage_ == Stage::HuntRoad && huntKills_ >= 2)
            Advance();
    }
    float distance = std::hypot(player.x - TargetPoint().x, player.y - TargetPoint().y);
    if (stage_ == Stage::EscortFamily)
    {
        if (distance < 4 && familyResolved && phase >= Village::Phase::Evacuation)
            evacuationWatch_ += std::clamp(dt, 0.f, .1f);
        if (evacuationWatch_ >= 1)
            Advance();
    }
    else if (stage_ == Stage::NextVillage && distance < 3)
        Advance();
}
const wchar_t *Quest::PageTitle(int page)
{
    const wchar_t *titles[] = {L"배달 뒤의 저녁 호수", L"꽃과 장난꾸러기 동료",
                               L"색은 멎고, 이야기는 남았다"};
    return titles[std::clamp(page, 0, 2)];
}
const wchar_t *Quest::PageNote(int page) const
{
    if (stage_ >= Stage::CompareRecords)
    {
        const wchar_t *notes[] = {
            L"첫 토벌과 배달을 마친 저녁의 호수. 당시에는 그저 지나온 곳을 남기고 싶었다. 지금 덧붙인다. "
            L"상인은 돈주머니보다 고양이를 먼저 안고 대피했다.",
            L"리오는 파란 꽃을 꺾지 않고 두었다. 엘린은 짐을 내려놓고 아이부터 찾았다. 내 손을 방해하던 푸른 "
            L"여우는 그날 꼬리를 낮추고 내 곁을 지켰다.",
            L"묘목지기 하온과 불꽃은 마지막 자세 그대로 멈췄다. 멸망한 것은 무너진 폐허가 아니라 멈춰 버린 "
            L"삶이었다. 되돌릴 힘은 없다. 적어도 이 페이지에는 누가 살았는지 남긴다."};
        return notes[std::clamp(page, 0, 2)];
    }
    const wchar_t *notes[] = {
        L"잿빛 여울 호숫가. 저녁이면 빵 냄새가 난다. 두 건의 보수로 오늘 식비는 해결. 여우는 물고기를 보다가 "
        L"수첩 위에 앉았다. 언제 없어질지 모르니 지나온 곳 정도는 남겨 두자.",
        L"북쪽 묘목 옆 파란 꽃. 리오가 좋아하는 길이다. 붓끝을 쫓는 여우 탓에 선이 삐뚤어졌다. 다음에도 함께 "
        L"이 길을 지나면 좋겠다.",
        L"이건 평소 알던 탈색의 속도가 아니다. 준비하던 사이 숲 일부가 멈췄다. 여우도 내 어깨에서 떨어지려 "
        L"하지 않았다. 그림이 죽음을 되돌리지는 않는다."};
    return notes[std::clamp(page, 0, 2)];
}
bool Quest::RunTests(std::string &report)
{
    auto check = [&](bool okay, const char *name) {
        if (!okay)
            report += std::string("FAIL: story v2 ") + name + "\n";
        return okay;
    };
    Quest q;
    q.Talk(WorldLayout::Cleric, false, Village::Phase::Daily);
    q.Talk(WorldLayout::Mara, true, Village::Phase::Daily, 8);
    if (!check(q.Step() == 0 && q.RequiredPhase() == Village::Phase::Daily,
               "premature and frozen interaction"))
        return false;
    q.Talk(0, false, Village::Phase::Daily, 8);
    q.Update(.1f, {0, 2}, Village::Phase::Daily, 8);
    if (!check(q.ContractKills() == 0 && q.Current() == Stage::HuntRoad, "pre-accept kills excluded"))
        return false;
    q.Update(.1f, {0, 2}, Village::Phase::Daily, 10);
    q.Talk(0, false, Village::Phase::Daily, 10);
    auto reward = q.TakeReward();
    if (!check(reward.coins == 15 && q.TakeReward().coins == 0 && q.ContractsCompleted() == 1,
               "one-time first pay"))
        return false;
    q.Talk(WorldLayout::Cleric, false, Village::Phase::Daily, 10);
    if (!check(!q.CarryingSupplies() && q.Current() == Stage::CollectSupplies, "no invented delivery"))
        return false;
    q.Talk(1, false, Village::Phase::Daily, 10);
    q.Talk(WorldLayout::Cleric, false, Village::Phase::Daily, 10);
    q.Talk(0, false, Village::Phase::Daily, 10);
    if (!check(q.TakeReward().coins == 10 && q.ContractsCompleted() == 2 &&
                   q.RequiredPhase() == Village::Phase::Daily,
               "two normal jobs before disaster"))
        return false;
    q.Record(Site::Lake, Village::Phase::Daily);
    q.Talk(WorldLayout::Child, false, Village::Phase::Daily);
    q.Record(Site::Flowers, Village::Phase::Daily);
    if (!check(q.RequiredPhase() == Village::Phase::Warning && q.PageCount() == 2,
               "ordinary sketches before warning"))
        return false;
    q.Talk(4, false, Village::Phase::Stillness);
    q.Talk(WorldLayout::Mother, false, Village::Phase::Stillness);
    q.Talk(WorldLayout::Mother, false, Village::Phase::Stillness);
    if (!check(q.Current() == Stage::FindFamily, "distinct family witnesses"))
        return false;
    q.Talk(WorldLayout::Child, false, Village::Phase::Stillness);
    for (int i = 0; i < 50; ++i)
        q.Update(.1f, {0, 18}, Village::Phase::Stillness, 10, false);
    if (!check(q.Current() == Stage::EscortFamily, "physical arrival required"))
        return false;
    for (int i = 0; i < 12; ++i)
        q.Update(.1f, {0, 18}, Village::Phase::Stillness, 10, true);
    q.Talk(WorldLayout::Cleric, false, Village::Phase::Stillness);
    if (!check(q.Record(Site::Stillness, Village::Phase::Warning) == -1, "full stillness record gate"))
        return false;
    q.Record(Site::Stillness, Village::Phase::Stillness);
    q.Talk(0, false, Village::Phase::Stillness);
    q.Update(.1f, WorldLayout::Plaza(1), Village::Phase::Stillness, 10);
    q.Talk(20, false, Village::Phase::Daily, 10);
    if (!check(q.Complete() && q.RepeatableActive() && q.PageCount() == 3 && q.RewardCoins() == 25,
               "next village continues livelihood"))
        return false;
    q.Update(.1f, WorldLayout::Plaza(1), Village::Phase::Stillness, 12);
    q.Talk(40, false, Village::Phase::Daily, 12);
    if (!check(q.TakeReward().coins == 0, "correct contract broker"))
        return false;
    q.Talk(20, false, Village::Phase::Daily, 12);
    if (!check(q.TakeReward().coins == 12 && !q.RepeatableActive() && q.ContractsCompleted() == 3,
               "repeat contract payout"))
        return false;
    q.Talk(40, false, Village::Phase::Daily, 12);
    if (!check(q.Hunting() && q.ContractKills() == 0, "fresh contract baseline"))
        return false;
    q = Quest{};
    if (!check(q.Step() == 0 && q.PageCount() == 0 && q.RewardCoins() == 0, "restart"))
        return false;
    report += "PASS: story v2 jobs-before-disaster, two distinct contracts and one-time pay, parcel handoff, "
              "hobby-to-life notes, physical rescue gate, next-village work and repeatable contracts.\n";
    return true;
}
} // namespace Journey
