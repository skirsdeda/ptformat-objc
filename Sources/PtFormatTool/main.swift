import ArgumentParser

struct PtFormatTool: ParsableCommand {
    static var configuration = CommandConfiguration(
        abstract: "ProTools format command line utilities.",
        subcommands: [Unxor.self, Blocks.self],
        defaultSubcommand: Unxor.self)
}

PtFormatTool.main()
