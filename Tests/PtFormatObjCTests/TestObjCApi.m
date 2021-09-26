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

- (void)testSampleRate {
    NSBundle *resourceBundle = SWIFTPM_MODULE_BUNDLE;
    NSString *resourcePath = [resourceBundle pathForResource:@"RegionTest" ofType:@"ptx" inDirectory:@"Resources"];
    ProToolsFormat *ptFormat = [ProToolsFormat newWithPath:resourcePath];
    XCTAssertEqual([ptFormat sessionRate], 44100);
}

@end
