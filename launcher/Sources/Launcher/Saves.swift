// Where each game's memory card is: the save profile's (the default profile's is the game's own card folder, where it saves
// when it's started without the launcher) and, with portable saves, a folder beside the launcher; and the card's backups,
// which the game makes in its card folder's backups when it starts (the newest 10). The choices are the launcher's
// (saves.json in its folder); the game is told its card folder when the launcher starts it (--memcards)
import AppKit
import Foundation

struct CardBackup: Identifiable {
    var url: URL
    var date: Date
    var id: URL { url }
}

@MainActor
final class SaveStore: ObservableObject {
    static let shared = SaveStore()

    /// The profile chosen ("" the default one) and the others made, in the order made
    @Published private(set) var profile = ""
    @Published private(set) var profiles: [String] = []
    @Published private(set) var portable = false
    @Published var problem: String?

    private struct Stored: Codable {
        var profile: String
        var profiles: [String]
        var portable: Bool
    }

    private var file: URL { Games.support.appendingPathComponent("saves.json") }

    private init() {
        guard let data = try? Data(contentsOf: file) else { return }
        do {
            let stored = try JSONDecoder().decode(Stored.self, from: data)
            profiles = stored.profiles
            profile = profiles.contains(stored.profile) ? stored.profile : ""
            portable = stored.portable
        } catch {
            problem = "Your save choices couldn't be read (\(error.localizedDescription)): the default profile is used."
        }
    }

    private func save() {
        do {
            try JSONEncoder().encode(Stored(profile: profile, profiles: profiles, portable: portable)).write(to: file)
        } catch {
            problem = "Your save choices couldn't be kept: \(error.localizedDescription)"
        }
    }

    /// The portable saves' folder: beside the launcher's app
    var portableRoot: URL {
        Bundle.main.bundleURL.deletingLastPathComponent().appendingPathComponent("Biohazard Outbreak Saves")
    }

    static func displayName(_ profile: String) -> String { profile.isEmpty ? "Default" : profile }

    /// The game's card folder for a profile, here or portable
    func folder(_ game: Game, profile: String? = nil, portable: Bool? = nil) -> URL {
        let profile = profile ?? self.profile
        if portable ?? self.portable {
            return portableRoot.appendingPathComponent(game.code).appendingPathComponent(Self.displayName(profile))
        }
        let settings = Games.settingsFolder(game)
        return profile.isEmpty ? settings.appendingPathComponent("memcards")
                               : settings.appendingPathComponent("profiles").appendingPathComponent(profile)
    }

    func choose(_ name: String) {
        guard name.isEmpty || profiles.contains(name) else { return }
        profile = name
        save()
    }

    /// A profile made and chosen: the reason when the name can't be one
    func add(_ raw: String) -> String? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if name.isEmpty { return "Give the profile a name." }
        if name.count > 24 { return "That name is too long (24 letters at most)." }
        if name.contains("/") || name.contains(":") || name.hasPrefix(".") { return "A name can't have / or : in it or start with a dot." }
        if name.caseInsensitiveCompare("Default") == .orderedSame || profiles.contains(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return "There's already a profile called \(name)."
        }
        profiles.append(name)
        profile = name
        save()
        return nil
    }

    /// Portable saves on or off: each profile's card copied to the new place where that has none yet, so the player keeps
    /// their saves either way
    func setPortable(_ on: Bool) {
        guard on != portable else { return }
        let fm = FileManager.default
        do {
            for game in Game.allCases {
                for name in [""] + profiles {
                    let from = folder(game, profile: name, portable: portable).appendingPathComponent("port0.ps2")
                    let to = folder(game, profile: name, portable: on).appendingPathComponent("port0.ps2")
                    guard fm.fileExists(atPath: from.path), !fm.fileExists(atPath: to.path) else { continue }
                    try fm.createDirectory(at: to.deletingLastPathComponent(), withIntermediateDirectories: true)
                    try fm.copyItem(at: from, to: to)
                }
            }
        } catch {
            problem = "Your saves couldn't be copied \(on ? "beside the launcher" : "back to your Library") (\(error.localizedDescription)). Nothing was changed."
            return
        }
        portable = on
        save()
    }

    /// The game's card backups, newest first
    func backups(_ game: Game) -> [CardBackup] {
        let folder = folder(game).appendingPathComponent("backups")
        let files = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: [.contentModificationDateKey])) ?? []
        return files.filter { $0.pathExtension == "ps2" }.map { url in
            CardBackup(url: url, date: (try? url.resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate) ?? .distantPast)
        }.sorted { $0.date > $1.date }
    }

    static func running(_ game: Game) -> Bool {
        NSWorkspace.shared.runningApplications.contains { $0.bundleURL?.standardizedFileURL == Games.app(game).standardizedFileURL }
    }

    /// A backup put back as the game's card; the card it replaces kept as a backup first, so the restore can be undone
    func restore(_ backup: CardBackup, _ game: Game) {
        if Self.running(game) {
            problem = "Close \(game.short) first: it has the card open."
            return
        }
        let fm = FileManager.default
        let card = folder(game).appendingPathComponent("port0.ps2")
        do {
            if fm.fileExists(atPath: card.path) {
                let formatter = DateFormatter()
                formatter.dateFormat = "yyyy-MM-dd HH-mm-ss"
                let kept = card.deletingLastPathComponent().appendingPathComponent("backups")
                    .appendingPathComponent("port0 \(formatter.string(from: Date())).ps2")
                try fm.createDirectory(at: kept.deletingLastPathComponent(), withIntermediateDirectories: true)
                if !fm.fileExists(atPath: kept.path) { try fm.copyItem(at: card, to: kept) }
                try fm.removeItem(at: card)
            }
            try fm.copyItem(at: backup.url, to: card)
            problem = nil
        } catch {
            problem = "The backup couldn't be put back: \(error.localizedDescription)"
        }
        objectWillChange.send()
    }
}
