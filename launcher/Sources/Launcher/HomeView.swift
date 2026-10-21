// The main screen: the game's own picture (taken from the player's disc) behind, and a card for each game side by side,
// each with one big Play and that game's Settings and Mods under it. Everything a player does starts from a card
import SwiftUI

struct HomeView: View {
    @EnvironmentObject var model: LauncherModel

    var body: some View {
        ZStack {
            backdrop
            VStack(spacing: 0) {
                HStack {
                    Spacer()
                    CornerButton(symbol: "opticaldisc", title: "Discs") { model.overlay = .discs }
                }
                .padding(.top, 34)
                .padding(.horizontal, 30)
                Spacer()
                HStack(spacing: 24) {
                    ForEach(Game.allCases) { GameCard(game: $0) }
                }
                Text("Unofficial fan project, not affiliated with Capcom. Your discs are only read.")
                    .font(.system(size: 11)).foregroundStyle(Style.faint)
                    .padding(.top, 22)
                    .padding(.bottom, 22)
            }
        }
        .ignoresSafeArea()
    }

    private var backdrop: some View {
        GeometryReader { size in
            ZStack {
                if let picture = model.picture {
                    Image(nsImage: picture)
                        .resizable()
                        .aspectRatio(contentMode: .fill)
                        .frame(width: size.size.width, height: size.size.height * 0.78, alignment: .top)
                        .clipped()
                        .frame(maxHeight: .infinity, alignment: .top)
                } else {
                    RadialGradient(colors: [Color(red: 0.06, green: 0.1, blue: 0.2), .black], center: .top, startRadius: 40,
                                   endRadius: size.size.width * 0.7)
                }
                LinearGradient(stops: [.init(color: .clear, location: 0.3), .init(color: .black, location: 0.78)],
                               startPoint: .top, endPoint: .bottom)
            }
        }
        .animation(.easeInOut(duration: 0.6), value: model.picture)
    }
}

/// A game: its name and disc, Play (a bar while its English patch is made), its Settings and its Mods
struct GameCard: View {
    @EnvironmentObject var model: LauncherModel
    var game: Game

    private var ready: Bool { model.discs[game] != nil && Games.installed(game) }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            VStack(alignment: .leading, spacing: 4) {
                Text(game.short).font(.system(size: 30, weight: .semibold)).foregroundStyle(Style.ink)
                HStack(spacing: 6) {
                    Circle().fill(ready ? Color.green.opacity(0.85) : Style.faint).frame(width: 7, height: 7)
                    Text(ready ? "\(game.title) · ready" : "Disc not chosen")
                        .font(.system(size: 13)).foregroundStyle(Style.muted)
                }
            }
            if let preparing = model.preparing[game] {
                VStack(alignment: .leading, spacing: 8) {
                    ProgressView(value: preparing.fraction).tint(Style.gold)
                    Text(preparing.step).font(.system(size: 13)).foregroundStyle(Style.muted).lineLimit(1)
                }
                .frame(height: 50)
            } else if ready {
                BigButton(title: "Play") { model.play(game) }
            } else {
                BigButton(title: "Choose disc…", quiet: true) { model.overlay = .discs }
            }
            HStack(spacing: 10) {
                SmallButton(symbol: "slider.horizontal.3", title: "Settings") { model.overlay = .settings(game) }
                SmallButton(symbol: "puzzlepiece.extension", title: "Mods") { model.overlay = .mods(game) }
            }
            .disabled(!Games.installed(game))
        }
        .padding(24)
        .frame(width: 360)
        .background(RoundedRectangle(cornerRadius: 18).fill(Color.black.opacity(0.55)))
        .background(RoundedRectangle(cornerRadius: 18).fill(.ultraThinMaterial))
        .overlay(RoundedRectangle(cornerRadius: 18).stroke(Style.line, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 18))
    }
}

struct BigButton: View {
    var title: String
    var quiet = false
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 19, weight: .semibold))
                .foregroundStyle(quiet ? Style.ink : .black)
                .frame(maxWidth: .infinity, minHeight: 50)
                .background(RoundedRectangle(cornerRadius: 12)
                    .fill(quiet ? Color.white.opacity(hovering ? 0.16 : 0.09) : Style.gold.opacity(hovering ? 1 : 0.9)))
                .scaleEffect(hovering ? 1.015 : 1)
                .animation(.easeOut(duration: 0.12), value: hovering)
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}

struct SmallButton: View {
    var symbol: String
    var title: String
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            Label(title, systemImage: symbol)
                .font(.system(size: 14, weight: .medium))
                .foregroundStyle(hovering ? Style.ink : Style.muted)
                .frame(maxWidth: .infinity, minHeight: 36)
                .background(RoundedRectangle(cornerRadius: 10).fill(Color.white.opacity(hovering ? 0.1 : 0.05)))
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}

struct CornerButton: View {
    var symbol: String
    var title: String
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            Label(title, systemImage: symbol)
                .font(.system(size: 13, weight: .medium))
                .foregroundStyle(hovering ? Style.ink : Style.muted)
                .padding(.horizontal, 12).padding(.vertical, 7)
                .background(Capsule().fill(Color.black.opacity(0.45)))
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}
