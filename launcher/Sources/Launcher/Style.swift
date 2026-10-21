// The launcher's look, after RaccoonRecomp's Resident Evil Outbreak Recompiled launcher (its main menu over a picture, the
// menu bottom right with the chosen item gold, its dark panels with tabs across the top): fewer things on screen, the type
// larger and quieter
import SwiftUI

enum Style {
    static let gold = Color(red: 0.96, green: 0.68, blue: 0.24)
    static let ink = Color.white.opacity(0.92)
    static let muted = Color.white.opacity(0.5)
    static let faint = Color.white.opacity(0.28)
    static let section = Color(red: 0.62, green: 0.68, blue: 0.95)
    static let panel = Color(red: 0.05, green: 0.05, blue: 0.06).opacity(0.94)
    static let line = Color.white.opacity(0.09)
}

/// The dark panel the settings, the mods and the discs are shown in
struct Panel<Content: View>: View {
    @ViewBuilder var content: Content

    var body: some View {
        content
            .background(RoundedRectangle(cornerRadius: 14).fill(Style.panel))
            .overlay(RoundedRectangle(cornerRadius: 14).stroke(Style.line, lineWidth: 1))
            .shadow(color: .black.opacity(0.6), radius: 40, y: 20)
    }
}

/// A tab's title: gold and underlined when it's the one shown
struct TabTitle: View {
    var title: String
    var selected: Bool
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            VStack(spacing: 6) {
                Text(title)
                    .font(.system(size: 19, weight: .semibold))
                    .foregroundStyle(selected ? Style.gold : hovering ? Style.ink : Style.muted)
                Rectangle().fill(selected ? Style.gold : .clear).frame(height: 2)
            }
            .fixedSize()
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}

/// A small button with an outline (Add…, Choose…, Continue)
struct QuietButton: View {
    var title: String
    var prominent = false
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 15, weight: .medium))
                .foregroundStyle(prominent ? Color.black : Style.ink)
                .padding(.horizontal, 18)
                .padding(.vertical, 9)
                .background(RoundedRectangle(cornerRadius: 8).fill(prominent ? Style.gold.opacity(hovering ? 1 : 0.9)
                                                                             : Color.white.opacity(hovering ? 0.12 : 0.06)))
                .overlay(RoundedRectangle(cornerRadius: 8).stroke(prominent ? .clear : Style.line, lineWidth: 1))
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}
