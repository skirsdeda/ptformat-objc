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
    NSBundle *resourceBundle = SWIFTPM_MODULE_BUNDLE;
    NSString *resourcePath = [resourceBundle pathForResource:path ofType:type inDirectory:@"Resources"];
    NSError *error;
    ProToolsFormat *ptFormat = [ProToolsFormat newWithPath:resourcePath error:&error];
    XCTAssertNotNil(ptFormat);
    XCTAssertNil(error);
    XCTAssertEqual([ptFormat version], ver);
    XCTAssertEqual([ptFormat sessionRate], sr);
    XCTAssertEqual([ptFormat bitDepth], bits);
}

- (void)testErrorOnInvalidPath {
    NSError *error;
    ProToolsFormat *ptFormat = [ProToolsFormat newWithPath:@"/dev/null" error:&error];
    XCTAssertNil(ptFormat);
    XCTAssertNotNil(error);
    XCTAssertEqual([error code], 1);
}

@end
