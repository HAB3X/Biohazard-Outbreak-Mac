// The mods the player adds: a folder or an archive (zip, rar, 7z) given to the launcher is copied (unpacked) into the
// launcher's own Mods folder, sorted by what's in it (an HD texture pack: PCSX2's replacement pictures, named by the texture's
// hash; a character mod: .nbd files; sound: .adx), kept per game, and switched on and off. The player's own files are only
// read. The games read the lists of what's on from their settings folders (textures.txt, mods.txt)
import Foundation

enum ModKind: String, Codable, CaseIterable, Identifiable {
    case textures, characters, sound, other
    var id: String { rawValue }
    var title: String {
        switch self {
        case .textures: return "HD textures"
        case .characters: return "Characters"
        case .sound: return "Music and sound"
        case .other: return "Other"
        }
    }
}

struct Mod: Codable, Identifiable, Equatable {
    var id: UUID
    var name: String
    var kind: ModKind
    var game: Game
    var enabled: Bool
    var folder: String // its copy in the launcher's Mods folder
    var files: Int
    var bytes: Int64
}

@MainActor
final class ModStore: ObservableObject {
    static let shared = ModStore()

    @Published var mods: [Mod] = []
    /// What an import is doing now (nil: none)
    @Published var working: String?
    @Published var problem: String?
    /// An import waiting for the player to say which game it's for
    @Published var askGame: URL?

    private var manifest: URL { Games.support.appendingPathComponent("mods.json") }
    nonisolated static var folder: URL { Games.support.appendingPathComponent("Mods") }

    init() {
        if let data = try? Data(contentsOf: manifest), let saved = try? JSONDecoder().decode([Mod].self, from: data) {
            mods = saved
        }
    }

    func mods(_ game: Game, _ kind: ModKind) -> [Mod] { mods.filter { $0.game == game && $0.kind == kind } }

    /// A folder or archive added: its game from its name when it says, else asked (askGame)
    func add(_ source: URL, for game: Game? = nil) {
        guard let game = game ?? Self.gameNamed(source.lastPathComponent) else {
            askGame = source
            return
        }
        askGame = nil
        working = "Adding \(source.lastPathComponent)…"
        Task {
            let result = await Self.importMod(source, game: game)
            working = nil
            switch result {
            case .success(let mod):
                mods.append(mod)
                save()
            case .failure(let error):
                problem = error.message
            }
        }
    }

    func toggle(_ mod: Mod) {
        guard let i = mods.firstIndex(of: mod) else { return }
        mods[i].enabled.toggle()
        save()
    }

    /// The copy moved to the Trash (the player's own files were never touched)
    func remove(_ mod: Mod) {
        try? FileManager.default.trashItem(at: URL(fileURLWithPath: mod.folder), resultingItemURL: nil)
        mods.removeAll { $0.id == mod.id }
        save()
    }

    private func save() {
        if let data = try? JSONEncoder().encode(mods) {
            try? data.write(to: manifest)
        }
        // The lists the games read: the folders switched on, one a line
        for game in Game.allCases {
            let settings = Games.settingsFolder(game)
            try? FileManager.default.createDirectory(at: settings, withIntermediateDirectories: true)
            for (file, kinds) in [("textures.txt", [ModKind.textures]), ("mods.txt", [.characters, .sound, .other])] {
                let lines = mods.filter { $0.game == game && $0.enabled && kinds.contains($0.kind) }.map(\.folder)
                let text = "# Switched on in the launcher's Mods, in this order (later ones over earlier ones)\n" +
                    lines.joined(separator: "\n") + (lines.isEmpty ? "" : "\n")
                try? text.write(to: settings.appendingPathComponent(file), atomically: true, encoding: .utf8)
            }
        }
    }

    // MARK: importing

    struct ImportError: Error { var message: String }

    static func gameNamed(_ name: String) -> Game? {
        let n = name.lowercased().replacingOccurrences(of: "_", with: " ")
        if n.contains("file 2") || n.contains("file2") || n.contains("file #2") || n.contains("slpm 656") || n.contains("65692") {
            return .file2
        }
        if n.contains("file 1") || n.contains("file1") || n.contains("file #1") || n.contains("slpm 654") || n.contains("65428") ||
            n.contains("desperate times") {
            return .file1
        }
        return nil
    }

