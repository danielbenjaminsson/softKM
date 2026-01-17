import Cocoa
import Foundation

class AppDelegate: NSObject, NSApplicationDelegate {
    private var eventCapture: EventCapture?
    private var switchController: SwitchController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Show log window on start (for development)
        LogWindowController.shared.show()

        // Request accessibility permissions
        requestAccessibilityPermission()

        // Initialize the switch controller
        switchController = SwitchController.shared

        // Initialize and start event capture for edge detection
        eventCapture = EventCapture.shared
        if eventCapture?.startCapture() == true {
            LOG("Event capture started for edge detection")
        } else {
            LOG("Failed to start event capture - check Accessibility permissions")
        }

        // Auto-connect to Haiku server
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
            LOG("Auto-connecting to server...")
            ConnectionManager.shared.connect()
        }
    }

    func applicationWillTerminate(_ notification: Notification) {
        eventCapture?.stopCapture()
        ConnectionManager.shared.disconnect()
    }

    private func requestAccessibilityPermission() {
        let options: NSDictionary = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true]
        let accessEnabled = AXIsProcessTrustedWithOptions(options)

        if !accessEnabled {
            LOG("Accessibility permission required. Please grant permission in System Preferences.")
        }
    }
}
