// A character mod's model drawn as the game shows it, for the preview: the .nbd's AMO chunk (Characters.swift has the
// file's table) is a tree of nodes, each 12 bytes [flags, count, size (header included)] then its contents. A node whose
// flags' bits 16-23 are 0 holds `count` child nodes; otherwise it's an array of that type. Worked out from the files (each
// field checked against counts that must agree; reo's amo_parser.h names the arrays):
//
//   kind 1 (the model): a type 2 descriptor, a kind 2 node of meshes, kind 9 (materials), kind 10
//   kind 4 (a mesh): a kind 5 node holding its triangle strips, then type 5 (the mesh's materials: indices into the model's),
//     type 6 (a material for each strip: an index into type 5's), 7 (positions, x y z floats, standing as in the game),
//     8 (normals), 10 (u v floats), 11 and 12 (bone weights), 16
//   the strips: type 3 and type 4 arrays, each strips of [n, then n vertex indices]; type 6 counts both, type 3's first
//   type 12, a vertex's bones: n, then n times (bone, its weight in percent as a float); the bone is an index into type 16,
//   the mesh's list of the skeleton's bones
//
// A model's material i is drawn with the .nbd's texture i. The AHI chunk is the skeleton: a 12 byte header (count: the
// bones), then a node per bone (flagged 0x4000000n, after a 16 byte record; 12 bytes, then 256: index, parent, first child, next sibling (-1 none), scale x y z w,
// rotation x y z w, offset from the parent x y z w, ...); the model stands in the bones' rest pose, its arms out
import Foundation
import SceneKit

struct CharacterMesh {
    var weights: [[(bone: Int, weight: Float)]] = []
    var positions: [SCNVector3] = []
    var normals: [SCNVector3] = []
    var uvs: [CGPoint] = []
    var strips: [[UInt32]] = []
    var stripMaterial: [Int] = []
}

enum CharacterModel {
    /// The meshes of an .nbd's AMO chunk (empty when it can't be read)
    static func meshes(_ file: URL) -> [CharacterMesh] {
        guard let data = try? Data(contentsOf: file), data.count > 0x30 else { return [] }
        let b = [UInt8](data)
        // The chunk table: TEX, AMO, AHI, 16 bytes each
        guard let row = stride(from: 0, to: 0x80, by: 16).first(where: { b[$0] == 0x41 && b[$0 + 1] == 0x4D && b[$0 + 2] == 0x4F }) else {
            return []
        }
        let start = Int(NBD.u32(b, row + 4)), size = Int(NBD.u32(b, row + 8))
        guard start + size <= b.count, size > 12 else { return [] }
        var meshes: [CharacterMesh] = []
        walk(b, start, start + size) { kind, at, end in
            if kind == 4, let mesh = mesh(b, at, end) { meshes.append(mesh) }
        }
        return meshes
    }

    private static func u32(_ b: [UInt8], _ at: Int) -> Int { Int(NBD.u32(b, at)) }

    private static func f32(_ b: [UInt8], _ at: Int) -> Float { Float(bitPattern: NBD.u32(b, at)) }

    /// Each node under [at, end): `visit(kind, its start, its end)`, for containers at any depth
    private static func walk(_ b: [UInt8], _ at: Int, _ end: Int, _ visit: (Int, Int, Int) -> Void) {
        var pos = at
        while pos + 12 <= end {
            let flags = u32(b, pos), count = u32(b, pos + 4), size = u32(b, pos + 8)
            guard size >= 12, pos + size <= end else { return }
            if (flags >> 16) & 0xFF == 0 && count > 0 {
                visit(flags & 0xFFFF, pos, pos + size)
                walk(b, pos + 12, pos + size, visit)
            }
            pos += size
        }
    }

