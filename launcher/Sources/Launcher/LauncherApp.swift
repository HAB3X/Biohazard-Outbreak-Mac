// The launcher: choose the two games' discs once, then play either, change its settings, or add mods, from one window
import SwiftUI

@main
struct LauncherApp: App {
    @StateObject private var model = LauncherModel()

    init() {
        // Launcher --import [--game file1|file2] PATH...: mods added as the Mods panel's Add… adds them, without a window
        let arguments = Array(CommandLine.arguments.dropFirst())
        if arguments.first == "--import" {
            exit(ModStore.importFromCommandLine(Array(arguments.dropFirst())))
        }
    }

    var body: some Scene {
        WindowGroup("Biohazard Outbreak") {
            RootView()
                .environmentObject(model)
                .frame(minWidth: 960, minHeight: 600)
                .preferredColorScheme(.dark)
        }
        .windowStyle(.hiddenTitleBar)
        .defaultSize(width: 1280, height: 760)
    }
}

enum Overlay: Equatable {
    case none, discs
    case settings(Game)
    case mods(Game)
}

/// A game's English patch being made before it starts: how far, and the step
struct Preparing {
    var fraction: Double
    var step: String
}

@MainActor
final class LauncherModel: ObservableObject {
    @Published var discs: [Game: String] = [:]
    @Published var overlay: Overlay = .none
    @Published var picture: NSImage?
    @Published var preparingPicture = false
    @Published var preparing: [Game: Preparing] = [:]
    @Published var problem: String?

    init() {
        refreshDiscs()
        loadPicture()
        if discs.isEmpty && ProcessInfo.processInfo.environment["BH_LAUNCHER_OPEN"] == nil {
            overlay = .discs
        }
        // A panel opened at the start ($BH_LAUNCHER_OPEN: settings, mods, discs), for pictures of it
        switch ProcessInfo.processInfo.environment["BH_LAUNCHER_OPEN"] {
        case "settings": overlay = .settings(.file1)
        case "mods": overlay = .mods(.file1)
        case "characters": overlay = .mods(.file2)
        case "discs": overlay = .discs
        default: break
        }
    }

    func refreshDiscs() {
        for game in Game.allCases {
            discs[game] = Games.remembered(game).disc
        }
    }

    /// The eye of File 1's title if its disc is known, else File 2's R.P.D.; made from the disc the first time
    func loadPicture() {
        let order: [Game] = [.file1, .file2]
        if let game = order.first(where: { FileManager.default.fileExists(atPath: Games.pictureFile($0).path) && discs[$0] != nil })
            ?? order.first(where: { FileManager.default.fileExists(atPath: Games.pictureFile($0).path) }) {
            picture = NSImage(contentsOf: Games.pictureFile(game))
        }
        guard let game = order.first(where: { discs[$0] != nil }),
              !FileManager.default.fileExists(atPath: Games.pictureFile(game).path), let disc = discs[game] else { return }
        preparingPicture = true
        Task {
            if await Games.capturePicture(game, disc: disc) {
                picture = NSImage(contentsOf: Games.pictureFile(game))
            }
            preparingPicture = false
        }
    }

    /// The game's English patch made first if it's due (the app's own --prepare, its steps shown on the card), then the game
    func play(_ game: Game) {
        guard preparing[game] == nil else { return }
        preparing[game] = Preparing(fraction: 0, step: "Checking the English patch…")
        let progressFile = FileManager.default.temporaryDirectory.appendingPathComponent("bh-progress-\(game.rawValue).txt")
        try? FileManager.default.removeItem(at: progressFile)
        Task {
            let poll = Task {
                while !Task.isCancelled {
                    if let text = try? String(contentsOf: progressFile, encoding: .utf8),
                       let last = text.split(separator: "\n").last {
                        let parts = last.split(separator: "\t", maxSplits: 1)
                        if parts.count == 2, let fraction = Double(parts[0]) {
                            preparing[game] = Preparing(fraction: fraction, step: String(parts[1]))
                        }
                    }
                    try? await Task.sleep(nanoseconds: 200_000_000)
                }
            }
            let result = await Games.prepare(game, progress: progressFile)
            poll.cancel()
            preparing[game] = nil
            if result.status == 0 {
                Games.play(game)
            } else {
                problem = "The English patch for \(game.short) couldn't be made:\n\(result.errors)"
            }
        }
    }

