# Actor / SceneGraph 개발 안내

기존 ui-so/GSE 저장소의 Tutorial 실행 프로젝트에 적용했다. 첫 레벨과 F1 튜토리얼 모두 사용한다. 원본 SimpleGame 샘플은 별도이며 이번 변경 대상이 아니다.

## 구조

- `Actor.h/.cpp`: 식별자, 종류, 부모, 로컬/월드 좌표, affine 변환, 높이, 활성·표시 상태, Update 콜백.
- `SceneGraph.h/.cpp`: unique_ptr 소유권, 생성·삭제, 부모 변경, DFS 갱신 순회, 레이어별 깊이 정렬.
- `SceneRender.h`: 월드 변환을 기존 쿼터뷰 화면 변환으로 연결한다. HUD는 화면 좌표를 사용한다.
- `FirstLevel::World`: SceneGraph를 소유한다. Hero/Enemy/Drop은 ActorId를 보유하며 별도 위치 복사본을 보유하지 않는다.
- `LevelView`: 매 프레임 지형과 엔티티를 다시 배치하지 않고 SceneGraph의 가시 Actor를 그린다.
- `Tutorial.cpp`: 주민·집·나무·우물·씨앗·추모석·야생동물·플레이어를 등록한다. 위치와 상호작용은 Actor를 참조한다.

첫 레벨의 계층:

```text
Root
├─ Scenery
│  ├─ Terrain (Ground; 바닥·수면 배치)
│  ├─ Tree / Rock ...
│  └─ Brazier
│     └─ Flame
├─ Characters
│  ├─ Player
│  │  └─ Attack
│  └─ Enemy ...
├─ Loot
│  └─ Loot ...
└─ HUD (Interface; 패널·미니맵·문구 묶음)
```

튜토리얼은 같은 구조에 주민, 건물, 야생동물, 환경 효과 묶음과 목표 표식을 사용한다. 목표 표식은 현재 목표의 자식이다. 그림자는 해당 모델 Actor의 렌더 구성 요소이며, 후처리는 렌더 패스다.

## 사용 예

```cpp
auto &scene = world.GetScene();
auto playerId = world.Player().actor;
auto *player = scene.Find(playerId);
if (player)
{
    player->SetWorldPosition({2.5f, 0.5f});
    player->SetVisible(false); // 시각만 숨김
    player->SetEnabled(false); // 하위 객체의 갱신·표시와 게임 동작도 중지
}
auto &group = scene.Create("Party", Scene::Kind::Group);
scene.Reparent(playerId, group.GetId(), true); // 월드 변환 유지
group.SetLocalPosition({1, 0}); // 자식에 누적 적용
```

`SetLocalMatrix`는 이동·회전·크기 및 shear를 표현한다. `Reparent(..., false)`는 기존 로컬 변환을 유지한다. 사이클, 루트 재배치, 존재하지 않는 부모, 역변환 불가능한 keep-world 부모는 거부한다.

`RenderQueue`는 Ground/World/Effects/Interface 레이어별로 가시 노드를 반환한다. 월드 깊이 x+y, 같은 깊이는 생성 순서로 정렬한다. 레이어는 부모에서 상속하지 않고 각 노드가 지정한다. 활성/표시와 변환은 상속한다.

## 수명과 확장 규칙

- 장기 참조는 포인터 대신 ActorId로 보관하고 사용할 때 Find를 호출한다. ID는 해당 SceneGraph 안에서만 유효하다.
- Remove는 하위 노드도 삭제한다. Update 도중 삭제는 순회 종료까지 지연하며 즉시 비활성으로 취급한다. Update 중 생성한 객체는 다음 갱신부터 순회한다.
- Clear 후 ID를 재사용하지 않는다. 이전 맵의 핸들을 새 맵에서 사용하지 않는다. Update 중 Clear나 중첩 Update는 지원하지 않는다.
- 처치·획득한 Actor는 비활성으로 전환하고 게임 기록은 남긴다. 맵 재생성 시 전체 정리한다.
- 새로운 렌더 종류는 해당 View에 그리기 코드를 연결한다. Enemy/Loot처럼 게임 데이터가 필요한 종류는 유효한 데이터 인덱스와 함께 생성해야 한다.
- 타일·도형·텍스트 글자마다 Actor를 만드는 대신 논리적인 배치 단위로 등록한다. 캐시 파일은 모델 데이터이며, 실행 중 Actor 인스턴스 저장과는 별개다.

## 현재 범위

Actor는 배치·계층·표시·활성 상태를 제어하고 전투/퀘스트 규칙은 World와 기존 튜토리얼 로직에 둔다. SceneGraph는 Win32/OpenGL을 참조하지 않는다.

첫 레벨의 길찾기와 지형 충돌은 생성된 고정 타일맵을 사용한다. 지형이나 나무·바위 Actor를 실행 중 옮겨도 내비게이션 격자가 자동 재작성되지는 않는다. 현재 플레이에서는 이 정적 배치를 이동하지 않으며, 지형 편집·이동식 장애물을 추가할 때 격자 갱신을 연결해야 한다. 캐릭터와 전리품의 이동·거리 판정은 Actor 월드 좌표를 사용한다.

## 검증

`Tutorial/build.cmd`로 C++17 /W4 /WX 빌드한다. `AshenShore.exe --self-test`로 다음을 검사한다.

- 부모 affine 변환, 월드 좌표 지정, 월드 변환 보존 재배치, 사이클 거부.
- 부모 상태 상속, Update 순회와 순회 중 삭제, 하위 트리 삭제, ID 무효화, 깊이 정렬.
- 실제 게임의 부모 이동, 비활성 그룹의 동작 중지, Actor 수 유지, 삭제된 핸들의 안전한 처리.
- 128개 랜덤 맵의 연결성, 전투·경험치·전리품·능력치, 캐시, 튜토리얼 대화·퀘스트·이동·동물.

`--capture-level` 및 `--capture`는 실제 OpenGL 화면을 캡처한다. 성장 화면은 UI 검증을 위해 경험치를 직접 지급하는 캡처 시나리오이며 실제 전투 검증은 self-test에서 수행한다.
