#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <MultipeerConnectivity/MultipeerConnectivity.h>

#include "BluetoothPeerTransport.hpp"

#include <deque>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

constexpr char kServiceType[] = "th06-peer";
@interface PeerBridge : NSObject <MCSessionDelegate, MCNearbyServiceAdvertiserDelegate, MCNearbyServiceBrowserDelegate> {
@public
    MCPeerID *peerId;
    MCSession *session;
    MCNearbyServiceAdvertiser *advertiser;
    MCNearbyServiceBrowser *browser;
    std::mutex mutex;
    std::deque<std::vector<uint8_t>> packets;
    bool hostRole;
    bool connected;
    std::string status;
}
- (void)start:(BOOL)hostRole;
- (void)stop;
- (BOOL)sendBytes:(const void *)bytes size:(int)size reliable:(BOOL)reliable;
@end

static PeerBridge *gBridge = nil;

@implementation PeerBridge
- (instancetype)init {
    self = [super init];
    if (self) {
        hostRole = false;
        connected = false;
        status = "idle";
        peerId = [[MCPeerID alloc] initWithDisplayName:[[UIDevice currentDevice] name]];
        session = [[MCSession alloc] initWithPeer:peerId
                                  securityIdentity:nil
                              encryptionPreference:MCEncryptionNone];
        session.delegate = self;
    }
    return self;
}

- (void)dealloc {
    [self stop];
    session.delegate = nil;
    [session release];
    [peerId release];
    [super dealloc];
}

- (void)start:(BOOL)role {
    [self stop];
    {
        std::lock_guard<std::mutex> lock(mutex);
        hostRole = role;
        session.delegate = nil;
        [session release];
        session = [[MCSession alloc] initWithPeer:peerId
                                  securityIdentity:nil
                              encryptionPreference:MCEncryptionNone];
        session.delegate = self;
        advertiser = [[MCNearbyServiceAdvertiser alloc] initWithPeer:peerId
                                                        discoveryInfo:@{ @"role": role ? @"host" : @"guest" }
                                                          serviceType:[NSString stringWithUTF8String:kServiceType]];
        browser = [[MCNearbyServiceBrowser alloc] initWithPeer:peerId
                                                   serviceType:[NSString stringWithUTF8String:kServiceType]];
        advertiser.delegate = self;
        browser.delegate = self;
        connected = NO;
        status = role ? "advertising nearby" : "searching nearby";
    }
    if (role) [advertiser startAdvertisingPeer];
    [browser startBrowsingForPeers];
}

- (void)stop {
    MCNearbyServiceAdvertiser *oldAdvertiser = nil;
    MCNearbyServiceBrowser *oldBrowser = nil;
    MCSession *activeSession = nil;
    {
        std::lock_guard<std::mutex> lock(mutex);
        oldAdvertiser = [advertiser retain];
        oldBrowser = [browser retain];
        activeSession = [session retain];
        advertiser.delegate = nil;
        browser.delegate = nil;
        session.delegate = nil;
        [advertiser release];
        [browser release];
        advertiser = nil;
        browser = nil;
        connected = NO;
        packets.clear();
        status = "stopped";
    }
    [oldAdvertiser stopAdvertisingPeer];
    [oldBrowser stopBrowsingForPeers];
    [activeSession disconnect];
    [oldAdvertiser release];
    [oldBrowser release];
    [activeSession release];
}

- (BOOL)sendBytes:(const void *)bytes size:(int)size reliable:(BOOL)reliable {
    MCSession *activeSession = nil;
    NSArray *peers = nil;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!connected || session == nil) return NO;
        activeSession = [session retain];
        peers = [session.connectedPeers retain];
    }
    if (peers.count == 0) {
        [peers release];
        [activeSession release];
        return NO;
    }
    NSData *data = [NSData dataWithBytes:bytes length:(NSUInteger)size];
    NSError *error = nil;
    const MCSessionSendDataMode mode = reliable ? MCSessionSendDataReliable : MCSessionSendDataUnreliable;
    const BOOL sent = [activeSession sendData:data toPeers:peers withMode:mode error:&error];
    if (!sent && error != nil) {
        std::lock_guard<std::mutex> lock(mutex);
        status = error.localizedDescription.UTF8String ? error.localizedDescription.UTF8String : "nearby send failed";
    }
    [peers release];
    [activeSession release];
    return sent;
}

- (void)advertiser:(MCNearbyServiceAdvertiser *)advertiser
 didReceiveInvitationFromPeer:(MCPeerID *)peerID
       withContext:(NSData *)context
 invitationHandler:(void (^)(BOOL, MCSession *))invitationHandler {
    MCSession *activeSession = nil;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (advertiser == self->advertiser && session != nil)
            activeSession = [session retain];
    }
    if (activeSession == nil) {
        invitationHandler(NO, nil);
        return;
    }
    invitationHandler(YES, activeSession);
    [activeSession release];
}

