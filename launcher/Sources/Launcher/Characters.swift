// Character mods: an .nbd file is a costume's model, the one of the 26 player costumes in the disc's romdata.afs it replaces
// (Pcc_oo.nbd: character cc, outfit oo, the same names in File 1 and File 2). Its textures are read here for the previews:
// the file starts with a table of chunks (a 4 byte tag, its offset, size and count, 16 bytes each: TEX, AMO the model, AHI its
// skeleton); TEX holds `count` textures, each a 32 bit size then the games' LZ stream (tools/bh_sld.py) of a TIM2 file
// (tools/tm2.py), whose first picture is drawn here
import AppKit
import Foundation

/// A player costume a mod can replace
struct Costume: Identifiable, Hashable {
    var file: String // romdata.afs's member, "P06_02.nbd"
    var character: String
    var outfit: String
    var id: String { file }
    var title: String { "\(character) · \(outfit)" }

    static let all: [Costume] = {
        let list: [(String, [String])] = [
            ("Kevin", ["R.P.D. uniform", "Ending (cowboy)", "Casual"]),
            ("Mark", ["Security uniform", "Ending (polo and shorts)", "Army dress greens"]),
            ("Jim", ["Subway uniform", "Ending", "Bicycle messenger"]),
            ("George", ["Suit and tie", "Volunteer Corps", "Hospital scrubs"]),
            ("David", ["Plumber", "Casual", "Bare-chested"]),
            ("Alyssa", ["Reporter", "Workout", "Red slip", "Dojo", "Marathon"]),
            ("Yoko", ["Student", "Green dress", "Gym clothes", "Chinese suit", "Swimsuit"]),
            ("Cindy", ["Waitress", "Ending", "Bunny suit", "Leathers", "Bikini"]),
        ]
        return list.enumerated().flatMap { c, entry in
            entry.1.enumerated().map { o, outfit in
                Costume(file: String(format: "P%02d_%02d.nbd", c, o), character: entry.0, outfit: outfit)
            }
        }
    }()

    static func named(_ file: String) -> Costume? { all.first { $0.file.caseInsensitiveCompare(file) == .orderedSame } }

    /// The costume a mod says it replaces: an .nbd named as one, or else its character's first outfit when its name says
    /// whose it is (nil: the player chooses)
    static func guess(modName: String, nbd: URL) -> Costume? {
        if let exact = named(nbd.lastPathComponent) { return exact }
        let name = modName.lowercased()
        for character in ["Kevin", "Mark", "Jim", "George", "David", "Alyssa", "Yoko", "Cindy"] where name.contains(character.lowercased()) {
            return all.first { $0.character == character }
        }
        return nil
    }
}

enum NBD {
    /// The model files in a mod's folder
    static func files(in folder: URL) -> [URL] {
        let walker = FileManager.default.enumerator(at: folder, includingPropertiesForKeys: nil)
        var out: [URL] = []
        while let url = walker?.nextObject() as? URL {
            if url.pathExtension.lowercased() == "nbd" { out.append(url) }
        }
        return out.sorted { $0.lastPathComponent < $1.lastPathComponent }
    }

    /// The textures of an .nbd (empty when it can't be read)
    static func textures(_ file: URL) -> [CGImage] {
        guard let data = try? Data(contentsOf: file), data.count > 0x80, data.prefix(3) == Data("TEX".utf8) else { return [] }
        let bytes = [UInt8](data)
        let offset = Int(u32(bytes, 4)), count = Int(u32(bytes, 12))
        var out: [CGImage] = []
        var at = offset
        for _ in 0..<min(count, 16) {
            guard at + 4 <= bytes.count else { break }
            let size = Int(u32(bytes, at))
            guard size > 0, at + 4 + size <= bytes.count, let tim2 = SLD.unpack(Array(bytes[(at + 4)..<(at + 4 + size)])),
                  let image = TIM2.firstPicture(tim2) else { break }
            out.append(image)
            at += 4 + size
        }
        return out
    }

    static func u32(_ b: [UInt8], _ at: Int) -> UInt32 {
        UInt32(b[at]) | UInt32(b[at + 1]) << 8 | UInt32(b[at + 2]) << 16 | UInt32(b[at + 3]) << 24
    }
}

