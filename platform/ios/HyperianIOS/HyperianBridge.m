#import "HyperianBridge.h"
#include "Runtime/hyperian.h"
#include <stdlib.h>

@implementation HyperianBridge {
    HyperianMobile *_mobile;
    char _error[512];
}

- (NSString *)openBytecode:(NSString *)bytecodePath dataPath:(NSString *)dataPath {
    if (_mobile) hyperian_mobile_close(_mobile);
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

- (void)dealloc { if (_mobile) hyperian_mobile_close(_mobile); }
@end
