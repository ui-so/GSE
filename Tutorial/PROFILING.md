# 성능 최적화와 프로파일링

## 실행

`Tutorial/build.cmd`로 빌드한다. 일반 실행은 매 프레임 JSON 한 줄을 콘솔에 표시하고 현재 작업 디렉터리의 `profiles/<session-id>/profile-*.jsonl`에 기록한다. 로그 출력은 별도 스레드에서 수행한다.

```powershell
.\build\AshenShore.exe
.\build\AshenShore.exe --benchmark --quiet-profile --validate-render
.\build\AshenShore.exe --benchmark --tutorial --quiet-profile --validate-render
python tools/analyze_profile.py profiles/<session-id> --output summary.json --assert-clean
python tools/compare_profiles.py before-summary.json after-summary.json
```

- `--benchmark`: 시드 42, 고정 1/60초 시뮬레이션, 20프레임 워밍업 후 120프레임 측정, 숨김 실행 후 종료. 이 모드에만 GPU 완료 대기(glFinish)가 있다.
- `--quiet-profile`: 콘솔만 끄고 파일 프로파일은 유지한다. 동일 조건 비교에 권장한다.
- `--no-profile`: 계측 출력 비활성화. 계측 오버헤드 비교용이다.
- `--no-post`: 후처리 제외 비교용이다.
- `--validate-render`: 프레임 마지막에 OpenGL 오류 검사. 드라이버 호출 비용이 있어 검증할 때 사용한다.
- `--capture-level`, `--capture`: 실제 이미지 검증. 한 메인 루프에서 여러 draw를 실행하므로 같은 frame_id에 렌더 수치가 누적된다. 성능 비교 시나리오로 사용하지 않는다.
- `tools/benchmark.ps1`: 별도 실행 폴더에서 검증·벤치마크를 실행하고 JSON 요약을 만든다.

## 적용 구조

- 16×16 타일 청크, 재질별 정적 VBO, 카메라 영역 밖 청크 제외.
- 모델 부품을 초기화 때 삼각형 메시로 변환해 GPU에 저장. 반복 모델은 인스턴싱으로 처리하고 애니메이션은 버텍스 셰이더가 수행한다.
- 깊이 순서를 유지한 인접 명령 배칭. 다른 메시나 텍스처, 상태 경계에서는 배치를 분리한다. 알파 객체를 재질순으로 무조건 재배열하지 않는다.
- 한글 글리프 페이지 아틀라스, 캐릭터 팔레트 아틀라스, 그림자·단색 도형 공유 텍스처, 미니맵 지형 텍스처 캐시.
- SceneGraph의 하위 트리 경계 합집합·카메라 컬링과 월드 변환 캐시. 정적 배치를 공간 그룹으로 묶는다.
- 후처리 FBO 직접 렌더, 절반 해상도 블룸, uniform 위치 캐시. FBO 불가 시 기존 화면 복사 경로로 fallback하고 이벤트를 기록한다.
- 풍경 스프라이트의 색상과 무채색 버전은 단일 아틀라스에 업로드한다. `scenery_atlas_load`와 `scenery_atlas_upload_bytes`는 시작할 때만 기록한다. 물가 암석과 포말은 기존 지형 패스에서 컬링되는 정적 메시다. `terrain_shore_edges_built`는 지형 생성 시에만 기록한다.
- 튜토리얼 충돌은 4단위 공간 격자 사용. 정적 장애물의 Actor 월드 위치·활성 상태를 시뮬레이션 단계마다 갱신한 스냅샷이며 캐릭터 위치의 별도 원본이 아니다.
- 매 프레임 고정 Sleep(8) 제거. GPU 드라이버의 화면 동기화 정책은 강제로 변경하지 않는다.

## AI 분석 규약

JSONL은 한 줄마다 완전한 JSON이며 `schema_version=1`이다. 영어 snake_case 키를 유지한다. 구체적인 계측 이름 목록은 `profile-metrics.json`을 참고한다.

