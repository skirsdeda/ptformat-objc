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

+ (instancetype) newWithPath:(NSString *)path {
    return [[ProToolsFormat alloc] initWithPath:path];
}

- (instancetype) initWithPath:(NSString *)path {
    self = [super init];

    if (self != nil) {
        object = new PTFFormat();
        std::string cPath = std::string([path UTF8String], [path lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);
        object->load(cPath);
        // TODO: return error codes from load or throw exception even
    }

    return self;
}

- (void) dealloc {
    delete object;
}

- (int64_t)sessionRate { 
    return object->sessionrate();
}

@end
