#include "EndlessMode.hpp"

#include "BulletManager.hpp"
#include "Chain.hpp"
#include "ChainPriorities.hpp"
#include "Enemy.hpp"
#include "GameManager.hpp"
#include "MainMenu.hpp"
#include "NetplaySession.hpp"
#include "Player.hpp"
#include "Rng.hpp"
#include "SoundPlayer.hpp"
#include "Supervisor.hpp"
#include "ZunMath.hpp"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>

namespace th06::EndlessMode
{
namespace
{
constexpr u32 kWarmupFrames = 3 * 60;
constexpr u32 kRampFrames = 5 * 60 * 60;
constexpr i32 kBulletSoftCap = 460;
constexpr f32 kSafeSpawnDistance = 120.0f;
constexpr f32 kSafeSpawnDistanceSq = kSafeSpawnDistance * kSafeSpawnDistance;

struct State
{
    bool selected = false;
    bool active = false;
    u32 elapsedFrames = 0;
    u32 nextWaveFrame = kWarmupFrames;
    u32 nextBurstFrame = 0;
    i32 pendingBursts = 0;
    i32 burstTotal = 0;
    i32 burstInterval = 8;
    i32 family = -1;
    i32 lastFamily = -1;
    i32 repeatedFamily = 0;
    i32 side = 0;
};

State g_State;
ChainElem g_EndlessCalcChain;

i32 RandomInt(i32 range)
{
    return range > 0 ? (i32)g_Rng.GetRandomU32InRange((u32)range) : 0;
}

f32 RandomFloat(f32 range)
{
    return g_Rng.GetRandomF32InRange(range);
}

f32 DistanceSqToPlayer(f32 x, f32 y)
{
    const f32 dx = x - g_Player.positionCenter.x;
    const f32 dy = y - g_Player.positionCenter.y;
    return dx * dx + dy * dy;
}

D3DXVECTOR3 SafeTopEmitter(f32 requestedX = -1.0f, f32 requestedY = -1.0f)
{
    D3DXVECTOR3 result(192.0f, 32.0f, 0.0f);
    for (i32 attempt = 0; attempt < 8; ++attempt)
    {
        result.x = requestedX >= 0.0f ? requestedX : 24.0f + RandomFloat(336.0f);
        result.y = requestedY >= 0.0f ? requestedY : 8.0f + RandomFloat(132.0f);
        if (DistanceSqToPlayer(result.x, result.y) >= kSafeSpawnDistanceSq)
        {
            return result;
        }
        requestedX = -1.0f;
        requestedY = -1.0f;
    }

    const D3DXVECTOR3 candidates[] = {
        D3DXVECTOR3(20.0f, 12.0f, 0.0f),
        D3DXVECTOR3(364.0f, 12.0f, 0.0f),
        D3DXVECTOR3(20.0f, 132.0f, 0.0f),
        D3DXVECTOR3(364.0f, 132.0f, 0.0f),
    };
    result = candidates[0];
    f32 bestDistance = DistanceSqToPlayer(result.x, result.y);
    for (i32 i = 1; i < 4; ++i)
    {
        const f32 distance = DistanceSqToPlayer(candidates[i].x, candidates[i].y);
        if (distance > bestDistance)
        {
            bestDistance = distance;
            result = candidates[i];
        }
    }
    return result;
}

D3DXVECTOR3 SafeSideEmitter(i32 side, f32 requestedY)
{
    D3DXVECTOR3 result(side == 0 ? -18.0f : 402.0f, requestedY, 0.0f);
    if (DistanceSqToPlayer(result.x, result.y) >= kSafeSpawnDistanceSq)
    {
        return result;
    }
    result.y = g_Player.positionCenter.y < 224.0f ? 380.0f : 64.0f;
    return result;
}

f32 Intensity()
{
    if (g_State.elapsedFrames <= kWarmupFrames)
    {
        return 0.0f;
    }
    return std::min(1.0f, (f32)(g_State.elapsedFrames - kWarmupFrames) / (f32)kRampFrames);
}

i32 IntensityTier()
{
    return 1 + std::min(5, (i32)(Intensity() * 6.0f));
}

void FinishProps(EnemyBulletShooter &props, i32 family, bool slow)
{
    static const i16 sprites[] = {0, 1, 2, 4, 5, 7, 8, 10};
    props.sprite = sprites[(family + RandomInt(3)) % (i32)(sizeof(sprites) / sizeof(sprites[0]))];
    props.spriteOffset = (i16)RandomInt(8);
    props.flags = (slow ? 8u : 4u) | 0x200u;
    props.provokedPlayer = 1;
    props.sfx = SOUND_SHOOT_BOSS;
}

void EmitBurst(i32 family, i32 step, i32 total, f32 intensity)
{
    if (g_BulletManager.bulletCount >= kBulletSoftCap)
    {
        return;
    }

    const i32 tier = 1 + std::min(5, (i32)(intensity * 6.0f));
    EnemyBulletShooter props;
    props.count2 = 1;
    props.speed1 = 1.35f + intensity * 1.25f;
    props.speed2 = props.speed1;

    switch (family)
    {
    case 0: // Aimed fan from the upper field.
        props.position = SafeTopEmitter();
        props.aimMode = FAN_AIMED;
        props.angle1 = (RandomFloat(0.18f) - 0.09f);
        props.angle2 = 0.12f + intensity * 0.035f;
        props.count1 = (i16)(5 + tier * 2);
        props.count2 = (i16)(1 + tier / 3);
        props.speed1 = 1.65f + intensity * 1.25f;
        props.speed2 = 1.05f + intensity * 0.75f;
        FinishProps(props, family, false);
        break;

    case 1: // Rotating circular wave.
        props.position = SafeTopEmitter();
        props.aimMode = CIRCLE_AIMED;
        props.angle1 = step * 0.11f + RandomFloat(0.12f);
        props.angle2 = 0.0f;
        props.count1 = (i16)(14 + tier * 3);
        props.speed1 = 1.05f + intensity * 1.2f;
        props.speed2 = props.speed1;
        FinishProps(props, family, false);
        break;

    case 2: // Top-to-bottom sweep, emitted in readable columns.
    {
        const f32 progress = total > 1 ? (f32)step / (f32)(total - 1) : 0.5f;
        props.position = SafeTopEmitter(24.0f + 336.0f * progress, 10.0f);
        props.aimMode = FAN;
        props.angle1 = ZUN_PI * 0.5f + ((step & 1) ? 0.08f : -0.08f);
        props.angle2 = 0.10f;
        props.count1 = (i16)(3 + tier);
        props.speed1 = 1.5f + intensity * 1.15f;
        props.speed2 = props.speed1;
        FinishProps(props, family, false);
        break;
    }

    case 3: // Slow random rain using normal random-angle bullets.
        props.position = SafeTopEmitter();
        props.aimMode = RANDOM;
        props.angle1 = 2.12f;
        props.angle2 = 1.02f;
        props.speed1 = 1.15f + intensity * 0.55f;
        props.speed2 = 0.48f + intensity * 0.25f;
        props.count1 = (i16)(2 + tier);
        props.count2 = 1;
        FinishProps(props, family, true);
        break;

    case 4: // Alternating offset rings.
        props.position = SafeTopEmitter();
        props.aimMode = (step & 1) ? OFFSET_CIRCLE_AIMED : CIRCLE_AIMED;
        props.angle1 = step * (0.15f + intensity * 0.05f);
        props.angle2 = 0.0f;
        props.count1 = (i16)(12 + tier * 3);
        props.speed1 = 0.95f + intensity * 1.0f;
        props.speed2 = props.speed1;
        FinishProps(props, family, true);
        break;

    default: // Side-to-side sweep from outside the playfield.
    {
        const f32 progress = total > 1 ? (f32)step / (f32)(total - 1) : 0.5f;
        props.position = SafeSideEmitter(g_State.side, 48.0f + progress * 300.0f);
        props.aimMode = FAN;
        props.angle1 = g_State.side == 0 ? 0.0f : ZUN_PI;
        props.angle2 = 0.075f + intensity * 0.025f;
        props.count1 = (i16)(3 + tier);
        props.speed1 = 1.45f + intensity * 1.25f;
        props.speed2 = props.speed1;
        FinishProps(props, family, false);
        break;
    }
    }

    g_BulletManager.SpawnBulletPattern(&props);
}

i32 ChooseFamily()
{
    i32 family = RandomInt(6);
    if (family == g_State.lastFamily && g_State.repeatedFamily >= 2)
    {
        family = (family + 1 + RandomInt(5)) % 6;
    }

    if (family == g_State.lastFamily)
    {
        ++g_State.repeatedFamily;
    }
    else
    {
        g_State.lastFamily = family;
        g_State.repeatedFamily = 1;
    }
    return family;
}

void BeginWave()
{
    const f32 intensity = Intensity();
    const i32 tier = IntensityTier();
    g_State.family = ChooseFamily();
    g_State.side = RandomInt(2);

    switch (g_State.family)
    {
    case 0:
    case 1:
        g_State.burstTotal = 1 + tier / 3;
        g_State.burstInterval = 12;
        break;
    case 2:
        g_State.burstTotal = 6 + tier;
        g_State.burstInterval = std::max(6, 12 - tier);
        break;
    case 3:
        g_State.burstTotal = 5 + tier;
        g_State.burstInterval = std::max(7, 14 - tier);
        break;
    case 4:
        g_State.burstTotal = 3 + tier / 2;
        g_State.burstInterval = std::max(9, 17 - tier);
        break;
    default:
        g_State.burstTotal = 5 + tier;
        g_State.burstInterval = std::max(7, 13 - tier);
        break;
    }

    g_State.pendingBursts = g_State.burstTotal;
    g_State.nextBurstFrame = g_State.elapsedFrames;
    (void)intensity;
}

ChainCallbackResult OnUpdate(void *)
{
    if (!g_State.active)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }
    if (g_GameManager.isTimeStopped || g_GameManager.isInGameMenu || g_GameManager.isInRetryMenu)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }

