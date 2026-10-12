"use strict";
const $ = id => document.getElementById(id);
const labels = {
    daily : "평온한 일상",
    warning : "탈색 징후",
    evacuation : "주민 대피",
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
            : "로컬 JSON 수신 정상 · 다른 월드와 일시정지 상태는 갱신 여부를 별도로 표시합니다.";
    $("received").textContent =
        data.sessions.length ? `최근 수신 ${
                                   new Date(Math.max(...data.sessions.map(s => Date.parse(s.received_at))))
                                       .toLocaleTimeString("ko-KR")}`
                             : "127.0.0.1:8765에서 대기 중";
    const fresh = selected.filter(x => !x.s.stale);
    const sum = k => fresh.reduce((n, x) => n + x.w.counts[k], 0);
    $("totals").replaceChildren(
        ...[[
            "수신 중 월드 / 마을", `${fresh.length} / ${fresh.filter(x => x.w.kind === "village").length}`
        ],
            [ "수신 중 NPC / 활성 Actor", `${sum("npc_total")} / ${sum("npc_active")}` ],
            [ "수신 중 생존 몹 / 전체", `${sum("monster_alive")} / ${sum("monster_total")}` ],
            [ "수신 중 야생동물", sum("wildlife") ]]
            .map(([ title, value ]) => {
                const e = node("div", undefined, "metric");
                e.append(node("span", title), node("strong", value));
                return e;
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
            node("h3", w.name + (active ? " · 현재 월드" : "")), node("p", labels[w.phase] || w.phase),
            node("div", `${s.snapshot.project_id} / ${s.snapshot.session_id} / ${w.world_id}`, "meta"),
            node("div", `수신 ${Math.round(s.age_ms / 1000)}초 전 · 순번 ${s.snapshot.sequence}`, "meta"));
        const n = node("div", undefined, "numbers");
        n.append(node("span", `NPC ${w.counts.npc_total}`),
                 node("strong", `몹 ${w.counts.monster_alive} / ${w.counts.monster_total}`));
        e.append(n);
        return e;
    }));
    const search = $("search").value.toLocaleLowerCase(), state = $("stateFilter").value;
    const rows = selected.flatMap(
        x =>
            x.w.npcs.filter(n => (!state || n.state === state) && n.name.toLocaleLowerCase().includes(search))
                .map(n => ({...x, n})));
    $("npcs").replaceChildren(...rows.map(({s, w, n}) => {
        const tr = node("tr");
        const name = node("td", n.name);
        name.append(node("small", `Actor ${n.entity_id}`));
        const status = node("td");
        status.append(badge(labels[n.state] || n.state, n.state === "frozen" ? "dim" : ""));
        if (s.stale || !w.simulated)
            status.append(node("small", s.stale ? "오래된 관측값" : "시뮬레이션 중지"));
        tr.append(name, node("td", `${w.name} · ${s.snapshot.session_id}`), status, node("td", n.behavior),
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