    nonisolated static let archives: Set<String> = ["zip", "rar", "7z", "tar", "gz", "tgz"]

    nonisolated static func importMod(_ source: URL, game: Game) async -> Result<Mod, ImportError> {
        await Task.detached {
            let fm = FileManager.default
            let name = source.deletingPathExtension().lastPathComponent
            let id = UUID()
            let staging = folder.appendingPathComponent(".adding-\(id.uuidString)")
            try? fm.createDirectory(at: staging, withIntermediateDirectories: true)
            defer { try? fm.removeItem(at: staging) }
            if archives.contains(source.pathExtension.lowercased()) {
                // The system's bsdtar unpacks zip, rar (4 and 5) and 7z
                let tar = Process()
                tar.executableURL = URL(fileURLWithPath: "/usr/bin/tar")
                tar.arguments = ["-xf", source.path, "-C", staging.path]
                let err = Pipe()
                tar.standardError = err
                do { try tar.run() } catch { return .failure(ImportError(message: "\(source.lastPathComponent) couldn't be opened")) }
                let said = String(decoding: err.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
                tar.waitUntilExit()
                if tar.terminationStatus != 0 {
                    return .failure(ImportError(message: "\(source.lastPathComponent) couldn't be unpacked: \(said)"))
                }
            } else {
                do {
                    try fm.copyItem(at: source, to: staging.appendingPathComponent(source.lastPathComponent))
                } catch {
                    return .failure(ImportError(message: "\(source.lastPathComponent) couldn't be copied: \(error.localizedDescription)"))
                }
            }
            // One folder holding everything: that folder is the mod
            var root = staging
            while let items = try? fm.contentsOfDirectory(at: root, includingPropertiesForKeys: [.isDirectoryKey])
                .filter({ !$0.lastPathComponent.hasPrefix(".") && $0.lastPathComponent != "__MACOSX" }),
                items.count == 1, (try? items[0].resourceValues(forKeys: [.isDirectoryKey]).isDirectory) == true {
                root = items[0]
            }
            let (kind, files, bytes) = classify(root)
            if files == 0 {
                return .failure(ImportError(message: "\(source.lastPathComponent) has no files in it"))
            }
            let destination = folder.appendingPathComponent(game.rawValue).appendingPathComponent(kind.rawValue)
                .appendingPathComponent("\(name)-\(id.uuidString.prefix(8))")
            do {
                try fm.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
                try fm.moveItem(at: root, to: destination)
            } catch {
                return .failure(ImportError(message: "\(name) couldn't be put in the Mods folder: \(error.localizedDescription)"))
            }
            return .success(Mod(id: id, name: name, kind: kind, game: game, enabled: true, folder: destination.path,
                                files: files, bytes: bytes))
        }.value
    }

    /// What a mod is by its files: PCSX2's replacement pictures (<hash>-<hash>-<8 hex digits>.png or .dds) make an HD texture
    /// pack, .nbd files a character mod, .adx files sound
    nonisolated static func classify(_ root: URL) -> (ModKind, Int, Int64) {
        let hashName = try! NSRegularExpression(pattern: "^[0-9a-f]{6,16}-[0-9a-f]{6,16}-[0-9a-f]{8}\\.(png|dds)$",
                                                options: .caseInsensitive)
        var textures = 0, characters = 0, sound = 0, files = 0
        var bytes: Int64 = 0
        let walker = FileManager.default.enumerator(at: root, includingPropertiesForKeys: [.fileSizeKey, .isRegularFileKey])
        while let url = walker?.nextObject() as? URL {
            guard (try? url.resourceValues(forKeys: [.isRegularFileKey]).isRegularFile) == true else { continue }
            files += 1
            bytes += Int64((try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0)
            let name = url.lastPathComponent
            if hashName.firstMatch(in: name, range: NSRange(name.startIndex..., in: name)) != nil {
                textures += 1
            }
            switch url.pathExtension.lowercased() {
            case "nbd": characters += 1
            case "adx": sound += 1
            default: break
            }
        }
        let kind: ModKind = textures >= 10 ? .textures : characters > 0 ? .characters : sound > 0 ? .sound : .other
        return (kind, files, bytes)
    }
}
