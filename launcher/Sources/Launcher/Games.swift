// The two games as the launcher sees them: their apps inside the launcher's bundle, their programs (which check a disc, print
// and change the settings, and capture a picture: the runtime's --disc-check, --menu-json, --menu-set, --menu-bind,
// --snapshot-at) and what the player has chosen for each
import AppKit
import Foundation

enum Game: String, CaseIterable, Identifiable, Codable {
    case file1, file2

    var id: String { rawValue }
    var title: String { self == .file1 ? "Biohazard Outbreak" : "Biohazard Outbreak File 2" }
    var short: String { self == .file1 ? "File #1" : "File #2" }
    var code: String { self == .file1 ? "SLPM-65428" : "SLPM-65692" }
    var appName: String { self == .file1 ? "Biohazard Outbreak.app" : "Biohazard Outbreak File 2.app" }
    var program: String { self == .file1 ? "biohazard-file1" : "biohazard-file2" }
    /// The game time of a frame worth showing: File 1's title (the eye under the logo, PRESS START BUTTON at its faintest),
    /// File 2's opening movie's R.P.D.
    var pictureSeconds: Double { self == .file1 ? 36.4 : 57 }
    /// How much of the picture's height is kept from its top: the title's PRESS START BUTTON is left out
    var pictureKeep: Double { self == .file1 ? 0.68 : 1.0 }
}

/// What a program run printed and how it ended
struct ProgramResult {
    var status: Int32
    var output: String
    var errors: String
}

enum Games {
    /// The games' apps: in the launcher's bundle (Resources/Games), or for a development build the folder $BH_GAMES names
    static var appsFolder: URL {
        if let folder = ProcessInfo.processInfo.environment["BH_GAMES"] {
            return URL(fileURLWithPath: folder)
        }
        return Bundle.main.resourceURL!.appendingPathComponent("Games")
    }

    static func app(_ game: Game) -> URL { appsFolder.appendingPathComponent(game.appName) }

    static func program(_ game: Game) -> URL {
        app(game).appendingPathComponent("Contents/MacOS").appendingPathComponent(game.program)
    }

    static func installed(_ game: Game) -> Bool { FileManager.default.isExecutableFile(atPath: program(game).path) }

    /// The launcher's own folder: the pictures taken from the discs, the mods
    static var support: URL {
        let folder = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Biohazard Outbreak Launcher")
        try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        return folder
    }

    /// A game's program run with these arguments, off the main thread
    static func run(_ game: Game, _ arguments: [String]) async -> ProgramResult {
        await Task.detached {
            let process = Process()
            process.executableURL = program(game)
            process.arguments = arguments
            let out = Pipe(), err = Pipe()
            process.standardOutput = out
            process.standardError = err
            do {
                try process.run()
            } catch {
                return ProgramResult(status: -1, output: "", errors: "\(game.program) didn't start: \(error.localizedDescription)")
            }
            let output = out.fileHandleForReading.readDataToEndOfFile()
            let errors = err.fileHandleForReading.readDataToEndOfFile()
            process.waitUntilExit()
            return ProgramResult(status: process.terminationStatus, output: String(decoding: output, as: UTF8.self),
                                 errors: String(decoding: errors, as: UTF8.self))
        }.value
    }

    /// The disc image the game remembers and its settings folder (nil, nil: none chosen yet)
    static func remembered(_ game: Game) -> (disc: String?, settings: URL?) {
        let settings = settingsFolder(game)
        let disc = try? String(contentsOf: settings.appendingPathComponent("disc_image.txt"), encoding: .utf8)
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard let disc, !disc.isEmpty, FileManager.default.fileExists(atPath: disc) else { return (nil, settings) }
        return (disc, settings)
    }

