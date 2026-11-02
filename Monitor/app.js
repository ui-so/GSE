"use strict";
const $ = id => document.getElementById(id);
const labels = {
    calm : "평온", alert : "경계", anxious : "불안", mourning : "애도",
    daily : "평온한 일상",
    warning : "일반 탈색 징후 · 대피 준비",
    evacuation : "급속 탈색 · 긴급 대피",
    stillness : "국소 정지",
    hunting : "사냥 진행",
    complete : "사냥 완료",
    walking : "이동",
    idle : "대기",
    talking : "대화",
    frozen : "정지",
    inactive : "비활성",
    leave : "대피 준비",
    family : "가족 찾기",
    stay : "남아 있기",
    help : "이웃 돕기",
    close_shop : "가게 정리",
    share_bread : "빵 나누기",
    none : "없음",
    merchant : "상인",
    cleric : "성직자",
    baker : "제빵사"
};
let data = {sessions : []};
let worldOptions = "";
function node(tag, value, cls)
{
    const el = document.createElement(tag);
    if (value !== undefined)
        el.textContent = value;
    if (cls)
        el.className = cls;
    return el;
}
function badge(text, style = "")
{
    return node("span", text, "badge " + style);
}
function key(session, world)
{
    return JSON.stringify([ session.snapshot.project_id, session.snapshot.session_id, world.world_id ]);
}
function render()
{
    const worlds = data.sessions.flatMap(s => s.snapshot.worlds.map(w => ({s, w, key : key(s, w)})));
    const chosen = $("worldFilter").value;
    const optionKeys = JSON.stringify(worlds.map(x => [x.key, x.w.name]));
    if (optionKeys !== worldOptions)
    {
        worldOptions = optionKeys;
        $("worldFilter")
            .replaceChildren(
                new Option("전체", ""),
                ...worlds.map(({s, w, key}) => new Option(`${w.name} · ${s.snapshot.session_id}`, key)));
        if (worlds.some(w => w.key === chosen))
            $("worldFilter").value = chosen;
    }
    const selected = worlds.filter(x => !$("worldFilter").value || x.key === $("worldFilter").value);
    const live = data.sessions.filter(s => !s.stale).length;
    $("connection").textContent = live ? `수신 중 · ${live}개 세션`
                                  : data.sessions.length ? "연결 끊김 / 오래된 상태"
                                                         : "게임 수신 대기";
    $("connection").className = "badge " + (live ? "" : "warn");
    $("notice").textContent =
        !data.sessions.length ? "게임을 실행하세요. 수신된 JSON이 아직 없습니다."
        : data.sessions.some(s => s.stale)
            ? "5초 이상 갱신되지 않은 세션이 있습니다. 해당 수치는 마지막 관측값입니다."
            : "로컬 JSON 수신 정상 · 다른 구역과 일시정지 상태는 갱신 여부를 별도로 표시합니다.";
    $("received").textContent =
        data.sessions.length ? `최근 수신 ${
                                   new Date(Math.max(...data.sessions.map(s => Date.parse(s.received_at))))
                                       .toLocaleTimeString("ko-KR")}`
                             : "127.0.0.1:8765에서 대기 중";
    const fresh = selected.filter(x => !x.s.stale);
    const sum = k => fresh.reduce((n, x) => n + x.w.counts[k], 0);
    $("totals").replaceChildren(
        ...[[
            "수신 중 구역 / 마을", `${fresh.length} / ${fresh.filter(x => x.w.kind === "village").length}`
        ],
            [ "수신 중 NPC / 활성 Actor", `${sum("npc_total")} / ${sum("npc_active")}` ],
            [ "관측 몹 / 전체 (별도 사냥터 포함)", `${sum("monster_alive")} / ${sum("monster_total")}` ],
            [ "수신 중 야생동물", sum("wildlife") ]]
            .map(([ title, value ]) => {
                const e = node("div", undefined, "metric");
                e.append(node("span", title), node("strong", value));
                return e;
            }));
    const selectedSessions=[...new Set(selected.map(x=>x.s))];
    $("stories").replaceChildren(...selectedSessions.filter(s=>s.snapshot.story).map(s=>{
        const p=s.snapshot.story, card=node("article",undefined,"world story");
        card.append(badge(s.stale?"마지막 관측값":"서장 진행"),node("h3",p.objective),
            node("p",`단계 ${p.step} / ${p.steps} · 완료 의뢰 ${p.contracts_completed} · 누적 보수 ${p.reward_coins}닢`),
            node("p",`진행 의뢰 처치 ${p.contract_kills} / 2 · 보급품 ${p.carrying_supplies?"소지":"없음"} · 가족 구조 ${p.family_rescued} / 2`),
            node("p",`여우 동료: ${p.companion_state==="wary"?"바짝 붙어 경계":"장난스럽게 동행"} · 잿빛 여울: ${
                ({none:"평온",ordinary_signs:"익숙한 탈색 징후",rapid:"비정상적인 급속 탈색"})[p.disaster_kind]}`),
            node("div",`${p.stage} · ${s.snapshot.session_id}`,"meta"));
        return card;
    }));
    $("worlds").replaceChildren(...selected.map(({s, w}) => {
        const e = node("article", undefined, "world");
        const active = w.world_id === s.snapshot.active_world_id;
        e.append(
            badge(s.stale             ? "수신 중단"
                  : w.simulated       ? "시뮬레이션 중"
                  : s.snapshot.paused ? "일시정지"
                                      : "시뮬레이션 중지",
                  s.stale       ? "warn"
                  : w.simulated ? ""
                                : "dim"),
            node("h3", w.name + (active ? " · 현재 위치" : "")), node("p", labels[w.phase] || w.phase),
            node("div", `${s.snapshot.project_id} / ${s.snapshot.session_id} / ${w.world_id}`, "meta"),
            node("div", `수신 ${Math.round(s.age_ms / 1000)}초 전 · 순번 ${s.snapshot.sequence}`, "meta"));
        const n = node("div", undefined, "numbers");
        n.append(node("span", `NPC ${w.counts.npc_total}`),
                 node("strong", `몹 ${w.counts.monster_alive} / ${w.counts.monster_total}`));
        e.append(n);
        if (w.atmosphere)
        {
            const a=w.atmosphere;
            e.append(badge("분위기 · "+(labels[a.mood]||a.mood)),
                node("p", `소속 ${w.counts.npc_total}명 · 현지 ${w.counts.npc_present}명 (방문자 포함)`,"meta"));
            for (const [key,title] of [["calm","평온"],["fear","불안"],["trust","신뢰"],["hope","희망"]])
            {
                const row=node("label",undefined,"mood-row"), bar=node("progress");
                bar.max=100;bar.value=a[key];bar.setAttribute("aria-label",title);
                row.append(node("span",title),bar,node("span",a[key].toFixed(0)));
                e.append(row);
            }
            e.append(node("p",`이동 ${a.walking} · 대기 ${a.idle} · 대화 ${a.talking} · 정지 ${a.frozen}`,"meta"));
            const known=[0,1,2,3].filter(bit=>a.known_rumor_mask&(1<<bit));
            e.append(node("p",`알려진 소문 ${known.length}개 · NPC가 전달한 새 소식 ${a.deliveries.length}건`,"meta"));
            if(a.deliveries.length)
            {
                const d=a.deliveries[a.deliveries.length-1];
                e.append(node("p",`최근 도착: Actor ${d.carrier} · 게임 시간 ${d.simulation_seconds.toFixed(1)}초`,"meta"));
            }
        }
        if(w.world_id==="wilderness") e.append(node("p",`빠른 리젠 누적 ${w.counts.respawns_total||0}회 · 기본 대기 3초`,"meta"));
        return e;
    }));
    const search = $("search").value.toLocaleLowerCase(), state = $("stateFilter").value;
    const rows = selected.flatMap(
        x =>
            x.w.npcs.filter(n => (!state || (state==="courier" ? n.courier : n.state === state)) && n.name.toLocaleLowerCase().includes(search))
                .map(n => ({...x, n})));
    const place=id=>worlds.find(x=>x.w.world_id===id)?.w.name||id||"휴식";
    $("npcs").replaceChildren(...rows.map(({s, w, n}) => {
        const tr = node("tr");
        const name = node("td", n.name);
        name.append(node("small", `Actor ${n.entity_id}${n.courier?" · 순회자":""}`));
        const residence=node("td",`${w.name} · ${s.snapshot.session_id}`);
        if(n.current_village) residence.append(node("small",`현재 ${place(n.current_village)} → ${place(n.destination)}`));
        const behavior=node("td",n.behavior);
        if(n.rumor_mask) behavior.append(node("small",`소지한 소문 ${[0,1,2,3].filter(bit=>n.rumor_mask&(1<<bit)).length}개`));
        const status = node("td");
        status.append(badge(labels[n.state] || n.state, n.state === "frozen" ? "dim" : ""));
        if (s.stale || !w.simulated)
            status.append(node("small", s.stale ? "오래된 관측값" : "시뮬레이션 중지"));
        tr.append(name, residence, status, behavior,
                  node("td", `${labels[n.reaction] || n.reaction} / ${labels[n.service] || n.service}`),
                  node("td", `${n.position.x.toFixed(1)}, ${n.position.y.toFixed(1)}`),
                  node("td", n.recorded ? "기록함" : "미기록"));
        return tr;
    }));
    if (!rows.length)
    {
        const row = node("tr"), cell = node("td", "표시할 NPC가 없습니다.", "empty");
        cell.colSpan = 7;
        row.append(cell);
        $("npcs").append(row);
    }
    $("rowCount").textContent =
        `${rows.length}명 표시 · 활성 Actor 수에는 탈색으로 정지한 주민도 포함됩니다.`;
}
for (const id of ["worldFilter", "stateFilter", "search"])
    $(id).addEventListener("input", render);
async function poll()
{
    try
    {
        const response = await fetch("/api/state", {cache : "no-store", signal : AbortSignal.timeout(2500)});
        if (!response.ok)
            throw Error(response.status);
        const next = await response.json();
        if (next.application !== "gse-monitor" || next.schema_version !== 1)
            throw Error("schema");
        data = next;
        render();
    }
    catch (error)
    {
        for (const session of data.sessions)
            session.stale = true;
        render();
        $("connection").textContent = "수신 서버 연결 끊김";
        $("connection").className = "badge warn";
        $("notice").textContent =
            "로컬 서버를 확인하세요. 아래 정보는 마지막 수신 시점의 값이며 현재 상태가 아닙니다.";
    }
    finally
    {
        setTimeout(poll, 500);
    }
}
render();
poll();
