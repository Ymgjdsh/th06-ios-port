#include "Supervisor.hpp"
#include "AnmManager.hpp"
#include "AsciiManager.hpp"
#include "AssetIO.hpp"
#include "thprac_th06.h"
#include "Chain.hpp"
#include "ChainPriorities.hpp"
#include "Controller.hpp"
#include "Ending.hpp"
#include "FileSystem.hpp"
#include "GameErrorContext.hpp"
#include "GamePaths.hpp"
#include "GameManager.hpp"
#include "GameWindow.hpp"
#include "MainMenu.hpp"
#include "NetplaySession.hpp"
#include "MusicRoom.hpp"
#include "PortableGameplayRestore.hpp"
#include "ReplayManager.hpp"
#include "ResultScreen.hpp"
#include "Rng.hpp"
#include "Session.hpp"
#include "SinglePlayerSnapshot.hpp"
#include "SoundPlayer.hpp"
#include "TextHelper.hpp"
#include "i18n.hpp"
#include "inttypes.hpp"
#include "utils.hpp"
#include "sdl2_renderer.hpp"
#include "AndroidTouchInput.hpp"
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include <SDL_image.h>
#ifndef _WIN32
#ifdef __ANDROID__
#include <dirent.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#else
#include <glob.h>
#endif
#endif

namespace th06
{
DIFFABLE_STATIC(Supervisor, g_Supervisor)
DIFFABLE_STATIC_ASSIGN(ControllerMapping, g_ControllerMapping) = {0, 1, 0, -1, -1, -1, -1, -1, -1};
DIFFABLE_STATIC(SoftSurface *, g_TextBufferSurface)
DIFFABLE_STATIC(u16, g_LastFrameInput);
DIFFABLE_STATIC(u16, g_CurFrameInput);
DIFFABLE_STATIC(u16, g_IsEigthFrameOfHeldInput);
DIFFABLE_STATIC(u16, g_NumOfFramesInputsWereHeld);

static bool IsUninitializedControllerMapping(const ControllerMapping &mapping)
{
    return mapping.shootButton == 0 && mapping.bombButton == 0 && mapping.focusButton == 0 &&
           mapping.menuButton == 0 && mapping.upButton == 0 && mapping.downButton == 0 &&
           mapping.leftButton == 0 && mapping.rightButton == 0 && mapping.skipButton == 0;
}

static bool HasPackagedBgm()
{
    static const char *candidates[] = {
        "bgm/th06_01.ogg",
        "bgm/th06_01.wav",
    };
    for (const char *path : candidates)
    {
        SDL_RWops *rw = AssetIO::OpenRW(path);
        if (rw != nullptr)
        {
            SDL_RWclose(rw);
            return true;
        }
    }
    return false;
}

ChainCallbackResult Supervisor::OnUpdate(Supervisor *s)
{

    if (g_SoundPlayer.backgroundMusic != NULL)
    {
        g_SoundPlayer.backgroundMusic->UpdateFadeOut();
    }
    Session::AdvanceFrameInput();
    if (Session::IsRemoteNetplaySession())
    {
        // In netplay, clear transient touch state after it has been captured.
        // Gameplay consumes deterministic lockstep input; the local sprite
        // preview is reconciled independently after the player update.
        AndroidTouchInput::ClearPendingTouchAfterCapture();
        Netplay::ApplyReceivedRemoteTouchData();
        Netplay::RouteAnalogInputsToPlayers();
        AndroidTouchInput::InjectDeferredButtons();
    }
    else
    {
        AndroidTouchInput::InjectDeferredButtons();
    }
    PortableGameplayRestore::TickPortableRestore();
    if (PortableGameplayRestore::ConsumeFrameBreakRequested())
    {
        return CHAIN_CALLBACK_RESULT_BREAK;
    }
    Netplay::DrawOverlay();
    SinglePlayerSnapshot::DrawQuickSnapshotOverlay();
    const bool netplayStallRequested = Session::IsRemoteNetplaySession() && Netplay::ConsumeFrameStallRequested();
    const bool allowTransitionWhileStalled =
        netplayStallRequested && PortableGameplayRestore::ShouldAdvanceSupervisorTransitionWhileStalled();
    if (netplayStallRequested && !allowTransitionWhileStalled)
    {
        return CHAIN_CALLBACK_RESULT_BREAK;
    }

    if (s->wantedState != s->curState)
    {
#ifdef TH06_IOS
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[StateProbe] supervisor current=%d wanted=%d previous=%d calc=%u",
                        (int)s->curState, (int)s->wantedState, (int)s->wantedState2,
                        (unsigned)s->calcCount);
#endif
        s->wantedState2 = s->wantedState;
        switch (s->wantedState)
        {
        case SUPERVISOR_STATE_INIT:
        REINIT_MAINMENU:
            s->curState = SUPERVISOR_STATE_MAINMENU;
            if (MainMenu::RegisterChain(0) != ZUN_SUCCESS)
            {
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            }
            break;
        case SUPERVISOR_STATE_MAINMENU:
            switch (s->curState)
            {
            case SUPERVISOR_STATE_EXITSUCCESS:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case SUPERVISOR_STATE_GAMEMANAGER:
                if (GameManager::RegisterChain() != ZUN_SUCCESS)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            case SUPERVISOR_STATE_EXITERROR:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_ERROR;
            case SUPERVISOR_STATE_RESULTSCREEN:
                if (ResultScreen::RegisterChain(NULL) != ZUN_SUCCESS)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            case SUPERVISOR_STATE_MUSICROOM:
                if (MusicRoom::RegisterChain() != ZUN_SUCCESS)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            case SUPERVISOR_STATE_ENDING:
                GameManager::CutChain();
                if (Ending::RegisterChain() != ZUN_SUCCESS)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            }
            break;

        case SUPERVISOR_STATE_RESULTSCREEN:
            switch (s->curState)
            {
            case SUPERVISOR_STATE_EXITSUCCESS:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case SUPERVISOR_STATE_MAINMENU:
                s->curState = SUPERVISOR_STATE_INIT;
                goto REINIT_MAINMENU;
            }
            break;
        case SUPERVISOR_STATE_GAMEMANAGER:
            switch (s->curState)
            {
            case SUPERVISOR_STATE_EXITSUCCESS:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;

            case SUPERVISOR_STATE_MAINMENU:
            RETURN_TO_MENU_FROM_GAME:
                GameManager::CutChain();
                s->curState = SUPERVISOR_STATE_INIT;
                ReplayManager::SaveReplay(NULL, NULL);
                goto REINIT_MAINMENU;

            case SUPERVISOR_STATE_RESULTSCREEN_FROMGAME:
                GameManager::CutChain();
                if (ResultScreen::RegisterChain(TRUE) != ZUN_SUCCESS)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            case SUPERVISOR_STATE_GAMEMANAGER_REINIT:
                GameManager::CutChain();
                if (GameManager::RegisterChain() != ZUN_SUCCESS)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                if (s->curState == SUPERVISOR_STATE_MAINMENU)
                {
                    goto RETURN_TO_MENU_FROM_GAME;
                }
                s->curState = SUPERVISOR_STATE_GAMEMANAGER;
                break;
            case SUPERVISOR_STATE_MAINMENU_REPLAY:
                GameManager::CutChain();
                s->curState = SUPERVISOR_STATE_INIT;
                ReplayManager::SaveReplay(NULL, NULL);
                s->curState = SUPERVISOR_STATE_MAINMENU;
                if (MainMenu::RegisterChain(1) != ZUN_SUCCESS)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;

            case 10:
                GameManager::CutChain();
                if (Ending::RegisterChain() != ZUN_SUCCESS)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
                break;
            }
            break;
        case SUPERVISOR_STATE_RESULTSCREEN_FROMGAME:
            switch (s->curState)
            {
            case SUPERVISOR_STATE_EXITSUCCESS:
                ReplayManager::SaveReplay(NULL, NULL);
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case SUPERVISOR_STATE_MAINMENU:
                s->curState = SUPERVISOR_STATE_INIT;
                ReplayManager::SaveReplay(NULL, NULL);
                goto REINIT_MAINMENU;
            }
            break;
        case SUPERVISOR_STATE_MUSICROOM:
            switch (s->curState)
            {
            case SUPERVISOR_STATE_EXITSUCCESS:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;

            case SUPERVISOR_STATE_MAINMENU:
                s->curState = SUPERVISOR_STATE_INIT;
                goto REINIT_MAINMENU;
            }
            break;
        case SUPERVISOR_STATE_ENDING:
            switch (s->curState)
            {
            case SUPERVISOR_STATE_EXITSUCCESS:
                return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
            case SUPERVISOR_STATE_MAINMENU:
                s->curState = SUPERVISOR_STATE_INIT;
                goto REINIT_MAINMENU;
            case SUPERVISOR_STATE_RESULTSCREEN_FROMGAME:
                if (ResultScreen::RegisterChain(TRUE) != ZUN_SUCCESS)
                {
                    return CHAIN_CALLBACK_RESULT_EXIT_GAME_SUCCESS;
                }
            }
            break;
        }
        g_CurFrameInput = g_LastFrameInput = g_IsEigthFrameOfHeldInput = 0;
    }

