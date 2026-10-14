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
        Profiler::Event("journey_stage_changed", std::to_string(Step()));
    }
}
const wchar_t *Quest::Objective() const
{
    const wchar_t *steps[] = {L"마라에게 여행 이야기를 꺼내기",
                              L"호숫가에서 P로 첫 풍경 기록",
                              L"파란 꽃을 좋아하는 리오와 대화",
                              L"북쪽 묘목에서 P로 꽃길 기록",
                              L"제빵사 오렌의 마지막 빵 이야기 듣기",
                              L"엘린과 리오, 두 사람의 안부 확인",
                              L"성직자 세라에게 살아 있는 이의 돌봄 묻기",
                              L"남쪽 집결지에서 대피를 5초간 지켜보기",
                              L"북쪽 묘목에서 P로 멈춘 숲 기록",
                              L"마라에게 사람들의 기록 전하기",
                              L"남쪽 길로 걸어가 다음 여행 준비",
                              L"첫 장 완료 · 남겨진 색과 사람들"};
    return steps[Step()];
}
const wchar_t *Quest::Hint() const
{
    if (stage_ == Stage::FindFamily)
        return mother_ ? L"리오에게도 직접 안부를 묻자. 목적지 표시는 움직이는 주민을 따라갑니다."
                       : L"엘린을 먼저 만나자. 물건보다 서로를 먼저 찾는 사람들이다.";
    if (stage_ == Stage::WatchEvacuation)
        return L"남쪽 집결지 안에서 5초간 기다리세요. 수첩을 열면 시간도 멈춥니다.";
    if (stage_ == Stage::RecordStillness)
        return L"기록은 멈춘 생명을 되돌리지 않는다. 남아 있던 모습을 그리자.";
    if (Complete())
        return L"다음 지역은 아직 준비 중입니다. J로 이번 마을의 세 그림을 다시 볼 수 있습니다.";
    return L"금빛 표시는 현재 목적지입니다. E 대화 · P 풍경 기록 · J 그림 수첩";
}
int Quest::TargetNpc() const
{
    switch (stage_)
    {
    case Stage::MeetMara:
    case Stage::ReturnMara:
        return 0;
    case Stage::MeetChild:
        return 20;
    case Stage::ShareBread:
        return 1;
    case Stage::FindFamily:
        return mother_ ? 20 : 19;
    case Stage::VisitCleric:
        return 17;
    default:
        return -1;
    }
}
Scene::Position Quest::TargetPoint() const
{
    switch (stage_)
    {
    case Stage::RecordLake:
        return {10, -5};
    case Stage::RecordFlowers:
    case Stage::RecordStillness:
        return {0, -13};
    case Stage::WatchEvacuation:
        return {0, 18};
    case Stage::LeaveVillage:
        return {0, 26};
    default:
        return {0, 2};
    }
}
Village::Phase Quest::RequiredPhase() const
{
    if (stage_ >= Stage::RecordStillness)
        return Village::Phase::Stillness;
    if (stage_ >= Stage::WatchEvacuation)
        return Village::Phase::Evacuation;
    if (stage_ >= Stage::ShareBread)
        return Village::Phase::Warning;
    return Village::Phase::Daily;
}
std::wstring Quest::Dialogue(int npc, Village::Phase phase) const
{
    if (npc == 0 && stage_ == Stage::MeetMara)
        return L"그 방울은 네 작은 동행의 것이구나. 함께 오지 못한 여행이라면 그림 속에서 자리를 내어 주렴. "
               L"먼저 호숫가를 그려 보겠니? 꽃을 좋아하는 리오도 만나 보고.";
    if (npc == 20 && stage_ == Stage::MeetChild)
        return L"북쪽 묘목 옆에는 파란 꽃이 피어. 네 그림 속 긴 귀 친구도 좋아할까? 엄마는 길에 핀 꽃을 밟지 "
               L"말래. 꽃도 자기 자리가 있대.";
    if ((npc == 1 || npc == 25) && stage_ == Stage::ShareBread && phase > Village::Phase::Warning)
        return L"마지막 빵은 벌써 이웃에게 나눴지. 가게를 접고 나니 외상 장부만 남더군. 엘린과 리오도 서로를 "
               L"찾고 있어. 두 사람이 함께 떠날 수 있으면 좋겠네.";
    if ((npc == 1 || npc == 25) && stage_ == Stage::ShareBread)
        return L"북쪽 잎이 색을 잃는다더군. 마지막 빵은 이웃에게 나눠 줄 참이야. 딱딱한 건 길에서 먹고, 더 "
               L"딱딱한 건 문받침으로 써. 엘린과 리오도 서로 찾고 있더라.";
    if (npc == 19 && stage_ == Stage::FindFamily)
        return L"짐은 두고 가도 괜찮아. 리오가 어디 있는지만 알면 돼. 남쪽 집결지에서 기다리겠다고 전해 "
               L"줄래? 그 아이가 좋아하는 꽃 이름도 잊고 있었네.";
    if (npc == 20 && stage_ == Stage::FindFamily)
        return L"엄마를 찾았어? 그럼 나도 남쪽으로 갈게. 파란 꽃을 꺾어 갈까 했는데 그냥 두었어. 대신 그림에 "
               L"남겨 줘. 꽃이 여기 살았다는 걸 알 수 있게.";
    if ((npc == 17 || npc == 13 || npc == 26) && stage_ == Stage::VisitCleric)
        return L"상처는 돌볼 수 있지만 탈색을 되돌린다는 약속은 못 하겠어요. 지금 살아 있는 이들이 함께 떠날 "
               L"수 있도록 곁을 지킬 뿐이죠. 남쪽 집결지까지 와 주시겠어요?";
    if (npc == 0 && stage_ == Stage::ReturnMara)
        return L"처음엔 네 동행이 좋아했을 풍경뿐이었는데, 이제는 이곳 사람들의 이름도 있구나. 누구도 "
               L"돌아오지는 않지만 어떤 삶이 있었는지는 남았어. 남쪽 길로 여행을 계속하렴.";
    return {};
}
void Quest::Talk(int npc, bool frozen, Village::Phase phase)
{
    Profiler::Scope scope("journey_interaction");
    if (frozen)
        return;
    switch (stage_)
    {
    case Stage::MeetMara:
        if (npc == 0)
            Advance();
        break;
    case Stage::MeetChild:
        if (npc == 20)
            Advance();
        break;
    case Stage::ShareBread:
        if ((npc == 1 || npc == 25) && phase >= Village::Phase::Warning)
            Advance();
        break;
    case Stage::FindFamily:
        mother_ |= npc == 19;
        child_ |= npc == 20;
        if (mother_ && child_)
            Advance();
        break;
    case Stage::VisitCleric:
        if (npc == 17 || npc == 13 || npc == 26)
            Advance();
        break;
    case Stage::ReturnMara:
        if (npc == 0)
            Advance();
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
void Quest::Update(float dt, Scene::Position player, Village::Phase phase)
{
    Profiler::Scope scope("journey_update");
    auto goal = TargetPoint();
    float distance = std::hypot(player.x - goal.x, player.y - goal.y);
    if (stage_ == Stage::WatchEvacuation)
    {
        if (distance < 4 && phase >= Village::Phase::Evacuation)
            evacuationWatch_ += std::clamp(dt, 0.f, .1f);
        if (evacuationWatch_ >= 5)
            Advance();
    }
    else if (stage_ == Stage::LeaveVillage && distance < 2)
        Advance();
}
const wchar_t *Quest::PageTitle(int page)
{
    const wchar_t *titles[] = {L"함께 보았을 호수", L"파란 꽃을 좋아하는 아이", L"멈춘 숲, 남겨진 이름"};
    return titles[std::clamp(page, 0, 2)];
}
const wchar_t *Quest::PageNote(int page)
{
    const wchar_t *notes[] = {L"물 위의 등불을 오래 바라보았다. 네가 함께 왔다면 물새를 따라 귀를 움직였을 "
                              L"것이다. 오지 못한 너의 자리를 그림에 남긴다.",
                              L"리오는 파란 꽃을 좋아한다고 했다. 꽃을 밟지 말라는 어머니의 말도 기억하고 "
                              L"있었다. 풍경 곁에 여기 살아가는 사람들의 말을 적기 시작했다.",
                              L"하온은 묘목 곁에 남았다. 불꽃과 꽃은 꺼지거나 마르지 않고 그대로 멈췄다. "
                              L"기록은 이 죽음을 되돌리지 않는다. 다만 누가 여기 살았는지 남긴다."};
    return notes[std::clamp(page, 0, 2)];
}
bool Quest::RunTests(std::string &report)
{
    auto fail = [&](bool okay, const char *name) {
        if (!okay)
            report += std::string("FAIL: journey ") + name + "\n";
        return okay;
    };
    Quest q;
    if (!fail(q.Record(Site::Stillness, Village::Phase::Daily) == -1, "locked records"))
        return false;
    q.Talk(0, true, Village::Phase::Daily);
    if (!fail(q.Current() == Stage::MeetMara, "frozen conversations"))
        return false;
    q.Talk(0, false, Village::Phase::Daily);
    q.Record(Site::Lake, Village::Phase::Daily);
    q.Talk(20, false, Village::Phase::Daily);
    q.Record(Site::Flowers, Village::Phase::Daily);
    q.Talk(1, false, Village::Phase::Daily);
    if (!fail(q.Current() == Stage::ShareBread && q.RequiredPhase() == Village::Phase::Warning,
              "automatic warning and bread gate"))
        return false;
    // F3 may advance the event early; required witnesses must remain recoverable.
    q.Talk(1, false, Village::Phase::Stillness);
    q.Talk(19, false, Village::Phase::Stillness);
    q.Talk(19, false, Village::Phase::Stillness);
    if (!fail(q.Current() == Stage::FindFamily && q.TargetNpc() == 20, "distinct family members"))
        return false;
    q.Talk(20, false, Village::Phase::Stillness);
    q.Talk(17, false, Village::Phase::Stillness);
    for (int i = 0; i < 100; ++i)
        q.Update(.1f, {0, 0}, Village::Phase::Stillness);
    if (!fail(q.Current() == Stage::WatchEvacuation, "assembly proximity"))
        return false;
    for (int i = 0; i < 60; ++i)
        q.Update(.1f, {0, 18}, Village::Phase::Stillness);
    if (!fail(q.Record(Site::Stillness, Village::Phase::Warning) == -1, "stillness gate"))
        return false;
    q.Record(Site::Stillness, Village::Phase::Stillness);
    q.Talk(0, false, Village::Phase::Stillness);
    q.Update(.1f, {0, 26}, Village::Phase::Stillness);
    if (!fail(q.Complete() && q.PageCount() == 3 && q.Record(Site::Lake, Village::Phase::Stillness) == 0,
              "chapter completion and rereading"))
        return false;
    q = Quest{};
    if (!fail(q.Step() == 0 && q.PageCount() == 0, "restart clears chapter"))
        return false;
    report += "PASS: journey chapter, 3 image unlocks, witness order, family uniqueness, assembly wait, "
              "phase gates, early-event recovery and restart.\n";
    return true;
}
} // namespace Journey
