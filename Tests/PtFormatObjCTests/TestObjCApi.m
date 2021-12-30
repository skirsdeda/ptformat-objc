//
//  TestObjCApi.m
//  
//
//  Created by Tadas Dailyda on 2021-09-18.
//

#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>
#import "ProToolsFormat.h"

@interface TestObjCApi : XCTestCase

@end

@implementation TestObjCApi

- (void)testMetadataPts1 {
    [self metadataCheckFor:@"Damien_monos" ofType:@"pts" ver:5 sr:48000 bits:24];
}

- (void)testMetadataPts2 {
    [self metadataCheckFor:@"Fa_16_48" ofType:@"pts" ver:5 sr:48000 bits:16];
}

- (void)testMetadataPts3 {
    [self metadataCheckFor:@"forArdour" ofType:@"pts" ver:5 sr:48000 bits:24];
}

- (void)testMetadataPtf1 {
    [self metadataCheckFor:@"goodplaylists2" ofType:@"ptf" ver:8 sr:48000 bits:24];
}

- (void)testMetadataPtx1 {
    [self metadataCheckFor:@"RegionTest" ofType:@"ptx" ver:12 sr:44100 bits:24];
}

- (void)testMetadataPtx2 {
    [self metadataCheckFor:@"Untitled32" ofType:@"ptx" ver:12 sr:44100 bits:32];
}

- (void)testMetadataFields {
    ProToolsFormat *ptFormat = [self loadAndCheck:@"MetadataFields" ofType:@"ptx"];
    PTMetadata *meta = [ptFormat metadata];
    XCTAssertEqualObjects([meta title], @"Title with some UTF8 ąčęėįšųūž");
    XCTAssertEqualObjects([meta artist], @"AFKAAFKAP");
    NSArray<NSString *> *expectContributors = @[ @"Prince", @"Foo", @"Buz" ];
    XCTAssertEqualObjects([meta contributors], expectContributors);
    XCTAssertEqualObjects([meta location], @"Paisley Park");
}

- (void)metadataCheckFor:(NSString*)path ofType:(NSString*)type ver:(uint8_t)ver sr:(int64_t)sr bits:(uint8_t)bits {
    ProToolsFormat *ptFormat = [self loadAndCheck:path ofType:type];
    XCTAssertEqual([ptFormat version], ver);
    XCTAssertEqual([ptFormat sessionRate], sr);
    XCTAssertEqual([ptFormat bitDepth], bits);
    XCTAssertEqual([[ptFormat keySignatures] count], 0);
    XCTAssertEqual([[ptFormat timeSignatures] count], 0);
}

- (ProToolsFormat*)loadAndCheck:(NSString*)path ofType:(NSString*)type {
    NSError *error;
    ProToolsFormat *ptFormat = [self loadFromResource:path ofType:type error:&error];
    XCTAssertNotNil(ptFormat);
    XCTAssertNil(error);
    return ptFormat;
}

- (ProToolsFormat*)loadFromResource:(NSString*)path ofType:(NSString*)type error:(NSError**)error {
    NSBundle *resourceBundle = SWIFTPM_MODULE_BUNDLE;
    NSString *resourcePath = [resourceBundle pathForResource:path ofType:type inDirectory:@"Resources"];
    return [ProToolsFormat newWithPath:resourcePath error:error];
}

- (void)testErrorOnInvalidPath {
    NSError *error;
    ProToolsFormat *ptFormat = [ProToolsFormat newWithPath:@"/dev/null" error:&error];
    XCTAssertNil(ptFormat);
    XCTAssertNotNil(error);
    XCTAssertEqual([error code], 1);
}

- (void)testUnxored {
    NSError *error;
    ProToolsFormat *ptFormat1 = [self loadFromResource:@"Untitled32" ofType:@"ptx" error:&error];
    XCTAssertNotNil(ptFormat1);
    XCTAssertNil(error);
    ProToolsFormat *ptFormat2 = [self loadFromResource:@"Untitled32" ofType:@"ptx" error:&error];
    XCTAssertNotNil(ptFormat2);
    XCTAssertNil(error);
    XCTAssertEqualObjects([ptFormat1 unxoredData], [ptFormat2 unxoredData]);
}

