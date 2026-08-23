#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <SDL.h>

#include <atomic>
#include <arpa/inet.h>
#include <cstring>
#include <cstdio>
#include <netinet/in.h>

static std::atomic<int> g_TH06LocalNetworkPermissionState(0);
static NSNetService *g_TH06BonjourHostService = nil;

@interface TH06LocalNetworkPermissionProbe : NSObject <NSNetServiceBrowserDelegate, NSNetServiceDelegate>
{
    NSNetServiceBrowser *_browser;
    NSMutableArray *_resolvingServices;
    NSString *_resolvedHost;
    NSInteger _resolvedPort;
    BOOL _searching;
}
- (void)start;
- (void)stop;
- (BOOL)consumeResolvedHost:(char *)host capacity:(int)capacity port:(int *)port;
@end

@implementation TH06LocalNetworkPermissionProbe

- (id)init
{
    self = [super init];
    if (self != nil)
    {
        _browser = [[NSNetServiceBrowser alloc] init];
        [_browser setDelegate:self];
        _resolvingServices = [[NSMutableArray alloc] init];
    }
    return self;
}

- (void)dealloc
{
    [NSObject cancelPreviousPerformRequestsWithTarget:self];
    [_browser stop];
    [_browser setDelegate:nil];
    for (NSNetService *service in _resolvingServices)
    {
        [service stop];
        [service setDelegate:nil];
    }
    [_browser release];
    [_resolvingServices release];
    [_resolvedHost release];
    [super dealloc];
}

- (void)start
{
    if (_searching)
    {
        return;
    }
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(stop)
                                               object:nil];
    [_browser stop];
    @synchronized(self)
    {
        for (NSNetService *service in _resolvingServices)
        {
            [service stop];
            [service setDelegate:nil];
        }
        [_resolvingServices removeAllObjects];
        [_resolvedHost release];
        _resolvedHost = nil;
        _resolvedPort = 0;
    }
    g_TH06LocalNetworkPermissionState.store(1);
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[local-network] Bonjour permission probe start type=_th06-netplay._udp.");
    [_browser searchForServicesOfType:@"_th06-netplay._udp." inDomain:@"local."];
    [self performSelector:@selector(stop) withObject:nil afterDelay:12.0];
}

- (void)stop
{
    [NSObject cancelPreviousPerformRequestsWithTarget:self
                                             selector:@selector(stop)
                                               object:nil];
    [_browser stop];
    @synchronized(self)
    {
        for (NSNetService *service in _resolvingServices)
        {
            [service stop];
            [service setDelegate:nil];
        }
        [_resolvingServices removeAllObjects];
        [_resolvedHost release];
        _resolvedHost = nil;
        _resolvedPort = 0;
    }
    _searching = NO;
    g_TH06LocalNetworkPermissionState.store(0);
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[local-network] Bonjour permission probe stopped");
}

- (void)netServiceBrowserWillSearch:(NSNetServiceBrowser *)browser
{
    (void)browser;
    _searching = YES;
    g_TH06LocalNetworkPermissionState.store(2);
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[local-network] Bonjour browser searching; iOS permission request accepted for processing");
}

- (void)netServiceBrowserDidStopSearch:(NSNetServiceBrowser *)browser
{
    (void)browser;
    _searching = NO;
    if (g_TH06LocalNetworkPermissionState.load() != -1)
        g_TH06LocalNetworkPermissionState.store(0);
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[local-network] Bonjour browser did stop");
}

- (void)netServiceBrowser:(NSNetServiceBrowser *)browser
               didNotSearch:(NSDictionary *)errorDict
{
    (void)browser;
    _searching = NO;
    g_TH06LocalNetworkPermissionState.store(-1);
    NSNumber *domain = [errorDict objectForKey:NSNetServicesErrorDomain];
    NSNumber *code = [errorDict objectForKey:NSNetServicesErrorCode];
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[local-network] Bonjour browser failed domain=%ld code=%ld",
                 (long)[domain integerValue], (long)[code integerValue]);
}

- (void)netServiceBrowser:(NSNetServiceBrowser *)browser didFindService:(NSNetService *)service
                moreComing:(BOOL)moreComing
{
    (void)browser;
    (void)moreComing;
    @synchronized(self)
    {
        [_resolvingServices addObject:service];
    }
    [service setDelegate:self];
    [service resolveWithTimeout:3.0];
}