- (void)browser:(MCNearbyServiceBrowser *)browser
     foundPeer:(MCPeerID *)peerID
 withDiscoveryInfo:(NSDictionary<NSString *, NSString *> *)info {
    if (peerID == self->peerId) return;
    MCSession *activeSession = nil;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (browser != self->browser || hostRole || ![info[@"role"] isEqualToString:@"host"] || session == nil)
            return;
        activeSession = [session retain];
    }
    [browser invitePeer:peerID toSession:activeSession withContext:nil timeout:10.0];
    [activeSession release];
}

- (void)session:(MCSession *)session peer:(MCPeerID *)peerID didChangeState:(MCSessionState)state {
    std::lock_guard<std::mutex> lock(mutex);
    if (session != self->session) return;
    connected = state == MCSessionStateConnected;
    status = connected ? "connected" : (state == MCSessionStateConnecting ? "connecting" : "nearby disconnected");
}

- (void)session:(MCSession *)session didReceiveData:(NSData *)data fromPeer:(MCPeerID *)peerID {
    if (data.length == 0) return;
    std::vector<uint8_t> packet((const uint8_t *)data.bytes,
                                (const uint8_t *)data.bytes + data.length);
    std::lock_guard<std::mutex> lock(mutex);
    if (session != self->session) return;
    if (packets.size() >= 256) packets.pop_front();
    packets.push_back(std::move(packet));
}

- (void)session:(MCSession *)session didReceiveStream:(NSInputStream *)stream
        withName:(NSString *)streamName fromPeer:(MCPeerID *)peerID {}
- (void)session:(MCSession *)session didStartReceivingResourceWithName:(NSString *)resourceName
        fromPeer:(MCPeerID *)peerID withProgress:(NSProgress *)progress {}
- (void)session:(MCSession *)session didFinishReceivingResourceWithName:(NSString *)resourceName
        fromPeer:(MCPeerID *)peerID atURL:(NSURL *)localURL withError:(NSError *)error {}

- (void)browser:(MCNearbyServiceBrowser *)browser lostPeer:(MCPeerID *)peerID {}
- (void)browser:(MCNearbyServiceBrowser *)browser didNotStartBrowsingForPeers:(NSError *)error {
    std::lock_guard<std::mutex> lock(mutex);
    if (browser != self->browser) return;
    connected = NO;
    status = error.localizedDescription.UTF8String ? error.localizedDescription.UTF8String : "browser failed";
}
- (void)advertiser:(MCNearbyServiceAdvertiser *)advertiser didNotStartAdvertisingPeer:(NSError *)error {
    std::lock_guard<std::mutex> lock(mutex);
    if (advertiser != self->advertiser) return;
    connected = NO;
    status = error.localizedDescription.UTF8String ? error.localizedDescription.UTF8String : "advertiser failed";
}
@end

extern "C" int TH06_IOS_BluetoothAvailable() {
    return 1;
}

extern "C" int TH06_IOS_BluetoothStart(int hostRole) {
    @autoreleasepool {
        if (gBridge == nil) gBridge = [PeerBridge new];
        [gBridge start:hostRole ? YES : NO];
        return 1;
    }
}

extern "C" void TH06_IOS_BluetoothStop() {
    @autoreleasepool {
        [gBridge stop];
    }
}

extern "C" int TH06_IOS_BluetoothIsConnected() {
    if (gBridge == nil) return 0;
    std::lock_guard<std::mutex> lock(gBridge->mutex);
    return gBridge->connected ? 1 : 0;
}

extern "C" int TH06_IOS_BluetoothSend(const void *bytes, int size, int reliable) {
    if (gBridge == nil || bytes == nullptr || size <= 0) return 0;
    @autoreleasepool {
        return [gBridge sendBytes:bytes size:size reliable:reliable ? YES : NO] ? 1 : 0;
    }
}

extern "C" int TH06_IOS_BluetoothPoll(void *bytes, int capacity) {
    if (gBridge == nil || bytes == nullptr || capacity <= 0) return 0;
    std::lock_guard<std::mutex> lock(gBridge->mutex);
    if (gBridge->packets.empty()) return 0;
    std::vector<uint8_t> packet = std::move(gBridge->packets.front());
    gBridge->packets.pop_front();
    if ((int)packet.size() > capacity) return -1;
    std::memcpy(bytes, packet.data(), packet.size());
    return (int)packet.size();
}

extern "C" const char *TH06_IOS_BluetoothStatus() {
    static std::string result;
    if (gBridge == nil) return "unavailable";
    std::lock_guard<std::mutex> lock(gBridge->mutex);
    result = gBridge->status;
    return result.c_str();
}
