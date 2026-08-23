#pragma once

#include "NetplayInternal.hpp"

namespace th06::Netplay::AuthoritativePresentation
{
struct LocalPredictionState
{
    bool valid = false;
    bool localIsPlayer1 = true;
    int stage = -1;
    int supervisorState = -1;
    int visualFrame = -1;
    u16 inputBits = 0;
    D3DXVECTOR3 displayPosition {};
    D3DXVECTOR3 authoritativePosition {};
    D3DXVECTOR3 authoritativeOrbs[2] {};
};

void Reset();
// Predict only the local player's presentation.  The canonical simulation
// remains lockstep-delayed; touch displacement is replayed here immediately
// so the local device does not feel the network delay.
void NoteLocalPredictedInput(int frame, u16 inputBits, const TouchFrameData *touchData = nullptr);
void SyncFromCanonicalLocalPlayer(int frame);
void ReconcileFromAuthoritativeState(const AuthoritativeFrameState &state);
bool TryGetRenderOverride(const Player *player, D3DXVECTOR3 &outPosition, D3DXVECTOR3 outOrbs[2]);
} // namespace th06::Netplay::AuthoritativePresentation
