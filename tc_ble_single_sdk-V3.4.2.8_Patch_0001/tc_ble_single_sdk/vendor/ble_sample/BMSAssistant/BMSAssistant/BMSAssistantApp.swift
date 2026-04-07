import SwiftUI

@main
struct BMSAssistantApp: App {
    @StateObject private var bleManager = BLEManager.shared
    @StateObject private var bmsService = BMSService.shared

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(bleManager)
                .environmentObject(bmsService)
        }
        .windowStyle(.hiddenTitleBar)
        .windowToolbarStyle(.unified)
        .commands {
            CommandGroup(replacing: .newItem) {}
        }
    }
}
