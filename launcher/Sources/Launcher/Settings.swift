// A game's settings as its own PC menus have them: printed by the game (--menu-json), changed through it (--menu-set,
// --menu-bind), so the launcher shows and saves exactly what the in-game menus (F10) do
import Foundation

struct SettingsPage: Decodable, Identifiable {
    var title: String
    var rows: [SettingsRow]
    var id: String { title }
}

struct SettingsRow: Decodable, Identifiable {
    var label: String
    var description: String
    var kind: String
    var value: Int?
    var minimum: Int?
    var maximum: Int?
    var choices: [String]?
    var action: Int?
    var keys: [String]?
    var group: String?
    var id: String { "\(kind)/\(label)/\(action ?? -1)" }
}

struct SettingsMenu: Decodable {
    var title: String
    var pages: [SettingsPage]
}

@MainActor
final class GameSettings: ObservableObject {
    let game: Game
    @Published var pages: [SettingsPage] = []
    @Published var problem: String?

    init(game: Game) { self.game = game }

    func load() async {
        let result = await Games.run(game, ["--menu-json"])
        guard result.status == 0, let data = result.output.data(using: .utf8),
              let menu = try? JSONDecoder().decode(SettingsMenu.self, from: data) else {
            problem = "The settings couldn't be read: \(result.errors.isEmpty ? "no answer from the game" : result.errors)"
            return
        }
        // The Controls' key table is long: the launcher's Controls show the options and the keys (the gamepad's are the game's)
        pages = menu.pages
        problem = nil
    }

    /// A choice or a slider set (or an action done: value 0), then the pages read again (a change can show or hide rows)
    func set(page: String, row: String, value: Int) async {
        let result = await Games.run(game, ["--menu-set", page, row, String(value)])
        if result.status != 0 {
            problem = "\(row) couldn't be changed: \(result.errors.trimmingCharacters(in: .whitespacesAndNewlines))"
        }
        await load()
    }

    /// A key put in an action's slot ("" clears it)
    func bind(action: Int, slot: Int, key: String) async {
        let result = await Games.run(game, ["--menu-bind", String(action), String(slot), key])
        if result.status != 0 {
            problem = "That key couldn't be used: \(result.errors.trimmingCharacters(in: .whitespacesAndNewlines))"
        }
        await load()
    }
}