    ++g_State.elapsedFrames;
    if (g_State.elapsedFrames % 60 == 0 && g_GameManager.score <= 999999994u)
    {
        g_GameManager.AddScore(5);
    }
    if (g_State.elapsedFrames < kWarmupFrames)
    {
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }

    if (g_State.pendingBursts > 0 && g_State.elapsedFrames >= g_State.nextBurstFrame)
    {
        if (g_BulletManager.bulletCount >= kBulletSoftCap)
        {
            g_State.nextBurstFrame = g_State.elapsedFrames + 15;
            return CHAIN_CALLBACK_RESULT_CONTINUE;
        }

        const i32 step = g_State.burstTotal - g_State.pendingBursts;
        EmitBurst(g_State.family, step, g_State.burstTotal, Intensity());
        --g_State.pendingBursts;
        g_State.nextBurstFrame = g_State.elapsedFrames + (u32)g_State.burstInterval;
        if (g_State.pendingBursts == 0)
        {
            const u32 gap = (u32)(96.0f - Intensity() * 62.0f) + (u32)RandomInt(25);
            g_State.nextWaveFrame = g_State.elapsedFrames + gap;
        }
        return CHAIN_CALLBACK_RESULT_CONTINUE;
    }

    const u32 restCycle = g_State.elapsedFrames % (34 * 60);
    const bool resting = restCycle >= 30 * 60;
    if (!resting && g_State.pendingBursts == 0 && g_State.elapsedFrames >= g_State.nextWaveFrame)
    {
        BeginWave();
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

void GetVmBounds(const AnmVm &vm, f32 &left, f32 &top, f32 &right, f32 &bottom)
{
    if (vm.sprite == nullptr)
    {
        left = 300.0f;
        top = 397.0f;
        right = 570.0f;
        bottom = 467.0f;
        return;
    }

    const f32 width = std::fabs(vm.sprite->widthPx * vm.scaleX);
    const f32 height = std::fabs(vm.sprite->heightPx * vm.scaleY);
    const f32 x = vm.pos.x + vm.posOffset.x;
    const f32 y = vm.pos.y + vm.posOffset.y;
    left = (vm.flags.anchor & AnmVmAnchor_Left) ? x : x - width * 0.5f;
    top = (vm.flags.anchor & AnmVmAnchor_Top) ? y : y - height * 0.5f;
    right = left + width;
    bottom = top + height;
}
} // namespace

bool IsSelected()
{
    return g_State.selected;
}

bool IsActive()
{
    return g_State.active;
}

void SetSelected(bool selected)
{
    g_State.selected = selected;
    if (!selected)
    {
        g_State.active = false;
    }
}

void Reset()
{
    CutChain();
    g_State = State {};
}

void ConfigurePracticeDifficultyMenu(MainMenu *menu)
{
    if (menu == nullptr || menu->gameState != STATE_DIFFICULTY_SELECT || !g_GameManager.isInPracticeMode ||
        Session::IsDualPlayerSession())
    {
        return;
    }

    static const f32 centersY[5] = {116.0f, 194.0f, 272.0f, 350.0f, 428.0f};
    const f32 commonX = menu->vm[84].pos.x;
    for (i32 i = 0; i < 5; ++i)
    {
        AnmVm &vm = menu->vm[81 + i];
        vm.pos.x = commonX;
        vm.pos.y = centersY[i];
        vm.scaleX = 0.84f;
        vm.scaleY = 0.72f;
        vm.flags.isVisible = true;
    }
}

ZunResult RegisterChain()
{
    if (!g_State.selected || Session::IsDualPlayerSession())
    {
        return ZUN_ERROR;
    }

    g_Chain.Cut(&g_EndlessCalcChain);
    g_EndlessCalcChain.prev = nullptr;
    g_EndlessCalcChain.next = nullptr;
    g_EndlessCalcChain.unkPtr = &g_EndlessCalcChain;
    g_EndlessCalcChain.priority = 0;
    g_EndlessCalcChain.callback = OnUpdate;
    g_EndlessCalcChain.addedCallback = nullptr;
    g_EndlessCalcChain.deletedCallback = nullptr;
    g_EndlessCalcChain.arg = nullptr;

    g_State.active = true;
    g_State.elapsedFrames = 0;
    g_State.nextWaveFrame = kWarmupFrames;
    g_State.nextBurstFrame = 0;
    g_State.pendingBursts = 0;
    g_State.burstTotal = 0;
    g_State.family = -1;
    g_State.lastFamily = -1;
    g_State.repeatedFamily = 0;
    return g_Chain.AddToCalcChain(&g_EndlessCalcChain, TH_CHAIN_PRIO_CALC_ENEMYMANAGER) == 0 ? ZUN_SUCCESS : ZUN_ERROR;
}

void CutChain()
{
    g_Chain.Cut(&g_EndlessCalcChain);
    g_EndlessCalcChain.prev = nullptr;
    g_EndlessCalcChain.next = nullptr;
    g_EndlessCalcChain.unkPtr = &g_EndlessCalcChain;
    g_State.active = false;
}

void DrawImGuiOverlay()
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    ImDrawList *draw = ImGui::GetForegroundDrawList();
    if (g_MainMenu.gameState == STATE_DIFFICULTY_SELECT && g_GameManager.isInPracticeMode &&
        !Session::IsDualPlayerSession())
    {
        f32 left, top, right, bottom;
        GetVmBounds(g_MainMenu.vm[85], left, top, right, bottom);
        const bool selected = g_MainMenu.cursor == 4;
        const ImU32 fill = selected ? IM_COL32(240, 240, 240, 250) : IM_COL32(110, 110, 110, 238);
        const ImU32 border = selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(175, 175, 175, 220);
        const ImU32 title = selected ? IM_COL32(215, 45, 35, 255) : IM_COL32(245, 175, 65, 235);
        const ImU32 detail = selected ? IM_COL32(20, 20, 20, 255) : IM_COL32(25, 25, 25, 225);
        draw->AddRectFilled(ImVec2(left + 2.0f, top + 2.0f), ImVec2(right - 2.0f, bottom - 2.0f), fill);
        draw->AddRect(ImVec2(left, top), ImVec2(right, bottom), border, 0.0f, 0, selected ? 2.0f : 1.0f);
        draw->AddText(ImVec2(left + 14.0f, top + 8.0f), title, "无尽 Endless");
        draw->AddText(ImVec2(left + 14.0f, top + 30.0f), detail, "随机弹幕  生存 +5分/秒");
    }

    if (g_State.active)
    {
        const u32 seconds = g_State.elapsedFrames / 60;
        char line[96];
        std::snprintf(line, sizeof(line), "无尽 ENDLESS   %02u:%02u   强度 %d/6",
                      (unsigned)(seconds / 60), (unsigned)(seconds % 60), IntensityTier());
        const ImVec2 textSize = ImGui::CalcTextSize(line);
        const ImVec2 min(42.0f, 24.0f);
        const ImVec2 max(min.x + textSize.x + 18.0f, min.y + textSize.y + 12.0f);
        draw->AddRectFilled(min, max, IM_COL32(0, 0, 0, 150));
        draw->AddRect(min, max, IM_COL32(255, 220, 130, 165));
        draw->AddText(ImVec2(min.x + 9.0f, min.y + 6.0f), IM_COL32(255, 238, 180, 255), line);
    }
}
} // namespace th06::EndlessMode