| type | 용도 |
|---|---|
| session | 시나리오, 시드, 해상도, GL 버전, GPU, 빌드 식별자, 논리 CPU 수, GPU 타이머 지원 |
| frame | frame_id, frame_start_ms, frame_ms, frame_interval_ms, warmup, over_budget, cpu_ms, counters |
| gpu_span | 원래 frame_id, name, gpu_ms. 결과가 준비된 뒤 비동기로 기록 |
| startup_span | 초기화·로딩 등 프레임 외 CPU 구간 시간 |
| event | 캐시 적중·생성·실패, 렌더러 오류, fallback, 실행 옵션 |
| session_end | 처리 프레임 수, 유실 기록, 미회수 GPU 결과 수 |

`cpu_ms`/`gpu_ms`는 밀리초, `_bytes`는 바이트, `_calls`/`_count` 및 tested/visible/culled는 횟수다. `scene_mode`는 0=tutorial, 1=level1이며 프레임 중 전환도 기록한다. 여러 패스에서 반복되는 카운터는 고유 객체 수가 아닌 수행 횟수다.

- `frame_ms`: 프레임 시작부터 화면 출력 대기 및 수치 수집까지의 시간. 마지막 JSON 직렬화·큐 삽입 비용은 제외된다.
- `frame_interval_ms`: 연속 프레임 시작 사이의 실제 시간으로 로그 생성 비용도 포함한다. 실제 루프 처리율은 이 값을 사용한다.
- `cpu_ms`의 중첩 구간과 GPU 구간은 inclusive다. 부모와 자식을 더하지 않는다. GPU와 CPU 시간도 서로 겹칠 수 있다.
- GPU 결과는 원래 frame_id로 조인한다. 결과 없음은 0ms가 아니다. 미지원, query pool 부족, 종료 시 미완료는 각각 메타데이터·gpu_spans_skipped·pending_gpu_spans로 확인한다.
- 알려진 작업량 카운터는 매 프레임 0으로 시작한다. 조건부 CPU 구간과 120프레임마다 측정하는 프로세스 메모리는 값이 없을 때 미측정이다.
- draw_calls는 실제 glDrawArrays 및 인스턴싱 제출 수다. 과거 표시 목록 기반 primitive_batches와는 정의가 다르다. submitted_vertices는 인스턴스 수를 곱한 정점 제출량이다.
- P50/P95/P99/최댓값을 평균과 함께 사용한다. 워밍업은 기본 20프레임 제외한다. 16.6667ms 초과는 60 FPS 예산 초과 관측이며 원인을 단정하는 라벨이 아니다.
- 분석기는 OpenGL 오류, 로그 유실, GPU 결과 누락, 프레임 예산 초과를 flags로 표시한다. `--assert-clean`은 GL 오류·로그 유실에 실패한다. 임계 성능은 `--max-frame-p95-ms`, `--max-draw-calls`로 별도 지정한다.

## 주요 관측 지점

입력 디스패치, 전체 시뮬레이션, 씬 갱신, AI 길찾기, 충돌 후보·시야 검사, 전투·획득, 맵 생성, 캐시 로딩·생성, 씬 경계 갱신, 렌더 큐 정렬, 지형·그림자·모델·UI·후처리, VBO 업로드·제출, 표시 대기, 주기적 프로세스 메모리, 프로파일러 GPU 폴링·직렬화·로그 큐를 계측한다.

병목 후보 판단 예: draw_calls와 batch_break_texture가 함께 증가하면 텍스처 배칭을 확인한다. game_update와 collision_candidates_tested가 증가하면 공간 분할을 확인한다. CPU 시간은 안정적인데 GPU postprocess만 증가하면 픽셀 처리 비용을 확인한다. 이는 관측에 따른 조사 방향이며 자동 확정 진단은 아니다.

## 새 기능에 계측 추가

