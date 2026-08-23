#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int TH06_IOS_BluetoothAvailable();
int TH06_IOS_BluetoothStart(int hostRole);
void TH06_IOS_BluetoothStop();
int TH06_IOS_BluetoothIsConnected();
int TH06_IOS_BluetoothSend(const void *bytes, int size, int reliable);
int TH06_IOS_BluetoothPoll(void *bytes, int capacity);
const char *TH06_IOS_BluetoothStatus();

#ifdef __cplusplus
}
#endif
