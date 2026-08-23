#include "CrashHandler.hpp"

#include <SDL.h>
#include <execinfo.h>
#include <signal.h>
#include <stddef.h>
#include <unistd.h>

namespace th06::CrashHandler
{
namespace
{
struct SignalEntry
{
    int signal;
    const char *name;
};

constexpr SignalEntry kSignals[] = {
    {SIGABRT, "SIGABRT"},
    {SIGBUS, "SIGBUS"},
    {SIGFPE, "SIGFPE"},
    {SIGILL, "SIGILL"},
    {SIGSEGV, "SIGSEGV"},
    {SIGTRAP, "SIGTRAP"},
};

struct sigaction g_OldActions[sizeof(kSignals) / sizeof(kSignals[0])];
bool g_Installed = false;
volatile sig_atomic_t g_HandlingCrash = 0;

void SafeWrite(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    size_t length = 0;
    while (text[length] != '\0')
    {
        length++;
    }
    while (length > 0)
    {
        const ssize_t written = write(STDERR_FILENO, text, length);
        if (written <= 0)
        {
            return;
        }
        text += written;
        length -= static_cast<size_t>(written);
    }
}

const char *SignalName(int signal)
{
    for (const SignalEntry &entry : kSignals)
    {
        if (entry.signal == signal)
        {
            return entry.name;
        }
    }
    return "UNKNOWN";
}

void CrashSignalHandler(int signal, siginfo_t *, void *)
{
    if (g_HandlingCrash != 0)
    {
        _exit(128 + signal);
    }
    g_HandlingCrash = 1;

    SafeWrite("\n[IOS-CRASH] fatal signal: ");
    SafeWrite(SignalName(signal));
    SafeWrite("\n[IOS-CRASH] native backtrace follows\n");

    void *frames[48];
    const int frameCount = backtrace(frames, static_cast<int>(sizeof(frames) / sizeof(frames[0])));
    if (frameCount > 0)
    {
        backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);
    }
    SafeWrite("[IOS-CRASH] end backtrace\n");

    struct sigaction defaultAction = {};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    sigaction(signal, &defaultAction, NULL);
    raise(signal);
    _exit(128 + signal);
}
} // namespace

void Init()
{
    if (g_Installed)
    {
        return;
    }

    static unsigned char alternateStack[64 * 1024];
    stack_t stack = {};
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof(alternateStack);
    stack.ss_flags = 0;
    sigaltstack(&stack, NULL);

    struct sigaction action = {};
    action.sa_sigaction = CrashSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const SignalEntry &entry : kSignals)
    {
        sigaddset(&action.sa_mask, entry.signal);
    }

    for (size_t idx = 0; idx < sizeof(kSignals) / sizeof(kSignals[0]); idx++)
    {
        sigaction(kSignals[idx].signal, &action, &g_OldActions[idx]);
    }

    g_Installed = true;
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[IOS-CRASH] native signal handler installed");
}

void Shutdown()
{
    if (!g_Installed)
    {
        return;
    }

    for (size_t idx = 0; idx < sizeof(kSignals) / sizeof(kSignals[0]); idx++)
    {
        sigaction(kSignals[idx].signal, &g_OldActions[idx], NULL);
    }
    g_Installed = false;
}
} // namespace th06::CrashHandler
