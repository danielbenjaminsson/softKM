import SwiftUI

@main
struct softKMApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) var appDelegate
    @StateObject private var connectionManager = ConnectionManager.shared

    var body: some Scene {
        MenuBarExtra {
            StatusBarView()
                .environmentObject(connectionManager)
        } label: {
            Image("MenuBarIcon")
                .renderingMode(.original)
        }

        Window("softKM Settings", id: "settings") {
            SettingsView()
                .environmentObject(connectionManager)
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 500, height: 580)
    }
}
