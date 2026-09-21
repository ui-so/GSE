#pragma once
namespace SceneModels
{
enum class Kind
{
    Tree,
    Rock,
    Boar,
    Wraith,
    Potion,
    Coin,
    Brazier,
    Count
};
void Initialize();
void Draw(Kind kind, float x, float y, float scale = 1, float phase = 0, bool flash = false);
} // namespace SceneModels
