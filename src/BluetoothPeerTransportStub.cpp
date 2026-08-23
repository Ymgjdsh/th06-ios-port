#include "../ios/BluetoothPeerTransport.hpp"

extern "C" int TH06_IOS_BluetoothAvailable() { return 0; }
extern "C" int TH06_IOS_BluetoothStart(int) { return 0; }
extern "C" void TH06_IOS_BluetoothStop() {}
extern "C" int TH06_IOS_BluetoothIsConnected() { return 0; }
extern "C" int TH06_IOS_BluetoothSend(const void *, int, int) { return 0; }
extern "C" int TH06_IOS_BluetoothPoll(void *, int) { return 0; }
extern "C" const char *TH06_IOS_BluetoothStatus() { return "iOS only"; }