```cpp
Profiler::Scope cpu("inventory_update");
Profiler::Add("inventory_items_tested", items.size());
// GPU 명령을 실제 제출하는 구간에서만 사용. 지연 배치를 끝까지 Flush한 뒤 Scope를 닫는다.
Profiler::GpuScope gpu("particles");
Profiler::Event("asset_stream_failure", "region_12");
```

이름은 수명이 유지되는 문자열 리터럴을 사용한다. Add/Time/Scope/GpuScope는 메인 스레드 API다. Event는 로그 큐가 동기화되지만 frame_id 접근 때문에 작업 스레드에서 직접 호출하지 않는다. 새 비동기 작업은 결과를 메인 스레드로 전달하거나 명시적 job_id/thread_id 기반 계측을 추가한다.

새 시스템은 CPU 실행 시간, 작업량, 캐시 적중/실패, 예상 밖 대기, 오류를 기록한다. 새 GPU 패스는 시간과 제출 횟수·정점/업로드량을 기록한다. `profile-metrics.json`에 이름·단위를 추가하고 동일 조건 벤치마크와 이미지 검증을 남긴다. 개인 데이터·대화 내용은 로그에 넣지 않는다.

## 비용과 한계

로그 큐는 최대 256개 레코드다. 초과 시 렌더 스레드를 I/O로 막지 않고 기록을 버리며 유실 수를 남긴다. 한 세션은 16MiB 파일 4개를 순환하므로 장시간 실행에서는 최근 기록만 남는다. session.json은 별도 보존한다. 세션 폴더 자체는 자동 삭제하지 않는다. stdout 수신기가 느리면 유실이 발생할 수 있으므로 벤치마크에는 quiet 모드를 쓴다.

GPU 타이밍은 결과 준비 여부를 확인한 후 읽으며 일반 프레임을 glFinish로 막지 않는다. 프로세스 메모리는 Working Set/Private Bytes이며 GPU VRAM이나 개별 할당 추적이 아니다. 드라이버 내부 stall, 하드웨어 카운터, OS 스케줄링, 아직 없는 오디오·네트워크 시스템은 자동으로 측정되지 않는다. 앞으로 해당 기능을 추가할 때 위 API로 계측해야 한다.

컬링 경계는 현재 모델에 맞춘 보수적인 범위이며, 새 대형 모델은 경계 정책을 확장해야 한다. 첫 레벨의 지형 충돌/길찾기는 기존 고정 격자다. GPU 배칭에는 VBO·GLSL·인스턴싱을 지원하는 OpenGL 호환 컨텍스트가 필요하다. 미지원 시 조용히 잘못 그리지 않고 초기화 실패를 표시한다.


## 스토리 마을 계측

주민 36명과 확장된 숲의 시뮬레이션은 `village_simulation`, 경로 탐색은 `npc_pathfinding`으로 분리한다. `npc_path_requests`, `npc_path_cells_visited`, `npc_path_failures`, `npc_entities_tested`, `npc_frozen`, `village_monsters_tested`는 매 프레임 0에서 시작한다. 초기 탐색 격자·이동 간선 검증은 `village_initialize`, 거래는 `village_trade`에 포함된다. 이벤트와 테스트 흐름은 `Docs/VILLAGE_TEST.md`를 참고한다.

마을의 기본 140프레임 벤치마크는 초기 일상 화면 비교다. F3 사건의 집단 경로 변경은 이 짧은 실행에 포함되지 않으므로 실제 플레이 로그에서 `village_story_phase`와 경로 탐색 스파이크를 함께 확인한다. 캡처 모드의 대피 가속 갱신은 동작·이미지 검증용이며 실제 1프레임 성능으로 해석하지 않는다.


## 로컬 상태 모니터링

