// The discs: each game's disc image chosen once (or dropped on its row), checked by the game itself and remembered by it
import SwiftUI
import UniformTypeIdentifiers

struct DiscsPanel: View {
    @EnvironmentObject var model: LauncherModel

    var body: some View {
        Panel {
            VStack(alignment: .leading, spacing: 18) {
                Text("Your discs").font(.system(size: 26, weight: .semibold)).foregroundStyle(Style.ink)
                Text("Choose the disc image (.iso) you made from each of your Japanese discs. It's only read, never changed or copied; only where it is is remembered. You can play with one game and add the other later.")
                    .font(.system(size: 14)).foregroundStyle(Style.muted).fixedSize(horizontal: false, vertical: true)
                ForEach(Game.allCases) { game in
                    DiscRow(game: game)
                }
                HStack {
                    Text("The first time a game starts, its English patch is made from the disc (less than a minute).")
                        .font(.system(size: 12)).foregroundStyle(Style.faint)
                    Spacer()
                    QuietButton(title: "Done", prominent: true) { model.overlay = .none }
                        .disabled(model.discs.isEmpty)
                        .opacity(model.discs.isEmpty ? 0.4 : 1)
                }
            }
            .padding(32)
            .frame(width: 720)
        }
    }
}

struct DiscRow: View {
    @EnvironmentObject var model: LauncherModel
    var game: Game
    @State private var checking = false
    @State private var problem: String?
    @State private var dropping = false

    var body: some View {
        HStack(spacing: 16) {
            Image(systemName: model.discs[game] != nil ? "checkmark.circle.fill" : "opticaldisc")
                .font(.system(size: 26))
                .foregroundStyle(model.discs[game] != nil ? Color.green.opacity(0.85) : Style.muted)
                .frame(width: 34)
            VStack(alignment: .leading, spacing: 3) {
                Text(game.title).font(.system(size: 17, weight: .medium)).foregroundStyle(Style.ink)
                Text(problem ?? model.discs[game].map { URL(fileURLWithPath: $0).lastPathComponent } ?? "\(game.code) · not chosen yet")
                    .font(.system(size: 12))
                    .foregroundStyle(problem != nil ? Color.red.opacity(0.85) : Style.muted)
                    .lineLimit(2)
            }
            Spacer()
            if checking {
                ProgressView().controlSize(.small)
            } else {
                QuietButton(title: model.discs[game] != nil ? "Change…" : "Choose…") { pick() }
            }
        }
        .padding(16)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color.white.opacity(dropping ? 0.1 : 0.04)))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(dropping ? Style.gold : Style.line, lineWidth: 1))
        .onDrop(of: [.fileURL], isTargeted: $dropping) { items in
            _ = items.first?.loadObject(ofClass: URL.self) { url, _ in
                if let url { Task { @MainActor in check(url) } }
            }
            return true
        }
    }

    private func pick() {
        let panel = NSOpenPanel()
        panel.title = "Choose your \(game.title) disc image"
        panel.allowedContentTypes = [UTType(filenameExtension: "iso") ?? .data, UTType(filenameExtension: "bin") ?? .data,
                                     UTType(filenameExtension: "cue") ?? .data]
        if panel.runModal() == .OK, let url = panel.url {
            check(url)
        }
    }

    private func check(_ url: URL) {
        checking = true
        problem = nil
        Task {
            problem = await model.choose(game, disc: url)
            checking = false
        }
    }
}
