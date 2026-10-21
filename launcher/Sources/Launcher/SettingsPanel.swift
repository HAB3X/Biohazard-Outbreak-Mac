// The settings: a game's own PC menus' pages (Gameplay, Display, Sound, Controls, Camera, Cheats) as tabs across the top, the
// rows on the left (a few choices side by side, the chosen one underlined; more as ‹ value ›), what the row under the pointer
// does on the right
import SwiftUI

struct SettingsPanel: View {
    @EnvironmentObject var model: LauncherModel
    var game: Game
    @StateObject private var file1 = GameSettings(game: .file1)
    @StateObject private var file2 = GameSettings(game: .file2)
    @State private var page = "Gameplay"
    @State private var described: SettingsRow?

    private var settings: GameSettings { game == .file1 ? file1 : file2 }

    var body: some View {
        Panel {
            VStack(spacing: 0) {
                header
                Divider().overlay(Style.line)
                HStack(alignment: .top, spacing: 0) {
                    rows
                    Divider().overlay(Style.line)
                    description
                }
            }
            .frame(width: 1120, height: 640)
        }
        .task(id: game) {
            if settings.pages.isEmpty { await settings.load() }
            if !settings.pages.contains(where: { $0.title == page }) { page = settings.pages.first?.title ?? page }
        }
    }

