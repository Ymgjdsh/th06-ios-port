#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_IPHONE
#define TH06_IOS 1
#endif

#if defined(__ANDROID__) || defined(TH06_IOS)
#define TH06_MOBILE 1
#endif