- (void)netServiceDidResolveAddress:(NSNetService *)service
{
    char addressText[INET6_ADDRSTRLEN] = {};
    for (NSData *addressData in [service addresses])
    {
        const struct sockaddr *address = (const struct sockaddr *)[addressData bytes];
        if (address != NULL && address->sa_family == AF_INET)
        {
            const struct sockaddr_in *address4 = (const struct sockaddr_in *)address;
            if (inet_ntop(AF_INET, &address4->sin_addr, addressText, sizeof(addressText)) != NULL)
                break;
        }
    }
    if (addressText[0] != '\0')
    {
        @synchronized(self)
        {
            [_resolvedHost release];
            _resolvedHost = [[NSString alloc] initWithUTF8String:addressText];
            _resolvedPort = [service port];
        }
        SDL_Log("[netplay/discovery] Bonjour resolved host=%s port=%ld", addressText, (long)[service port]);
    }
    @synchronized(self)
    {
        [_resolvingServices removeObject:service];
    }
}

- (void)netService:(NSNetService *)service didNotResolve:(NSDictionary *)errorDict
{
    NSNumber *domain = [errorDict objectForKey:NSNetServicesErrorDomain];
    NSNumber *code = [errorDict objectForKey:NSNetServicesErrorCode];
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "[netplay/discovery] Bonjour resolve failed domain=%ld code=%ld",
                (long)[domain integerValue], (long)[code integerValue]);
    @synchronized(self)
    {
        [service setDelegate:nil];
        [_resolvingServices removeObject:service];
    }
}

- (BOOL)consumeResolvedHost:(char *)host capacity:(int)capacity port:(int *)port
{
    @synchronized(self)
    {
        if (_resolvedHost == nil || host == NULL || capacity <= 0 || port == NULL)
            return NO;
        std::snprintf(host, (size_t)capacity, "%s", [_resolvedHost UTF8String]);
        *port = (int)_resolvedPort;
        [_resolvedHost release];
        _resolvedHost = nil;
        _resolvedPort = 0;
        return YES;
    }
}

@end

static TH06LocalNetworkPermissionProbe *g_TH06LocalNetworkPermissionProbe = nil;

static TH06LocalNetworkPermissionProbe *TH06GetLocalNetworkPermissionProbe()
{
    @synchronized([TH06LocalNetworkPermissionProbe class])
    {
        if (g_TH06LocalNetworkPermissionProbe == nil)
            g_TH06LocalNetworkPermissionProbe = [[TH06LocalNetworkPermissionProbe alloc] init];
        return g_TH06LocalNetworkPermissionProbe;
    }
}

extern "C" void TH06_IOS_TriggerLocalNetworkPermission()
{
    TH06LocalNetworkPermissionProbe *probe = TH06GetLocalNetworkPermissionProbe();
    // The Objective-C probe is idempotent. Always enqueue the request so a
    // queued stop from the previous search is followed by a fresh start.
    [probe performSelectorOnMainThread:@selector(start) withObject:nil waitUntilDone:NO];
}

extern "C" void TH06_IOS_StopLocalNetworkPermissionProbe()
{
    TH06LocalNetworkPermissionProbe *probe = TH06GetLocalNetworkPermissionProbe();
    [probe performSelectorOnMainThread:@selector(stop) withObject:nil waitUntilDone:NO];
}

extern "C" int TH06_IOS_GetLocalNetworkPermissionState()
{
    return g_TH06LocalNetworkPermissionState.load();
}

extern "C" void TH06_IOS_StartBonjourHost(int port)
{
    if (port <= 0 || port > 65535) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [g_TH06BonjourHostService stop];
        [g_TH06BonjourHostService release];
        g_TH06BonjourHostService = [[NSNetService alloc] initWithDomain:@"local."
                                                                  type:@"_th06-netplay._udp."
                                                                  name:[[UIDevice currentDevice] name]
                                                                  port:port];
        [g_TH06BonjourHostService publish];
        SDL_Log("[netplay/discovery] Bonjour host published port=%d", port);
    });
}

extern "C" void TH06_IOS_StopBonjourHost()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [g_TH06BonjourHostService stop];
        [g_TH06BonjourHostService release];
        g_TH06BonjourHostService = nil;
    });
}

extern "C" int TH06_IOS_PollBonjourHost(char *host, int capacity, int *port)
{
    return [TH06GetLocalNetworkPermissionProbe() consumeResolvedHost:host capacity:capacity port:port] ? 1 : 0;
}