    s->wantedState = s->curState;
    s->calcCount++;
    if (netplayStallRequested)
    {
        return CHAIN_CALLBACK_RESULT_BREAK;
    }
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

#pragma var_order(anmm0, anmm1, anmm2, anmm3, anmm4, anmm5)
ChainCallbackResult Supervisor::OnDraw(Supervisor *s)
{
    AnmManager *anmm0 = g_AnmManager;
    anmm0->currentVertexShader = 0xff;

    AnmManager *anmm1 = g_AnmManager;
    anmm1->currentSprite = NULL;

    AnmManager *anmm2 = g_AnmManager;
    anmm2->currentTexture = NULL;

    AnmManager *anmm3 = g_AnmManager;
    anmm3->currentColorOp = 0xff;

    AnmManager *anmm4 = g_AnmManager;
    anmm4->currentBlendMode = 0xff;

    AnmManager *anmm5 = g_AnmManager;
    anmm5->currentZWriteDisable = 0xff;

    Supervisor::DrawFpsCounter();
    // Virtual buttons are drawn in screen-space by RendererGLES::EndFrame
    // (after BlitFBOToScreen, on the pillarbox black border).
    return CHAIN_CALLBACK_RESULT_CONTINUE;
}

#pragma var_order(chain, supervisor)
ZunResult Supervisor::RegisterChain()
{
    ChainElem *chain;
    Supervisor *supervisor = &g_Supervisor;

    supervisor->wantedState = 0;
    supervisor->curState = -1;
    supervisor->calcCount = 0;

    chain = g_Chain.CreateElem((ChainCallback)Supervisor::OnUpdate);
    chain->arg = supervisor;
    chain->addedCallback = (ChainAddedCallback)Supervisor::AddedCallback;
    chain->deletedCallback = (ChainDeletedCallback)Supervisor::DeletedCallback;
    if (g_Chain.AddToCalcChain(chain, TH_CHAIN_PRIO_CALC_SUPERVISOR) != 0)
    {
        return ZUN_ERROR;
    }

    chain = g_Chain.CreateElem((ChainCallback)Supervisor::OnDraw);
    chain->arg = supervisor;
    g_Chain.AddToDrawChain(chain, TH_CHAIN_PRIO_DRAW_SUPERVISOR);

    return ZUN_SUCCESS;
}

#pragma var_order(i)
ZunResult Supervisor::AddedCallback(Supervisor *s)
{
    i32 i;

    for (i = 0; i < (i32)(sizeof(s->pbg3Archives) / sizeof(s->pbg3Archives[0])); i++)
    {
        s->pbg3Archives[i] = NULL;
    }

    g_Pbg3Archives = s->pbg3Archives;
    if (s->LoadPbg3(IN_PBG3_INDEX, TH_IN_DAT_FILE))
    {
        return ZUN_ERROR;
    }
#ifdef TH06_IOS
    static const char *startupEntries[] = {"th06logo.jpg", "text.anm", "plst00.wav"};
    for (const char *entryName : startupEntries)
    {
        const i32 entryIndex = s->pbg3Archives[IN_PBG3_INDEX]->FindEntry((char *)entryName);
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[AssetProbe] IN entry %s -> %d", entryName, entryIndex);
        if (entryIndex < 0)
        {
            return ZUN_ERROR;
        }
    }
#endif
    if (g_AnmManager->LoadSurface(0, "data/title/th06logo.jpg") != ZUN_SUCCESS)
    {
#ifdef TH06_IOS
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[IOS-BOOT] title JPEG decode FAILED: %s", IMG_GetError());
#endif
        return ZUN_ERROR;
    }
#ifdef TH06_IOS
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[IOS-BOOT] title JPEG decoded successfully");
#endif
    g_AnmManager->CopySurfaceToBackBuffer(0, 0, 0, 0, 0);
    g_Renderer->EndFrame();
    SDL_PumpEvents();

    g_Renderer->BeginFrame();
    g_AnmManager->CopySurfaceToBackBuffer(0, 0, 0, 0, 0);
    g_Renderer->EndFrame();
    SDL_PumpEvents();

    g_Renderer->BeginFrame();

    g_AnmManager->ReleaseSurface(0);

    s->startupTimeBeforeMenuMusic = timeGetTime();
    Supervisor::SetupDInput(s);

    s->midiOutput = new MidiOutput();

    g_Rng.Initialize(timeGetTime());

    const Uint32 sfxInitStart = SDL_GetTicks();
    g_SoundPlayer.InitSoundBuffers();
#ifdef TH06_IOS
    SDL_LogCritical(SDL_LOG_CATEGORY_AUDIO,
                    "[AudioProbe] SFX init time=%ums (lazy=%d)",
                    (unsigned)(SDL_GetTicks() - sfxInitStart), 1);
#endif
    if (g_AnmManager->LoadAnm(ANM_FILE_TEXT, "data/text.anm", ANM_OFFSET_TEXT) != 0)
    {
        return ZUN_ERROR;
    }

    if (AsciiManager::RegisterChain() != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_ASCIIMANAGER_INIT_FAILED);
        return ZUN_ERROR;
    }

