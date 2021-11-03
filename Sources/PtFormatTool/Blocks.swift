import ArgumentParser
import PtFormatObjC

enum BlockContentType: UInt16 {
    case InfoProductVersion = 0x0030
    case WavSampleRateSize = 0x1001
    case WavMeta = 0x1003
    case WavList = 0x1004
    case RegionNameNumber = 0x1007
    case AudioRegionNameNumberV5 = 0x1008
    case AudioRegionListV5 = 0x100b
    case AudioRegionTrackEntry = 0x100f
    case AudioRegionToTrackEntries = 0x1011
    case AudioRegionToTrackMap = 0x1012
    case AudioTrackNameNumber = 0x1014
    case AudioTracks = 0x1015
    case PluginEntry = 0x1017
    case PluginList = 0x1018
    case IOChannelEntry = 0x1021
    case IOChannelList = 0x1022
    case InfoSampleRate = 0x1028
    case WavNames = 0x103a
    case AudioRegionToTrackSubEntryV8 = 0x104f
    case AudioRegionToTrackEntryV8 = 0x1050
    case AudioRegionToTrackEntriesV8 = 0x1052
    case AudioRegionToTrackMapV8 = 0x1054
    case MidiRegionToTrackEntry = 0x1056
    case MidiRegionToTrackEntries = 0x1057
    case MidiRegionToTrackMap = 0x1058
    case MidiEventsBlock = 0x2000
    case MidiRegionNameNumberV5 = 0x2001
    case MidiRegionsMapV5 = 0x2002
    case GeneralInfo = 0x204b
    case InfoSessionPath = 0x2067
    case Snaps = 0x2511
    case MidiTrackList = 0x2519
    case MidiTrackNameNumber = 0x251a
    case CompoundRegionElement = 0x2523
    case IORoute = 0x2602
    case IORoutingTable = 0x2603
    case CompoundRegionGroup = 0x2628
    case AudioRegionNameNumberV10 = 0x2629
    case AudioRegionListV10 = 0x262a
    case CompoundRegionMap = 0x262c
    case MidiRegionsNameNumberV10 = 0x2633
    case MidiRegionsMapV10 = 0x2634
    case MetaDataBase64 = 0x2715
    case MarkerList = 0x271a
}

typealias BlockPostProcessor = (Data) -> Data?
let blockPostProcessors: [BlockContentType: BlockPostProcessor] = [.MetaDataBase64: metaDataBase64Proc]

let kMetaDataBase64HeaderOffset = 6
let kMetaDataBase64Header = "sessionMetadataBase64"
let metaDataBase64Proc: BlockPostProcessor = { data in
    let dataLenOffset = kMetaDataBase64HeaderOffset + kMetaDataBase64Header.count
    let header = String(data: data[kMetaDataBase64HeaderOffset..<dataLenOffset], encoding: .utf8)
    if data.count < 32 || header != kMetaDataBase64Header {
        return nil
    }
    let dataLen = data.advanced(by: dataLenOffset).withUnsafeBytes { $0.load(as: UInt32.self) }
    let base64Data = data.advanced(by: dataLenOffset + 4)
        .subdata(in: 0..<Data.Index(dataLen))
        .filter { $0 != 0x0D && $0 != 0x0A }
    return Data(base64Encoded: base64Data)
}

let kDiffMinus = "--"
let kDiffPlus = "++"
let kByteGroupsBy = 16

struct Blocks: ParsableCommand {
    static var configuration = CommandConfiguration(abstract: "Prints block structure.")

    @Argument(help: "Project file path")
    var projectFile: String

    @Option(name: .shortAndLong, parsing: .singleValue, transform: { v in
        let stripped = v.hasPrefix("0x") ? String(v.dropFirst(2)) : v
        guard let cType = UInt16(stripped, radix: 16) else {
            throw ValidationError(v)
        }
        return cType
    })
    var blockType: [UInt16]

    @Flag(name: .long, help: "Do not transform known block data.")
    var noTransform: Bool = false

