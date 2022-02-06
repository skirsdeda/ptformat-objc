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

@implementation PTWav
+ (instancetype) wavWithFilename:(nonnull NSString *)filename index:(uint16_t)index posAbsolute:(uint64_t)pos length:(uint64_t)length {
    PTWav *wav = [[PTWav alloc] init];
    wav->_filename = filename;
    wav->_index = index;
    wav->_posAbsolute = pos;
    wav->_length = length;
    return wav;
}

+ (nullable instancetype) fromWav:(PTFFormat::wav_t)wav {
    // FIXME: fix C++ code to not produce empty WAVs when region is split into audio/MIDI counterparts
    if (wav.filename.empty() && wav.index == 0 && wav.posabsolute == 0 && wav.length == 0) {
        return nil;
    }
    NSString *filename = [NSString stringWithUTF8String:wav.filename.c_str()];
    return [PTWav wavWithFilename:filename index:wav.index posAbsolute:wav.posabsolute length:wav.length];
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToWav:other];
}

- (BOOL) isEqualToWav:(nonnull PTWav *)wav {
    if (self == wav)
        return YES;
    return ([self->_filename isEqualToString:wav->_filename] && self->_index == wav->_index &&
            self->_posAbsolute == wav->_posAbsolute && self->_length == wav->_length);
}
@end

@implementation PTMidiEv
+ (instancetype) midiEvWithPos:(uint64_t)pos length:(uint64_t)length note:(uint8_t)note velocity:(uint8_t)velocity {
    PTMidiEv *midiEv = [[PTMidiEv alloc] init];
    midiEv->_pos = pos;
    midiEv->_length = length;
    midiEv->_note = note;
    midiEv->_velocity = velocity;
    return midiEv;
}

+ (instancetype) fromMidiEv:(PTFFormat::midi_ev_t)ev {
    return [PTMidiEv midiEvWithPos:ev.pos length:ev.length note:ev.note velocity:ev.velocity];
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToMidiEv:other];
}

- (BOOL) isEqualToMidiEv:(nonnull PTMidiEv *)midiEv {
    if (self == midiEv)
        return YES;
    return (self->_pos == midiEv->_pos && self->_length == midiEv->_length &&
            self->_note == midiEv->_note && self->_velocity == midiEv->_velocity);
}
@end

@implementation PTRegion
+ (instancetype) regionWithName:(nonnull NSString *)name index:(uint16_t)index isStartPosInTicks:(BOOL)isInTicks
                       startPos:(uint64_t)startPos sampleOffset:(uint64_t)sampleOffset length:(uint64_t)length
                           wave:(nullable PTWav *)wave midi:(nonnull NSArray<PTMidiEv *> *)midi {
    PTRegion *region = [[PTRegion alloc] init];
    region->_name = name;
    region->_index = index;
    region->_isStartPosInTicks = isInTicks;
    region->_startPos = startPos;
    region->_sampleOffset = sampleOffset;
    region->_length = length;
    region->_wave = wave;
    region->_midi = midi;
    return region;
}

+ (instancetype) fromRegion:(PTFFormat::region_t)r {
    NSString *name = [NSString stringWithUTF8String:r.name.c_str()];
    PTWav *maybeWave = [PTWav fromWav:r.wave];
    NSMutableArray<PTMidiEv *> *midi = [NSMutableArray arrayWithCapacity:r.midi.size()];
    for (int i = 0; i < r.midi.size(); i++) {
        [midi addObject:[PTMidiEv fromMidiEv:r.midi[i]]];
    }
    return [PTRegion regionWithName:name index:r.index isStartPosInTicks:r.is_startpos_in_ticks
                           startPos:r.startpos sampleOffset:r.sampleoffset length:r.length wave:maybeWave midi:midi];
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToRegion:other];
}

- (BOOL) isEqualToRegion:(nonnull PTRegion *)region {
    if (self == region)
        return YES;
    // FIXME: split Region (probably better called Clip) into ADT having AudioClip and MidiClip so that there are no nullable fields
    BOOL wavesEqual = (self->_wave && region->_wave) ? [self->_wave isEqualToWav:region->_wave] : self->_wave == region->_wave;
    return ([self->_name isEqualToString:region->_name] && self->_index == region->_index &&
            self->_isStartPosInTicks == region->_isStartPosInTicks && self->_startPos == region->_startPos &&
            self->_sampleOffset == region->_sampleOffset && self->_length == region->_length &&
            wavesEqual && [self->_midi isEqualToArray:region->_midi]);
}
@end

