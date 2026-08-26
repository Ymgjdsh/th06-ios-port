#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <SDL.h>

#include <atomic>
#include <arpa/inet.h>
#include <cstring>
#include <cstdio>
#include <net/if.h>
#include <netinet/in.h>

static std::atomic<int> g_TH06LocalNetworkPermissionState(0);
static NSNetService *g_TH06BonjourHostService = nil;

@interface TH06BonjourHostPublisherDelegate : NSObject <NSNetServiceDelegate>
@end

static TH06BonjourHostPublisherDelegate *g_TH06BonjourHostDelegate = nil;

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
    if (g_TH06LocalNetworkPermissionState.load() != -1)
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
                    "[local-network] Bonjour browser active; this confirms startup, not permission grant");
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
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[netplay/discovery] Bonjour service found name=%s type=%s domain=%s more=%d",
                    [[service name] UTF8String], [[service type] UTF8String],
                    [[service domain] UTF8String], moreComing ? 1 : 0);
    [service setDelegate:self];
    [service resolveWithTimeout:3.0];
}

- (void)netServiceDidResolveAddress:(NSNetService *)service
{
    NSString *selectedHost = nil;
    int selectedRank = 100;
    for (NSData *addressData in [service addresses])
    {
        const struct sockaddr *address = (const struct sockaddr *)[addressData bytes];
        if (address == NULL)
            continue;

        char addressText[INET6_ADDRSTRLEN + IF_NAMESIZE + 16] = {};
        int rank = 100;
        const char *familyText = "other";
        if (address->sa_family == AF_INET && [addressData length] >= sizeof(sockaddr_in))
        {
            const struct sockaddr_in *address4 = (const struct sockaddr_in *)address;
            if (inet_ntop(AF_INET, &address4->sin_addr, addressText, sizeof(addressText)) == NULL)
                continue;
            familyText = "IPv4";
            const uint32_t hostAddress = ntohl(address4->sin_addr.s_addr);
            if (hostAddress == INADDR_ANY)
                continue;
            const bool isLoopback = (hostAddress & 0xff000000u) == 0x7f000000u;
            const bool isLinkLocal = (hostAddress & 0xffff0000u) == 0xa9fe0000u;
            const bool isPrivate = (hostAddress & 0xff000000u) == 0x0a000000u ||
                                   (hostAddress & 0xfff00000u) == 0xac100000u ||
                                   (hostAddress & 0xffff0000u) == 0xc0a80000u;
            // Bonjour may resolve the same peer on both Wi-Fi and Apple's
            // AWDL link (169.254/16). AWDL duty cycling can add roughly 100 ms
            // of latency, so prefer the real private LAN address when present.
            rank = isLoopback ? 50 : (isPrivate ? 0 : (isLinkLocal ? 30 : 8));
        }
        else if (address->sa_family == AF_INET6 && [addressData length] >= sizeof(sockaddr_in6))
        {
            const struct sockaddr_in6 *address6 = (const struct sockaddr_in6 *)address;
            if (IN6_IS_ADDR_UNSPECIFIED(&address6->sin6_addr))
                continue;
            if (inet_ntop(AF_INET6, &address6->sin6_addr, addressText, sizeof(addressText)) == NULL)
                continue;
            familyText = "IPv6";
            const bool isUniqueLocal = (address6->sin6_addr.s6_addr[0] & 0xfe) == 0xfc;
            rank = IN6_IS_ADDR_LOOPBACK(&address6->sin6_addr) ? 50
                   : (isUniqueLocal ? 5 : (IN6_IS_ADDR_LINKLOCAL(&address6->sin6_addr) ? 30 : 10));
            if (address6->sin6_scope_id != 0)
            {
                const size_t used = std::strlen(addressText);
                if (used + 2 < sizeof(addressText))
                {
                    char interfaceName[IF_NAMESIZE] = {};
                    if (if_indextoname(address6->sin6_scope_id, interfaceName) != NULL)
                        std::snprintf(addressText + used, sizeof(addressText) - used, "%%%s", interfaceName);
                    else
                        std::snprintf(addressText + used, sizeof(addressText) - used, "%%%u",
                                      address6->sin6_scope_id);
                }
            }
        }
        else
        {
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                            "[netplay/discovery] Bonjour address ignored family=%d bytes=%lu",
                            (int)address->sa_family, (unsigned long)[addressData length]);
            continue;
        }

        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[netplay/discovery] Bonjour address candidate family=%s host=%s rank=%d",
                        familyText, addressText, rank);
        if (rank < selectedRank)
        {
            [selectedHost release];
            selectedHost = [[NSString alloc] initWithUTF8String:addressText];
            selectedRank = rank;
        }
    }

    NSString *resolvedName = [service hostName];
    if ((selectedHost == nil || selectedRank >= 50) && [resolvedName length] > 0)
    {
        [selectedHost release];
        selectedHost = [resolvedName copy];
        selectedRank = 25;
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[netplay/discovery] Bonjour using hostname fallback host=%s",
                        [selectedHost UTF8String]);
    }

    if (selectedHost != nil && [service port] > 0)
    {
        @synchronized(self)
        {
            [_resolvedHost release];
            _resolvedHost = [selectedHost copy];
            _resolvedPort = [service port];
        }
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[netplay/discovery] Bonjour resolved selectedHost=%s port=%ld rank=%d",
                        [selectedHost UTF8String], (long)[service port], selectedRank);
    }
    else
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[netplay/discovery] Bonjour resolved but no usable endpoint name=%s hostName=%s port=%ld addresses=%lu",
                     [[service name] UTF8String], [resolvedName UTF8String], (long)[service port],
                     (unsigned long)[[service addresses] count]);
    }
    [selectedHost release];
    @synchronized(self)
    {
        [service setDelegate:nil];
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

@implementation TH06BonjourHostPublisherDelegate

- (void)netServiceWillPublish:(NSNetService *)service
{
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[netplay/discovery] Bonjour host publication starting name=%s type=%s domain=%s port=%ld",
                    [[service name] UTF8String], [[service type] UTF8String],
                    [[service domain] UTF8String], (long)[service port]);
}

