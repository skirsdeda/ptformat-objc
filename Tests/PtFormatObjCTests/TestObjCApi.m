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

- (void)metadataCheckFor:(NSString*)path ofType:(NSString*)type ver:(uint8_t)ver sr:(int64_t)sr bits:(uint8_t)bits {
    NSError *error;
    ProToolsFormat *ptFormat = [self loadFromResource:path ofType:type error:&error];
    XCTAssertNotNil(ptFormat);
    XCTAssertNil(error);
    XCTAssertEqual([ptFormat version], ver);
    XCTAssertEqual([ptFormat sessionRate], sr);
    XCTAssertEqual([ptFormat bitDepth], bits);
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

@end
