# GSE — 잿빛 여울

중세 다크 판타지의 쿼터뷰 2.5D 오픈 월드 RPG를 위한 C++ / OpenGL 튜토리얼 프로토타입입니다. 생명의 신비와 죽음의 의미, 미지의 존재를 알아가는 이야기를 중심으로 합니다.

## 실행할 프로젝트

**새 튜토리얼은 `Tutorial.sln`을 열어 실행합니다.** 기존 `SimpleGame.sln`과 `SimpleGame/Renderer`는 원본 학습 샘플로 보존되어 있습니다. 튜토리얼은 같은 저장소와 C++ / OpenGL 기술 기반의 별도 실행 타깃이며 기존 Renderer 클래스를 직접 확장한 구현은 아닙니다.

1. Visual Studio 2022의 C++ 데스크톱 개발 도구와 Windows SDK를 설치합니다.
2. `Tutorial/build.cmd`를 실행합니다.
3. 생성된 `Tutorial/build/AshenShore.exe`를 실행합니다.

또는 `Tutorial.sln`을 Visual Studio에서 열어 x64 구성으로 빌드합니다. 외부 이미지·셰이더 파일이나 FreeGLUT/GLEW DLL은 필요하지 않습니다. 실행 파일과 임시 빌드 결과는 Git에 포함하지 않습니다.

## 현재 기능

- WASD 이동, Shift 빠르게 걷기, E 상호작용, Esc 일시정지, F2 후처리 비교
- 한국어 UI와 대사, 3–5분 목표의 작은 퀘스트
- 주민 16명, 마을·숲·호수와 확장된 외곽
- 사슴·산토끼·멧돼지 총 15마리의 배회와 도주
- 스프라이트 애니메이션, 지형 재질, 그림자, 수면·불꽃 효과와 장면 후처리

## 문서와 검증

- [게임 개발 기본 지침](GAME_GUIDELINES.md)
- [초기 기획서](INITIAL_GAME_PLAN.md)
- [튜토리얼 기능·구조·조작·검증 안내](Tutorial/README.md)

`AshenShore.exe --self-test`로 퀘스트·이동·주민·야생동물 검사를 실행할 수 있습니다. `--capture`는 실제 렌더링 화면을 현재 작업 디렉터리에 저장합니다. `--benchmark`는 그리기 시간을 측정합니다.

레벨명과 등장인물은 프로토타입용 임시 설정입니다. 전투·저장·청크 스트리밍은 아직 구현하지 않았습니다.