- (void)netServiceDidPublish:(NSNetService *)service
{
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[netplay/discovery] Bonjour host publication succeeded name=%s type=%s domain=%s port=%ld",
                    [[service name] UTF8String], [[service type] UTF8String],
                    [[service domain] UTF8String], (long)[service port]);
}

- (void)netService:(NSNetService *)service didNotPublish:(NSDictionary *)errorDict
{
    NSNumber *domain = [errorDict objectForKey:NSNetServicesErrorDomain];
    NSNumber *code = [errorDict objectForKey:NSNetServicesErrorCode];
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "[netplay/discovery] Bonjour host publication failed name=%s domain=%ld code=%ld",
                 [[service name] UTF8String], (long)[domain integerValue], (long)[code integerValue]);
}

- (void)netServiceDidStop:(NSNetService *)service
{
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                    "[netplay/discovery] Bonjour host publication stopped name=%s",
                    [[service name] UTF8String]);
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
        [g_TH06BonjourHostService setDelegate:nil];
        [g_TH06BonjourHostService release];
        if (g_TH06BonjourHostDelegate == nil)
            g_TH06BonjourHostDelegate = [[TH06BonjourHostPublisherDelegate alloc] init];
        g_TH06BonjourHostService = [[NSNetService alloc] initWithDomain:@"local."
                                                                  type:@"_th06-netplay._udp."
                                                                  name:[[UIDevice currentDevice] name]
                                                                  port:port];
        [g_TH06BonjourHostService setDelegate:g_TH06BonjourHostDelegate];
        [g_TH06BonjourHostService publish];
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION,
                        "[netplay/discovery] Bonjour host publication requested port=%d", port);
    });
}

extern "C" void TH06_IOS_StopBonjourHost()
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [g_TH06BonjourHostService stop];
        [g_TH06BonjourHostService setDelegate:nil];
        [g_TH06BonjourHostService release];
        g_TH06BonjourHostService = nil;
    });
}

extern "C" int TH06_IOS_PollBonjourHost(char *host, int capacity, int *port)
{
    return [TH06GetLocalNetworkPermissionProbe() consumeResolvedHost:host capacity:capacity port:port] ? 1 : 0;
}
