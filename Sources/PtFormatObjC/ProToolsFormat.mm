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

@interface PTMetadata()
+ (instancetype) metaWithTitle:(NSString *)title artist:(NSString *) artist contributors:(NSArray<NSString *> *)contributors
                      location:(NSString *)location;
@end

@implementation PTMetadata
+ (instancetype) metaWithTitle:(NSString *)title artist:(NSString *) artist contributors:(NSArray<NSString *> *)contributors
                      location:(NSString *)location {
    PTMetadata *ptMeta = [[PTMetadata alloc] init];
    ptMeta->_title = title;
    ptMeta->_artist = artist;
    ptMeta->_contributors = contributors;
    ptMeta->_location = location;
    return ptMeta;
}
@end

@implementation PTKeySignature
+ (instancetype) keySigWithPos:(uint64_t)pos isMajor:(BOOL)isMajor isSharp:(BOOL)isSharp signs:(uint8_t)signs {
    PTKeySignature *ptKeySig = [[PTKeySignature alloc] init];
    ptKeySig->_pos = pos;
    ptKeySig->_isMajor = isMajor;
    ptKeySig->_isSharp = isSharp;
    ptKeySig->_signs = signs;
    return ptKeySig;
}

- (BOOL)isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToKeySignature:other];
}

- (BOOL)isEqualToKeySignature:(PTKeySignature *)keySig {
    if (self == keySig)
        return YES;
    return (self->_pos == keySig->_pos && self->_isMajor == keySig->_isMajor &&
            self->_isSharp == keySig->_isSharp && self->_signs == keySig->_signs);
}
@end

@implementation ProToolsFormat {
    PTFFormat *object;
}

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

- (nullable NSData *) metadataBase64 {
    const unsigned char *data = object->metadata_base64();
    if (data == NULL) {
        return nil;
    }
    return [[NSData alloc] initWithBytes:object->metadata_base64() length:object->metadata_base64_size()];
}

- (nonnull PTMetadata *) metadata {
    const PTFFormat::metadata_t meta = object->metadata();
    NSString *title, *artist, *location;
    NSMutableArray<NSString *> *contributors = [NSMutableArray arrayWithCapacity:meta.contributors.size()];

    if (!meta.title.empty()) {
        title = [NSString stringWithUTF8String:meta.title.c_str()];
    }
    if (!meta.artist.empty()) {
        artist = [NSString stringWithUTF8String:meta.artist.c_str()];
    }
    if (!meta.location.empty()) {
        location = [NSString stringWithUTF8String:meta.location.c_str()];
    }
    for (int i = 0; i < meta.contributors.size(); ++i) {
        [contributors addObject:[NSString stringWithUTF8String:meta.contributors[i].c_str()]];
    }

    return [PTMetadata metaWithTitle:title artist:artist contributors:contributors location:location];
}

- (nonnull NSArray<PTKeySignature *> *) keySignatures {
    std::vector<PTFFormat::key_signature_t> keySigsSrc = object->keysignatures();
    PTKeySignature *keySigs[keySigsSrc.size()];
    for (int i = 0; i < keySigsSrc.size(); i++) {
        PTFFormat::key_signature_t k = keySigsSrc[i];
        keySigs[i] = [PTKeySignature keySigWithPos:k.pos isMajor:k.is_major isSharp:k.is_sharp signs:k.sign_count];
    }
    return [[NSArray alloc] initWithObjects:keySigs count:keySigsSrc.size()];
}

- (nonnull NSArray<PTBlock *> *) blocks {
    return [PTBlock arrayFromVector:object->blocks() unxored:object->unxored_data()];
}

@end
