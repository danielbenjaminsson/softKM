import Cocoa
import Foundation

class AppDelegate: NSObject, NSApplicationDelegate {
    private var eventCapture: EventCapture?
    private var switchController: SwitchController?
    private var permissionCheckTimer: Timer?

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Show log window on start (for development)
        LogWindowController.shared.show()

        // Log app identity for debugging
        if let bundleID = Bundle.main.bundleIdentifier {
            LOG("App bundle ID: \(bundleID)")
        }
        LOG("App path: \(Bundle.main.bundlePath)")

        // Check and request accessibility permissions
        checkAccessibilityPermission()

        // Initialize the switch controller
        switchController = SwitchController.shared

        // Start permission check timer - will start event capture when permission granted
        permissionCheckTimer = Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { [weak self] _ in
            self?.checkAndStartCapture()
        }

        // Auto-connect to Haiku server
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.0) {
            LOG("Auto-connecting to server...")
            ConnectionManager.shared.connect()
        }
    }

    func applicationWillTerminate(_ notification: Notification) {
        permissionCheckTimer?.invalidate()
        eventCapture?.stopCapture()
        ConnectionManager.shared.disconnect()
    }

    private func checkAccessibilityPermission() {
        // Check without prompting first
        let trusted = AXIsProcessTrusted()
        LOG("Accessibility permission granted: \(trusted)")

        if !trusted {
            LOG("Requesting accessibility permission...")
            // Prompt for permission
            let options: NSDictionary = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true]
            _ = AXIsProcessTrustedWithOptions(options)
            LOG("Please grant Accessibility permission in System Settings → Privacy & Security → Accessibility")
        }
    }

    private func checkAndStartCapture() {
        let trusted = AXIsProcessTrusted()

        if trusted {
            if eventCapture == nil {
                LOG("Accessibility permission now granted - starting event capture")
                eventCapture = EventCapture.shared
                if eventCapture?.startCapture() == true {
                    LOG("Event capture started successfully")
                    permissionCheckTimer?.invalidate()
                    permissionCheckTimer = nil
                } else {
                    LOG("Failed to start event capture even with permission granted")
                }
            }
        } else {
            LOG("Still waiting for Accessibility permission...")
        }
    }
}
