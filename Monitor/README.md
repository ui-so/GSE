# 로컬 월드 모니터링

## 실행

필요 환경: Windows x64, Python 3.10 이상(`python` 명령), 최신 Edge/Chrome. Python 표준 라이브러리만 사용하며 패키지 설치나 외부 CDN은 필요 없다.

1. `Tutorial/build.cmd`로 게임을 빌드한다.
2. `Monitor/start.cmd`를 더블 클릭한다. 로컬 수신 서버·브라우저·게임을 실행한다.
3. 브라우저 주소: `http://127.0.0.1:8765/`.

서버만 실행하려면 `python Monitor/server.py`. 기존 게임에 연결하려면 `Monitor/start.cmd -NoGame`. 브라우저도 열지 않으려면 `-NoBrowser`를 추가한다. 이미 같은 서버가 있으면 재사용한다. 서버가 없는 상태로 게임을 먼저 실행해도 나중에 서버를 시작하면 다시 연결된다. 실행 스크립트는 브라우저를 통한 실제 프로젝트 통합이나 외부 배포를 하지 않는다.

게임 F3으로 마을 사건을 진행하고 F1으로 사냥터를 전환한다. NPC 이름 검색, 월드·상태 필터, 원본 JSON 열기를 제공한다. Esc 일시정지나 게임 창의 포커스 상실도 시뮬레이션 중지로 표시한다. 서버는 게임과 별도 프로세스로 유지된다. 직접 실행한 서버는 Ctrl+C로 종료한다. 스크립트로 시작한 서버 PID는 `.runtime/server.pid`에 남으며 작업 관리자에서 해당 Python 프로세스를 종료할 수 있다.

## 표시와 집계

- 현재 마을: 주민 36명, 전투 몬스터 18마리, 관찰용 야생동물 15마리. 사냥터 수는 실제 생성 결과를 전달한다.
- NPC: 한글 이름, Actor ID, 위치, 대기/이동/대화/정지/비활성 상태, 사건 반응, 서비스, 이야기 기록 여부.
- 마을: 일상/징후/대피/정지 단계. 월드: 현재 플레이 월드, 시뮬레이션 갱신 여부.
- `npc_active`는 활성 Actor 수로, 죽음의 의미를 갖는 탈색 NPC도 Actor 자체가 활성 상태면 포함한다. 정지 상태는 별도 행에서 구분한다.
- `monster_alive`는 살아 있고 활성 상태인 전투 몹, `monster_total`은 현재 월드에 생성된 전체 전투 몹이다. 야생동물은 별도 집계한다.
- 마지막 수신 후 5초가 지나면 오래된 세션으로 표시한다. 상단 합계는 최근 수신 세션만 포함한다. 아래 카드·목록에는 마지막 관측값을 최대 10분간 남긴다.
- 비활성 월드는 마지막 게임 상태를 주기적으로 다시 보고하지만 시뮬레이션은 진행되지 않는다. 수신 중과 시뮬레이션 중은 다른 개념이다.
- 현재 실제 마을은 잿빛 여울 한 곳이다. 임의의 마을을 더 만든 것으로 표시하지 않는다. 여러 프로젝트/세션의 메시지를 함께 수신·구분할 수 있다.

## 구조와 JSON 계약 v1

`게임 메인 스레드에서 상태 복사 → 최신 메시지 슬롯 → 전용 HTTP 스레드 → 로컬 Python 수신기 → HTML의 GET 폴링`.

게임은 500ms마다 `POST /api/snapshots`에 UTF-8 `Content-Type: application/json`으로 전체 스냅샷을 보낸다. 브라우저는 500ms마다 `GET /api/state`를 받아 표시한다. WebSocket이나 게임 서버 통합은 전제하지 않는다. 통신 장애가 게임 프레임을 기다리게 하지 않으며, 적체 시 중간 메시지를 버리고 최신 상태를 우선한다.

[example.json](example.json)은 실제 C++ 송신 메시지에서 만든 예제다.

