// The launcher: choose the two games' discs once, then play either, change its settings, or add mods, from one window
import SwiftUI

@main
struct LauncherApp: App {
    @StateObject private var model = LauncherModel()

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
    case none, discs, settings, mods
}

@MainActor
final class LauncherModel: ObservableObject {
    @Published var discs: [Game: String] = [:]
    @Published var overlay: Overlay = .none
    @Published var picture: NSImage?
    @Published var preparingPicture = false

    init() {
        refreshDiscs()
        loadPicture()
        if discs.isEmpty {
            overlay = .discs
        }
        // A panel opened at the start ($BH_LAUNCHER_OPEN: settings, mods, discs), for pictures of it
        switch ProcessInfo.processInfo.environment["BH_LAUNCHER_OPEN"] {
        case "settings": overlay = .settings
        case "mods": overlay = .mods
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
                case .settings: SettingsPanel()
                case .mods: ModsPanel()
                case .none: EmptyView()
                }
            }
        }
        .animation(.easeOut(duration: 0.18), value: model.overlay)
        .background(Color.black)
    }
}
