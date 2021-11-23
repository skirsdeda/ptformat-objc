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
+ (nullable instancetype) new NS_UNAVAILABLE;
- (nullable instancetype) init NS_UNAVAILABLE;
@property (nonatomic, readonly) uint16_t type;
@property (nonatomic, readonly) uint16_t contentType;
@property (nonatomic, readonly) uint32_t offset;
@property (nonatomic, strong, readonly, nonnull) NSData *data;
@property (nonatomic, strong, readonly, nonnull) NSArray<PTBlock *> *children;
@end

@interface PTMetadata : NSObject
+ (nullable instancetype) new NS_UNAVAILABLE;
- (nullable instancetype) init NS_UNAVAILABLE;
@property (nonatomic, strong, readonly, nullable) NSString *title;
@property (nonatomic, strong, readonly, nullable) NSString *artist;
@property (nonatomic, strong, readonly, nonnull) NSArray<NSString *> *contributors;
@property (nonatomic, strong, readonly, nullable) NSString *location;
@end

@interface ProToolsFormat : NSObject
+ (nullable instancetype) new NS_UNAVAILABLE;
+ (nullable instancetype) newWithPath:(nonnull NSString *)path error:(NSError * _Nullable * _Nullable)error;
- (nullable instancetype) init NS_UNAVAILABLE;
- (nullable instancetype) initWithPath:(nonnull NSString *)path error:(NSError * _Nullable * _Nullable)error;
- (void) dealloc;
- (uint8_t) version;
- (int64_t) sessionRate;
- (uint8_t) bitDepth;
- (nonnull NSData *) unxoredData;
- (nullable NSData *) metadataBase64;
- (nonnull PTMetadata *) metadata;
- (nonnull NSArray<PTBlock *> *) blocks;
@end

#endif /* PRO_TOOLS_FORMAT_H */