    @Flag(name: .shortAndLong, help: "Diff with previous version of file (looking for files named <filename>_<NN>.<ext> and picking the one with largest number in NN part), then save a new version using NN+1 if there are differences.")
    var diffAndSave: Bool = false

    mutating func run() throws {
        try diffAndSave ? diffModeRun() : normalModeRun()
    }

    func diffModeRun() throws {
        let projectFileUrl = URL(fileURLWithPath: projectFile)
        let ext = projectFileUrl.pathExtension
        let filenameBase = projectFileUrl.deletingPathExtension().lastPathComponent
        let projectDirUrl = projectFileUrl.deletingLastPathComponent()
        let regexForOlderVersionFilename = "\(filenameBase)_(\\d+)\\.\(ext)".r()
        let olderVersionFiles = try FileManager.default.contentsOfDirectory(
            at: projectDirUrl,
            includingPropertiesForKeys: [],
            options: .skipsSubdirectoryDescendants)
            .compactMap { f in
                regexForOlderVersionFilename.group(in: f.lastPathComponent, at: 1)
                    .flatMap { UInt($0, radix: 10) }
                    .map { ($0, f) }
            }
            .sorted(by: { $0.0 > $1.0 })

        let ptfCurrent = try ProToolsFormat(path: projectFile)
        var newVersion: UInt? = 1
        if let (olderVer, olderVerFile) = olderVersionFiles[safe: 0] {
            var ptfOlder = try ProToolsFormat(path: olderVerFile.path)
            let initDifferent = ptfOlder.unxoredData() != ptfCurrent.unxoredData()
            var different = initDifferent
            if !different, let (_, evenOlderVerFile) = olderVersionFiles[safe: 1] {
                ptfOlder = try ProToolsFormat(path: evenOlderVerFile.path)
                different = ptfOlder.unxoredData() != ptfCurrent.unxoredData()
            }
            if different {
                for (prevB, currB) in zipBlocksForDiff(prev: ptfOlder.blocks(), curr: ptfCurrent.blocks()) {
                    printBlock(prevB, level: 0, filterType: Set(blockType), diffWith: currB)
                }
            }
            newVersion = initDifferent ? olderVer + 1 : nil
        }

        if let newVerToSave = newVersion {
            let newVerFmt = String(format: "%02d", newVerToSave)
            let newVerUrl = projectDirUrl.appendingPathComponent("\(filenameBase)_\(newVerFmt).\(ext)")
            try FileManager.default.copyItem(at: projectFileUrl, to: newVerUrl)
        }
    }

    func normalModeRun() throws {
        let ptf = try ProToolsFormat(path: projectFile)
        for block in ptf.blocks() {
            printBlock(block, level: 0, filterType: Set(blockType))
        }
    }

    func printBlock(_ b: PTBlock, level: Int, filterType: Set<UInt16>, diffWith b2Maybe: PTBlock? = nil) {
        if filterType.isEmpty || filterType.contains(b.contentType) {
            printContents(of: b, level: level, diffWith: b2Maybe)
        }

        if let b2 = b2Maybe {
            for (childB, childB2) in zipBlocksForDiff(prev: b.children, curr: b2.children) {
                printBlock(childB, level: level + 2, filterType: filterType, diffWith: childB2)
            }
        } else {
            for child in b.children {
                printBlock(child, level: level + 1, filterType: filterType)
            }
        }
    }

    func printContents(of b: PTBlock, level: Int, diffWith b2Maybe: PTBlock? = nil) {
        let prefix = String(repeating: "    ", count: level)

        let contentDescB = description(of: b)
        let groupedB = groupBytesForPrint(of: b)

        if let b2 = b2Maybe {
            printColored(str: "\(kDiffMinus)\(contentDescB)", colorCode: 31, newLine: true)
            let contentDescB2 = description(of: b2)
            printColored(str: "\(kDiffPlus)\(contentDescB2)", colorCode: 32, newLine: true)

            let groupedB2 = groupBytesForPrint(of: b2)
            for i in 0..<max(groupedB.count, groupedB2.count) {
                printByteGroupForDiff(prev: groupedB[safe: i], curr: groupedB2[safe: i], prefix: prefix)
            }
        } else {
            print(contentDescB)
            printBytesForNormalMode(from: groupedB, prefix: prefix)
        }
    }

