#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#include "Profiler.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
namespace Profiler
{
namespace
{
bool enabled = false, active = false, consoleOutput = false, stopping = false;
std::uint64_t frameId = 0;
Clock::time_point frameStart, sessionStart, previousFrameStart;
double previousEmitMs = 0, frameIntervalMs = 0;
std::map<std::string, double> counters, times;
std::deque<std::string> queue;
std::mutex mutex;
std::condition_variable wake;
std::thread writer;
std::atomic<unsigned> dropped{0};
std::atomic<double> writerMilliseconds{0};
std::filesystem::path directory;
using QueryCounter = void(APIENTRY *)(GLuint, GLenum);
using GenQueries = void(APIENTRY *)(GLsizei, GLuint *);
using DeleteQueries = void(APIENTRY *)(GLsizei, const GLuint *);
using GetQuery = void(APIENTRY *)(GLuint, GLenum, GLint *);
using GetQuery64 = void(APIENTRY *)(GLuint, GLenum, unsigned long long *);
QueryCounter queryCounter = nullptr;
GenQueries genQueries = nullptr;
DeleteQueries deleteQueries = nullptr;
GetQuery getQuery = nullptr;
GetQuery64 getQuery64 = nullptr;
struct Query
{
    GLuint ids[2]{};
    bool used = false, ended = false;
    std::uint64_t frame = 0;
    const char *name = nullptr;
};
std::array<Query, 256> queries;
std::string Escape(const std::string &s)
{
    std::ostringstream o;
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\')
            o << '\\' << c;
        else if (c < 32)
            o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else
            o << c;
    }
    return o.str();
}
std::string Object(const std::map<std::string, double> &values)
{
    std::ostringstream o;
    o << std::fixed << std::setprecision(4) << '{';
    bool first = true;
    for (auto &v : values)
    {
        if (!first)
            o << ',';
        first = false;
        o << '"' << Escape(v.first) << "\":" << v.second;
    }
    o << '}';
    return o.str();
}
void Enqueue(std::string line)
{
    if (!enabled)
        return;
    {
        std::lock_guard<std::mutex> guard(mutex);
        if (queue.size() >= 256)
        {
            ++dropped;
            return;
        }
        queue.push_back(std::move(line));
    }
    wake.notify_one();
}
void Writer()
{
    unsigned part = 0;
    std::uintmax_t bytes = 0;
    std::ofstream file(directory / "profile-0.jsonl", std::ios::binary);
    if (!file)
        ++dropped;
    for (;;)
    {
        std::deque<std::string> batch;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [] { return stopping || !queue.empty(); });
            if (stopping && queue.empty())
                break;
            batch.swap(queue);
        }
        auto start = Clock::now();
        for (auto &line : batch)
        {
            if (bytes + line.size() > 16 * 1024 * 1024)
            {
                file.close();
                part = (part + 1) % 4;
                bytes = 0;
                file.open(directory / ("profile-" + std::to_string(part) + ".jsonl"),
                          std::ios::binary | std::ios::trunc);
            }
            if (line.find("\"type\":\"session\"") != std::string::npos)
            {
                std::ofstream metadata(directory / "session.json", std::ios::binary);
                metadata << line;
            }
            if (file)
            {
                file << line << '\n';
                bytes += line.size() + 1;
            }
            else
                ++dropped;
            if (consoleOutput && line.find("\"type\":\"frame\"") != std::string::npos)
                std::printf("%s\n", line.c_str());
        }
        file.flush();
        writerMilliseconds.store(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
    }
}
void PollGpu()
{
    if (!getQuery)
        return;
    for (auto &q : queries)
        if (q.used && q.ended)
        {
            GLint ready = 0;
            getQuery(q.ids[1], 0x8867, &ready);
            if (!ready)
                continue;
            unsigned long long a = 0, b = 0;
            getQuery64(q.ids[0], 0x8866, &a);
            getQuery64(q.ids[1], 0x8866, &b);
            Enqueue("{\"schema_version\":1,\"type\":\"gpu_span\",\"frame_id\":" + std::to_string(q.frame) +
                    ",\"name\":\"" + q.name + "\",\"gpu_ms\":" + std::to_string(double(b - a) / 1000000.0) +
                    "}");
            q.used = false;
        }
}
} // namespace
void Initialize(bool on, bool console, const std::string &scenario, unsigned seed, int width, int height)
{
    if (!on)
        return;
    std::error_code error;
    directory = std::filesystem::path("profiles") /
                (std::to_string(GetTickCount64()) + "-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        std::fprintf(stderr, "Profiler log directory unavailable\n");
        return;
    }
    enabled = true;
    frameId = 0;
    sessionStart = Clock::now();
    dropped.store(0);
    consoleOutput = console;
    stopping = false;
    writer = std::thread(Writer);
    queryCounter = reinterpret_cast<QueryCounter>(wglGetProcAddress("glQueryCounter"));
    genQueries = reinterpret_cast<GenQueries>(wglGetProcAddress("glGenQueries"));
    deleteQueries = reinterpret_cast<DeleteQueries>(wglGetProcAddress("glDeleteQueries"));
    getQuery = reinterpret_cast<GetQuery>(wglGetProcAddress("glGetQueryObjectiv"));
    getQuery64 = reinterpret_cast<GetQuery64>(wglGetProcAddress("glGetQueryObjectui64v"));
    if (!queryCounter || !genQueries || !deleteQueries || !getQuery || !getQuery64)
    {
        queryCounter = nullptr;
        getQuery = nullptr;
    }
    if (queryCounter)
        for (auto &q : queries)
            genQueries(2, q.ids);
    const auto *renderer = glGetString(GL_RENDERER);
    const auto *version = glGetString(GL_VERSION);
    Enqueue("{\"schema_version\":1,\"type\":\"session\",\"scenario\":\"" + Escape(scenario) +
            "\",\"seed\":" + std::to_string(seed) + ",\"width\":" + std::to_string(width) +
            ",\"height\":" + std::to_string(height) + ",\"renderer\":\"" +
            Escape(renderer ? reinterpret_cast<const char *>(renderer) : "unknown") +
            "\",\"opengl_version\":\"" +
            Escape(version ? reinterpret_cast<const char *>(version) : "unknown") + "\",\"build_id\":\"" +
            std::string(__DATE__ " " __TIME__) +
            "\",\"logical_cpu_count\":" + std::to_string(std::thread::hardware_concurrency()) +
            ",\"gpu_timestamps_available\":" + (queryCounter ? "true" : "false") +
            ",\"frame_budget_ms\":16.6667,\"warmup_frames\":20}");
}
void Shutdown()
{
    if (!enabled)
        return;
    PollGpu();
    unsigned pending = 0;
    for (auto &q : queries)
    {
        if (q.used)
            ++pending;
        if (deleteQueries && q.ids[0])
            deleteQueries(2, q.ids);
        q = {};
    }
    Enqueue("{\"schema_version\":1,\"type\":\"session_end\",\"frames\":" + std::to_string(frameId) +
            ",\"dropped_records\":" + std::to_string(dropped.load()) +
            ",\"pending_gpu_spans\":" + std::to_string(pending) + "}");
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_all();
    writer.join();
    enabled = false;
}
void BeginFrame()
{
    if (!enabled)
        return;
    frameStart = Clock::now();
    frameIntervalMs =
        frameId ? std::chrono::duration<double, std::milli>(frameStart - previousFrameStart).count() : 0;
    previousFrameStart = frameStart;
    ++frameId;
    counters.clear();
    for (const char *name : {"draw_calls",
                             "submitted_vertices",
                             "dynamic_batches",
                             "instanced_draw_calls",
                             "static_draw_calls",
                             "model_instances",
                             "dynamic_upload_bytes",
                             "static_upload_bytes",
                             "scene_nodes_tested",
                             "scene_subtrees_culled",
                             "queued_actors",
                             "terrain_chunks_tested",
                             "terrain_chunks_culled",
                             "terrain_chunks_visible",
                             "collision_queries",
                             "npc_path_requests",
                             "npc_path_cells_visited",
                             "npc_path_failures",
                             "npc_entities_tested",
                             "npc_frozen",
                             "village_monsters_tested",
                             "collision_candidates_tested",
                             "line_of_sight_queries",
                             "ai_entities_tested",
                             "font_glyph_cache_misses",
                             "gpu_spans_skipped",
                             "batch_break_texture",
                             "batch_break_capacity",
                             "batch_break_mesh",
                             "opengl_errors"})
        counters[name] = 0;
    times.clear();
    active = true;
    Scope scope("profiler_gpu_poll");
    PollGpu();
}
void EndFrame()
{
    if (!enabled)
        return;
    auto emitStart = Clock::now();
    counters["profiler_emit_previous_ms"] = previousEmitMs;
    if (frameId % 120 == 0)
    {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                                 sizeof(memory)))
        {
            counters["process_working_set_bytes"] = double(memory.WorkingSetSize);
            counters["process_private_bytes"] = double(memory.PrivateUsage);
        }
    }
    double duration = std::chrono::duration<double, std::milli>(Clock::now() - frameStart).count();
    counters["log_records_dropped_total"] = dropped.load();
    counters["log_writer_last_batch_ms"] = writerMilliseconds.load();
    {
        std::lock_guard<std::mutex> lock(mutex);
        counters["log_queue_depth"] = double(queue.size());
    }
    Enqueue("{\"schema_version\":1,\"type\":\"frame\",\"frame_id\":" + std::to_string(frameId) +
            ",\"frame_start_ms\":" +
            std::to_string(std::chrono::duration<double, std::milli>(frameStart - sessionStart).count()) +
            ",\"warmup\":" + (frameId <= 20 ? "true" : "false") + ",\"frame_ms\":" +
            std::to_string(duration) + ",\"frame_interval_ms\":" + std::to_string(frameIntervalMs) +
            ",\"over_budget\":" + (duration > 16.6667 ? "true" : "false") + ",\"cpu_ms\":" + Object(times) +
            ",\"counters\":" + Object(counters) + "}");
    active = false;
    previousEmitMs = std::chrono::duration<double, std::milli>(Clock::now() - emitStart).count();
}
void Add(const char *name, double value)
{
    if (enabled && active)
        counters[name] += value;
}
void Time(const char *name, double value)
{
    if (enabled && active)
        times[name] += value;
}
void Event(const char *name, const std::string &detail)
{
    Enqueue("{\"schema_version\":1,\"type\":\"event\",\"frame_id\":" + std::to_string(frameId) +
            ",\"name\":\"" + Escape(name) + "\",\"detail\":\"" + Escape(detail) + "\"}");
}
Scope::Scope(const char *name) : name_(name), start_(Clock::now())
{
}
Scope::~Scope()
{
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    if (active)
        Time(name_, ms);
    else if (enabled)
        Enqueue("{\"schema_version\":1,\"type\":\"startup_span\",\"name\":\"" + Escape(name_) +
                "\",\"cpu_ms\":" + std::to_string(ms) + "}");
}
GpuScope::GpuScope(const char *name)
{
    if (!enabled || !active || !queryCounter)
        return;
    for (size_t i = 0; i < queries.size(); ++i)
        if (!queries[i].used)
        {
            slot_ = int(i);
            auto &q = queries[i];
            q.used = true;
            q.ended = false;
            q.name = name;
            q.frame = frameId;
            queryCounter(q.ids[0], 0x8E28);
            return;
        }
    Add("gpu_spans_skipped");
}
GpuScope::~GpuScope()
{
    if (slot_ >= 0)
    {
        auto &q = queries[size_t(slot_)];
        queryCounter(q.ids[1], 0x8E28);
        q.ended = true;
    }
}
bool RunTests(std::string &report)
{
    bool okay = Escape("a\"b\n") == "a\\\"b\\u000a" && Object({{"ms", 1.25}}) == "{\"ms\":1.2500}";
    report += okay ? "PASS: profiler JSON escaping and numeric schema.\n" : "FAIL: profiler schema.\n";
    return okay;
}
} // namespace Profiler
