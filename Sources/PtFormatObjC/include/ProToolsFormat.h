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
- (instancetype)init;
- (void)dealloc;
- (instancetype)loadFrom:(NSString*)path withSr:(int64_t)targetsr;
- (int64_t)sessionRate;
@end

#endif /* PRO_TOOLS_FORMAT_H */
