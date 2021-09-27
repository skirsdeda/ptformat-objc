//
//  ProToolsFormat.h
//  
//
//  Created by Tadas Dailyda on 2021-09-18.
//

#ifndef PRO_TOOLS_FORMAT_H
#define PRO_TOOLS_FORMAT_H
#import <Foundation/Foundation.h>

@interface ProToolsFormat : NSObject
+ (instancetype)new NS_UNAVAILABLE;
+ (instancetype)newWithPath:(NSString*)path error:(NSError **)error;
- (instancetype)init NS_UNAVAILABLE;
- (instancetype)initWithPath:(NSString*)path error:(NSError **)error;
- (void)dealloc;
- (uint8_t)version;
- (int64_t)sessionRate;
- (uint8_t)bitDepth;
@end

#endif /* PRO_TOOLS_FORMAT_H */
