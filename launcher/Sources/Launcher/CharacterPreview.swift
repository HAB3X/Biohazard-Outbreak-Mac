// A character mod in 3D: its model (CharacterModel) standing on a dark stage, turning slowly; drag to turn it yourself
import SceneKit
import SwiftUI

struct CharacterPreview: View {
    var file: String
    @State private var scene: SCNScene?
    @State private var failed = false

    var body: some View {
        ZStack {
            RadialGradient(colors: [Color(white: 0.16), Color(white: 0.04)], center: .center, startRadius: 10, endRadius: 260)
            if let scene {
                SceneView(scene: scene, options: [.allowsCameraControl, .autoenablesDefaultLighting])
                    .background(Color.clear)
            } else if failed {
                Text("This model couldn't be read").font(.system(size: 13)).foregroundStyle(Style.muted)
            } else {
                ProgressView().controlSize(.small)
            }
        }
        .clipShape(RoundedRectangle(cornerRadius: 12))
        .task(id: file) {
            scene = nil
            failed = false
            let path = file
            let made = await Task.detached(priority: .userInitiated) { CharacterPreview.makeScene(URL(fileURLWithPath: path)) }.value
            scene = made
            failed = made == nil
        }
    }

    nonisolated static func makeScene(_ file: URL) -> SCNScene? {
        guard let model = CharacterModel.node(file) else { return nil }
        let scene = SCNScene()
        scene.background.contents = NSColor(white: 0.07, alpha: 1)
        // Centred on the stage, feet on the floor, turning once every 12 seconds
        let (low, high) = model.boundingBox
        let height = max(1, high.y - low.y)
        model.position = SCNVector3(-(low.x + high.x) / 2, -low.y, -(low.z + high.z) / 2)
        let turntable = SCNNode()
        turntable.addChildNode(model)
        turntable.runAction(.repeatForever(.rotateBy(x: 0, y: .pi * 2, z: 0, duration: 12)))
        scene.rootNode.addChildNode(turntable)
        let camera = SCNNode()
        camera.camera = SCNCamera()
        camera.camera?.fieldOfView = 30
        camera.camera?.zFar = Double(height * 20)
        camera.position = SCNVector3(0, height * 0.55, height * 2.1)
        camera.look(at: SCNVector3(0, height * 0.5, 0))
        scene.rootNode.addChildNode(camera)
        let key = SCNNode()
        key.light = SCNLight()
        key.light?.type = .directional
        key.light?.intensity = 1300
        key.eulerAngles = SCNVector3(-0.5, 0.6, 0)
        scene.rootNode.addChildNode(key)
        let fill = SCNNode()
        fill.light = SCNLight()
        fill.light?.type = .ambient
        fill.light?.intensity = 750
        scene.rootNode.addChildNode(fill)
        return scene
    }
}
