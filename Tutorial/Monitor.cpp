#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Monitor.h"
#include "Profiler.h"
#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <locale>
#include <mutex>
#include <sstream>
#include <thread>
#pragma comment(lib, "winhttp.lib")
namespace Monitor
{
namespace
{
std::mutex mutex;
std::condition_variable wake;
std::thread worker;
std::string pending, session;
bool enabled = false, stopping = false;
std::uint64_t sequence = 0;
std::atomic<std::uint64_t> sent{0}, failed{0}, replaced{0};
std::chrono::steady_clock::time_point next;
std::string Utf8(const std::wstring &s)
{
    if (s.empty())
        return {};
    int length = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size_t(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), result.data(), length, nullptr, nullptr);
    return result;
}
std::string Quote(const std::string &s)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\')
            out << '\\' << char(c);
        else if (c < 32)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else
            out << char(c);
    }
    out << '"';
    return out.str();
}
// Only the worker owns WinHTTP handles. No Actor or profiler access from this thread.
void SendLoop()
{
    HINTERNET http = WinHttpOpen(L"GSE-Monitor/1", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (http)
        WinHttpSetTimeouts(http, 300, 300, 300, 300);
    HINTERNET connection = http ? WinHttpConnect(http, L"127.0.0.1", 8765, 0) : nullptr;
    for (;;)
    {
        std::string body;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [] { return stopping || !pending.empty(); });
            if (stopping)
                break;
            body = std::move(pending);
            pending.clear();
        }
        HINTERNET request = connection
                                ? WinHttpOpenRequest(connection, L"POST", L"/api/snapshots", nullptr,
                                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0)
                                : nullptr;
        bool okay = false;
        if (request)
        {
            DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
            if (WinHttpSendRequest(request, L"Content-Type: application/json\r\n", DWORD(-1), body.data(),
                                   DWORD(body.size()), DWORD(body.size()), 0) &&
                WinHttpReceiveResponse(request, nullptr))
            {
                DWORD status = 0, length = sizeof(status);
                okay = WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &length,
                                           WINHTTP_NO_HEADER_INDEX) &&
                       status == 202;
            }
            WinHttpCloseHandle(request);
        }
        if (okay)
            ++sent;
        else
            ++failed;
    }
    if (connection)
        WinHttpCloseHandle(connection);
    if (http)
        WinHttpCloseHandle(http);
}
} // namespace
std::string Serialize(const std::vector<Region> &regions, const std::string &activeWorld, bool paused,
                      const std::string &sessionId, std::uint64_t seq)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << std::fixed << std::setprecision(3);
    out << "{\"schema_version\":1,\"type\":\"world_snapshot\",\"project_id\":\"gse\",\"session_id\":"
        << Quote(sessionId) << ",\"sequence\":" << seq << ",\"active_world_id\":" << Quote(activeWorld)
        << ",\"paused\":" << paused << ",\"worlds\":[";
    bool firstRegion = true;
    for (const auto &r : regions)
    {
        if (!firstRegion)
            out << ',';
        firstRegion = false;
        int activeCount = 0;
        for (const auto &n : r.npcs)
            if (n.active)
                ++activeCount;
        out << "{\"world_id\":" << Quote(r.id) << ",\"village_id\":" << Quote(r.kind == "village" ? r.id : "")
            << ",\"kind\":" << Quote(r.kind) << ",\"name\":" << Quote(Utf8(r.name))
            << ",\"phase\":" << Quote(r.phase) << ",\"simulated\":" << r.simulated
            << ",\"counts\":{\"npc_total\":" << r.npcs.size() << ",\"npc_active\":" << activeCount
            << ",\"monster_alive\":" << r.monstersAlive << ",\"monster_total\":" << r.monstersTotal
            << ",\"wildlife\":" << r.wildlife << "},\"npcs\":[";
        bool firstNpc = true;
        for (const auto &n : r.npcs)
        {
            if (!firstNpc)
                out << ',';
            firstNpc = false;
            out << "{\"entity_id\":" << Quote(std::to_string(n.actor)) << ",\"name\":" << Quote(Utf8(n.name))
                << ",\"state\":" << Quote(n.state) << ",\"behavior\":" << Quote(Utf8(n.behavior))
                << ",\"reaction\":" << Quote(n.reaction) << ",\"service\":" << Quote(n.service)
                << ",\"active\":" << n.active << ",\"recorded\":" << n.recorded
                << ",\"position\":{\"x\":" << n.x << ",\"y\":" << n.y << "}}";
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}
void Initialize(bool enable)
{
    if (enabled || !enable)
        return;
    session = std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
    sequence = 0;
    sent = 0;
    failed = 0;
    replaced = 0;
    stopping = false;
    pending.clear();
    next = std::chrono::steady_clock::now();
    enabled = true;
    worker = std::thread(SendLoop);
}
bool Due()
{
    if (!enabled)
        return false;
    Profiler::Add("monitor_sent_total", double(sent.load()));
    Profiler::Add("monitor_failed_total", double(failed.load()));
    Profiler::Add("monitor_replaced_total", double(replaced.load()));
    auto now = std::chrono::steady_clock::now();
    if (now < next)
        return false;
    next = now + std::chrono::milliseconds(500);
    return true;
}
void Publish(const std::vector<Region> &regions, const std::string &activeWorld, bool paused)
{
    if (!enabled)
        return;
    Profiler::Scope scope("monitor_serialize_enqueue");
    auto body = Serialize(regions, activeWorld, paused, session, ++sequence);
    Profiler::Add("monitor_snapshot_bytes", double(body.size()));
    Profiler::Add("monitor_snapshots_built");
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!pending.empty())
            ++replaced;
        pending = std::move(body);
    }
    wake.notify_one();
}
void Shutdown()
{
    if (!enabled)
        return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        pending.clear();
    }
    wake.notify_one();
    worker.join();
    enabled = false;
}
bool RunTests(std::string &report)
{
    Region r;
    r.id = "village";
    r.kind = "village";
    r.name = L"마을";
    Npc n;
    n.name = L"한글 \"이름\"\n";
    n.actor = 9;
    n.active = true;
    r.npcs.push_back(n);
    auto json = Serialize({r}, "village", true, "test", 1);
    bool okay = json.find("\\u000a") != std::string::npos && json.find("\\\"이름\\\"") != std::string::npos &&
                json.find("\"npc_active\":1") != std::string::npos;
    report +=
        okay ? "PASS: monitor UTF-8 JSON escaping and active counts.\n" : "FAIL: monitor serialization.\n";
    return okay;
}
} // namespace Monitor
