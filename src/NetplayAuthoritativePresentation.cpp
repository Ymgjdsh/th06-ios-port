#include "NetplayAuthoritativePresentation.hpp"
#include <new>

namespace th06::Netplay::AuthoritativePresentation
{
namespace
{
LocalPredictionState g_LocalPredictionState {};

Player *GetLocalPlayer()
{
    if (!Session::IsRemoteNetplaySession() || !g_State.isSessionActive)
    {
        return nullptr;
    }
    return IsLocalPlayer1() ? &g_Player : &g_Player2;
}

void ClampToMovementBounds(D3DXVECTOR3 &position)
{
    if (position.x < g_GameManager.playerMovementAreaTopLeftPos.x)
    {
        position.x = g_GameManager.playerMovementAreaTopLeftPos.x;
    }
    else if (position.x > g_GameManager.playerMovementAreaTopLeftPos.x + g_GameManager.playerMovementAreaSize.x)
    {
        position.x = g_GameManager.playerMovementAreaTopLeftPos.x + g_GameManager.playerMovementAreaSize.x;
    }

    if (position.y < g_GameManager.playerMovementAreaTopLeftPos.y)
    {
        position.y = g_GameManager.playerMovementAreaTopLeftPos.y;
    }
    else if (position.y > g_GameManager.playerMovementAreaTopLeftPos.y + g_GameManager.playerMovementAreaSize.y)
    {
        position.y = g_GameManager.playerMovementAreaTopLeftPos.y + g_GameManager.playerMovementAreaSize.y;
    }
}

void EnsureInitialized(int frame)
{
    Player *localPlayer = GetLocalPlayer();
    if (localPlayer == nullptr)
    {
        return;
    }

    const bool localIsPlayer1 = IsLocalPlayer1();
    const int stage = g_GameManager.currentStage;
    const int supervisorState = static_cast<int>(g_Supervisor.curState);
    if (g_LocalPredictionState.valid && g_LocalPredictionState.localIsPlayer1 == localIsPlayer1 &&
        g_LocalPredictionState.stage == stage && g_LocalPredictionState.supervisorState == supervisorState)
    {
        return;
    }

    g_LocalPredictionState.valid = true;
    g_LocalPredictionState.localIsPlayer1 = localIsPlayer1;
    g_LocalPredictionState.stage = stage;
    g_LocalPredictionState.supervisorState = supervisorState;
    // Start one frame behind so the first captured touch sample is applied.
    g_LocalPredictionState.visualFrame = frame - 1;
    g_LocalPredictionState.displayPosition = localPlayer->positionCenter;
    g_LocalPredictionState.authoritativePosition = localPlayer->positionCenter;
    g_LocalPredictionState.authoritativeOrbs[0] = localPlayer->orbsPosition[0];
    g_LocalPredictionState.authoritativeOrbs[1] = localPlayer->orbsPosition[1];
}

void AdvancePredictedPosition(Player &player, u16 inputBits, const TouchFrameData *touchData,
                              D3DXVECTOR3 &position)
{
    if (touchData != nullptr && (touchData->flags & TouchFrameData::kFlagAnalog) != 0)
    {
        // TouchFrameData stores the same per-game-frame displacement that the
        // canonical Player path consumes.  Apply it directly, including the
        // frame multiplier, so the local preview has the exact same trajectory.
        position.x += static_cast<float>(touchData->analogX) * g_Supervisor.effectiveFramerateMultiplier;
        position.y += static_cast<float>(touchData->analogY) * g_Supervisor.effectiveFramerateMultiplier;
        ClampToMovementBounds(position);
        return;
    }

    const bool upPressed = (inputBits & TH_BUTTON_UP) != 0;
    const bool downPressed = (inputBits & TH_BUTTON_DOWN) != 0;
    const bool leftPressed = (inputBits & TH_BUTTON_LEFT) != 0;
    const bool rightPressed = (inputBits & TH_BUTTON_RIGHT) != 0;
    const bool focusPressed = (inputBits & TH_BUTTON_FOCUS) != 0;

    PlayerDirection direction = MOVEMENT_NONE;
    if (upPressed)
    {
        direction = MOVEMENT_UP;
        if (leftPressed)
        {
            direction = MOVEMENT_UP_LEFT;
        }
        if (rightPressed)
        {
            direction = MOVEMENT_UP_RIGHT;
        }
    }
    else if (downPressed)
    {
        direction = MOVEMENT_DOWN;
        if (leftPressed)
        {
            direction = MOVEMENT_DOWN_LEFT;
        }
        if (rightPressed)
        {
            direction = MOVEMENT_DOWN_RIGHT;
        }
    }
    else
    {
        if (leftPressed)
        {
            direction = MOVEMENT_LEFT;
        }
        if (rightPressed)
        {
            direction = MOVEMENT_RIGHT;
        }
    }

    float horizontalSpeed = 0.0f;
    float verticalSpeed = 0.0f;
    switch (direction)
    {
    case MOVEMENT_RIGHT:
        horizontalSpeed = focusPressed ? player.characterData.orthogonalMovementSpeedFocus
                                       : player.characterData.orthogonalMovementSpeed;
        break;
    case MOVEMENT_LEFT:
        horizontalSpeed = focusPressed ? -player.characterData.orthogonalMovementSpeedFocus
                                       : -player.characterData.orthogonalMovementSpeed;
        break;
    case MOVEMENT_UP:
        verticalSpeed = focusPressed ? -player.characterData.orthogonalMovementSpeedFocus
                                     : -player.characterData.orthogonalMovementSpeed;
        break;
    case MOVEMENT_DOWN:
        verticalSpeed = focusPressed ? player.characterData.orthogonalMovementSpeedFocus
                                     : player.characterData.orthogonalMovementSpeed;
        break;
    case MOVEMENT_UP_LEFT:
        horizontalSpeed = focusPressed ? -player.characterData.diagonalMovementSpeedFocus
                                       : -player.characterData.diagonalMovementSpeed;
        verticalSpeed = horizontalSpeed;
        break;
    case MOVEMENT_DOWN_LEFT:
        horizontalSpeed = focusPressed ? -player.characterData.diagonalMovementSpeedFocus
                                       : -player.characterData.diagonalMovementSpeed;
        verticalSpeed = -horizontalSpeed;
        break;
    case MOVEMENT_UP_RIGHT:
        horizontalSpeed = focusPressed ? player.characterData.diagonalMovementSpeedFocus
                                       : player.characterData.diagonalMovementSpeed;
        verticalSpeed = -horizontalSpeed;
        break;
    case MOVEMENT_DOWN_RIGHT:
        horizontalSpeed = focusPressed ? player.characterData.diagonalMovementSpeedFocus
                                       : player.characterData.diagonalMovementSpeed;
        verticalSpeed = horizontalSpeed;
        break;
    default:
        break;
    }

    position.x += horizontalSpeed * player.horizontalMovementSpeedMultiplierDuringBomb *
                  g_Supervisor.effectiveFramerateMultiplier;
    position.y += verticalSpeed * player.verticalMovementSpeedMultiplierDuringBomb *
                  g_Supervisor.effectiveFramerateMultiplier;
    ClampToMovementBounds(position);
}
} // namespace

void Reset()
{
    g_LocalPredictionState.~LocalPredictionState();
    new (&g_LocalPredictionState) LocalPredictionState();
}

void NoteLocalPredictedInput(int frame, u16 inputBits, const TouchFrameData *touchData)
{
    EnsureInitialized(frame);
    Player *localPlayer = GetLocalPlayer();
    if (localPlayer == nullptr || !g_LocalPredictionState.valid)
    {
        return;
    }

    if (g_LocalPredictionState.visualFrame < frame)
    {
        AdvancePredictedPosition(*localPlayer, inputBits, touchData, g_LocalPredictionState.displayPosition);
    }

    g_LocalPredictionState.inputBits = inputBits;
    g_LocalPredictionState.visualFrame = frame;
}

void SyncFromCanonicalLocalPlayer(int frame)
{
    EnsureInitialized(frame);
    Player *localPlayer = GetLocalPlayer();
    if (localPlayer == nullptr || !g_LocalPredictionState.valid)
    {
        return;
    }

    g_LocalPredictionState.authoritativePosition = localPlayer->positionCenter;
    g_LocalPredictionState.authoritativeOrbs[0] = localPlayer->orbsPosition[0];
    g_LocalPredictionState.authoritativeOrbs[1] = localPlayer->orbsPosition[1];

    const float dx = g_LocalPredictionState.authoritativePosition.x - g_LocalPredictionState.displayPosition.x;
    const float dy = g_LocalPredictionState.authoritativePosition.y - g_LocalPredictionState.displayPosition.y;
    const float distanceSq = dx * dx + dy * dy;
    // A touch sample is encoded as an i8 displacement (up to 127 pixels per
    // simulation frame).  The canonical position can therefore legitimately
    // trail the local preview by the whole delay window.  Do not mistake that
    // expected prediction gap for a desync and erase a fast drag.
    const float predictionWindow = g_State.authoritativeModeEnabled
                                       ? 48.0f
                                       : static_cast<float>(std::max(96, (g_State.delay + 2) * 127));
    const bool snap = localPlayer->playerState == PLAYER_STATE_DEAD ||
                      localPlayer->playerState == PLAYER_STATE_SPAWNING ||
                      distanceSq > predictionWindow * predictionWindow;
    if (snap)
    {
        g_LocalPredictionState.displayPosition = g_LocalPredictionState.authoritativePosition;
    }
    else if (g_State.authoritativeModeEnabled)
    {
        g_LocalPredictionState.displayPosition.x += dx * 0.35f;
        g_LocalPredictionState.displayPosition.y += dy * 0.35f;
    }

    g_LocalPredictionState.displayPosition.z = g_LocalPredictionState.authoritativePosition.z;
    g_LocalPredictionState.visualFrame = std::max(g_LocalPredictionState.visualFrame, frame);
}

void ReconcileFromAuthoritativeState(const AuthoritativeFrameState &state)
{
    if (!state.valid)
    {
        return;
    }

    EnsureInitialized(state.serverFrame);
    if (!g_LocalPredictionState.valid)
    {
        return;
    }

    if (g_LocalPredictionState.visualFrame < state.serverFrame)
    {
        g_LocalPredictionState.visualFrame = state.serverFrame;
    }
}

bool TryGetRenderOverride(const Player *player, D3DXVECTOR3 &outPosition, D3DXVECTOR3 outOrbs[2])
{
    if (!g_LocalPredictionState.valid || !Session::IsRemoteNetplaySession())
    {
        return false;
    }

    Player *localPlayer = GetLocalPlayer();
    if (localPlayer == nullptr || player != localPlayer)
    {
        return false;
    }

    outPosition = g_LocalPredictionState.displayPosition;
    const D3DXVECTOR3 delta = g_LocalPredictionState.displayPosition - g_LocalPredictionState.authoritativePosition;
    outOrbs[0] = g_LocalPredictionState.authoritativeOrbs[0] + delta;
    outOrbs[1] = g_LocalPredictionState.authoritativeOrbs[1] + delta;
    outOrbs[0].z = g_LocalPredictionState.authoritativeOrbs[0].z;
    outOrbs[1].z = g_LocalPredictionState.authoritativeOrbs[1].z;
    return true;
}
} // namespace th06::Netplay::AuthoritativePresentation
