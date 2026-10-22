// The settings' Saves tab: the save profile (each its own memory card, for both games), portable saves, and the game's card
// backups with a way to put one back
import SwiftUI

struct SavesView: View {
    var game: Game
    @ObservedObject private var saves = SaveStore.shared
    @State private var newName = ""
    @State private var nameProblem: String?
    @State private var restoring: CardBackup?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 26) {
                if let problem = saves.problem {
                    Text(problem).font(.system(size: 13)).foregroundStyle(.red.opacity(0.85)).fixedSize(horizontal: false, vertical: true)
                }
                profiles
                portable
                backups
            }
            .padding(26)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .alert("Put this backup back?", isPresented: Binding(get: { restoring != nil }, set: { if !$0 { restoring = nil } })) {
            Button("Put it back") {
                if let backup = restoring { saves.restore(backup, game) }
                restoring = nil
            }
            Button("Cancel", role: .cancel) { restoring = nil }
        } message: {
            Text("\(game.short)'s saves become the ones from \(restoring.map { Self.when($0.date) } ?? ""). The saves you have now are kept as a backup.")
        }
    }

    private var profiles: some View {
        VStack(alignment: .leading, spacing: 10) {
            Section(title: "Profile", detail: "Each profile has its own saves in both games: one each for everyone who plays on this Mac.")
            HStack(spacing: 16) {
                ForEach([""] + saves.profiles, id: \.self) { name in
                    ChoiceWord(text: SaveStore.displayName(name), selected: name == saves.profile) { saves.choose(name) }
                }
            }
            HStack(spacing: 10) {
                TextField("New profile's name", text: $newName)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 220)
                    .onSubmit(add)
                QuietButton(title: "Add") { add() }
            }
            if let nameProblem {
                Text(nameProblem).font(.system(size: 12)).foregroundStyle(.red.opacity(0.85))
            }
        }
    }

    private var portable: some View {
        VStack(alignment: .leading, spacing: 10) {
            Section(title: "Portable saves",
                    detail: "Keep the saves in a folder beside the launcher (Biohazard Outbreak Saves), to carry them with it on a drive. Your saves are copied there, or back, when you switch.")
            HStack(spacing: 16) {
                ChoiceWord(text: "Off", selected: !saves.portable) { saves.setPortable(false) }
                ChoiceWord(text: "On", selected: saves.portable) { saves.setPortable(true) }
            }
        }
    }

    private var backups: some View {
        let list = saves.backups(game)
        return VStack(alignment: .leading, spacing: 10) {
            Section(title: "Backups",
                    detail: "\(game.short) backs up its saves each time it starts (the last 10). Put one back if a save went wrong.")
            if list.isEmpty {
                Text("None yet: the first is made the next time \(game.short) starts with saves on its card.")
                    .font(.system(size: 14)).foregroundStyle(Style.faint)
            }
            ForEach(list) { backup in
                HStack {
                    Text(Self.when(backup.date)).font(.system(size: 15)).foregroundStyle(Style.ink)
                    Spacer()
                    QuietButton(title: "Put back…") { restoring = backup }
                }
                .padding(.vertical, 2)
            }
            QuietButton(title: "Show the saves in Finder") {
                let folder = saves.folder(game)
                try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
                NSWorkspace.shared.activateFileViewerSelecting([folder])
            }
            .padding(.top, 6)
        }
        .frame(maxWidth: 640, alignment: .leading)
    }

    private func add() {
        nameProblem = saves.add(newName)
        if nameProblem == nil { newName = "" }
    }

    static func when(_ date: Date) -> String {
        date.formatted(date: .abbreviated, time: .shortened)
    }
}

private struct Section: View {
    var title: String
    var detail: String

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(title).font(.system(size: 19)).foregroundStyle(Style.ink)
            Text(detail).font(.system(size: 13)).foregroundStyle(Style.muted).fixedSize(horizontal: false, vertical: true)
        }
    }
}
