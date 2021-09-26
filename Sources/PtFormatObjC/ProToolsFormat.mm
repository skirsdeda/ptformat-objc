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

+ (instancetype) newWithPath:(NSString *)path error:(NSError **)error {
    return [[ProToolsFormat alloc] initWithPath:path error:error];
}

- (instancetype) initWithPath:(NSString *)path error:(NSError **)error {
    self = [super init];

    if (self != nil) {
        object = new PTFFormat();
        std::string cPath = std::string([path UTF8String], [path lengthOfBytesUsingEncoding:NSUTF8StringEncoding]);

        int result;
        if ((result = object->load(cPath))) {
            if (error != NULL) {
                *error = [NSError errorWithDomain:@"com.github.ptformat-objc.ErrorDomain" code:result userInfo:nil];
            }
            delete object;
            return nil;
        }
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