일반 게임 실행은 500ms 간격으로 `Monitor/server.py`에 JSON 스냅샷을 전송한다. `--no-monitor`로 끌 수 있다. 벤치마크와 캡처는 기본 비활성화이며 벤치마크에 `--monitor`를 추가하면 비용을 비교할 수 있다.

`monitor_collect`는 Actor 상태 복사와 메시지 준비의 inclusive CPU 시간이다. 그 안의 `monitor_serialize_enqueue`는 UTF-8 JSON 직렬화와 최신 메시지 슬롯 갱신 비용이다. `monitor_snapshot_bytes`, `monitor_snapshots_built`는 해당 프레임의 제출량이다. `monitor_sent_total`/`monitor_failed_total`/`monitor_replaced_total`은 세션 누적 전송 성공/실패/미전송 메시지 교체 횟수로 매 프레임 델타가 아니다. 조건부 지표가 없는 프레임은 0회 제출이다.

HTTP 전송은 전용 스레드에서 수행한다. 미전송 슬롯은 1개이며 밀리면 최신 스냅샷으로 교체한다. 서버를 실행하지 않아도 게임은 계속 진행하며 다음 제출 때 재시도한다. 네트워크 전송 시간은 CPU Scope에 포함되지 않는다. 자세한 JSON 계약과 월드 활성 상태 해석은 `Monitor/README.md`를 따른다.


## 첫 장과 그림 수첩 계측

`journey_update`/`journey_interaction`/`journey_record`는 목표 갱신·대화·기록의 CPU 구간이다. `journey_stage_changed`의 0~11 값은 첫 장 단계, `journey_page_unlocked`의 0~2 값은 호수/꽃길/정지 숲이다.

`journal_art_load`는 시작 시 내장 PNG 디코딩과 GPU 업로드 시간이다. `journal_art_loaded`에는 페이지와 원본 크기가 기록되며 실패는 `journal_art_load_failed`로 구분한다. 세 장은 1536×1024 RGBA로 디코딩되어 약 18MiB의 텍스처 데이터가 상주한다(드라이버 내부 메모리는 제외). 파일 I/O·생성 서비스 호출은 게임 프레임마다 수행하지 않는다.

`journal_ui_cpu`/GPU `journal_ui`는 수첩 표시, `journal_art_submit`는 이미지 제출이다. 이들은 기존 UI 구간에 포함되므로 합산하지 않는다. `journal_images_drawn`, `journal_open`, `journal_pages_unlocked`, `journey_stage`는 매 프레임 0에서 시작하고 해당 프레임 값을 기록한다. 수첩 중 마을 시뮬레이션은 정지하며 모니터 JSON도 정지로 보고한다.

`--capture-story --quiet-profile --validate-render`는 실제 목표 순서로 첫 장을 진행하고 잠긴 페이지·세 그림·빵 대사·대피·완료·1000×720 수첩을 캡처한다. 여러 draw와 가속 시뮬레이션이 한 프레임에 포함되므로 성능 수치로 비교하지 않는다.


## 다섯 마을 계측 (2026-10-21)

현재 주민100명·몬스터100개가 확장 월드에서 시뮬레이션된다. `npc_pathfinding`은 전처리된 간선 위의 A* 탐색이다. 방문 셀·실패·요청 수는 기존 키를 유지한다. 화면 밖 Actor도 순회와 소문 시뮬레이션을 계속하며 렌더는 SceneGraph 공간 컬링·배칭을 사용한다.

`monster_spawn_select`는 유효 재생성 후보 탐색, `monster_spawn_candidates`는 검사 후보 수, `monster_respawns`는 해당 프레임 재생성 수, `monster_respawn_retries`는 후보 없음 재시도 수다. 초기 배치도 같은 탐색 Scope에 포함되고 프레임 시작 전 초기화 구간과 정상 프레임을 구분한다. 한 프레임 최대4개, 드롭 풀 최대128개이며 `loot_pool_full`은 드롭 생략 횟수다.