    /// The arrays directly and one level under a mesh node, by type
    private static func arrays(_ b: [UInt8], _ at: Int, _ end: Int) -> [Int: [(start: Int, count: Int, bytes: Int)]] {
        var out: [Int: [(Int, Int, Int)]] = [:]
        func scan(_ from: Int, _ to: Int, _ depth: Int) {
            var pos = from
            while pos + 12 <= to {
                let flags = u32(b, pos), count = u32(b, pos + 4), size = u32(b, pos + 8)
                guard size >= 12, pos + size <= to else { return }
                let type = (flags >> 16) & 0xFF
                if type == 0 {
                    if depth == 0 { scan(pos + 12, pos + size, 1) }
                } else {
                    out[type, default: []].append((pos + 12, count, size - 12))
                }
                pos += size
            }
        }
        scan(at + 12, end, 0)
        return out
    }

    private static func mesh(_ b: [UInt8], _ at: Int, _ end: Int) -> CharacterMesh? {
        let a = arrays(b, at, end)
        guard let positions = a[7]?.first, let uvs = a[10]?.first else { return nil }
        var mesh = CharacterMesh()
        for i in 0..<positions.count {
            let p = positions.start + i * 12
            mesh.positions.append(SCNVector3(f32(b, p), f32(b, p + 4), f32(b, p + 8)))
        }
        if let normals = a[8]?.first, normals.count == positions.count {
            for i in 0..<normals.count {
                let p = normals.start + i * 12
                mesh.normals.append(SCNVector3(f32(b, p), f32(b, p + 4), f32(b, p + 8)))
            }
        }
        for i in 0..<min(uvs.count, positions.count) {
            let p = uvs.start + i * 8
            mesh.uvs.append(CGPoint(x: Double(f32(b, p)), y: Double(f32(b, p + 4))))
        }
        // A vertex's bones are numbers in the mesh's own list (type 16), which names the skeleton's
        let palette = (a[16]?.first).map { m in (0..<m.count).map { u32(b, m.start + $0 * 4) } } ?? []
        if let bones = a[12]?.first {
            var pos = bones.start
            let stop = bones.start + bones.bytes
            for _ in 0..<bones.count {
                guard pos + 4 <= stop else { break }
                let n = u32(b, pos)
                guard n <= 8, pos + 4 + n * 8 <= stop else { break }
                mesh.weights.append((0..<n).map { k in
                    let local = u32(b, pos + 4 + k * 8)
                    return (local < palette.count ? palette[local] : local, f32(b, pos + 8 + k * 8) / 100)
                })
                pos += 4 + n * 8
            }
        }
        for type in [3, 4] {
            for list in a[type] ?? [] {
                var pos = list.start
                let stop = list.start + list.bytes
                for _ in 0..<list.count {
                    guard pos + 4 <= stop else { break }
                    let n = u32(b, pos)
                    guard n >= 3, pos + 4 + n * 4 <= stop else { pos += 4 + max(0, n) * 4; continue }
                    mesh.strips.append((0..<n).map { UInt32(u32(b, pos + 4 + $0 * 4)) })
                    pos += 4 + n * 4
                }
            }
        }
        let meshMaterials = (a[5]?.first).map { m in (0..<m.count).map { u32(b, m.start + $0 * 4) } } ?? [0]
        let perStrip = (a[6]?.first).map { m in (0..<m.count).map { u32(b, m.start + $0 * 4) } } ?? []
        mesh.stripMaterial = mesh.strips.indices.map { i in
            let local = i < perStrip.count ? perStrip[i] : 0
            return local < meshMaterials.count ? meshMaterials[local] : 0
        }
        return mesh.strips.isEmpty ? nil : mesh
    }

    struct Bone {
        var parent: Int
        var offset: SCNVector3
    }

