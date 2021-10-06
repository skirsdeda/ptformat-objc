import Foundation
import ArgumentParser
import PtFormatObjC

struct Unxor: ParsableCommand {
    static var configuration = CommandConfiguration(abstract: "Prints unxored data to stdout.")

    @Argument(help: "Project file path")
    var projectFile: String

    mutating func run() throws {
        let ptf = try ProToolsFormat(path: projectFile)
        let data: Data = ptf.unxoredData()
        FileHandle.standardOutput.write(data)
    }
}