- (void)testTempoTimeSigKeySig {
    NSError *error;
    ProToolsFormat *ptFormat = [self loadFromResource:@"TempoTimeKeySig" ofType:@"ptx" error:&error];
    XCTAssertNotNil(ptFormat);
    XCTAssertNil(error);

    NSArray<PTKeySignature *> *keySigsActual = [ptFormat keySignatures];
    NSArray<PTKeySignature *> *keySigsExpected = @[
        [PTKeySignature keySigWithPos: 0u isMajor: YES isSharp: YES signs: 6],
        [PTKeySignature keySigWithPos: 3840000u isMajor: YES isSharp: NO signs: 1],
        [PTKeySignature keySigWithPos: 5760000u isMajor: NO isSharp: NO signs: 5],
        [PTKeySignature keySigWithPos: 8640000u isMajor: NO isSharp: YES signs: 0],
        [PTKeySignature keySigWithPos: 11520000u isMajor: NO isSharp: YES signs: 1],
        [PTKeySignature keySigWithPos: 14400000u isMajor: NO isSharp: YES signs: 2],
        [PTKeySignature keySigWithPos: 17280000u isMajor: NO isSharp: YES signs: 3],
        [PTKeySignature keySigWithPos: 20160000u isMajor: NO isSharp: YES signs: 4],
        [PTKeySignature keySigWithPos: 23040000u isMajor: NO isSharp: YES signs: 5],
        [PTKeySignature keySigWithPos: 25920000u isMajor: NO isSharp: YES signs: 6],
        [PTKeySignature keySigWithPos: 28800000u isMajor: NO isSharp: YES signs: 7],
        [PTKeySignature keySigWithPos: 28169760000u isMajor: NO isSharp: NO signs: 1],
        [PTKeySignature keySigWithPos: 137353440000u isMajor: NO isSharp: NO signs: 4],
        [PTKeySignature keySigWithPos: 164747040000u isMajor: NO isSharp: YES signs: 5],
        [PTKeySignature keySigWithPos: 183007200000u isMajor: NO isSharp: YES signs: 6]
    ];

    XCTAssertEqualObjects(keySigsActual, keySigsExpected);

    NSArray<PTTimeSignature *> *timeSigsActual = [ptFormat timeSignatures];
    NSArray<PTTimeSignature *> *timeSigsExpected = @[
        [PTTimeSignature timeSigWithPos: 0u measureNum: 1 nom: 4 denom: 4],
        [PTTimeSignature timeSigWithPos: 3840000u measureNum: 2 nom: 2 denom: 4],
        [PTTimeSignature timeSigWithPos: 5760000u measureNum: 3 nom: 3 denom: 4],
        [PTTimeSignature timeSigWithPos: 7474080000u measureNum: 2597 nom: 3 denom: 4],
        [PTTimeSignature timeSigWithPos: 50311200000u measureNum: 17471 nom: 2 denom: 4],
        [PTTimeSignature timeSigWithPos: 191832480000u measureNum: 91180 nom: 3 denom: 8],
        [PTTimeSignature timeSigWithPos: 191833920000u measureNum: 91181 nom: 5 denom: 2],
        [PTTimeSignature timeSigWithPos: 191843520000u measureNum: 91182 nom: 6 denom: 16],
        [PTTimeSignature timeSigWithPos: 191844960000u measureNum: 91183 nom: 2 denom: 4]
    ];

    XCTAssertEqualObjects(timeSigsActual, timeSigsExpected);

    static const uint64_t SIXTEENTH = 240000u;
    static const uint64_t QUARTER = SIXTEENTH * 4;
    NSArray<PTTempoChange *> *tempoChangesActual = [ptFormat tempoChanges];
    NSArray<PTTempoChange *> *tempoChangesExpected = @[
        [PTTempoChange tempoChangeWithPos:0u tempo:200. beatLength:SIXTEENTH],
        [PTTempoChange tempoChangeWithPos:1 * SIXTEENTH tempo:51. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:2 * SIXTEENTH tempo:52. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:3 * SIXTEENTH tempo:53. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:4 * SIXTEENTH tempo:54. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:5 * SIXTEENTH tempo:55. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:6 * SIXTEENTH tempo:56. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:7 * SIXTEENTH tempo:57. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:8 * SIXTEENTH tempo:60. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:9 * SIXTEENTH tempo:61. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:10 * SIXTEENTH tempo:62. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:11 * SIXTEENTH tempo:63. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:12 * SIXTEENTH tempo:64. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:13 * SIXTEENTH tempo:65. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:14 * SIXTEENTH tempo:66. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:15 * SIXTEENTH tempo:67. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:3840000u tempo:99.9998 beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:5760000u tempo:110. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:8640000u tempo:101. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:11520000u tempo:80. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:14400000u tempo:90. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:7361280000u tempo:100. beatLength:QUARTER],
        [PTTempoChange tempoChangeWithPos:7701600000u tempo:200. beatLength:QUARTER]
    ];

    XCTAssertEqualObjects(tempoChangesActual, tempoChangesExpected);
}

@end
