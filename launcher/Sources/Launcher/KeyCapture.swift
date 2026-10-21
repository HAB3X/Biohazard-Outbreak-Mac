// The next key or mouse button pressed, by the name bindings.txt gives it (SDL's scancode names: "A", "Left Shift", "Mouse
// Right"); Escape cancels
import AppKit

enum KeyCapture {
    private static var monitor: Any?

    static func next(_ done: @escaping (String?) -> Void) {
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = NSEvent.addLocalMonitorForEvents(matching: [.keyDown, .flagsChanged, .otherMouseDown, .rightMouseDown]) { event in
            var name: String?
            switch event.type {
            case .keyDown:
                if event.keyCode == 53 { finish(nil, done); return nil } // Escape cancels
                name = keyNames[event.keyCode]
            case .flagsChanged:
                name = modifierNames[event.keyCode]
                // Only the press (the flag set), not its release
                if let flag = modifierFlags[event.keyCode], !event.modifierFlags.contains(flag) { return nil }
            case .rightMouseDown: name = "Mouse Right"
            case .otherMouseDown: name = event.buttonNumber == 2 ? "Mouse Middle" : event.buttonNumber == 3 ? "Mouse 4" : "Mouse 5"
            default: break
            }
            guard let name else { return nil }
            finish(name, done)
            return nil
        }
    }

    private static func finish(_ name: String?, _ done: (String?) -> Void) {
        if let monitor { NSEvent.removeMonitor(monitor) }
        monitor = nil
        done(name)
    }

    private static let modifierNames: [UInt16: String] = [56: "Left Shift", 60: "Right Shift", 59: "Left Ctrl", 62: "Right Ctrl",
                                                          58: "Left Alt", 61: "Right Alt", 55: "Left GUI", 54: "Right GUI"]
    private static let modifierFlags: [UInt16: NSEvent.ModifierFlags] = [56: .shift, 60: .shift, 59: .control, 62: .control,
                                                                         58: .option, 61: .option, 55: .command, 54: .command]

    // macOS's virtual key codes (ANSI) to SDL's scancode names
    private static let keyNames: [UInt16: String] = {
        var names: [UInt16: String] = [
            0: "A", 11: "B", 8: "C", 2: "D", 14: "E", 3: "F", 5: "G", 4: "H", 34: "I", 38: "J", 40: "K", 37: "L", 46: "M",
            45: "N", 31: "O", 35: "P", 12: "Q", 15: "R", 1: "S", 17: "T", 32: "U", 9: "V", 13: "W", 7: "X", 16: "Y", 6: "Z",
            29: "0", 18: "1", 19: "2", 20: "3", 21: "4", 23: "5", 22: "6", 26: "7", 28: "8", 25: "9",
            36: "Return", 48: "Tab", 49: "Space", 51: "Backspace", 117: "Delete", 115: "Home", 119: "End", 116: "PageUp",
            121: "PageDown", 126: "Up", 125: "Down", 123: "Left", 124: "Right", 27: "-", 24: "=", 33: "[", 30: "]",
            41: ";", 39: "'", 43: ",", 47: ".", 44: "/", 50: "`", 42: "\\",
            122: "F1", 120: "F2", 99: "F3", 118: "F4", 96: "F5", 97: "F6", 98: "F7", 100: "F8", 101: "F9", 109: "F10",
            103: "F11", 111: "F12",
        ]
        names[76] = "Keypad Enter"
        return names
    }()
}
