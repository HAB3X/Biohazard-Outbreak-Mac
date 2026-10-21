// The main menu: the game's own picture (taken from the player's disc) filling the window, the menu bottom right
import SwiftUI

struct HomeView: View {
    @EnvironmentObject var model: LauncherModel

    var body: some View {
        ZStack(alignment: .bottom) {
            backdrop
            HStack(alignment: .bottom) {
                status
                Spacer()
                menu
            }
            .padding(.horizontal, 56)
            .padding(.bottom, 44)
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
                        .frame(width: size.size.width, height: size.size.height)
                        .clipped()
                        .transition(.opacity)
                } else {
                    RadialGradient(colors: [Color(red: 0.06, green: 0.1, blue: 0.2), .black], center: .center, startRadius: 40,
                                   endRadius: size.size.width * 0.7)
                }
                // Dark under the menu and the status: the picture's lower part (the title's PRESS START BUTTON) falls away
                LinearGradient(stops: [.init(color: .clear, location: 0.45), .init(color: .black.opacity(0.92), location: 0.86)],
                               startPoint: .top, endPoint: .bottom)
                LinearGradient(colors: [.clear, .black.opacity(0.35)], startPoint: .leading, endPoint: .trailing)
            }
        }
        .animation(.easeInOut(duration: 0.6), value: model.picture)
    }

    private var menu: some View {
        VStack(alignment: .trailing, spacing: 6) {
            ForEach(Game.allCases) { game in
                MenuItem(title: "Play \(game.short)", enabled: model.discs[game] != nil && Games.installed(game),
                         hint: model.discs[game] == nil ? "Choose its disc first" : nil) {
                    Games.play(game)
                }
            }
            MenuItem(title: "Settings") { model.overlay = .settings }
            MenuItem(title: "Mods") { model.overlay = .mods }
            MenuItem(title: "Discs") { model.overlay = .discs }
            MenuItem(title: "Quit") { NSApp.terminate(nil) }
        }
    }

    private var status: some View {
        VStack(alignment: .leading, spacing: 4) {
            if model.preparingPicture {
                Text("Preparing the title picture from your disc…").foregroundStyle(Style.muted)
            }
            ForEach(Game.allCases) { game in
                HStack(spacing: 6) {
                    Circle().fill(model.discs[game] != nil ? Color.green.opacity(0.8) : Style.faint).frame(width: 6, height: 6)
                    Text("\(game.title) · \(game.code)")
                }
            }
            Text("Unofficial fan project, not affiliated with Capcom").padding(.top, 6)
        }
        .font(.system(size: 11))
        .foregroundStyle(Style.faint)
    }
}

/// A line of the main menu: white, gold under the pointer, sliding a little towards it
struct MenuItem: View {
    var title: String
    var enabled = true
    var hint: String?
    var action: () -> Void
    @State private var hovering = false

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 30, weight: .regular))
                .tracking(1.2)
                .foregroundStyle(!enabled ? Style.faint : hovering ? Style.gold : Style.ink)
                .shadow(color: .black.opacity(0.7), radius: 6)
                .offset(x: hovering && enabled ? -6 : 0)
                .animation(.easeOut(duration: 0.12), value: hovering)
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .onHover { hovering = $0 }
        .help(hint ?? "")
    }
}
