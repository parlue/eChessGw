#pragma once
#include "Arduino.h"
struct NimBLEAddress { std::string toString() const { return "move"; } };
struct NimBLERemoteCharacteristic {
  std::vector<std::vector<uint8_t>> writes;
  bool fail=false;
  int failAfter=-1;
  bool writeValue(const uint8_t* p,size_t n,bool response) {
    if (!response || fail || failAfter==0) return false;
    if (failAfter>0) --failAfter;
    writes.emplace_back(p,p+n); return true;
  }
  bool canNotify() { return true; }
  template<class T> bool subscribe(bool,T) { return true; }
};
struct NimBLERemoteService {
  NimBLERemoteCharacteristic characteristic;
  NimBLERemoteCharacteristic* getCharacteristic(const char*) { return &characteristic; }
};
struct NimBLEClient;
struct NimBLEClientCallbacks {
  virtual ~NimBLEClientCallbacks()=default;
  virtual void onConnect(NimBLEClient*) {}
  virtual void onDisconnect(NimBLEClient*,int) {}
};
struct NimBLEClient {
  bool connected=true;
  uint16_t mtu=255;
  NimBLERemoteService service;
  void setClientCallbacks(NimBLEClientCallbacks*,bool) {}
  void setConnectTimeout(uint32_t) {}
  void setConnectionParams(int,int,int,int) {}
  bool connect(const NimBLEAddress&) { connected=true; return true; }
  bool isConnected() { return connected; }
  void disconnect() { connected=false; }
  std::vector<NimBLERemoteService*> getServices(bool=false) { return {&service}; }
  uint16_t getMTU() { return mtu; }
};
struct NimBLEDevice { static NimBLEClient* createClient() { return new NimBLEClient; } };
