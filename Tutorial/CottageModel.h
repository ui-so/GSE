#pragma once
#include "RenderBatch.h"
#include <string>
#include <vector>

namespace CottageModel
{
std::vector<RenderBatch::ModelVertex> Load();
bool RunTests(std::string &report);
} // namespace CottageModel
