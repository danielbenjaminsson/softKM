import Cocoa
import Foundation

class AppDelegate: NSObject, NSApplicationDelegate {
    private var eventCapture: EventCapture?
    private var switchController: SwitchController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Request accessibility permissions
        requestAccessibilityPermission()

        // Initialize the switch controller
        switchController = SwitchController.shared

        // Initialize event capture
        eventCapture = EventCapture.shared
    }

    func applicationWillTerminate(_ notification: Notification) {
        eventCapture?.stopCapture()
        ConnectionManager.shared.disconnect()
    }

    private func requestAccessibilityPermission() {
        let options: NSDictionary = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true]
        let accessEnabled = AXIsProcessTrustedWithOptions(options)

        if !accessEnabled {
            print("Accessibility permission required. Please grant permission in System Preferences.")
        }
    }
}
