//
//  ProToolsFormat.mm
//  
//
//  Created by Tadas Dailyda on 2021-09-18.
//

#import <Foundation/Foundation.h>
#import "ptformat/ptformat.h"
#import "include/ProToolsFormat.h"

@interface PTBlock()
+ (instancetype) fromBlock:(PTFFormat::block_t)block unxored:(const unsigned char *)unxored;
+ (NSArray<PTBlock *> *) arrayFromVector:(std::vector<PTFFormat::block_t>)blocksVec unxored:(const unsigned char *)unxored;
@end

@implementation PTBlock

+ (instancetype) fromBlock:(PTFFormat::block_t)block unxored:(const unsigned char *)unxored {
    PTBlock *ptBlock = [[PTBlock alloc] init];
    ptBlock->_type = block.block_type;
    ptBlock->_contentType = block.content_type;
    ptBlock->_offset = block.offset;
    ptBlock->_data = [[NSData alloc] initWithBytes:&unxored[block.offset] length:block.block_size];
    ptBlock->_children = [PTBlock arrayFromVector:block.child unxored:unxored];
    return ptBlock;
}

+ (NSArray<PTBlock *> *) arrayFromVector:(std::vector<PTFFormat::block_t>)blocksVec unxored:(const unsigned char *)unxored {
    PTBlock *blocks[blocksVec.size()];
    for (int i = 0; i < blocksVec.size(); i++) {
        blocks[i] = [PTBlock fromBlock:blocksVec[i] unxored:unxored];
    }
    return [[NSArray alloc] initWithObjects:blocks count:blocksVec.size()];
}

@end

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
                // TODO: add error description to userInfo dictionary
                // convert error code to a positive integer for NSError, otherwise it will overflow
                *error = [NSError errorWithDomain:@"com.github.ptformat-objc.ErrorDomain" code:-result userInfo:nil];
            }
            // dealloc will still be called so no need to delete object here
            return nil;
        }
    }

    return self;
}

- (void) dealloc {
    if (object != NULL) {
        delete object;
    }
}

- (uint8_t) version {
    return object->version();
}

- (int64_t) sessionRate {
    return object->sessionrate();
}

- (uint8_t) bitDepth {
    return object->bitdepth();
}

- (NSData *) unxoredData {
    return [[NSData alloc] initWithBytes:object->unxored_data() length:object->unxored_size()];
}

- (NSArray<PTBlock *> *) blocks {
    return [PTBlock arrayFromVector:object->blocks() unxored:object->unxored_data()];
}

@end