/// The games' LZ stream of 16 bit words (tools/bh_sld.py): a control word whose bits (most significant first) say for each of
/// the next 16 items a literal word (0) or a token (1): count = T >> 11 (0: the next word), offset = T & 0x7FF; offset 0 is a
/// run of zeros, else a copy from that many words back; a count of 0 with offset 0 ends it
enum SLD {
    static func unpack(_ bytes: [UInt8]) -> [UInt8]? {
        var src = [UInt16](repeating: 0, count: bytes.count / 2)
        for i in 0..<src.count { src[i] = UInt16(bytes[2 * i]) | UInt16(bytes[2 * i + 1]) << 8 }
        var out: [UInt16] = []
        out.reserveCapacity(src.count * 2)
        var pos = 0
        while pos < src.count {
            let control = src[pos]
            pos += 1
            for bit in stride(from: 15, through: 0, by: -1) {
                guard pos < src.count else { return nil }
                if (control >> UInt16(bit)) & 1 == 0 {
                    out.append(src[pos])
                    pos += 1
                    continue
                }
                let token = src[pos]
                pos += 1
                var count = Int(token >> 11)
                let offset = Int(token & 0x7FF)
                if count == 0 {
                    guard pos < src.count else { return nil }
                    count = Int(src[pos])
                    pos += 1
                    if count == 0 && offset == 0 {
                        return out.flatMap { [UInt8($0 & 0xFF), UInt8($0 >> 8)] }
                    }
                }
                if offset == 0 {
                    out.append(contentsOf: repeatElement(0, count: count))
                } else {
                    guard offset <= out.count else { return nil }
                    let start = out.count - offset
                    for i in 0..<count { out.append(out[start + i]) }
                }
            }
        }
        return nil
    }
}

/// A TIM2 file's first picture as RGBA (tools/tm2.py): 4 and 8 bit indices into a 16, 24 or 32 bit palette (256 colour ones in
/// the PS2's CSM1 order), or 16 or 32 bit colours; the PS2's alpha (0-128) made 0-255
enum TIM2 {
    static func firstPicture(_ d: [UInt8]) -> CGImage? {
        guard d.count > 0x40, d[0] == 0x54, d[1] == 0x49, d[2] == 0x4D, d[3] == 0x32 else { return nil }
        let start = d[5] == 1 ? 0x80 : 0x10
        func u16(_ at: Int) -> Int { Int(d[at]) | Int(d[at + 1]) << 8 }
        func u32(_ at: Int) -> Int { Int(NBD.u32(d, at)) }
        let clutSize = u32(start + 4), imageSize = u32(start + 8), headerSize = u16(start + 12)
        let clutType = Int(d[start + 18]), imageType = Int(d[start + 19])
        let width = u16(start + 20), height = u16(start + 22)
        let imageAt = start + headerSize, clutAt = imageAt + imageSize
        guard width > 0, height > 0, clutAt + clutSize <= d.count else { return nil }
        func colour(_ at: Int, _ kind: Int) -> UInt32 {
            switch kind {
            case 1:
                let v = Int(d[at]) | Int(d[at + 1]) << 8
                return pack((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3, v & 0x8000 != 0 ? 255 : 0)
            case 2: return pack(Int(d[at]), Int(d[at + 1]), Int(d[at + 2]), 255)
            default: return pack(Int(d[at]), Int(d[at + 1]), Int(d[at + 2]), min(255, Int(d[at + 3]) * 2))
            }
        }
        var palette: [UInt32] = []
        if imageType == 4 || imageType == 5 {
            let kind = clutType & 0x3F, step = kind == 1 ? 2 : kind == 2 ? 3 : 4
            palette = (0..<(clutSize / step)).map { colour(clutAt + $0 * step, kind) }
            if imageType == 5 && clutType & 0x80 == 0 && palette.count == 256 {
                palette = (0..<256).map { palette[($0 & ~0x18) | (($0 & 0x08) << 1) | (($0 & 0x10) >> 1)] }
            }
        }
        var pixels = [UInt32](repeating: 0, count: width * height)
        for i in 0..<(width * height) {
            switch imageType {
            case 4:
                let b = Int(d[imageAt + i / 2]), index = i & 1 == 0 ? b & 15 : b >> 4
                pixels[i] = index < palette.count ? palette[index] : 0
            case 5:
                let index = Int(d[imageAt + i])
                pixels[i] = index < palette.count ? palette[index] : 0
            case 1: pixels[i] = colour(imageAt + i * 2, 1)
            case 3: pixels[i] = colour(imageAt + i * 4, 3)
            default: return nil
            }
        }
        let data = pixels.withUnsafeBufferPointer { Data(buffer: $0) } as CFData
        guard let provider = CGDataProvider(data: data) else { return nil }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                       space: CGColorSpaceCreateDeviceRGB(),
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                       provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
    }

    /// R, G, B, A in memory order (premultiplied, as Core Graphics draws it)
    static func pack(_ r: Int, _ g: Int, _ b: Int, _ a: Int) -> UInt32 {
        let pr = r * a / 255, pg = g * a / 255, pb = b * a / 255
        return UInt32(pr) | UInt32(pg) << 8 | UInt32(pb) << 16 | UInt32(a) << 24
    }
}
