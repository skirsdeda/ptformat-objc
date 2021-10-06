//
//  ProToolsFormat.h
//  
//
//  Created by Tadas Dailyda on 2021-09-18.
//

#ifndef PRO_TOOLS_FORMAT_H
#define PRO_TOOLS_FORMAT_H
#import <Foundation/Foundation.h>

@interface PTBlock : NSObject
+ (instancetype) new NS_UNAVAILABLE;
- (instancetype) init NS_UNAVAILABLE;
@property (nonatomic, readonly) uint16_t type;
@property (nonatomic, readonly) uint16_t contentType;
@property (nonatomic, readonly) uint32_t offset;
@property (nonatomic, strong, readonly) NSData *data;
@property (nonatomic, strong, readonly) NSArray<PTBlock *> *children;
@end

@interface ProToolsFormat : NSObject
+ (instancetype) new NS_UNAVAILABLE;
+ (instancetype) newWithPath:(NSString *)path error:(NSError **)error;
- (instancetype) init NS_UNAVAILABLE;
- (instancetype) initWithPath:(NSString *)path error:(NSError **)error;
- (void) dealloc;
- (uint8_t) version;
- (int64_t) sessionRate;
- (uint8_t) bitDepth;
- (NSData *) unxoredData;
- (NSArray<PTBlock *> *) blocks;
@end

#endif /* PRO_TOOLS_FORMAT_H */
