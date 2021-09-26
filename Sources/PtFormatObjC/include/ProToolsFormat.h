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
+ (instancetype)newWithPath:(NSString*)path;
- (instancetype)init NS_UNAVAILABLE;
- (instancetype)initWithPath:(NSString*)path;
- (void)dealloc;
- (int64_t)sessionRate;
@end

#endif /* PRO_TOOLS_FORMAT_H */