    s->unk198 = 0;
    g_AnmManager->SetupVertexBuffer();
    TextHelper::CreateTextBuffer();
#ifndef TH06_IOS
    s->ReleasePbg3(IN_PBG3_INDEX);
#else
    // iOS loads sound effects lazily. Their WAV entries live in the IN
    // archive, so releasing it here turns every first button press into a
    // failed fallback archive scan and leaves the game without SFX.
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[AssetProbe] keeping KOUMAKYO_IN.dat for lazy SFX");
#endif
    if (g_Supervisor.LoadPbg3(MD_PBG3_INDEX, TH_MD_DAT_FILE) != 0)
        return ZUN_ERROR;

    return ZUN_SUCCESS;
}

ZunResult Supervisor::SetupDInput(Supervisor *supervisor)
{
    if (supervisor->cfg.opts >> GCOS_NO_DIRECTINPUT_PAD & 1)
    {
        return ZUN_ERROR;
    }

    Controller::InitSDLController();
    GameErrorContext::Log(&g_GameErrorContext, TH_ERR_DIRECTINPUT_INITIALIZED);
    GameErrorContext::Log(&g_GameErrorContext, TH_ERR_PAD_FOUND);

    return ZUN_SUCCESS;
}

ZunResult Supervisor::DeletedCallback(Supervisor *s)
{
    i32 pbg3Idx;

    g_AnmManager->ReleaseVertexBuffer();
    for (pbg3Idx = 0; pbg3Idx < ARRAY_SIZE_SIGNED(s->pbg3Archives); pbg3Idx += 1)
    {
        s->ReleasePbg3(pbg3Idx);
    }
    g_AnmManager->ReleaseAnm(0);
    AsciiManager::CutChain();
    g_SoundPlayer.StopBGM();
    if (s->midiOutput != NULL)
    {
        s->midiOutput->StopPlayback();
        delete s->midiOutput;
        s->midiOutput = NULL;
    }
    ReplayManager::SaveReplay(NULL, NULL);
    THPrac::TH06::THPracSaveData();
    TextHelper::ReleaseTextBuffer();
    Controller::CloseSDLController();
    return ZUN_SUCCESS;
}