    /// The skeleton of an .nbd (empty when it can't be read)
    static func bones(_ file: URL) -> [Bone] {
        guard let data = try? Data(contentsOf: file), data.count > 0x30 else { return [] }
        let b = [UInt8](data)
        guard let row = stride(from: 0, to: 0x80, by: 16).first(where: { b[$0] == 0x41 && b[$0 + 1] == 0x48 && b[$0 + 2] == 0x49 }) else {
            return []
        }
        let start = Int(NBD.u32(b, row + 4)), size = Int(NBD.u32(b, row + 8))
        guard start + 12 <= b.count, start + size <= b.count else { return [] }
        // The nodes after the chunk's header: the bones are those flagged 0x4000000n (a 16 byte record comes first)
        var out: [Bone] = []
        var pos = start + 12
        while pos + 12 <= start + size {
            let flags = u32(b, pos), nodeSize = u32(b, pos + 8)
            guard nodeSize >= 12, pos + nodeSize <= start + size else { break }
            if flags & 0xF000_0000 == 0x4000_0000, nodeSize >= 12 + 64 {
                let p = pos + 12
                let parent = Int(Int32(bitPattern: UInt32(u32(b, p + 4))))
                out.append(Bone(parent: parent, offset: SCNVector3(f32(b, p + 48), f32(b, p + 52), f32(b, p + 56))))
            }
            pos += nodeSize
        }
        return out
    }

    /// The model as a SceneKit node: a geometry per mesh (an element per material, the .nbd's textures), skinned to its
    /// skeleton, the arms lowered from the rest pose and swaying a little, as a character standing idle
    static func node(_ file: URL) -> SCNNode? {
        let meshes = meshes(file)
        guard !meshes.isEmpty else { return nil }
        let textures = NBD.textures(file)
        let root = SCNNode()

        // The skeleton: a node per bone at its offset from its parent; each bone's place standing (the rest pose)
        let skeleton = bones(file)
        var boneNodes: [SCNNode] = []
        var rest: [SCNVector3] = []
        for (i, bone) in skeleton.enumerated() {
            let node = SCNNode()
            node.position = bone.offset
            boneNodes.append(node)
            let parentRest = bone.parent >= 0 && bone.parent < i ? rest[bone.parent] : SCNVector3(0, 0, 0)
            rest.append(SCNVector3(parentRest.x + bone.offset.x, parentRest.y + bone.offset.y, parentRest.z + bone.offset.z))
        }
        for (i, bone) in skeleton.enumerated() {
            if bone.parent >= 0 && bone.parent < boneNodes.count && bone.parent != i {
                boneNodes[bone.parent].addChildNode(boneNodes[i])
            } else {
                root.addChildNode(boneNodes[i])
            }
        }

        for mesh in meshes {
            var sources = [SCNGeometrySource(vertices: mesh.positions),
                           SCNGeometrySource(textureCoordinates: mesh.uvs)]
            if !mesh.normals.isEmpty { sources.append(SCNGeometrySource(normals: mesh.normals)) }
            var elements: [SCNGeometryElement] = []
            var materials: [SCNMaterial] = []
            let count = UInt32(mesh.positions.count)
            for material in Set(mesh.stripMaterial).sorted() {
                var triangles: [UInt32] = []
                for (i, strip) in mesh.strips.enumerated() where mesh.stripMaterial[i] == material {
                    for k in 0..<(strip.count - 2) {
                        let (p, q, r) = (strip[k], strip[k + 1], strip[k + 2])
                        guard p != q, q != r, p != r, p < count, q < count, r < count else { continue }
                        triangles += k % 2 == 0 ? [p, q, r] : [q, p, r]
                    }
                }
                guard !triangles.isEmpty else { continue }
                elements.append(SCNGeometryElement(indices: triangles, primitiveType: .triangles))
                let look = SCNMaterial()
                look.diffuse.contents = material < textures.count ? textures[material] : NSColor.gray
                look.isDoubleSided = true
                look.transparencyMode = .aOne
                look.lightingModel = .lambert
                materials.append(look)
            }
            let geometry = SCNGeometry(sources: sources, elements: elements)
            geometry.materials = materials
            let node = SCNNode(geometry: geometry)
            // Skinned when every vertex has its bones: four a vertex, the strongest, weights adding up to one
            if !boneNodes.isEmpty, mesh.weights.count == mesh.positions.count {
                var indices: [UInt16] = []
                var weights: [Float] = []
                for w in mesh.weights {
                    let top = w.filter { $0.bone < boneNodes.count }.sorted { $0.weight > $1.weight }.prefix(4)
                    let total = max(top.reduce(0) { $0 + $1.weight }, 0.0001)
                    for k in 0..<4 {
                        indices.append(k < top.count ? UInt16(top[top.index(top.startIndex, offsetBy: k)].bone) : 0)
                        weights.append(k < top.count ? top[top.index(top.startIndex, offsetBy: k)].weight / total : 0)
                    }
                }
                let weightData = weights.withUnsafeBufferPointer { Data(buffer: $0) }
                let indexData = indices.withUnsafeBufferPointer { Data(buffer: $0) }
                let weightSource = SCNGeometrySource(data: weightData, semantic: .boneWeights, vectorCount: mesh.weights.count,
                                                     usesFloatComponents: true, componentsPerVector: 4, bytesPerComponent: 4,
                                                     dataOffset: 0, dataStride: 16)
                let indexSource = SCNGeometrySource(data: indexData, semantic: .boneIndices, vectorCount: mesh.weights.count,
                                                    usesFloatComponents: false, componentsPerVector: 4, bytesPerComponent: 2,
                                                    dataOffset: 0, dataStride: 8)
                let inverseBind = rest.map { NSValue(scnMatrix4: SCNMatrix4MakeTranslation(-$0.x, -$0.y, -$0.z)) }
                let skinner = SCNSkinner(baseGeometry: geometry, bones: boneNodes, boneInverseBindTransforms: inverseBind,
                                         boneWeights: weightSource, boneIndices: indexSource)
                skinner.skeleton = boneNodes.first
                node.skinner = skinner
            }
            root.addChildNode(node)
        }
        pose(boneNodes, skeleton, rest)
        return root
    }

