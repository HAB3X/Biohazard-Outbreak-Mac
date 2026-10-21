// swift-tools-version:5.10
// The launcher: one app for both games (tools/make_app.py launcher bundles it with the two game apps)
import PackageDescription

let package = Package(
    name: "Launcher",
    platforms: [.macOS(.v14)],
    targets: [
        .executableTarget(name: "Launcher", path: "Sources/Launcher")
    ]
)