#pragma var_order(curTime, framerate, fps, elapsed, fpsCounterPos)
void Supervisor::DrawFpsCounter()
{
    DWORD curTime;
    float framerate;
    float elapsed;
    float fps;
    D3DXVECTOR3 fpsCounterPos;

    static u32 g_NumFramesSinceLastTime = 0;
    static DWORD g_LastTime = SDL_GetTicks();
    static char g_FpsCounterBuffer[256];

    curTime = SDL_GetTicks();
    g_NumFramesSinceLastTime = g_NumFramesSinceLastTime + 1 + (u32)g_Supervisor.cfg.frameskipConfig;
    if (500 <= curTime - g_LastTime)
    {
        elapsed = (curTime - g_LastTime) / 1000.f;
        fps = g_NumFramesSinceLastTime / elapsed;
        g_LastTime = curTime;
        g_NumFramesSinceLastTime = 0;
        sprintf(g_FpsCounterBuffer, "%.02ffps", fps);
        if (g_GameManager.isInMenu != 0)
        {
            framerate = 60.f / g_Supervisor.framerateMultiplier;
            g_Supervisor.unk1b8 = g_Supervisor.unk1b8 + framerate;

            if (framerate * .89999998f < fps)
                g_Supervisor.unk1b4 = g_Supervisor.unk1b4 + framerate;
            else if (framerate * 0.69999999f < fps)
                g_Supervisor.unk1b4 = framerate * .8f + g_Supervisor.unk1b4;
            else if (framerate * 0.5f < fps)
                g_Supervisor.unk1b4 = framerate * .6f + g_Supervisor.unk1b4;
            else
                g_Supervisor.unk1b4 = framerate * .5f + g_Supervisor.unk1b4;
        }
    }
    if (!g_Supervisor.isInEnding)
    {
        fpsCounterPos.x = 512.0;
        fpsCounterPos.y = 464.0;
        fpsCounterPos.z = 0.0;
        g_AsciiManager.AddString(&fpsCounterPos, g_FpsCounterBuffer);
    }
    return;
}

void Supervisor::TickTimer(i32 *frames, f32 *subframes)
{
    if (this->framerateMultiplier <= 0.99f)
    {
        *subframes = *subframes + this->effectiveFramerateMultiplier;
        if (*subframes >= 1.0f)
        {
            *frames = *frames + 1;
            *subframes = *subframes - 1.0f;
        }
    }
    else
    {
        *frames = *frames + 1;
    }
}

void Supervisor::ReleasePbg3(i32 pbg3FileIdx)
{
    if (this->pbg3Archives[pbg3FileIdx] == NULL)
    {
        return;
    }

    // Pbg3Archive::~Pbg3Archive() owns the Release() call. Calling Release()
    // here first leaves the destructor with a second free of the same parser
    // and entry table, which is especially visible during stage transitions.
    delete this->pbg3Archives[pbg3FileIdx];
    this->pbg3Archives[pbg3FileIdx] = NULL;
}

