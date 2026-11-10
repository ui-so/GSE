# GLFW 전환

`Tutorial.sln`의 창 생성, 입력 콜백, OpenGL 컨텍스트 및 화면 출력에 GLFW를 사용한다. OpenGL 렌더 큐와 배칭은 기존 경로를 사용한다. 최소화된 창은 렌더를 쉬고 복원 시 프레임 시간을 다시 잡는다. GL 드라이버의 화면 동기화 설정은 유지한다.

## 빌드

Windows x64에서 Visual Studio 2022 또는 Build Tools의 MSVC v143과 Windows SDK가 필요하다. 저장소에서 `Tutorial\build.cmd`를 실행하면 `Tutorial\build\AshenShore.exe`가 생성된다. Visual Studio에서는 `Tutorial.sln`을 열고 `Release | x64` 또는 `Debug | x64`로 빌드한다.

[GLFW 3.5.1 소스와 라이선스](../ThirdParty/README.md)는 `ThirdParty/glfw`에 들어 있다. `Tutorial/GlfwSources.props`가 필요한 Windows C 소스를 직접 컴파일해 게임에 정적으로 링크한다. 빌드 중 인터넷 접속과 GLFW 별도 설치는 필요 없다. 실행 파일 옆에 복사되는 `GLFW-LICENSE.md`는 실행 파일을 배포할 때 함께 배포한다.

게임은 한국어 글꼴에 GDI, 이미지 디코딩에 WIC, 모니터링에 WinHTTP를 사용한다. 게임 타깃의 검증 범위는 Windows x64다.

## 검증 재현

빌드한 실행 파일을 쓰기 가능한 작업 디렉터리에서 실행한다. `--window-test`는 실제 GLFW 프레임버퍼 리사이즈와 입력 콜백의 키 상태, 포커스 상실 시 키 초기화, 반복 키 처리를 검사한다. `--self-test`는 게임 로직, `--capture-story`는 스토리 진행과 실제 OpenGL 출력 경로를 검사한다.

```powershell
.\AshenShore.exe --window-test
.\AshenShore.exe --self-test
.\AshenShore.exe --capture-story --quiet-profile --validate-render
.\AshenShore.exe --benchmark --tutorial --quiet-profile --validate-render
.\AshenShore.exe --benchmark --tutorial --benchmark-rapid --quiet-profile --validate-render
.\AshenShore.exe --benchmark --quiet-profile --validate-render
python <저장소>/Tutorial/tools/analyze_profile.py profiles/<세션> --output summary.json --assert-clean
```

각 실행은 1280×800, 시드 42, 고정 시뮬레이션 단계, 워밍업 20프레임 및 측정 120프레임의 기존 벤치마크를 쓴다. 숨김 창과 GPU 완료 대기를 포함하는 벤치마크이므로 화면에 표시되는 실제 플레이 FPS와는 측정 조건이 다르다.

## 전환 전후 결과

Intel Iris Plus Graphics의 OpenGL 4.6 드라이버에서 전환 전 `4d9266e`와 전환 후 코드를 같은 PC에서 각각 클린 빌드했다. 장면마다 두 실행을 한 쌍으로 5회 측정했고, 라운드마다 실행 순서를 번갈아 바꿨다. 표의 전후 수치는 5회 결과의 중앙값이며, 마지막 열은 각 쌍의 P95 차이를 구한 뒤 중앙값을 낸 값이다.

| 장면 | 평균 프레임 시간 전 → 후 (ms) | P95 전 → 후 (ms) | 평균 드로우콜 전 → 후 | 쌍별 P95 차이 중앙값 (ms) |
|---|---:|---:|---:|---:|
| 마을 | 8.40 → 8.51 | 10.34 → 11.46 | 107.84 → 107.84 | -0.18 |
| 급변 이벤트 | 8.75 → 8.62 | 11.46 → 10.87 | 123.05 → 123.05 | -0.60 |
| 사냥터 | 5.76 → 5.61 | 7.57 → 7.44 | 165.00 → 165.00 | -0.16 |

30회 실행에서 OpenGL 오류와 프로파일 기록 유실은 0건이었다. 입력 처리 증가량의 중앙값은 프레임당 약 0.001 ms였다. 마을 P95는 버전별 중앙값에서 1.12 ms 증가했고, 같은 라운드끼리 비교하면 5쌍 중 3쌍에서 감소했다. 이 환경에서 일관된 성능 저하는 확인되지 않았으며, 표시 모드와 다른 GPU의 동작은 측정 범위 밖이다.