    /// The arms down from the rest pose (the shoulders: the upper body's bones whose child reaches out sideways), then a
    /// slow sway of them and of the body, as an idle stance
    private static func pose(_ nodes: [SCNNode], _ skeleton: [Bone], _ rest: [SCNVector3]) {
        guard !nodes.isEmpty else { return }
        // The shoulders: on each side, the bone nearest the body whose child reaches out sideways, high in the body
        var shoulders: [CGFloat: Int] = [:] // side (1 right, -1 left) -> bone
        for bone in skeleton where bone.parent >= 0 && bone.parent < skeleton.count {
            guard abs(bone.offset.x) > 15, abs(bone.offset.y) < 10, rest[bone.parent].y > 100 else { continue }
            let side: CGFloat = bone.offset.x > 0 ? 1 : -1
            if let found = shoulders[side], abs(rest[found].x) <= abs(rest[bone.parent].x) { continue }
            shoulders[side] = bone.parent
        }
        for (side, shoulder) in shoulders {
            nodes[shoulder].eulerAngles = SCNVector3(0, 0, -side * 1.2)
            let sway = SCNAction.sequence([
                .rotateTo(x: 0.05, y: 0, z: -side * 1.15, duration: 1.8, usesShortestUnitArc: true),
                .rotateTo(x: -0.03, y: 0, z: -side * 1.25, duration: 1.8, usesShortestUnitArc: true),
            ])
            sway.timingMode = .easeInEaseOut
            nodes[shoulder].runAction(.repeatForever(sway))
        }
        // The body breathing: the root's child (the hips) rising and settling a little
        if nodes.count > 2 {
            let hips = nodes[min(2, nodes.count - 1)]
            let base = hips.position
            let breathe = SCNAction.sequence([.move(to: SCNVector3(base.x, base.y + 0.6, base.z), duration: 1.8),
                                              .move(to: base, duration: 1.8)])
            breathe.timingMode = .easeInEaseOut
            hips.runAction(.repeatForever(breathe))
        }
    }
}