    /// Looking for the games' discs the player hasn't chosen: disc images in Downloads, Documents and the Desktop (and the
    /// folders in them, three deep), each checked by the game itself; the first that's the game's is taken
    @Published var searching = false

    func findDiscs() {
        let missing = Game.allCases.filter { discs[$0] == nil && Games.installed($0) }
        guard !missing.isEmpty, !searching else { return }
        searching = true
        Task {
            let candidates = await Task.detached(priority: .userInitiated) { LauncherModel.discImages() }.value
            for game in missing {
                for candidate in candidates where discs[game] == nil {
                    if await Games.choose(game, disc: candidate) == nil {
                        refreshDiscs()
                    }
                }
            }
            searching = false
            loadPicture()
            if !discs.isEmpty && overlay == .discs { overlay = .none }
        }
    }

    nonisolated static func discImages() -> [URL] {
        let fm = FileManager.default
        let home = fm.homeDirectoryForCurrentUser
        var found: [URL] = []
        for folder in ["Downloads", "Documents", "Desktop"] {
            let root = home.appendingPathComponent(folder)
            guard let walker = fm.enumerator(at: root, includingPropertiesForKeys: [.fileSizeKey],
                                             options: [.skipsHiddenFiles, .skipsPackageDescendants]) else { continue }
            while let url = walker.nextObject() as? URL {
                if walker.level > 3 { walker.skipDescendants(); continue }
                let ext = url.pathExtension.lowercased()
                // A PS2 DVD image: an .iso (or a .bin) of at least 1 GB
                guard ext == "iso" || ext == "bin",
                      ((try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize) ?? 0) > 1_000_000_000 else { continue }
                found.append(url)
            }
        }
        // Names that say Biohazard or Outbreak first
        return found.sorted { a, b in
            let score = { (u: URL) in u.lastPathComponent.lowercased().contains("outbreak") || u.lastPathComponent.lowercased().contains("biohazard") ? 0 : 1 }
            return score(a) < score(b)
        }
    }

    /// Something dropped on the window: a disc image is tried as each game's disc; anything else is a mod
    func dropped(_ url: URL) {
        let ext = url.pathExtension.lowercased()
        if ext == "iso" || ext == "bin" || ext == "cue" {
            Task {
                for game in Game.allCases where Games.installed(game) {
                    if await Games.choose(game, disc: url) == nil {
                        refreshDiscs()
                        loadPicture()
                        return
                    }
                }
                problem = "\(url.lastPathComponent) isn't the Japanese Biohazard Outbreak or File 2 (SLPM-65428, SLPM-65692)."
            }
        } else {
            ModStore.shared.add(url)
        }
    }

    /// A disc chosen for a game: the reason it isn't the game's, or nil when it's taken
    func choose(_ game: Game, disc: URL) async -> String? {
        if let problem = await Games.choose(game, disc: disc) {
            return problem
        }
        refreshDiscs()
        loadPicture()
        return nil
    }
}

struct RootView: View {
    @EnvironmentObject var model: LauncherModel

    var body: some View {
        ZStack {
            HomeView()
            if model.overlay != .none {
                Color.black.opacity(0.55).ignoresSafeArea()
                    .onTapGesture { if model.overlay != .discs || !model.discs.isEmpty { model.overlay = .none } }
                switch model.overlay {
                case .discs: DiscsPanel()
                case .settings(let game): SettingsPanel(game: game)
                case .mods(let game): ModsPanel(game: game)
                case .none: EmptyView()
                }
            }
        }
        .animation(.easeOut(duration: 0.18), value: model.overlay)
        .background(Color.black)
        .onAppear {
            model.findDiscs()
            // $BH_LAUNCHER_PLAY (file1, file2): Play pressed at the start, for a test of it
            if let name = ProcessInfo.processInfo.environment["BH_LAUNCHER_PLAY"], let game = Game(rawValue: name) {
                model.play(game)
            }
        }
        // Files opened with the launcher (dropped on its Dock icon, Open With, open -a): as if dropped on the window
        .onOpenURL { model.dropped($0) }
        .onDrop(of: [.fileURL], isTargeted: nil) { items in
            for item in items {
                _ = item.loadObject(ofClass: URL.self) { url, _ in
                    if let url { Task { @MainActor in model.dropped(url) } }
                }
            }
            return true
        }
        .alert("Couldn't start the game", isPresented: Binding(get: { model.problem != nil }, set: { if !$0 { model.problem = nil } })) {
            Button("OK") { model.problem = nil }
        } message: {
            Text(model.problem ?? "")
        }
    }
}
