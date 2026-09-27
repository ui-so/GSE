#pragma once
#include <chrono>
#include <string>
namespace Profiler
{
using Clock = std::chrono::steady_clock;
void Initialize(bool enabled, bool console, const std::string &scenario, unsigned seed, int width,
                int height);
void Shutdown();
void BeginFrame();
void EndFrame();
void Add(const char *name, double value = 1);
void Time(const char *name, double milliseconds);
void Event(const char *name, const std::string &detail);
bool RunTests(std::string &report);
class Scope
{
  public:
    explicit Scope(const char *name);
    ~Scope();

  private:
    const char *name_;
    Clock::time_point start_;
};
class GpuScope
{
  public:
    explicit GpuScope(const char *name);
    ~GpuScope();

  private:
    int slot_ = -1;
};
} // namespace Profiler
