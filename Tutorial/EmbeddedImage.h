#pragma once
#include <vector>

namespace EmbeddedImage
{
bool Decode(int resourceId, std::vector<unsigned char> &pixels, unsigned &width, unsigned &height,
            unsigned maxSide = 4096);
}
