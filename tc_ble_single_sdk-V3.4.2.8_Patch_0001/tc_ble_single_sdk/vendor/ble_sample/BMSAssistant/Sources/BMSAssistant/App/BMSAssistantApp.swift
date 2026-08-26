import SwiftUI

@main
struct BMSAssistantApp: App {
    @State private var model = AppModel()

    var body: some Scene {
#if os(macOS)
        WindowGroup("BMS Assistant") {
            ContentView()
                .environment(model)
                .frame(minWidth: 1320, minHeight: 860)
        }
        .windowResizability(.contentMinSize)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
#else
        WindowGroup("BMS Assistant") {
            MobileContentView()
                .environment(model)
        }
#endif
    }
}
