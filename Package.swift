// swift-tools-version:5.4
// The swift-tools-version declares the minimum version of Swift required to build this package.

import PackageDescription

let package = Package(
    name: "PtFormatObjC",
    platforms: [
        .macOS(.v10_12)
    ],
    products: [
        // Products define the executables and libraries a package produces, and make them visible to other packages.
        .library(
            name: "PtFormatObjC",
            //type: .dynamic,
            targets: ["PtFormatObjC"]),
        .executable(
            name: "PtFormatTool",
            targets: ["PtFormatTool"]),
    ],
    dependencies: [
        // Dependencies declare other packages that this package depends on.
        .package(url: "https://github.com/apple/swift-argument-parser", from: "1.0.1"),
    ],
    targets: [
        // Targets are the basic building blocks of a package. A target can define a module or a test suite.
        // Targets can depend on other targets in this package, and on products in packages this package depends on.
        .target(
            name: "PtFormatObjC",
            dependencies: [],
            cSettings: [
                .headerSearchPath(".")
            ]),
        .executableTarget(
            name: "PtFormatTool",
            dependencies: ["PtFormatObjC", .product(name: "ArgumentParser", package: "swift-argument-parser")]),
        .testTarget(
            name: "PtFormatObjCTests",
            dependencies: ["PtFormatObjC"],
            resources: [
                .copy("Resources")
            ]),
    ],
    cxxLanguageStandard: .cxx20
)
