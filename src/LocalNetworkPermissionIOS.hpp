#pragma once

#ifdef TH06_IOS
extern "C" void TH06_IOS_TriggerLocalNetworkPermission();
extern "C" void TH06_IOS_StopLocalNetworkPermissionProbe();
// 0 = idle, 1 = permission probe is starting, 2 = Bonjour search active,
// -1 = iOS rejected the local-network search.
extern "C" int TH06_IOS_GetLocalNetworkPermissionState();
extern "C" void TH06_IOS_StartBonjourHost(int port);
extern "C" void TH06_IOS_StopBonjourHost();
extern "C" int TH06_IOS_PollBonjourHost(char *host, int capacity, int *port);
#else
inline void TH06_IOS_TriggerLocalNetworkPermission() {}
inline void TH06_IOS_StopLocalNetworkPermissionProbe() {}
inline int TH06_IOS_GetLocalNetworkPermissionState() { return 2; }
inline void TH06_IOS_StartBonjourHost(int) {}
inline void TH06_IOS_StopBonjourHost() {}
inline int TH06_IOS_PollBonjourHost(char *, int, int *) { return 0; }
#endif