static const wchar_t *FindDatBySuffixW(const char *filename, wchar_t *outBuf, size_t outBufLen)
{
    const char *suffix = NULL;
    static const char *kSuffixes[] = {"_CM.dat", "_ED.dat", "_IN.dat", "_MD.dat", "_ST.dat", "_TL.dat"};
    for (size_t i = 0; i < sizeof(kSuffixes) / sizeof(kSuffixes[0]); i++)
    {
        size_t fnLen = strlen(filename);
        size_t sfxLen = strlen(kSuffixes[i]);
        if (fnLen > sfxLen)
        {
            const char *tail = filename + fnLen - sfxLen;
#ifdef _WIN32
            if (_stricmp(tail, kSuffixes[i]) == 0)
#else
            if (strcasecmp(tail, kSuffixes[i]) == 0)
#endif
            {
                suffix = kSuffixes[i];
                break;
            }
        }
    }
    if (suffix == NULL)
        return NULL;

#ifdef _WIN32
    wchar_t pattern[32];
    size_t sfxLen = strlen(suffix);
    pattern[0] = L'*';
    for (size_t i = 0; i < sfxLen && i < 30; i++)
        pattern[1 + i] = (wchar_t)(unsigned char)suffix[i];
    pattern[1 + sfxLen] = L'\0';

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return NULL;

    size_t nameLen = wcslen(fd.cFileName);
    if (nameLen >= outBufLen)
    {
        FindClose(hFind);
        return NULL;
    }
    wcscpy(outBuf, fd.cFileName);
    FindClose(hFind);
    return outBuf;
#elif defined(__ANDROID__)
    // Android: enumerate APK assets in root directory using AAssetManager,
    // equivalent to FindFirstFileW("*suffix") on Windows.
    // AAssetDir_getNextFileName returns files in unspecified order, so we
    // enumerate ALL matches and prefer non-fallback archives (TOLOL/TOTOL)
    // over fallback ones.  This ensures th06c_CM.DAT is loaded as the
    // primary archive when both th06c_CM.DAT and TOLOL_CM.DAT are present.
    {
        JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
        jobject activity = (jobject)SDL_AndroidGetActivity();
        jclass activityClass = env->GetObjectClass(activity);
        jmethodID getAssets = env->GetMethodID(activityClass, "getAssets",
                                               "()Landroid/content/res/AssetManager;");
        jobject jAssetManager = env->CallObjectMethod(activity, getAssets);
        AAssetManager *mgr = AAssetManager_fromJava(env, jAssetManager);

        if (mgr)
        {
            AAssetDir *dir = AAssetManager_openDir(mgr, "");
            if (dir)
            {
                size_t sfxLen = strlen(suffix);
                const char *assetName;
                bool haveFallback = false;
                while ((assetName = AAssetDir_getNextFileName(dir)) != NULL)
                {
                    size_t nameLen = strlen(assetName);
                    if (nameLen > sfxLen && nameLen < outBufLen)
                    {
                        const char *tail = assetName + nameLen - sfxLen;
                        if (strcasecmp(tail, suffix) == 0)
                        {
                            // Check if this is a fallback archive (TOLOL/TOTOL)
                            bool isFallback = (strncasecmp(assetName, "TOLOL", 5) == 0 ||
                                               strncasecmp(assetName, "TOTOL", 5) == 0);
                            if (!isFallback)
                            {
                                // Non-fallback match: use immediately
                                for (size_t i = 0; i <= nameLen; i++)
                                    outBuf[i] = (wchar_t)(unsigned char)assetName[i];
                                AAssetDir_close(dir);
                                env->DeleteLocalRef(jAssetManager);
                                env->DeleteLocalRef(activityClass);
                                env->DeleteLocalRef(activity);
                                return outBuf;
                            }
                            else if (!haveFallback)
                            {
                                // Remember fallback in case no primary is found
                                for (size_t i = 0; i <= nameLen; i++)
                                    outBuf[i] = (wchar_t)(unsigned char)assetName[i];
                                haveFallback = true;
                            }
                        }
                    }
                }
                AAssetDir_close(dir);
                if (haveFallback)
                {
                    env->DeleteLocalRef(jAssetManager);
                    env->DeleteLocalRef(activityClass);
                    env->DeleteLocalRef(activity);
                    return outBuf;
                }
            }
        }

        env->DeleteLocalRef(jAssetManager);
        env->DeleteLocalRef(activityClass);
        env->DeleteLocalRef(activity);
    }
    return NULL;
#elif defined(TH06_IOS)
    // iOS bundles are immutable and SDL_RWFromFile resolves relative paths
    // inside the application resources. Probe the archive names produced by
    // ios/stage_assets.py instead of relying on filesystem globbing.
    const char *canonicalSuffix = NULL;
    static const char *canonicalSuffixes[] = {
        "_CM.DAT", "_ED.DAT", "_IN.DAT", "_MD.DAT", "_ST.DAT", "_TL.DAT"
    };
    for (const char *candidateSuffix : canonicalSuffixes)
    {
        // `suffix` includes the leading underscore (for example `_IN.dat`).
        // The old two-character comparison started at the underscore and
        // therefore never selected an iOS fallback archive.
        if (strcasecmp(suffix, candidateSuffix) == 0)
        {
            canonicalSuffix = candidateSuffix;
            break;
        }
    }
    if (canonicalSuffix == NULL)
        return NULL;

    static const char *prefixes[] = {"th06c", "th06", "TOLOL", "TOTOL"};
    for (const char *prefix : prefixes)
    {
        char assetName[64];
        snprintf(assetName, sizeof(assetName), "%s%s", prefix, canonicalSuffix);
        SDL_RWops *rw = AssetIO::OpenRW(assetName);
        if (rw == NULL)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[AssetProbe] missing iOS archive candidate: %s", assetName);
            continue;
        }
        SDL_RWclose(rw);
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[AssetProbe] using iOS archive: %s", assetName);

        const size_t nameLen = strlen(assetName);
        if (nameLen >= outBufLen)
            return NULL;
        for (size_t i = 0; i <= nameLen; ++i)
            outBuf[i] = static_cast<wchar_t>(static_cast<unsigned char>(assetName[i]));
        return outBuf;
    }
    return NULL;
#else
    // Linux: search current directory for a file ending with the suffix using glob.
    // Use character classes [xX] for case-insensitivity since drvfs (WSL /mnt/c/) is case-sensitive.
    size_t sfxLen = strlen(suffix);
    char pattern[64];
    size_t p = 0;
    pattern[p++] = '*';
    for (size_t i = 0; i < sfxLen && p + 5 < sizeof(pattern); i++)
    {
        char c = suffix[i];
        if (c >= 'A' && c <= 'Z')
        {
            pattern[p++] = '[';
            pattern[p++] = c;
            pattern[p++] = c + 32;
            pattern[p++] = ']';
        }
        else if (c >= 'a' && c <= 'z')
        {
            pattern[p++] = '[';
            pattern[p++] = c - 32;
            pattern[p++] = c;
            pattern[p++] = ']';
        }
        else
        {
            pattern[p++] = c;
        }
    }
    pattern[p] = '\0';

    glob_t g;
    if (glob(pattern, 0, NULL, &g) != 0 || g.gl_pathc == 0)
    {
        globfree(&g);
        return NULL;
    }
    // Prefer non-fallback archives (not TOLOL/TOTOL) so that th06c_CM.DAT wins
    // over TOLOL_CM.DAT when both are present in the working directory.
    const char *found = NULL;
    for (size_t gi = 0; gi < g.gl_pathc; ++gi)
    {
        const char *cand = g.gl_pathv[gi];
        // Strip leading directory components for the prefix check.
        const char *base = strrchr(cand, '/');
        base = base ? base + 1 : cand;
        if (strncasecmp(base, "TOLOL", 5) != 0 && strncasecmp(base, "TOTOL", 5) != 0)
        {
            found = cand;
            break;
        }
    }
    if (!found)
    {
        found = g.gl_pathv[0];
    }
    size_t nameLen = strlen(found);
    if (nameLen >= outBufLen)
    {
        globfree(&g);
        return NULL;
    }
    for (size_t i = 0; i <= nameLen; i++)
        outBuf[i] = (wchar_t)(unsigned char)found[i];
    globfree(&g);
    return outBuf;
