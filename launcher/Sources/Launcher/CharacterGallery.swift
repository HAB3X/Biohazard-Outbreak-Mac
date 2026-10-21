// The Characters tab: every character mod as a face (its texture's top left corner) in a grid that scrolls, and the one
// clicked on the right: all its textures, the costume it replaces (chosen from the 26), on or off
import SwiftUI

@MainActor
final class TextureCache: ObservableObject {
    static let shared = TextureCache()
    @Published private(set) var textures: [String: [CGImage]] = [:]
    private var loading: Set<String> = []

    func textures(for file: String) -> [CGImage]? {
        if let found = textures[file] { return found }
        if !loading.contains(file) {
            loading.insert(file)
            Task.detached(priority: .utility) {
                let images = NBD.textures(URL(fileURLWithPath: file))
                await MainActor.run { TextureCache.shared.textures[file] = images }
            }
        }
        return nil
    }
}

struct CharacterGallery: View {
    var game: Game
    @ObservedObject var store: ModStore
    @ObservedObject private var cache = TextureCache.shared
    @State private var selected: UUID?

    private var mods: [Mod] { store.mods(game, .characters) }

    var body: some View {
        HStack(spacing: 0) {
            ScrollView {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 128), spacing: 14)], spacing: 14) {
                    ForEach(mods) { mod in
                        CharacterTile(mod: mod, face: face(mod), selected: mod.id == selected)
                            .onTapGesture { selected = mod.id }
                    }
                }
                .padding(20)
            }
            .frame(width: 560)
            Divider().overlay(Style.line)
            detail
        }
        .onAppear { if selected == nil { selected = mods.first?.id } }
    }

    private func face(_ mod: Mod) -> CGImage? {
        guard let nbd = mod.nbd, let first = cache.textures(for: nbd)?.first else { return nil }
        // The face is the first texture's top left corner in these models
        return first.cropping(to: CGRect(x: 0, y: 0, width: first.width / 2, height: first.height / 2))
    }

    @ViewBuilder private var detail: some View {
        if let mod = mods.first(where: { $0.id == selected }) {
            VStack(alignment: .leading, spacing: 14) {
                Text(mod.name).font(.system(size: 22, weight: .semibold)).foregroundStyle(Style.ink)
                if let nbd = mod.nbd {
                    CharacterPreview(file: nbd).frame(height: 250)
                } else {
                    Text("No model (.nbd) in this mod.").foregroundStyle(Style.muted)
                }
                VStack(alignment: .leading, spacing: 6) {
                    Text("Replaces").font(.system(size: 13, weight: .semibold)).foregroundStyle(Style.section)
                    Menu {
                        Button("Nothing (not used)") { store.setReplaces(mod, costume: nil) }
                        ForEach(["Kevin", "Mark", "Jim", "George", "David", "Alyssa", "Yoko", "Cindy"], id: \.self) { who in
                            Section(who) {
                                ForEach(Costume.all.filter { $0.character == who }) { costume in
                                    Button(costume.outfit) { store.setReplaces(mod, costume: costume.file) }
                                }
                            }
                        }
                    } label: {
                        Text(mod.replaces.flatMap(Costume.named)?.title ?? "Choose a costume…")
                            .font(.system(size: 15, weight: .medium))
                    }
                    .menuStyle(.borderlessButton)
                    .fixedSize()
                    let models = NBD.files(in: URL(fileURLWithPath: mod.folder))
                    if models.count > 1 {
                        Picker("Model", selection: Binding(get: { mod.nbd ?? "" },
                                                           set: { store.setReplaces(mod, costume: mod.replaces, nbd: $0) })) {
                            ForEach(models, id: \.path) { Text($0.lastPathComponent).tag($0.path) }
                        }
                        .fixedSize()
                    }
                }
                Toggle("On", isOn: Binding(get: { mod.enabled }, set: { _ in store.toggle(mod) }))
                    .toggleStyle(.switch).tint(Style.gold)
                Text("Drag the model to turn it.").font(.system(size: 12)).foregroundStyle(Style.faint)
                Button(role: .destructive) { store.remove(mod); selected = nil } label: {
                    Label("Remove", systemImage: "trash").font(.system(size: 13))
                }
                .buttonStyle(.plain).foregroundStyle(Style.muted)
            }
            .padding(22)
            .frame(maxWidth: .infinity, alignment: .leading)
        } else {
            Text("No character mods yet").foregroundStyle(Style.muted).frame(maxWidth: .infinity)
        }
    }
}

struct CharacterTile: View {
    var mod: Mod
    var face: CGImage?
    var selected: Bool
    @State private var hovering = false

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            ZStack {
                RoundedRectangle(cornerRadius: 10).fill(Color.white.opacity(0.05))
                if let face {
                    Image(decorative: face, scale: 1).resizable().interpolation(.high).aspectRatio(contentMode: .fill)
                } else {
                    Image(systemName: "person.crop.square").font(.system(size: 30)).foregroundStyle(Style.faint)
                }
            }
            .frame(height: 128)
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(selected ? Style.gold : hovering ? Style.muted : Style.line,
                                                               lineWidth: selected ? 2 : 1))
            .opacity(mod.enabled ? 1 : 0.45)
            Text(mod.name).font(.system(size: 13, weight: .medium)).foregroundStyle(Style.ink).lineLimit(1)
            Text(mod.replaces.flatMap(Costume.named)?.title ?? "Not used yet")
                .font(.system(size: 11)).foregroundStyle(mod.replaces == nil ? Style.faint : Style.muted).lineLimit(1)
        }
        .onHover { hovering = $0 }
    }
}
