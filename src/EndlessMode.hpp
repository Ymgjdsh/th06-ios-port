#pragma once

#include "ZunResult.hpp"
#include "inttypes.hpp"

struct ImDrawList;

namespace th06
{
struct MainMenu;

namespace EndlessMode
{
bool IsSelected();
bool IsActive();
u32 SurvivalFrames();
i32 IntensityLevel();
void SetSelected(bool selected);
void Reset();

void ConfigurePracticeDifficultyMenu(MainMenu *menu);
ZunResult RegisterChain();
void CutChain();
void DrawImGuiOverlay();
} // namespace EndlessMode
} // namespace th06