| 필드 | 의미 |
|---|---|
| schema_version / type | 1 / world_snapshot |
| project_id | 송신 프로젝트 식별자. 현재 gse |
| session_id | 실행 인스턴스. 현재 프로세스 ID와 시작 시점 조합 |
| sequence | 세션 내 증가하는 순번. 같은/이전 순번은 HTTP 409 |
| active_world_id / paused | 플레이 중인 월드와 명시적 일시정지 |
| worlds[] | 현재 송신자가 관측하는 전체 월드 목록. 다음 메시지가 이전 목록을 대체 |
| world_id / village_id | 프로젝트·세션 내 월드와 마을 식별자. 사냥터의 village_id는 빈 문자열 |
| kind / name / phase / simulated | 영역 종류, 표시명, 단계, 실제 시뮬레이션 활성 여부 |
| counts | npc_total, npc_active, monster_alive, monster_total, wildlife |
| npcs[] | entity_id, name, state, behavior, reaction, service, active, recorded, position{x,y} |

NPC의 완전한 관측 키는 `(project_id, session_id, world_id, entity_id)`다. Actor ID는 월드 재생성 시 바뀔 수 있어 영속 인물 ID로 사용하지 않는다. 한 v1 world 항목은 하나의 관측 영역을 나타내며 현재 마을은 해당 영역의 village_id를 함께 보낸다. 다중 마을을 품는 월드 등 최종 통합 계층은 미정이므로 필요 시 다음 스키마 버전으로 확장한다.

`GET /api/state` 응답은 `application=gse-monitor`, `schema_version=1`, `sessions[]`를 포함한다. 각 세션에는 `received_at`(서버 UTC 시각), `age_ms`, `stale`, 원문 `snapshot`이 있다. 수신 시각은 송신 시각이 아니며 전송 지연 측정값으로 쓰지 않는다.

수신기는 버전·필수 필드·중복 ID·수치 범위·카운트 일치를 검증한다. 정상 수신 202, 잘못된 메시지 400, 오래된 순번 409, 크기 초과 413, 잘못된 Content-Type 415, 세션 용량 초과 503이다. 요청 최대 2MiB, 세션 32개, 월드 64개/메시지, NPC 5,000명/영역이다. 127.0.0.1에만 바인딩하고 다른 Origin/Host는 거부한다. 로컬 개발용으로 사용자 인증·원격 접속·장기 저장은 제공하지 않는다.

## 향후 통합 경계

최종 목표는 프로젝트들을 통합해 월드 사이를 이동하는 것이다. 통합 방식은 학기 종료 후 결정될 수 있으므로 이번에는 관측 DTO와 전송기를 게임 규칙에서 분리했다. 다른 프로젝트는 같은 JSON 계약을 구현하는 어댑터로 참여할 수 있다. 현재 F1은 기존 두 레벨의 전환이며 플레이어 상태 이관·네트워크 동기화·저장 병합을 구현했다는 뜻은 아니다.

## 검증과 진단

```
python Monitor/test_monitor.py
python Monitor/test_monitor.py --integration Tutorial/build/AshenShore.exe ../outputs/Monitoring
python Monitor/test_monitor.py --reconnect Tutorial/build/AshenShore.exe ../outputs/Monitoring
Tutorial/build/AshenShore.exe --self-test
```

통합 검사는 8765 포트를 임시 사용하므로 실행 중인 모니터 서버를 먼저 종료한다. 실제 게임 모듈로 4개 사건 단계와 월드 전환을 재현해 수신 JSON의 주민·몹·야생동물 수를 검사한다. `--monitor-test`는 이 검사용으로 약 6초 송신 후 종료한다.

일반 실행은 기본 모니터링 활성화, `--no-monitor`는 비활성화다. 벤치마크와 캡처는 기본 비활성화이며 `--benchmark --monitor`로 계측 비용을 비교할 수 있다. `monitor_collect`, `monitor_serialize_enqueue`, `monitor_snapshot_bytes`, 전송 성공/실패/교체 누적값은 기존 JSONL 프로파일에 기록한다. 전송 실패가 증가하면 서버 실행 여부와 포트 8765를 확인한다.

실제 검증 결과: [MONITOR_VALIDATION.md](../Docs/MONITOR_VALIDATION.md).
