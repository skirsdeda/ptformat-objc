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

- (void)setUp {
    // Put setup code here. This method is called before the invocation of each test method in the class.
    // TODO: initialize ProToolsFormat and load RegionTest.ptx file

}

- (void)tearDown {
    // Put teardown code here. This method is called after the invocation of each test method in the class.
}

- (void)testSampleRate {
    NSBundle *main = [NSBundle bundleForClass:[self class]];
    NSURL *resourceURL = [main resourceURL];
    NSString *resourcePath = [main pathForResource:@"RegionTest" ofType:@"ptx"];
    NSLog(@"Resource path: %@", resourcePath);
    ProToolsFormat *ptFormat = [ProToolsFormat new];
    [ptFormat loadFrom: @"/Users/tadasdailyda/Documents/coding/ptformat/bins/RegionTest.ptx" withSr: 44100];
    XCTAssertEqual([ptFormat sessionRate], 44100);
}

@end