#endif
}

i32 Supervisor::LoadPbg3(i32 pbg3FileIdx, char *filename)
{
    if (this->pbg3Archives[pbg3FileIdx] == NULL || strcmp(filename, this->pbg3ArchiveNames[pbg3FileIdx]) != 0)
    {
        this->ReleasePbg3(pbg3FileIdx);
        this->pbg3Archives[pbg3FileIdx] = new Pbg3Archive();
        utils::DebugPrint("%s open ...\n", filename);
        const Uint32 pbg3LoadStart = SDL_GetTicks();
        i32 loadResult = this->pbg3Archives[pbg3FileIdx]->Load(filename);

        if (loadResult == 0)
        {
            wchar_t wFoundName[256];
            if (FindDatBySuffixW(filename, wFoundName, 256) != NULL)
            {
                utils::DebugPrint("primary name not found, trying fallback ...\n");
                SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                                "[AssetProbe] primary archive missing, trying suffix fallback for %s",
                                filename);
                loadResult = this->pbg3Archives[pbg3FileIdx]->LoadW(wFoundName);
            }
        }

        if (loadResult != 0)
        {
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                            "[AssetProbe] archive loaded: %s", filename);
#ifdef TH06_IOS
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                            "[AssetProbe] archive load time: %s %ums",
                            filename, (unsigned)(SDL_GetTicks() - pbg3LoadStart));
#endif
            strncpy(this->pbg3ArchiveNames[pbg3FileIdx], filename, sizeof(Pbg3ArchiveName) - 1);
            this->pbg3ArchiveNames[pbg3FileIdx][sizeof(Pbg3ArchiveName) - 1] = '\0';

            char verPath[128];
            sprintf(verPath, "ver%.4x.dat", GAME_VERSION);
            i32 res = this->pbg3Archives[pbg3FileIdx]->FindEntry(verPath);
            if (res < 0)
            {
                GameErrorContext::Fatal(&g_GameErrorContext, "error : データのバージョンが違います\n");
                return 1;
            }
        }
        else
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "[AssetProbe] archive load FAILED: %s", filename);
            delete this->pbg3Archives[pbg3FileIdx];
            // Let's really make sure this is null by nulling twice. I assume
            // there's some kind of inline function here, like it's actually
            // calling this->pbg3Archives.delete(pbg3FileIdx), followed by a
            // manual nulling?
            this->pbg3Archives[pbg3FileIdx] = NULL;
            this->pbg3Archives[pbg3FileIdx] = NULL;
            return 1;
        }
    }
    return 0;
}

