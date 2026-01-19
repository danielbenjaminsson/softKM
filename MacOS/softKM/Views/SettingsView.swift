import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var connectionManager: ConnectionManager
    @StateObject private var settings = SettingsManager.shared
    @State private var arrangement: MonitorArrangement = SettingsManager.shared.monitorArrangement

    var body: some View {
        Form {
            Section("Connection") {
                TextField("Host Address", text: $settings.hostAddress)
                    .textFieldStyle(.roundedBorder)

                TextField("Port", value: $settings.port, format: .number)
                    .textFieldStyle(.roundedBorder)

                Toggle("Use TLS Encryption", isOn: $settings.useTLS)
            }

            Section("Monitor Arrangement") {
                MonitorArrangementView(arrangement: $arrangement)
                    .onChange(of: arrangement) { newValue in
                        settings.monitorArrangement = newValue
                    }

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
        .frame(width: 420, height: 480)
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
