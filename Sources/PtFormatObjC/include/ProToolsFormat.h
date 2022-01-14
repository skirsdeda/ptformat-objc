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
+ (nonnull instancetype) new NS_UNAVAILABLE;
- (nonnull instancetype) init NS_UNAVAILABLE;
@property (nonatomic, readonly) uint16_t type;
@property (nonatomic, readonly) uint16_t contentType;
@property (nonatomic, readonly) uint32_t offset;
@property (nonatomic, strong, readonly, nonnull) NSData *data;
@property (nonatomic, strong, readonly, nonnull) NSArray<PTBlock *> *children;
@end

@interface PTWav : NSObject
+ (nonnull instancetype) new NS_UNAVAILABLE;
+ (nonnull instancetype) wavWithFilename:(nonnull NSString *)filename index:(uint16_t)index posAbsolute:(uint64_t)pos length:(uint64_t)length;
- (nonnull instancetype) init NS_UNAVAILABLE;
@property (nonatomic, strong, readonly, nonnull) NSString *filename;
@property (nonatomic, readonly) uint16_t index;
@property (nonatomic, readonly) uint64_t posAbsolute;
@property (nonatomic, readonly) uint64_t length;
@end

@interface PTMidiEv : NSObject
+ (nonnull instancetype) new NS_UNAVAILABLE;
+ (nonnull instancetype) midiEvWithPos:(uint64_t)pos length:(uint64_t)length note:(uint8_t)note velocity:(uint8_t)velocity;
- (nonnull instancetype) init NS_UNAVAILABLE;
@property (nonatomic, readonly) uint64_t pos;
@property (nonatomic, readonly) uint64_t length;
@property (nonatomic, readonly) uint8_t note;
@property (nonatomic, readonly) uint8_t velocity;
@end

// FIXME: split Region (probably better called Clip) into ADT having AudioClip and MidiClip so that there are no nullable fields
//        and both of them contain only the fields that won't be empty (wave and probably sampleOffset only in AudioClip; midi only in MidiClip)
@interface PTRegion : NSObject
+ (nonnull instancetype) new NS_UNAVAILABLE;
+ (nonnull instancetype) regionWithName:(nonnull NSString *)name index:(uint16_t)index isStartPosInTicks:(BOOL)isInTicks
                               startPos:(uint64_t)startPos sampleOffset:(uint64_t)sampleOffset length:(uint64_t)length
                                   wave:(nullable PTWav *)wave midi:(nonnull NSArray<PTMidiEv *> *)midi;
- (nonnull instancetype) init NS_UNAVAILABLE;
@property (nonatomic, strong, readonly, nonnull) NSString *name;
@property (nonatomic, readonly) uint16_t index;
@property (nonatomic, readonly) BOOL isStartPosInTicks;
@property (nonatomic, readonly) uint64_t startPos;
@property (nonatomic, readonly) uint64_t sampleOffset;
@property (nonatomic, readonly) uint64_t length;
@property (nonatomic, strong, readonly, nullable) PTWav *wave;
@property (nonatomic, strong, readonly, nonnull) NSArray<PTMidiEv *> *midi;
@end

// FIXME: this should have 1toMany rel with regions(clips) instead of 1to1!
@interface PTTrack : NSObject
+ (nonnull instancetype) new NS_UNAVAILABLE;
+ (nonnull instancetype) trackWithName:(nonnull NSString *)name index:(uint16_t)index playlist:(uint8_t)playlist region:(nonnull PTRegion *)region;
- (nonnull instancetype) init NS_UNAVAILABLE;
@property (nonatomic, strong, readonly, nonnull) NSString *name;
@property (nonatomic, readonly) uint16_t index;
@property (nonatomic, readonly) uint8_t playlist;
@property (nonatomic, strong, readonly, nonnull) PTRegion *region;
@end

@interface PTMetadata : NSObject
+ (nonnull instancetype) new NS_UNAVAILABLE;
- (nonnull instancetype) init NS_UNAVAILABLE;
@property (nonatomic, strong, readonly, nullable) NSString *title;
@property (nonatomic, strong, readonly, nullable) NSString *artist;
@property (nonatomic, strong, readonly, nonnull) NSArray<NSString *> *contributors;
@property (nonatomic, strong, readonly, nullable) NSString *location;
@end

@interface PTKeySignature : NSObject
+ (nonnull instancetype) new NS_UNAVAILABLE;
+ (nonnull instancetype) keySigWithPos:(uint64_t)pos isMajor:(BOOL)isMajor isSharp:(BOOL)isSharp signs:(uint8_t)signs;
- (nonnull instancetype) init NS_UNAVAILABLE;
@property (nonatomic, readonly) uint64_t pos;
@property (nonatomic, readonly) BOOL isMajor;
@property (nonatomic, readonly) BOOL isSharp;
@property (nonatomic, readonly) uint8_t signs;
@end

@interface PTTimeSignature : NSObject
+ (nonnull instancetype) new NS_UNAVAILABLE;
+ (nonnull instancetype) timeSigWithPos:(uint64_t)pos measureNum:(uint32_t)measureNum nom:(uint8_t)nom denom:(uint8_t)denom;
- (nonnull instancetype) init NS_UNAVAILABLE;
@property (nonatomic, readonly) uint64_t pos;
@property (nonatomic, readonly) uint32_t measureNum;
@property (nonatomic, readonly) uint8_t nom;
@property (nonatomic, readonly) uint8_t denom;
@end

@interface PTTempoChange : NSObject
+ (nonnull instancetype) new NS_UNAVAILABLE;
+ (nonnull instancetype) tempoChangeWithPos:(uint64_t)pos tempo:(double)tempo beatLength:(uint64_t)beatLength;
- (nonnull instancetype) init NS_UNAVAILABLE;
@property (nonatomic, readonly) uint64_t pos;
@property (nonatomic, readonly) double tempo;
@property (nonatomic, readonly) uint64_t beatLength;
@end

@interface ProToolsFormat : NSObject
+ (nonnull instancetype) new NS_UNAVAILABLE;
+ (nullable instancetype) newWithPath:(nonnull NSString *)path error:(NSError * _Nullable * _Nullable)error;
- (nonnull instancetype) init NS_UNAVAILABLE;
- (nullable instancetype) initWithPath:(nonnull NSString *)path error:(NSError * _Nullable * _Nullable)error;
- (void) dealloc;

- (nonnull NSData *) unxoredData;
- (nonnull NSArray<PTBlock *> *) blocks;
- (nullable NSData *) metadataBase64;

- (uint8_t) version;
- (int64_t) sessionRate;
- (uint8_t) bitDepth;
- (nonnull NSArray<PTTrack *> *) tracks;
- (nonnull NSArray<PTTrack *> *) midiTracks;
- (nonnull PTMetadata *) metadata;
- (nonnull NSArray<PTKeySignature *> *) keySignatures;
- (nonnull NSArray<PTTimeSignature *> *) timeSignatures;
- (nonnull NSArray<PTTempoChange *> *) tempoChanges;
@end

#endif /* PRO_TOOLS_FORMAT_H */
