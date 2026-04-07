// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "BMSAssistant",
    platforms: [
        .macOS(.v14),
        .iOS(.v17),
    ],
    products: [
        .executable(name: "BMSAssistant", targets: ["BMSAssistant"]),
    ],
    targets: [
        .executableTarget(
            name: "BMSAssistant",
            path: "Sources/BMSAssistant",
            linkerSettings: [
                .unsafeFlags([
                    "-Xlinker", "-sectcreate",
                    "-Xlinker", "__TEXT",
                    "-Xlinker", "__info_plist",
                    "-Xlinker", "Resources/Info.plist",
                ]),
            ]
        ),
    ]
)
