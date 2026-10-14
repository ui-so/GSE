#pragma once
#include <string>
namespace JournalArt
{
bool Initialize();
void Shutdown();
bool Draw(int page, float x, float y, float width, float height);
bool RunTests(std::string &report);
} // namespace JournalArt