    /// The runtime's settings folder for the game (its own default: PS2 Native Runtime/<executable>)
    static func settingsFolder(_ game: Game) -> URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("PS2 Native Runtime")
            .appendingPathComponent(game == .file1 ? "SLPM_654.28" : "SLPM_656.92")
    }

    /// The disc image checked by the game itself and remembered by it; the reason when it isn't this game's
    static func choose(_ game: Game, disc: URL) async -> String? {
        let result = await run(game, ["--disc", disc.path, "--disc-check", "--headless"])
        if result.status == 0 { return nil }
        let said = (result.errors + result.output).split(separator: "\n").map(String.init)
            .filter { !$0.isEmpty }.last ?? "it isn't \(game.title) (\(game.code))"
        return said.replacingOccurrences(of: "[ps2] ", with: "")
    }

    /// The app's --prepare: its English patch made if it's due (its steps written to `progress`), the game not started
    static func prepare(_ game: Game, progress: URL) async -> ProgramResult {
        await Task.detached {
            let process = Process()
            process.executableURL = app(game).appendingPathComponent("Contents/MacOS/Biohazard")
            process.arguments = ["--prepare"]
            process.environment = ProcessInfo.processInfo.environment.merging(["BH_PROGRESS": progress.path]) { $1 }
            let err = Pipe()
            process.standardError = err
            process.standardOutput = FileHandle.nullDevice
            do { try process.run() } catch {
                return ProgramResult(status: -1, output: "", errors: error.localizedDescription)
            }
            let errors = err.fileHandleForReading.readDataToEndOfFile()
            process.waitUntilExit()
            return ProgramResult(status: process.terminationStatus, output: "", errors: String(decoding: errors, as: UTF8.self))
        }.value
    }

    /// The game started, in English, as its own app does it (its first start makes the English patch from the disc), with its
    /// memory card in `cards` (the save profile's folder)
    static func play(_ game: Game, cards: URL) {
        let configuration = NSWorkspace.OpenConfiguration()
        configuration.createsNewApplicationInstance = false
        configuration.arguments = ["--memcards", cards.path]
        NSWorkspace.shared.openApplication(at: app(game), configuration: configuration)
    }

    // MARK: the pictures

    static func pictureFile(_ game: Game) -> URL { support.appendingPathComponent("art-\(game.rawValue).png") }

    /// A frame of the game's own (its title, its opening movie), taken from the player's disc by the game run headless, its
    /// black borders cut off. Nothing of the game ships with the launcher: the picture is made on the player's Mac
    static func capturePicture(_ game: Game, disc: String) async -> Bool {
        let work = FileManager.default.temporaryDirectory.appendingPathComponent("bh-launcher-\(game.rawValue)")
        try? FileManager.default.removeItem(at: work)
        let shots = work.appendingPathComponent("shots"), card = work.appendingPathComponent("card")
        try? FileManager.default.createDirectory(at: shots, withIntermediateDirectories: true)
        try? FileManager.default.createDirectory(at: card, withIntermediateDirectories: true)
        try? "pause_in_background = 0\n".write(to: card.appendingPathComponent("options.txt"), atomically: true, encoding: .utf8)
        let seconds = game.pictureSeconds
        _ = await run(game, ["--disc", disc, "--headless", "--run-seconds", String(Int(seconds) + 3),
                             "--snapshots", shots.path, "--snapshot-at", String(seconds),
                             "--settings", card.path, "--memcards", card.path])
        defer { try? FileManager.default.removeItem(at: work) }
        guard let file = try? FileManager.default.contentsOfDirectory(at: shots, includingPropertiesForKeys: nil).first,
              let image = NSImage(contentsOf: file), let cropped = Pictures.croppingBlackBorders(image),
              let data = Pictures.png(Pictures.resized(cropped, width: 1600, height: 1200, keep: game.pictureKeep)) else { return false }
        return (try? data.write(to: pictureFile(game))) != nil
    }
}

enum Pictures {
    /// The image without the rows and columns at its edges that are (nearly) black
    static func croppingBlackBorders(_ image: NSImage) -> NSImage? {
        guard let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return nil }
        let bitmap = NSBitmapImageRep(cgImage: cg)
        let width = bitmap.pixelsWide, height = bitmap.pixelsHigh
        func bright(_ x: Int, _ y: Int) -> Bool {
            guard let c = bitmap.colorAt(x: x, y: y) else { return false }
            return c.redComponent + c.greenComponent + c.blueComponent > 0.08
        }
        func rowLit(_ y: Int) -> Bool { stride(from: 0, to: width, by: 16).contains { bright($0, y) } }
        func columnLit(_ x: Int) -> Bool { stride(from: 0, to: height, by: 16).contains { bright(x, $0) } }
        guard let top = (0..<height).first(where: rowLit), let bottom = (0..<height).reversed().first(where: rowLit),
              let left = (0..<width).first(where: columnLit), let right = (0..<width).reversed().first(where: columnLit),
              bottom > top, right > left else { return nil }
        let rect = CGRect(x: left, y: top, width: right - left + 1, height: bottom - top + 1)
        guard let part = cg.cropping(to: rect) else { return nil }
        return NSImage(cgImage: part, size: NSSize(width: part.width, height: part.height))
    }

    /// The PS2's picture at its shape on a television (4:3): the frame's pixels aren't square
    /// (keep: the share of its height kept from the top)
    static func resized(_ image: NSImage, width: Int, height: Int, keep: Double = 1) -> NSImage {
        let kept = Int(Double(height) * keep)
        let out = NSImage(size: NSSize(width: width, height: kept))
        out.lockFocus()
        NSGraphicsContext.current?.imageInterpolation = .high
        image.draw(in: NSRect(x: 0, y: kept - height, width: width, height: height))
        out.unlockFocus()
        return out
    }

    static func png(_ image: NSImage) -> Data? {
        guard let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) else { return nil }
        return NSBitmapImageRep(cgImage: cg).representation(using: .png, properties: [:])
    }
}
