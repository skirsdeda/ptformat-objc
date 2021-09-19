//
//  ProToolsFormat.mm
//  
//
//  Created by Tadas Dailyda on 2021-09-18.
//

#import <Foundation/Foundation.h>
#import "ptformat/ptformat.h"
#import "include/ProToolsFormat.h"

@implementation ProToolsFormat
PTFFormat *object;
- (instancetype) init {
    self = [super init];

    if (self != nil) {
        object = new PTFFormat();
    }

    return self;
}

- (void) dealloc {
    delete object;
}

- (instancetype) loadFrom:(NSString *)path withSr:(int64_t)targetsr {
    std::string cPath = std::string([path UTF8String], [path lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
    object->load(cPath, targetsr);
    // TODO: return error codes from load
    return self;
}

- (int64_t)sessionRate { 
    return object->sessionrate();
}

@end
