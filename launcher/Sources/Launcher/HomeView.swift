// The main screen: the game's own picture (taken from the player's disc) whole at the top, nothing over it, and under it one
// slim bar: each game whose disc is chosen with its Play, Settings and Mods (a player with one game sees one), a link to add
// the other's disc, and Discs
import SwiftUI

struct HomeView: View {
    @EnvironmentObject var model: LauncherModel

    private var playable: [Game] { Game.allCases.filter { model.discs[$0] != nil && Games.installed($0) } }
    private var missing: [Game] { Game.allCases.filter { model.discs[$0] == nil && Games.installed($0) } }

    var body: some View {
        VStack(spacing: 0) {
            picture
            bar
        }
        .background(Color.black)
        .ignoresSafeArea()
    }

    private var picture: some View {
        ZStack(alignment: .bottom) {
            if let picture = model.picture {
                Image(nsImage: picture)
                    .resizable()
                    .aspectRatio(contentMode: .fill)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .clipped()
            } else {
                RadialGradient(colors: [Color(red: 0.06, green: 0.1, blue: 0.2), .black], center: .center, startRadius: 40,
                               endRadius: 700)
            }
            // The picture fades into the bar
            LinearGradient(colors: [.clear, .black], startPoint: .top, endPoint: .bottom).frame(height: 70)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .animation(.easeInOut(duration: 0.6), value: model.picture)
    }

    private var bar: some View {
        HStack(spacing: 0) {
            ForEach(Array(playable.enumerated()), id: \.element) { index, game in
                if index > 0 {
                    Rectangle().fill(Style.line).frame(width: 1, height: 44).padding(.horizontal, 36)
                }
                GameBar(game: game)
            }
            if playable.isEmpty {
                Text("Choose your disc to play").font(.system(size: 17)).foregroundStyle(Style.muted)
            }
            Spacer(minLength: 24)
            ForEach(missing) { game in
                TextLink(title: "Add \(game.short)") { model.overlay = .discs }
                    .padding(.trailing, 20)
            }
            TextLink(title: "Discs") { model.overlay = .discs }
        }
        .padding(.horizontal, 44)
        .frame(height: 104)
        .frame(maxWidth: .infinity)
        .background(Color.black)
    }
}

/// A game in the bar: its name, Play (the English patch's bar while it's made), Settings and Mods
struct GameBar: View {
    @EnvironmentObject var model: LauncherModel
    var game: Game

    var body: some View {
        HStack(spacing: 22) {
            VStack(alignment: .leading, spacing: 2) {
                Text(game.short).font(.system(size: 22, weight: .semibold)).foregroundStyle(Style.ink)
                Text(game.title).font(.system(size: 11)).foregroundStyle(Style.faint)
            }
            if let preparing = model.preparing[game] {
                VStack(alignment: .leading, spacing: 5) {
                    ProgressView(value: preparing.fraction).tint(Style.gold).frame(width: 220)
                    Text(preparing.step).font(.system(size: 11)).foregroundStyle(Style.muted).lineLimit(1).frame(width: 220, alignment: .leading)
                }
            } else {
                PlayButton { model.play(game) }
            }
            IconLink(symbol: "slider.horizontal.3", help: "\(game.short) settings") { model.overlay = .settings(game) }
            IconLink(symbol: "puzzlepiece.extension", help: "\(game.short) mods") { model.overlay = .mods(game) }
        }
    }
}

struct PlayButton: View {
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            Label("Play", systemImage: "play.fill")
                .font(.system(size: 16, weight: .semibold))
                .foregroundStyle(.black)
                .padding(.horizontal, 26)
                .frame(height: 40)
                .background(Capsule().fill(Style.gold.opacity(hovering ? 1 : 0.88)))
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}

struct IconLink: View {
    var symbol: String
    var help: String
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            Image(systemName: symbol).font(.system(size: 17)).foregroundStyle(hovering ? Style.ink : Style.muted)
                .frame(width: 30, height: 30)
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
        .help(help)
    }
}

struct TextLink: View {
    var title: String
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            Text(title).font(.system(size: 14, weight: .medium)).foregroundStyle(hovering ? Style.ink : Style.muted)
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
    }
}