#pragma var_order(data)
ZunResult Supervisor::LoadConfig(char *path)
{
    GameConfiguration *data;

    memset(&g_Supervisor.cfg, 0, sizeof(GameConfiguration));
    g_Supervisor.cfg.opts = g_Supervisor.cfg.opts | (1 << GCOS_USE_D3D_HW_TEXTURE_BLENDING);
    AssetIO::DiagLog("Cfg", "LoadConfig: path=%s sizeof(cfg)=%zu", path ? path : "(null)", sizeof(GameConfiguration));
    data = (GameConfiguration *)FileSystem::OpenPath(path, 1);
    AssetIO::DiagLog("Cfg", "LoadConfig: OpenPath -> data=%p g_LastFileSize=%u", (void*)data, g_LastFileSize);
    if (data == NULL)
    {
        g_Supervisor.cfg.lifeCount = 2;
        g_Supervisor.cfg.bombCount = 3;
        g_Supervisor.cfg.colorMode16bit = 0;
        g_Supervisor.cfg.version = GAME_VERSION;
        g_Supervisor.cfg.padXAxis = 600;
        g_Supervisor.cfg.padYAxis = 600;
        {
            // Route through AssetIO so the probe finds bgm/ regardless of
            // launch directory (cwd-relative or exe-dir-relative on desktop,
            // APK assets on Android).
            if (HasPackagedBgm())
            {
                g_Supervisor.cfg.musicMode = WAV;
            }
            else
            {
                g_Supervisor.cfg.musicMode = MIDI;
                utils::DebugPrint(TH_ERR_NO_WAVE_FILE);
            }
        }
        g_Supervisor.cfg.playSounds = 1;
        g_Supervisor.cfg.defaultDifficulty = 1;
        g_Supervisor.cfg.windowed = false;
        g_Supervisor.cfg.frameskipConfig = 0;
        g_Supervisor.cfg.controllerMapping = g_ControllerMapping;
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_CONFIG_NOT_FOUND);
    }
    else
    {
        g_Supervisor.cfg = *data;
        AssetIO::DiagLog("Cfg", "LoadConfig: read OK ver=0x%x life=%u bomb=%u music=%u sound=%u diff=%u win=%u fs=%u opts=0x%x unk0=%d unk1=%d",
                         g_Supervisor.cfg.version, g_Supervisor.cfg.lifeCount, g_Supervisor.cfg.bombCount,
                         g_Supervisor.cfg.musicMode, g_Supervisor.cfg.playSounds, g_Supervisor.cfg.defaultDifficulty,
                         g_Supervisor.cfg.windowed, g_Supervisor.cfg.frameskipConfig, g_Supervisor.cfg.opts,
                         (int)g_Supervisor.cfg.unk[0], (int)g_Supervisor.cfg.unk[1]);
        if (IsUninitializedControllerMapping(g_Supervisor.cfg.controllerMapping))
        {
            g_Supervisor.cfg.controllerMapping = g_ControllerMapping;
        }
        // SDL2: skip colorMode16bit validation (16-bit mode doesn't exist in GL)
        if ((g_Supervisor.cfg.lifeCount >= 5) || (g_Supervisor.cfg.bombCount >= 4) ||
            (g_Supervisor.cfg.musicMode >= 3) ||
            (g_Supervisor.cfg.defaultDifficulty >= 5) || (g_Supervisor.cfg.playSounds >= 2) ||
            (g_Supervisor.cfg.windowed >= 2) || (g_Supervisor.cfg.frameskipConfig >= 3) ||
            (g_Supervisor.cfg.version != GAME_VERSION) || (g_LastFileSize != 0x38))
        {
            AssetIO::DiagLog("Cfg", "LoadConfig: VALIDATION FAIL -> RESET (life=%u bomb=%u music=%u diff=%u sound=%u win=%u fs=%u ver=0x%x lastSize=%u)",
                             g_Supervisor.cfg.lifeCount, g_Supervisor.cfg.bombCount, g_Supervisor.cfg.musicMode,
                             g_Supervisor.cfg.defaultDifficulty, g_Supervisor.cfg.playSounds,
                             g_Supervisor.cfg.windowed, g_Supervisor.cfg.frameskipConfig,
                             g_Supervisor.cfg.version, g_LastFileSize);
            g_Supervisor.cfg.lifeCount = 2;
            g_Supervisor.cfg.bombCount = 3;
            g_Supervisor.cfg.colorMode16bit = 0;
            g_Supervisor.cfg.version = GAME_VERSION;
            g_Supervisor.cfg.padXAxis = 600;
            g_Supervisor.cfg.padYAxis = 600;
            {
                if (HasPackagedBgm())
                {
                    g_Supervisor.cfg.musicMode = WAV;
                }
                else
                {
                    g_Supervisor.cfg.musicMode = MIDI;
                    utils::DebugPrint(TH_ERR_NO_WAVE_FILE);
                }
            }
            g_Supervisor.cfg.playSounds = 1;
            g_Supervisor.cfg.defaultDifficulty = 1;
            g_Supervisor.cfg.windowed = false;
            g_Supervisor.cfg.frameskipConfig = 0;
            g_Supervisor.cfg.controllerMapping = g_ControllerMapping;
            memset(&g_Supervisor.cfg.opts, 0, sizeof(GameConfigOptsShifts));
            g_Supervisor.cfg.opts |= (1 << GCOS_USE_D3D_HW_TEXTURE_BLENDING);
            GameErrorContext::Log(&g_GameErrorContext, TH_ERR_CONFIG_CORRUPTED);
        }
        g_ControllerMapping = g_Supervisor.cfg.controllerMapping;
        // SDL2: normalize legacy 0xFF auto-detect marker to 0 (32-bit)
        if (g_Supervisor.cfg.colorMode16bit >= 2)
            g_Supervisor.cfg.colorMode16bit = 0;
        free(data);
    }
    if (((this->cfg.opts >> GCOS_DONT_USE_VERTEX_BUF) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_NO_VERTEX_BUFFER);
    }
    if (((this->cfg.opts >> GCOS_DONT_USE_FOG) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_NO_FOG);
    }
    if (((this->cfg.opts >> GCOS_FORCE_16BIT_COLOR_MODE) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_USE_16BIT_TEXTURES);
    }
    if (this->IsUnknown())
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_FORCE_BACKBUFFER_CLEAR);
    }
    if (((this->cfg.opts >> GCOS_DISPLAY_MINIMUM_GRAPHICS) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_DONT_RENDER_ITEMS);
    }
    if (((this->cfg.opts >> GCOS_SUPPRESS_USE_OF_GOROUD_SHADING) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_NO_GOURAUD_SHADING);
    }
    if (((this->cfg.opts >> GCOS_TURN_OFF_DEPTH_TEST) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_NO_DEPTH_TESTING);
    }
    if (((this->cfg.opts >> GCOS_FORCE_60FPS) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_FORCE_60FPS_MODE);
        this->vsyncEnabled = 0;
    }
    if (((this->cfg.opts >> GCOS_NO_COLOR_COMP) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_NO_TEXTURE_COLOR_COMPOSITING);
    }
    if (((this->cfg.opts >> GCOS_NO_COLOR_COMP) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_LAUNCH_WINDOWED);
    }
    if (((this->cfg.opts >> GCOS_REFERENCE_RASTERIZER_MODE) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_FORCE_REFERENCE_RASTERIZER);
    }
    if (((this->cfg.opts >> GCOS_NO_DIRECTINPUT_PAD) & 1) != 0)
    {
        GameErrorContext::Log(&g_GameErrorContext, TH_ERR_DO_NOT_USE_DIRECTINPUT);
    }
    if (FileSystem::WriteDataToFile(path, &g_Supervisor.cfg, sizeof(GameConfiguration)) != 0)
    {
        GameErrorContext::Fatal(&g_GameErrorContext, TH_ERR_FILE_CANNOT_BE_EXPORTED, path);
        GameErrorContext::Fatal(&g_GameErrorContext, TH_ERR_FOLDER_HAS_WRITE_PROTECT_OR_DISK_FULL);
        return ZUN_ERROR;
    }

    return ZUN_SUCCESS;
}

ZunBool Supervisor::ReadMidiFile(u32 midiFileIdx, char *path)
{
    // Return conventions seem opposite of normal? But they're never used anyway
    if (g_Supervisor.cfg.musicMode == MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            g_Supervisor.midiOutput->ReadFileData(midiFileIdx, path);
        }

        return FALSE;
    }

    return TRUE;
}