@implementation PTRegionRange
+ (instancetype) regionRangeWithStart:(uint64_t)start end:(uint64_t)end {
    PTRegionRange *range = [[PTRegionRange alloc] init];
    range->_start = start;
    range->_end = end;
    return range;
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToRegionRange:other];
}

- (BOOL) isEqualToRegionRange:(nonnull PTRegionRange *)range {
    if (self == range)
        return YES;
    return (self->_start == range->_start && self->_end == range->_end);
}
@end

@implementation PTTrack
+ (nonnull instancetype) trackWithName:(nonnull NSString *)name index:(uint16_t)index playlist:(uint8_t)playlist region:(nonnull PTRegion *)region {
    PTTrack *track = [[PTTrack alloc] init];
    track->_name = name;
    track->_index = index;
    track->_playlist = playlist;
    track->_region = region;
    return track;
}

+ (instancetype) fromTrack:(PTFFormat::track_t)t {
    NSString *name = [NSString stringWithUTF8String:t.name.c_str()];
    PTRegion *region = [PTRegion fromRegion:t.reg];
    return [PTTrack trackWithName:name index:t.index playlist:t.playlist region:region];
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToTrack:other];
}

- (BOOL) isEqualToTrack:(nonnull PTTrack *)track {
    if (self == track)
        return YES;
    return ([self->_name isEqualToString:track->_name] && self->_index == track->_index &&
            self->_playlist == track->_playlist && [self->_region isEqualToRegion:track->_region]);
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
+ (instancetype) keySigIsMajor:(BOOL)isMajor isSharp:(BOOL)isSharp signs:(uint8_t)signs {
    return [[PTKeySignature alloc] initIsMajor:isMajor isSharp:isSharp signs:signs];
}

- (instancetype) initIsMajor:(BOOL)isMajor isSharp:(BOOL)isSharp signs:(uint8_t)signs {
    self->_isMajor = isMajor;
    self->_isSharp = isSharp;
    self->_signs = signs;
    return self;
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToKeySignature:other];
}

- (BOOL) isEqualToKeySignature:(nonnull PTKeySignature *)keySig {
    if (self == keySig)
        return YES;
    return (self->_isMajor == keySig->_isMajor &&
            self->_isSharp == keySig->_isSharp && self->_signs == keySig->_signs);
}
@end

@implementation PTKeySignatureEv
+ (instancetype) keySigWithPos:(uint64_t)pos isMajor:(BOOL)isMajor isSharp:(BOOL)isSharp signs:(uint8_t)signs {
    PTKeySignatureEv *ptKeySig = [[PTKeySignatureEv alloc] init];
    ptKeySig->_pos = pos;
    return [ptKeySig initIsMajor:isMajor isSharp:isSharp signs:signs];
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToKeySignatureEv:other];
}

- (BOOL) isEqualToKeySignatureEv:(nonnull PTKeySignatureEv *)keySig {
    if (self == keySig)
        return YES;
    return (self->_pos == keySig->_pos && [super isEqualToKeySignature:keySig]);
}
@end

@implementation PTTimeSignature
+ (instancetype) timeSigWithNom:(uint8_t)nom denom:(uint8_t)denom {
    return [[PTTimeSignature alloc] initNom:nom denom:denom];
}

- (instancetype) initNom:(uint8_t)nom denom:(uint8_t)denom {
    self->_nom = nom;
    self->_denom = denom;
    return self;
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToTimeSignature:other];
}

- (BOOL) isEqualToTimeSignature:(nonnull PTTimeSignature *)timeSig {
    if (self == timeSig)
        return YES;
    return (self->_nom == timeSig->_nom && self->_denom == timeSig->_denom);
}
@end

@implementation PTTimeSignatureEv
+ (nonnull instancetype) timeSigWithPos:(uint64_t)pos measureNum:(uint32_t)measureNum nom:(uint8_t)nom denom:(uint8_t)denom {
    PTTimeSignatureEv *ptTimeSig = [[PTTimeSignatureEv alloc] init];
    ptTimeSig->_pos = pos;
    ptTimeSig->_measureNum = measureNum;
    return [ptTimeSig initNom:nom denom:denom];
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToTimeSignatureEv:other];
}

- (BOOL) isEqualToTimeSignatureEv:(nonnull PTTimeSignatureEv *)timeSig {
    if (self == timeSig)
        return YES;
    return (self->_pos == timeSig->_pos && self->_measureNum == timeSig->_measureNum &&
            [super isEqualToTimeSignature:timeSig]);
}
@end