    private var header: some View {
        HStack(spacing: 26) {
            Text("\(game.short) Settings").font(.system(size: 21, weight: .semibold)).foregroundStyle(Style.ink).fixedSize()
            Rectangle().fill(Style.line).frame(width: 1, height: 22)
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 24) {
                    ForEach(settings.pages) { p in
                        TabTitle(title: p.title, selected: p.title == page) { page = p.title; described = nil }
                    }
                }
            }
            Button { model.overlay = .none } label: {
                Image(systemName: "xmark").font(.system(size: 18, weight: .semibold)).foregroundStyle(Style.ink)
            }
            .buttonStyle(.plain)
            .keyboardShortcut(.cancelAction)
        }
        .padding(.horizontal, 26)
        .padding(.top, 20)
        .padding(.bottom, 12)
    }

    private var rows: some View {
        ScrollView {
            LazyVStack(alignment: .leading, spacing: 0) {
                if let problem = settings.problem {
                    Text(problem).font(.system(size: 13)).foregroundStyle(.red.opacity(0.85)).padding(.bottom, 10)
                }
                if !Games.installed(game) {
                    Text("\(game.title) isn't in this launcher.").foregroundStyle(Style.muted)
                }
                ForEach(settings.pages.first(where: { $0.title == page })?.rows ?? []) { row in
                    SettingRowView(row: row, page: page, settings: settings)
                        .onHover { if $0 { described = row } }
                }
            }
            .padding(.horizontal, 26)
            .padding(.vertical, 16)
        }
        .frame(width: 600)
    }

    private var description: some View {
        VStack(alignment: .leading, spacing: 12) {
            if let row = described, row.kind != "heading" {
                Text(row.label).font(.system(size: 18, weight: .semibold)).foregroundStyle(Style.ink)
                Text(row.description.isEmpty ? " " : row.description)
                    .font(.system(size: 15)).foregroundStyle(Style.muted).fixedSize(horizontal: false, vertical: true)
            } else {
                Text("Point at a setting to see what it does. These are the same settings as the game's own menu (F10 in the game); changes are saved at once.")
                    .font(.system(size: 15)).foregroundStyle(Style.faint).fixedSize(horizontal: false, vertical: true)
            }
            Spacer()
        }
        .padding(26)
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

struct SettingRowView: View {
    var row: SettingsRow
    var page: String
    @ObservedObject var settings: GameSettings
    @State private var capturing: Int?

    var body: some View {
        switch row.kind {
        case "heading":
            Text(row.label).font(.system(size: 13, weight: .semibold)).foregroundStyle(Style.section)
                .padding(.top, 18).padding(.bottom, 4)
        case "action":
            Button { Task { await settings.set(page: page, row: row.label, value: 0) } } label: {
                Text(row.label).font(.system(size: 18)).foregroundStyle(Style.gold)
            }
            .buttonStyle(.plain)
            .padding(.vertical, 10)
        case "binding":
            HStack {
                Text(row.label).font(.system(size: 16)).foregroundStyle(Style.ink)
                Spacer()
                ForEach(0..<2, id: \.self) { slot in
                    KeyChip(text: capturing == slot ? "Press a key…" : (row.keys?[slot] ?? "").isEmpty ? "–" : row.keys![slot],
                            active: capturing == slot) {
                        capturing = slot
                        KeyCapture.next { name in
                            capturing = nil
                            if let name, let action = row.action {
                                Task { await settings.bind(action: action, slot: slot, key: name) }
                            }
                        }
                    }
                }
            }
            .padding(.vertical, 6)
        default:
            VStack(alignment: .leading, spacing: 6) {
                Text(row.label).font(.system(size: 19)).foregroundStyle(Style.ink)
                if row.kind == "slider" {
                    SliderValue(row: row) { value in Task { await settings.set(page: page, row: row.label, value: value) } }
                } else if let choices = row.choices, choices.count <= 4, choices.joined().count < 40 {
                    HStack(spacing: 16) {
                        ForEach(Array(choices.enumerated()), id: \.offset) { i, choice in
                            ChoiceWord(text: choice, selected: (row.minimum ?? 0) + i == row.value) {
                                Task { await settings.set(page: page, row: row.label, value: (row.minimum ?? 0) + i) }
                            }
                        }
                    }
                } else if let choices = row.choices, let value = row.value {
                    let first = row.minimum ?? 0, last = row.maximum ?? first
                    HStack(spacing: 14) {
                        Arrow(name: "chevron.left", enabled: value > first) {
                            Task { await settings.set(page: page, row: row.label, value: value - 1) }
                        }
                        Text(value - first < choices.count && value >= first ? choices[value - first] : "\(value)")
                            .font(.system(size: 14, weight: .medium)).foregroundStyle(Style.ink).frame(minWidth: 90)
                        Arrow(name: "chevron.right", enabled: value < last) {
                            Task { await settings.set(page: page, row: row.label, value: value + 1) }
                        }
                    }
                }
            }
            .padding(.vertical, 9)
        }
    }
}

/// A choice among a few: small capitals, the chosen one white and underlined
struct ChoiceWord: View {
    var text: String
    var selected: Bool
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            VStack(spacing: 3) {
                Text(text.uppercased()).font(.system(size: 12, weight: .semibold)).tracking(0.8)
                    .foregroundStyle(selected ? Style.ink : hovering ? Style.muted : Style.faint)
                Rectangle().fill(selected ? Style.ink : .clear).frame(height: 1.5)
            }
            .fixedSize()
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}

struct Arrow: View {
    var name: String
    var enabled: Bool
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            Image(systemName: name).font(.system(size: 13, weight: .semibold))
                .foregroundStyle(enabled ? Style.ink : Style.faint)
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
    }
}

/// A slider set when it's let go (each change is the game saving it)
struct SliderValue: View {
    var row: SettingsRow
    var commit: (Int) -> Void
    @State private var value: Double = 0

    var body: some View {
        HStack(spacing: 14) {
            Text("\(Int(value))").font(.system(size: 13, weight: .medium)).foregroundStyle(Style.ink).frame(width: 34, alignment: .leading)
            Slider(value: $value, in: Double(row.minimum ?? 0)...Double(max(row.maximum ?? 1, (row.minimum ?? 0) + 1)), step: 1) { editing in
                if !editing { commit(Int(value)) }
            }
            .tint(Style.gold)
            .frame(width: 280)
        }
        .onAppear { value = Double(row.value ?? 0) }
    }
}

struct KeyChip: View {
    var text: String
    var active: Bool
    var action: () -> Void

    var body: some View {
        Button(action: action) {
            Text(text).font(.system(size: 12, weight: .semibold)).foregroundStyle(active ? .black : Style.ink)
                .frame(width: 120, height: 28)
                .background(RoundedRectangle(cornerRadius: 6).fill(active ? Style.gold : Color.white.opacity(0.07)))
                .overlay(RoundedRectangle(cornerRadius: 6).stroke(Style.line, lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}
