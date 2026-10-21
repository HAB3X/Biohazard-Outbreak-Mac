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
        if discs.isEmpty {
            overlay = .discs
        }
        // A panel opened at the start ($BH_LAUNCHER_OPEN: settings, mods, discs), for pictures of it
        switch ProcessInfo.processInfo.environment["BH_LAUNCHER_OPEN"] {
        case "settings": overlay = .settings(.file1)
        case "mods": overlay = .mods(.file1)
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
        .alert("Couldn't start the game", isPresented: Binding(get: { model.problem != nil }, set: { if !$0 { model.problem = nil } })) {
            Button("OK") { model.problem = nil }
        } message: {
            Text(model.problem ?? "")
        }
    }
}
