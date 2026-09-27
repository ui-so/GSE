#pragma once
#include <string>
#include <cstddef>
namespace DrawStats
{
void RecordDraw(std::size_t vertices, std::size_t instances);
void InitializeConsole(bool hidden);
void Begin(unsigned int mode);
void NewList(unsigned int list, unsigned int mode);
void EndList();
void CallList(unsigned int list);
void RegisterBitmap(unsigned int list);
bool RunTests(std::string &report);
class Frame
{
  public:
    Frame();
    ~Frame();
    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;
};
} // namespace DrawStats
