import SwiftUI

struct StatusBarView: View {
    @EnvironmentObject var connectionManager: ConnectionManager
    @StateObject private var settings = SettingsManager.shared
    @State private var showLog = false

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Circle()
                    .fill(statusColor)
                    .frame(width: 8, height: 8)
                Text(statusText)
                    .font(.headline)
            }
            .padding(.horizontal)

            Divider()

            if connectionManager.isConnected {
                Button(action: { connectionManager.disconnect() }) {
                    Label("Disconnect", systemImage: "wifi.slash")
                }
                .buttonStyle(.plain)
                .padding(.horizontal)

                if connectionManager.isCapturing {
                    Text("Capturing input for Haiku")
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .padding(.horizontal)
                }
            } else {
                Button(action: { connectionManager.connect() }) {
                    Label("Connect to \(settings.hostAddress)", systemImage: "wifi")
                }
                .buttonStyle(.plain)
                .padding(.horizontal)
            }

            Divider()

            Toggle(isOn: $showLog) {
                Label("Show Log", systemImage: "doc.text")
            }
            .toggleStyle(.checkbox)
            .padding(.horizontal)
            .onChange(of: showLog) { newValue in
                if newValue {
                    LogWindowController.shared.show()
                } else {
                    LogWindowController.shared.window?.orderOut(nil)
                }
            }
            .onAppear {
                showLog = LogWindowController.shared.window?.isVisible ?? false
            }

            if #available(macOS 14.0, *) {
                SettingsLink {
                    Label("Settings...", systemImage: "gear")
                }
                .buttonStyle(.plain)
                .padding(.horizontal)
            } else {
                Button(action: {
                    NSApp.sendAction(Selector(("showSettingsWindow:")), to: nil, from: nil)
                }) {
                    Label("Settings...", systemImage: "gear")
                }
                .buttonStyle(.plain)
                .padding(.horizontal)
            }

            Divider()

            Button(action: { NSApplication.shared.terminate(nil) }) {
                Label("Quit softKM", systemImage: "power")
            }
            .buttonStyle(.plain)
            .padding(.horizontal)
        }
        .padding(.vertical, 8)
        .frame(width: 220)
    }

    private var statusColor: Color {
        switch connectionManager.connectionState {
        case .connected:
            return connectionManager.isCapturing ? .orange : .green
        case .connecting:
            return .yellow
        case .disconnected:
            return .gray
        case .error:
            return .red
        }
    }

    private var statusText: String {
        switch connectionManager.connectionState {
        case .connected:
            return connectionManager.isCapturing ? "Capturing" : "Connected"
        case .connecting:
            return "Connecting..."
        case .disconnected:
            return "Disconnected"
        case .error(let message):
            return "Error: \(message)"
        }
    }
}
