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
    case MarkerList = 0x271a
}

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

    mutating func run() throws {
        let ptf = try ProToolsFormat(path: projectFile)
        for block in ptf.blocks() {
            printBlock(block, level: 0, filterType: Set(blockType))
        }
    }

    func printBlock(_ b: PTBlock, level: Int, filterType: Set<UInt16>) {
        if filterType.isEmpty || filterType.contains(b.contentType) {
            printContents(of: b, level: level)
        }

        for child in b.children {
            printBlock(child, level: level + 1, filterType: filterType)
        }
    }

    func printContents(of b: PTBlock, level: Int) {
        let prefix = String(repeating: "    ", count: level)
        let contentDesc = BlockContentType(rawValue: b.contentType).map { String(describing: $0) } ?? "Unknown"

        print("\(prefix)\(contentDesc)(0x\(String(format: "%04x", b.contentType))) at \(b.offset)")
        let byteGroup = 16
        for offset in stride(from: 0, to: b.data.count, by: byteGroup) {
            let end = min(offset + byteGroup, b.data.count)
            print(prefix, terminator: "")
            for i in offset..<end {
                print(String(format: "%02x ", b.data[i]), terminator: "")
            }
            for i in offset..<end {
                let byte = b.data[i]
                print((byte > 32 && byte < 128) ? String(format: "%c", byte) : ".", terminator: "")
            }
            print()
        }
    }
}

