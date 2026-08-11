#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
@interface HyperianBridge : NSObject
- (NSString *)openBytecode:(NSString *)bytecodePath dataPath:(NSString *)dataPath;
- (void)setValue:(NSString *)value named:(NSString *)name;
- (NSString *)runAction:(NSString *)action;
- (NSString *)sendEvent:(NSString *)event;
- (NSString *)render;
@end
NS_ASSUME_NONNULL_END
