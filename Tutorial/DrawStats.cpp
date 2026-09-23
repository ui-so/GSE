#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include "DrawStats.h"
#include <cstdio>
#include <cstdint>
#include <unordered_map>
namespace DrawStats
{
namespace
{
struct Counter
{
    std::uint64_t calls = 0, batches = 0, listCalls = 0;
    GLuint compiling = 0;
    std::uint64_t compiledBatches = 0;
    GLenum mode = GL_COMPILE;
    bool active = false;
    std::unordered_map<GLuint, std::uint64_t> lists;
    void Primitive()
    {
        if (compiling)
            ++compiledBatches;
        if (active && (!compiling || mode == GL_COMPILE_AND_EXECUTE))
        {
            ++calls;
            ++batches;
        }
    }
    void List(GLuint id)
    {
        auto found = lists.find(id);
        auto count = found == lists.end() ? 0 : found->second;
        if (compiling)
            compiledBatches += count;
        if (active && (!compiling || mode == GL_COMPILE_AND_EXECUTE))
        {
            ++calls;
            ++listCalls;
            batches += count;
        }
    }
    void Start()
    {
        calls = batches = listCalls = 0;
        active = true;
    }
};
Counter counter;
std::uint64_t frameNumber = 0;
} // namespace
void InitializeConsole(bool hidden)
{
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output && output != INVALID_HANDLE_VALUE && GetFileType(output) != FILE_TYPE_UNKNOWN)
        return;
    if (hidden)
        return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole())
        return;
    FILE *stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    SetConsoleTitleW(L"GSE - Draw Calls / Frame");
}
void Begin(unsigned int mode)
{
    counter.Primitive();
    glBegin(mode);
}
void NewList(unsigned int list, unsigned int mode)
{
    counter.compiling = list;
    counter.compiledBatches = 0;
    counter.mode = mode;
    glNewList(list, mode);
}
void EndList()
{
    glEndList();
    counter.lists[counter.compiling] = counter.compiledBatches;
    counter.compiling = 0;
}
void CallList(unsigned int list)
{
    counter.List(list);
    glCallList(list);
}
void RegisterBitmap(unsigned int list)
{
    counter.lists[list] = 1;
}
Frame::Frame()
{
    counter.Start();
}
Frame::~Frame()
{
    counter.active = false;
    std::printf(
        "[Frame %llu] draw_calls=%llu list_calls=%llu primitive_batches=%llu\n",
        static_cast<unsigned long long>(++frameNumber), static_cast<unsigned long long>(counter.calls),
        static_cast<unsigned long long>(counter.listCalls), static_cast<unsigned long long>(counter.batches));
    std::fflush(stdout);
}
bool RunTests(std::string &report)
{
    Counter test;
    test.Start();
    test.Primitive();
    test.compiling = 4;
    test.Primitive();
    test.Primitive();
    test.lists[4] = test.compiledBatches;
    test.compiling = 0;
    test.List(4);
    test.List(4);
    bool okay = test.calls == 3 && test.batches == 5 && test.listCalls == 2;
    test.Start();
    test.List(4);
    okay = okay && test.calls == 1 && test.batches == 2 && test.listCalls == 1;
    test.lists[5] = 1;
    test.List(5);
    okay = okay && test.calls == 2 && test.batches == 3;
    report +=
        okay ? "PASS: draw stats exclude list compilation, count list replay/bitmap, and reset per frame.\n"
             : "FAIL: draw stats.\n";
    return okay;
}
} // namespace DrawStats
