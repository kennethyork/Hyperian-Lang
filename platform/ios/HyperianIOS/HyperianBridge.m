#import "HyperianBridge.h"
#import <dispatch/dispatch.h>
#include "Runtime/hyperian.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ios_http_get(const char *url, char *body, size_t body_size, long *status, char *error, size_t error_size) {
    @autoreleasepool {
        NSString *addressText = [NSString stringWithUTF8String:url]; NSURL *address = addressText ? [NSURL URLWithString:addressText] : nil;
        NSString *scheme = address.scheme.lowercaseString;
        if (!address || (![scheme isEqualToString:@"https"] && ![scheme isEqualToString:@"http"])) {
            snprintf(error, error_size, "phone requests require an http or https address"); return 0;
        }
        NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:address cachePolicy:NSURLRequestReloadIgnoringLocalCacheData timeoutInterval:30.0];
        [request setValue:@"Hyperian Mobile" forHTTPHeaderField:@"User-Agent"];
        NSURLSessionConfiguration *configuration = NSURLSessionConfiguration.ephemeralSessionConfiguration;
        configuration.timeoutIntervalForRequest = 30.0; configuration.timeoutIntervalForResource = 30.0;
        NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration]; dispatch_semaphore_t completed = dispatch_semaphore_create(0);
        __block NSData *received = nil; __block NSHTTPURLResponse *httpResponse = nil; __block NSError *problem = nil;
        NSURLSessionDataTask *task = [session dataTaskWithRequest:request completionHandler:^(NSData *data, NSURLResponse *response, NSError *requestError) {
            received = data; httpResponse = [response isKindOfClass:NSHTTPURLResponse.class] ? (NSHTTPURLResponse *)response : nil;
            problem = requestError; dispatch_semaphore_signal(completed);
        }];
        [task resume]; dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER); [session finishTasksAndInvalidate];
        if (problem) { snprintf(error, error_size, "web request failed: %s", problem.localizedDescription.UTF8String); return 0; }
        if (!httpResponse) { snprintf(error, error_size, "the phone did not receive an HTTP response"); return 0; }
        if (received.length >= body_size) { snprintf(error, error_size, "the web response is larger than %zu characters", body_size - 1); return 0; }
        NSString *text = [[NSString alloc] initWithData:received ?: [NSData data] encoding:NSUTF8StringEncoding];
        if (!text) { snprintf(error, error_size, "the web response is not UTF-8 text"); return 0; }
        const char *utf8 = text.UTF8String; size_t length = strlen(utf8);
        if (length >= body_size) { snprintf(error, error_size, "the web response is larger than %zu characters", body_size - 1); return 0; }
        memcpy(body, utf8, length + 1); *status = httpResponse.statusCode; return 1;
    }
}

@implementation HyperianBridge {
    HyperianMobile *_mobile;
    char _error[512];
}

- (NSString *)openBytecode:(NSString *)bytecodePath dataPath:(NSString *)dataPath {
    if (_mobile) hyperian_mobile_close(_mobile);
    hyperian_set_http_handler(ios_http_get);
    setenv("HYPERIAN_DATA", dataPath.fileSystemRepresentation, 1);
    _mobile = hyperian_mobile_open(bytecodePath.fileSystemRepresentation, _error, sizeof(_error));
    if (!_mobile || !hyperian_mobile_start(_mobile, _error, sizeof(_error))) return [NSString stringWithUTF8String:_error];
    return @"";
}

- (void)setValue:(NSString *)value named:(NSString *)name {
    if (_mobile) hyperian_mobile_set(_mobile, name.UTF8String, value.UTF8String, _error, sizeof(_error));
}

- (NSString *)runAction:(NSString *)action {
    if (!_mobile || !hyperian_mobile_run_action(_mobile, action.UTF8String, NULL, _error, sizeof(_error))) return [NSString stringWithUTF8String:_error];
    return @"";
}

- (NSString *)sendEvent:(NSString *)event {
    if (!_mobile || !hyperian_mobile_send_event(_mobile, event.UTF8String, _error, sizeof(_error))) return [NSString stringWithUTF8String:_error];
    return @"";
}

- (NSString *)render {
    char *json = malloc(262144);
    if (!json) return @"{\"error\":\"not enough memory\"}";
    if (!_mobile || !hyperian_mobile_render_json(_mobile, json, 262144, _error, sizeof(_error))) { free(json); return @"{\"error\":\"mobile view could not be rendered\"}"; }
    NSString *result = [NSString stringWithUTF8String:json]; free(json); return result;
}

- (void)dealloc { if (_mobile) hyperian_mobile_close(_mobile); hyperian_set_http_handler(NULL); }
@end
