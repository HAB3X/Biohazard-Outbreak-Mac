// The mods: per game, its HD texture packs, character mods, sound and others, each switched on and off; Add… (or a drop) takes
// a folder or an archive and sorts it by itself
import SwiftUI
import UniformTypeIdentifiers

struct ModsPanel: View {
    @EnvironmentObject var model: LauncherModel
    @ObservedObject private var store: ModStore
    var game: Game
    @State private var kind: ModKind = .textures
    @State private var dropping = false

    init(game: Game) {
        self.game = game
        store = ModStore.shared
    }

    var body: some View {
        Panel {
            VStack(spacing: 0) {
                header
                Divider().overlay(Style.line)
                list
                Divider().overlay(Style.line)
                footer
            }
            .frame(width: 1000, height: 600)
        }
        .onDrop(of: [.fileURL], isTargeted: $dropping) { items in
            for item in items {
                _ = item.loadObject(ofClass: URL.self) { url, _ in
                    if let url { Task { @MainActor in store.add(url) } }
                }
            }
            return true
        }
        .confirmationDialog("Which game is \(store.askGame?.lastPathComponent ?? "it") for?",
                            isPresented: Binding(get: { store.askGame != nil }, set: { if !$0 { store.askGame = nil } })) {
            ForEach(Game.allCases) { g in
                Button(g.title) { if let url = store.askGame { store.add(url, for: g) } }
            }
        }
        .alert("The mod couldn't be added", isPresented: Binding(get: { store.problem != nil }, set: { if !$0 { store.problem = nil } })) {
            Button("OK") { store.problem = nil }
        } message: {
            Text(store.problem ?? "")
        }
    }

    private var header: some View {
        HStack(spacing: 26) {
            Text("\(game.short) Mods").font(.system(size: 21, weight: .semibold)).foregroundStyle(Style.ink).fixedSize()
            Rectangle().fill(Style.line).frame(width: 1, height: 22)
            ForEach(ModKind.allCases) { k in
                TabTitle(title: k.title, selected: k == kind) { kind = k }
            }
            Spacer()
            Button { model.overlay = .none } label: {
                Image(systemName: "xmark").font(.system(size: 18, weight: .semibold)).foregroundStyle(Style.ink)
            }
            .buttonStyle(.plain)
            .keyboardShortcut(.cancelAction)
        }
        .padding(.horizontal, 26).padding(.top, 20).padding(.bottom, 12)
    }

    private var list: some View {
        let mods = store.mods(game, kind)
        return ScrollView {
            VStack(spacing: 8) {
                if mods.isEmpty {
                    VStack(spacing: 10) {
                        Image(systemName: "tray.and.arrow.down").font(.system(size: 34)).foregroundStyle(Style.faint)
                        Text("No \(kind == .textures ? "HD textures" : kind.title.lowercased()) for \(game.short) yet")
                            .font(.system(size: 17)).foregroundStyle(Style.muted)
                        Text("Drop a folder, .zip, .rar or .7z here, or choose Add…. It's sorted for you.")
                            .font(.system(size: 13)).foregroundStyle(Style.faint)
                    }
                    .frame(maxWidth: .infinity).padding(.top, 120)
                }
                ForEach(mods) { mod in
                    ModRow(mod: mod, store: store)
                }
            }
            .padding(22)
        }
        .background(dropping ? Style.gold.opacity(0.06) : .clear)
    }

    private var footer: some View {
        HStack {
            if let working = store.working {
                ProgressView().controlSize(.small)
                Text(working).font(.system(size: 13)).foregroundStyle(Style.muted)
            } else {
                Text(kind == .textures ? "HD texture packs in PCSX2's format (folders of replacement pictures)."
                                       : "Files are copied into the launcher's Mods folder; yours are left as they are.")
                    .font(.system(size: 12)).foregroundStyle(Style.faint)
            }
            Spacer()
            QuietButton(title: "Show folder") { NSWorkspace.shared.open(ModStore.folder) }
            QuietButton(title: "Add…", prominent: true) { pick() }
        }
        .padding(.horizontal, 26).padding(.vertical, 16)
    }

    private func pick() {
        let panel = NSOpenPanel()
        panel.title = "Choose mods for \(game.title)"
        panel.canChooseDirectories = true
        panel.canChooseFiles = true
        panel.allowsMultipleSelection = true
        if panel.runModal() == .OK {
            for url in panel.urls {
                store.add(url, for: ModStore.gameNamed(url.lastPathComponent) ?? game)
            }
        }
    }
}

struct ModRow: View {
    var mod: Mod
    @ObservedObject var store: ModStore

    var body: some View {
        HStack(spacing: 16) {
            VStack(alignment: .leading, spacing: 3) {
                Text(mod.name).font(.system(size: 16, weight: .medium)).foregroundStyle(Style.ink)
                Text("\(mod.files) files · \(ByteCountFormatter.string(fromByteCount: mod.bytes, countStyle: .file))")
                    .font(.system(size: 12)).foregroundStyle(Style.muted)
            }
            Spacer()
            Button { store.remove(mod) } label: {
                Image(systemName: "trash").foregroundStyle(Style.faint)
            }
            .buttonStyle(.plain)
            .help("Move this copy to the Trash")
            Toggle("", isOn: Binding(get: { mod.enabled }, set: { _ in store.toggle(mod) }))
                .toggleStyle(.switch).tint(Style.gold).labelsHidden()
        }
        .padding(.horizontal, 18).padding(.vertical, 12)
        .background(RoundedRectangle(cornerRadius: 10).fill(Color.white.opacity(0.04)))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(Style.line, lineWidth: 1))
    }
}