    func description(of b: PTBlock) -> String {
        let contentDesc = BlockContentType(rawValue: b.contentType).map { String(describing: $0) } ?? "Unknown"
        return "\(contentDesc)(0x\(String(format: "%04x", b.contentType))) at \(b.offset)"
    }

    func printBytesForNormalMode(from byteGroups: [[(String, String)]], prefix: String) {
        for group in byteGroups {
            printNonColored(byteGroup: group, prefix: prefix)
        }
    }

    func printByteGroupForDiff(prev: [(String, String)]?, curr: [(String, String)]?, prefix: String) {
        var differing: [Int]? = nil
        if let g1 = prev, let g2 = curr {
            differing = zip(g1, g2)
                .enumerated()
                .flatMap { (i, bytes) in bytes.0.0 != bytes.1.0 ? [i] : [] }
        }
        let areDifferent = differing == nil || differing.exists { !$0.isEmpty }

        if let g1 = prev {
            if areDifferent {
                printColored(byteGroup: g1, prefix: kDiffMinus + prefix, highlightBytes: differing, colorCode: 31)
            } else {
                printNonColored(byteGroup: g1, prefix: "  " + prefix)
            }
        }
        if let g2 = curr, areDifferent {
            printColored(byteGroup: g2, prefix: kDiffPlus + prefix, highlightBytes: differing, colorCode: 32)
        }
    }

    func printColored(byteGroup: [(String, String)], prefix: String, highlightBytes: [Int]? = nil, colorCode: Int) {
        printColored(str: prefix, colorCode: colorCode)
        let colorCodes = byteGroup.indices.map { (i) -> Int? in
            if let highlightB = highlightBytes {
                return highlightB.contains(i) ? colorCode : nil
            }
            return colorCode
        }
        let bytesColors = zip(byteGroup, colorCodes)
        for (b, c) in bytesColors {
            printColored(str: "\(b.0) ", colorCode: c)
        }
        for (b, c) in bytesColors {
            printColored(str: b.1, colorCode: c)
        }
        printColored(str: "", newLine: true)
    }

    func printColored(str: String, colorCode: Int? = nil, newLine: Bool = false) {
        print("\u{001B}[0;\(colorCode ?? 39)m\(str)\u{001B}[0;m", terminator: newLine ? "\n" : "")
    }

    func printNonColored(byteGroup group: [(String, String)], prefix: String) {
        print(prefix, terminator: "")
        for b in group {
            print("\(b.0) ", terminator: "")
        }
        for b in group {
            print(b.1, terminator: "")
        }
        print()
    }

    func groupBytesForPrint(of b: PTBlock) -> [[(String, String)]] {
        let data = blockData(b)
        return stride(from: 0, to: data.count, by: kByteGroupsBy).map { offset in
            let end = min(offset + kByteGroupsBy, data.count)
            return (offset..<end).map { (i) -> (String, String) in
                let byte = data[i]
                let hex = String(format: "%02x ", byte)
                let txt = (byte > 32 && byte < 128) ? String(format: "%c", byte) : "."
                return (hex, txt)
            }
        }
    }

    func blockData(_ block: PTBlock) -> Data {
        return noTransform
        ? block.data
        : BlockContentType(rawValue: block.contentType)
            .flatMap { blockPostProcessors[$0] }
            .flatMap { $0(block.data) } ?? block.data
    }

    func zipBlocksForDiff(prev: [PTBlock], curr: [PTBlock]) -> [(PTBlock, PTBlock)] {
        // some extremely naive implementation for now
        zip(prev, curr).filter { (blockPrev, blockCurr) in
            blockPrev.contentType == blockCurr.contentType
        }
    }
}

