import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var connectionManager: ConnectionManager
    @StateObject private var settings = SettingsManager.shared

    var body: some View {
        Form {
            Section("Connection") {
                TextField("Host Address", text: $settings.hostAddress)
                    .textFieldStyle(.roundedBorder)

                TextField("Port", value: $settings.port, format: .number)
                    .textFieldStyle(.roundedBorder)

                Toggle("Use TLS Encryption", isOn: $settings.useTLS)
            }

            Section("Edge Switching") {
                Picker("Switch Edge", selection: $settings.switchEdge) {
                    Text("Right").tag(ScreenEdge.right)
                    Text("Left").tag(ScreenEdge.left)
                    Text("Top").tag(ScreenEdge.top)
                    Text("Bottom").tag(ScreenEdge.bottom)
                }
                .pickerStyle(.segmented)

                HStack {
                    Text("Edge Dwell Time")
                    Slider(value: $settings.edgeDwellTime, in: 0.1...1.0, step: 0.1)
                    Text("\(settings.edgeDwellTime, specifier: "%.1f")s")
                        .monospacedDigit()
                        .frame(width: 40)
                }
            }

            Section("Status") {
                LabeledContent("Connection") {
                    Text(connectionStateText)
                        .foregroundColor(connectionStateColor)
                }

                LabeledContent("Mode") {
                    Text(connectionManager.isCapturing ? "Capturing" : "Monitoring")
                }
            }
        }
        .formStyle(.grouped)
        .frame(width: 400, height: 320)
        .padding()
    }

    private var connectionStateText: String {
        switch connectionManager.connectionState {
        case .connected: return "Connected"
        case .connecting: return "Connecting..."
        case .disconnected: return "Disconnected"
        case .error(let msg): return "Error: \(msg)"
        }
    }

    private var connectionStateColor: Color {
        switch connectionManager.connectionState {
        case .connected: return .green
        case .connecting: return .yellow
        case .disconnected: return .secondary
        case .error: return .red
        }
    }
}

enum ScreenEdge: String, CaseIterable, Codable {
    case left, right, top, bottom
}