i32 Supervisor::PlayMidiFile(i32 midiFileIdx)
{
    MidiOutput *globalMidiController;

    if (g_Supervisor.cfg.musicMode == MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            globalMidiController = g_Supervisor.midiOutput;
            globalMidiController->StopPlayback();
            if (globalMidiController->ParseFile(midiFileIdx) != ZUN_SUCCESS)
            {
                return FALSE;
            }
            globalMidiController->Play();
        }

        return FALSE;
    }

    return TRUE;
}

ZunResult Supervisor::SetupMidiPlayback(char *path)
{
    // There doesn't seem to be a way to recreate the jump assembly needed without gotos?
    // Standard short circuiting boolean operators and nested conditionals don't seem to work, at least
    if (g_Supervisor.cfg.musicMode == MIDI)
    {
        goto success;
    }
    else if (g_Supervisor.cfg.musicMode == WAV)
    {
        goto success;
    }
    else
    {
        return ZUN_ERROR;
    }

success:
    return ZUN_SUCCESS;
}

ZunResult Supervisor::PlayAudio(char *path)
{
    char wavName[256];
    char wavPos[256];
    char *pathExtension;

    if (path == NULL || path[0] == '\0')
    {
#ifdef TH06_IOS
        SDL_LogError(SDL_LOG_CATEGORY_AUDIO, "[BgmSwitch] empty audio path");
#endif
        return ZUN_ERROR;
    }

    if (g_Supervisor.cfg.musicMode == MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            MidiOutput *midiOutput = g_Supervisor.midiOutput;
            midiOutput->StopPlayback();
            if (midiOutput->LoadFile(path) != ZUN_SUCCESS)
            {
                return ZUN_ERROR;
            }
            return midiOutput->Play();
        }
    }
    else if (g_Supervisor.cfg.musicMode == WAV)
    {
        strncpy(wavName, path, sizeof(wavName) - 1);
        wavName[sizeof(wavName) - 1] = '\0';
        strncpy(wavPos, path, sizeof(wavPos) - 1);
        wavPos[sizeof(wavPos) - 1] = '\0';
        pathExtension = strrchr(wavName, L'.');
        if (pathExtension == NULL)
        {
#ifdef TH06_IOS
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                         "[BgmSwitch] path has no extension: %s", path);
#endif
            return ZUN_ERROR;
        }
        pathExtension[1] = 'w';
        pathExtension[2] = 'a';
        pathExtension[3] = 'v';
        pathExtension[4] = '\0';
        pathExtension = strrchr(wavPos, L'.');
        if (pathExtension == NULL)
            return ZUN_ERROR;
        pathExtension[1] = 'p';
        pathExtension[2] = 'o';
        pathExtension[3] = 's';
        pathExtension[4] = '\0';
#ifdef TH06_IOS
        SDL_LogCritical(SDL_LOG_CATEGORY_AUDIO,
                        "[BgmSwitch] load begin source=%s wav=%s pos=%s",
                        path, wavName, wavPos);
#endif
        if (g_SoundPlayer.LoadWav(wavName) != ZUN_SUCCESS)
        {
#ifdef TH06_IOS
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                         "[BgmSwitch] audio decode failed: %s", wavName);
#endif
            return ZUN_ERROR;
        }
        const bool hasLoopPoints = g_SoundPlayer.LoadPos(wavPos) >= ZUN_SUCCESS;
        const ZunResult playResult = g_SoundPlayer.PlayBGM(hasLoopPoints ? TRUE : FALSE);
        if (playResult != ZUN_SUCCESS)
        {
#ifdef TH06_IOS
            SDL_LogError(SDL_LOG_CATEGORY_AUDIO,
                         "[BgmSwitch] playback start failed: %s", wavName);
#endif
            g_SoundPlayer.StopBGM();
            return ZUN_ERROR;
        }
#ifdef TH06_IOS
        SDL_LogCritical(SDL_LOG_CATEGORY_AUDIO,
                        "[BgmSwitch] load/play complete loop=%d", hasLoopPoints ? 1 : 0);
#endif
    }
    else
    {
        return ZUN_ERROR;
    }
    return ZUN_SUCCESS;
}

ZunResult Supervisor::StopAudio()
{
    if (g_Supervisor.cfg.musicMode == MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            g_Supervisor.midiOutput->StopPlayback();
        }
    }
    else
    {
        if (g_Supervisor.cfg.musicMode == WAV)
        {
            g_SoundPlayer.StopBGM();
        }
        else
        {
            return ZUN_ERROR;
        }
    }

    return ZUN_SUCCESS;
}

ZunResult Supervisor::FadeOutMusic(f32 fadeOutSeconds)
{
    if (g_Supervisor.cfg.musicMode == MIDI)
    {
        if (g_Supervisor.midiOutput != NULL)
        {
            g_Supervisor.midiOutput->SetFadeOut(1000.0f * fadeOutSeconds);
        }
    }
    else
    {
        if (g_Supervisor.cfg.musicMode == WAV)
        {
            if (this->effectiveFramerateMultiplier == 0.0f)
            {
                g_SoundPlayer.FadeOut(fadeOutSeconds);
            }
            else
            {
                if (this->effectiveFramerateMultiplier > 1.0f)
                {
                    g_SoundPlayer.FadeOut(fadeOutSeconds);
                }
                else
                {
                    g_SoundPlayer.FadeOut(fadeOutSeconds / this->effectiveFramerateMultiplier);
                }
            }
        }
        else
        {
            return ZUN_ERROR;
        }
    }

    return ZUN_SUCCESS;
}

}; // namespace th06
