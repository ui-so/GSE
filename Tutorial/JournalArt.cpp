#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include "JournalArt.h"
#include "EmbeddedImage.h"
#include "RenderBatch.h"
#include "Profiler.h"
#include <vector>
namespace JournalArt
{
namespace
{
unsigned textures[3]{};
} // namespace
bool Initialize()
{
    Profiler::Scope scope("journal_art_load");
    bool okay = true;
    for (int page = 0; page < 3; ++page)
    {
        std::vector<unsigned char> pixels;
        UINT width = 0, height = 0;
        if (!EmbeddedImage::Decode(4201 + page, pixels, width, height))
        {
            Profiler::Event("journal_art_load_failed", std::to_string(page));
            okay = false;
            continue;
        }
        glGenTextures(1, &textures[page]);
        glBindTexture(GL_TEXTURE_2D, textures[page]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(width), GLsizei(height), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels.data());
        Profiler::Add("journal_art_decoded_bytes", double(pixels.size()));
        Profiler::Event("journal_art_loaded",
                        std::to_string(page) + ":" + std::to_string(width) + "x" + std::to_string(height));
    }
    return okay;
}
void Shutdown()
{
    glDeleteTextures(3, textures);
    for (auto &texture : textures)
        texture = 0;
}
bool Draw(int page, float x, float y, float width, float height)
{
    if (page < 0 || page >= 3 || !textures[page])
        return false;
    Profiler::Scope scope("journal_art_submit");
    Profiler::Add("journal_images_drawn");
    RenderBatch::Enable(GL_TEXTURE_2D);
    RenderBatch::BindTexture(GL_TEXTURE_2D, textures[page]);
    RenderBatch::Color4f(1, 1, 1, 1);
    RenderBatch::Begin(GL_QUADS);
    RenderBatch::TexCoord2f(0, 0);
    RenderBatch::Vertex2f(x, y);
    RenderBatch::TexCoord2f(1, 0);
    RenderBatch::Vertex2f(x + width, y);
    RenderBatch::TexCoord2f(1, 1);
    RenderBatch::Vertex2f(x + width, y + height);
    RenderBatch::TexCoord2f(0, 1);
    RenderBatch::Vertex2f(x, y + height);
    RenderBatch::End();
    RenderBatch::Disable(GL_TEXTURE_2D);
    return true;
}
bool RunTests(std::string &report)
{
    for (int page = 0; page < 3; ++page)
    {
        std::vector<unsigned char> pixels;
        UINT width = 0, height = 0;
        if (!EmbeddedImage::Decode(4201 + page, pixels, width, height) || width != 1536 || height != 1024 ||
            pixels.size() != size_t(width) * height * 4)
        {
            report += "FAIL: embedded journal PNG decode.\n";
            return false;
        }
    }
    report += "PASS: 3 embedded PNG illustrations decode as RGBA without external files or working-directory "
              "dependency.\n";
    return true;
}
} // namespace JournalArt