@implementation PTTempoChange
+ (nonnull instancetype) tempoChangeWithPos:(uint64_t)pos tempo:(double)tempo beatLength:(uint64_t)beatLength {
    PTTempoChange *ptTempoChange = [[PTTempoChange alloc] init];
    ptTempoChange->_pos = pos;
    ptTempoChange->_tempo = tempo;
    ptTempoChange->_beatLength = beatLength;
    return ptTempoChange;
}

- (BOOL) isEqual:(id)other {
    if (other == self)
        return YES;
    if (!other || ![other isKindOfClass:[self class]])
        return NO;
    return [self isEqualToTempoChange:other];
}

- (BOOL) isEqualToTempoChange:(nonnull PTTempoChange *)tempoChange {
    if (self == tempoChange)
        return YES;
    return (self->_pos == tempoChange->_pos && self->_tempo == tempoChange->_tempo &&
            self->_beatLength == tempoChange->_beatLength);
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

- (nonnull NSArray<PTBlock *> *) blocks {
    return [PTBlock arrayFromVector:object->blocks() unxored:object->unxored_data()];
}

- (nonnull NSArray<PTTrack *> *) _tracksFromTracks:(std::vector<PTFFormat::track_t>)tracksSrc {
    NSMutableArray<PTTrack *> *ret = [NSMutableArray arrayWithCapacity:tracksSrc.size()];
    for (int i = 0; i < tracksSrc.size(); i++) {
        [ret addObject:[PTTrack fromTrack:tracksSrc[i]]];
    }
    return ret;
}

- (nonnull NSArray<PTTrack *> *) tracks {
    return [self _tracksFromTracks:object->tracks()];
}

- (nonnull NSArray<PTTrack *> *) midiTracks {
    return [self _tracksFromTracks:object->miditracks()];
}

- (nonnull NSArray<PTRegionRange *> *) regionRanges {
    auto rangesSrc = object->region_ranges();
    NSMutableArray<PTRegionRange *> *ranges = [NSMutableArray arrayWithCapacity:rangesSrc.size()];
    for (auto r = rangesSrc.cbegin(); r != rangesSrc.cend(); ++r) {
        [ranges addObject:[PTRegionRange regionRangeWithStart:r->startpos end:r->endpos]];
    }
    return ranges;
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

- (nonnull NSArray<PTKeySignatureEv *> *) keySignatures {
    std::vector<PTFFormat::key_signature_ev_t> keySigsSrc = object->keysignatures();
    PTKeySignatureEv *keySigs[keySigsSrc.size()];
    for (int i = 0; i < keySigsSrc.size(); i++) {
        PTFFormat::key_signature_ev_t k = keySigsSrc[i];
        keySigs[i] = [PTKeySignatureEv keySigWithPos:k.pos isMajor:k.is_major isSharp:k.is_sharp signs:k.sign_count];
    }
    return [[NSArray alloc] initWithObjects:keySigs count:keySigsSrc.size()];
}

- (nonnull NSArray<PTTimeSignatureEv *> *) timeSignatures {
    std::vector<PTFFormat::time_signature_ev_t> timeSigsSrc = object->timesignatures();
    PTTimeSignatureEv *timeSigs[timeSigsSrc.size()];
    for (int i = 0; i < timeSigsSrc.size(); i++) {
        PTFFormat::time_signature_ev_t t = timeSigsSrc[i];
        timeSigs[i] = [PTTimeSignatureEv timeSigWithPos:t.pos measureNum:t.measure_num nom:t.nominator denom:t.denominator];
    }
    return [[NSArray alloc] initWithObjects:timeSigs count:timeSigsSrc.size()];
}

- (nonnull NSArray<PTTempoChange *> *) tempoChanges {
    std::vector<PTFFormat::tempo_change_t> tempoChangesSrc = object->tempochanges();
    PTTempoChange *tempoChanges[tempoChangesSrc.size()];
    for (int i = 0; i < tempoChangesSrc.size(); i++) {
        PTFFormat::tempo_change_t t = tempoChangesSrc[i];
        tempoChanges[i] = [PTTempoChange tempoChangeWithPos:t.pos tempo:t.tempo beatLength:t.beat_len];
    }
    return [[NSArray alloc] initWithObjects:tempoChanges count:tempoChangesSrc.size()];
}

- (nonnull PTKeySignature *) mainKeySignature {
    auto [isMajor, isSharp, signs] = object->main_keysignature();
    return [PTKeySignature keySigIsMajor:isMajor isSharp:isSharp signs:signs];
}

- (nonnull PTTimeSignature *) mainTimeSignature {
    auto [nom, denom] = object->main_timesignature();
    return [PTTimeSignature timeSigWithNom:nom denom:denom];
}

- (double) mainTempo {
    return object->main_tempo();
}

@end