`rumor_contacts_tested`는 위치/소식 비교 횟수다. `rumor_contact`는 지식이 실제로 달라진 순회자 도착/현지 소식판 교환 CPU 구간, `rumor_deliveries`는 새로운 마을 소문 도착 건수다. `rumor_delivered` 이벤트는 `village=N;rumor=N;carrier=ActorId`이며 메시지 본문이나 개인정보를 기록하지 않는다. `village_atmosphere`는0.5초 간격 분위기 집계 CPU다. 각 Scope는 inclusive라 village_simulation과 합산하지 않는다.

`--benchmark --tutorial`은 기존 초기 마을과 비교한다. `--benchmark-region --quiet-profile --validate-render`는60초 순회를 준비한 뒤 숲길에서140프레임을 기록한다. 준비 갱신은 프레임 시작 전 구간이므로 정상 프레임 평균에 포함되지 않는다. `--capture-region`의 여러 뷰·가속 갱신은 성능 비교로 해석하지 않는다.


## 스토리 v2 · 동료와 실제 대피 (2026-11-02)

현재 서장은0~17단계다. `journey_stage_changed` detail은 v2 영문 단계 키이며 이전 0~11 숫자와 구분한다. `contract_reward_claimed`는 완료 의뢰 수와 보수, `companion_support_cast`는 대상 Actor, `npc_rescued`는 집결한 Actor다.

`companion_update`는 동료 위치/반응/자동 지원, 그 안의 `companion_support_select`는 재사용 대기 완료 후에만 대상/시야를 탐색한다. `companion_candidates_tested`, `companion_line_of_sight_samples`, `companion_spells_cast`로 작업량과 발동을 확인한다. `companion_render_submit`는 캐시한 여우 메시·광휘·연결선 제출이며 `companion_models_drawn`은 모델 제출 횟수다. 새 여우/용병 장비는 기존 AssetCache/정적 메시 경로를 사용한다.

`npc_escort_update`는 안내 대상만 경로를 0.8초 간격으로 갱신하고, `npc_rescues_completed`는 실제 집결 완료 횟수다. `desaturation_overlay_rebuild`는 반경이 0.25m 경계를 넘을 때만 작은 국소 지형 오버레이를 다시 만들고 `desaturation_overlay_tiles`에작업량을 기록한다. 나머지 대규모 지형 캐시를 재생성하지 않는다. `contract_rewards_claimed`는 실제 보수 지급 횟수다. 새 counter는 프레임마다 0에서 시작하며 Scope는 inclusive다.

정상 비교는 수정 전/후 동일한 `--benchmark --tutorial --quiet-profile --validate-render`, 시드42·1280×800·후처리 켬·모니터 끔에서 진행한다. 급속 확산/대피/전투는 짧은 초기 화면 벤치마크에 모두 포함되지 않으므로 가속 캡처와 실제 플레이 로그의 해당 구간을 함께 확인한다.

`--benchmark --tutorial --benchmark-rapid --quiet-profile --validate-render`는 2초 진행한 국소 급속 확산에서140프레임을 측정한다. `story_contracts_completed`, `story_family_rescued`, `companion_wary`, `desaturation_radius_m`는 프레임별 현재 값이다. `--story-monitor-test`는 실제 상호작용 시나리오를 진행하며18개 체크포인트를 모니터에 보낸다.


## GLFW 창 백엔드 (2026-11-11)

`window_backend=glfw` 이벤트가 현재 백엔드를 식별한다. `input_dispatch`는 `glfwPollEvents`와 콜백, `present_wait`는 `glfwSwapBuffers`를 포함한다. 일반 실행에서 최소화하면 이벤트를 기다리며 렌더링을 쉬고, 복원 시 대기 시간을 게임 시간에 합산하지 않는다. 벤치마크의 숨김 창은 기존처럼 고정 시간 간격으로 시뮬레이션과 렌더링을 진행한다. 스왑 간격을 강제로 변경하지 않는다.
