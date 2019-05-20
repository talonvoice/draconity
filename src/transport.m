// FIXME delete this once new network layer is implemented
// (keeping around as an easy reference for now)
#import <AppKit/AppKit.h>
#import "app/TalonHelper/TalonHelperProtocol.h"
#include "transport.h"

@class SpeechServer;
@class SpeechClient;

static id busProxy = NULL;
static NSXPCConnection *bus = NULL;
static NSXPCListener *listener = NULL;
static SpeechServer *server = NULL;
static transport_msg_fn handle_msg = NULL;

@interface SpeechClient : NSObject <TalonHelperEndpointProtocol>
@property (strong) id proxy;
@end

@implementation SpeechClient
+ (id)clientWithPid:(pid_t)pid {
    SpeechClient *obj = [[SpeechClient alloc] init];
    return obj;
}

- (void)send:(NSData *)data withReply:(void (^)(NSData *))reply {
    bson_t *resp = handle_msg([data bytes], [data length]);
    uint32_t length = 0;
    uint8_t *buf = bson_destroy_with_steal(resp, true, &length);
    reply([NSData dataWithBytes:buf length:length]);
    free(buf);
}
@end

@interface SpeechServer : NSObject <NSXPCListenerDelegate>
@property (strong) NSMutableSet *clients;
@end

@implementation SpeechServer

- (id)init {
    self = [super init];
    self.clients = [NSMutableSet set];
    return self;
}

- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    pid_t pid = connection.processIdentifier;
    SpeechClient *client = [SpeechClient clientWithPid:pid];
    connection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(TalonHelperEndpointProtocol)];
    connection.exportedObject = client;
    connection.invalidationHandler = ^{
        [self.clients removeObject:client];
    };
    connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(TalonHelperEndpointClientProtocol)];
    client.proxy = [connection remoteObjectProxyWithErrorHandler:^(NSError *error) {
        NSLog(@"proxy error %@ from %d", error, pid);
    }];
    [connection resume];
    [self.clients addObject:client];
    return YES;
}

@end

void draconity_transport_main(transport_msg_fn fn, const char *name) {
    handle_msg = fn;
    bus = [[NSXPCConnection alloc] initWithMachServiceName:@"com.talonvoice.TalonHelper" options:NSXPCConnectionPrivileged];
    bus.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(TalonBusProtocol)];
    bus.invalidationHandler = ^{
        NSLog(@"TalonBus connection invalidated");
        // TODO?
    };
    busProxy = [bus remoteObjectProxyWithErrorHandler:^(NSError *error) {
        NSLog(@"TalonBus connect failed: %@ / %d", [error domain], (int)[error code]);
    }];
    [bus resume];
    listener = [NSXPCListener anonymousListener];
    listener.delegate = server = [[SpeechServer alloc] init];
    [listener resume];
    [busProxy publish:[NSString stringWithFormat:@"speech.%s", name] endpoint:[listener endpoint]];
}

void draconity_transport_publish(const char *topic, uint8_t *data, uint32_t size) {
    dispatch_async(dispatch_get_main_queue(), ^{
        for (SpeechClient *client in server.clients) {
            [client.proxy publishTopic:[NSString stringWithUTF8String:topic] data:[NSData dataWithBytes:data length:size]];
        }
        bson_free(data);
    });
}
