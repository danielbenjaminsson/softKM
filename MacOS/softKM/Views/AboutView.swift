import SwiftUI

struct AboutView: View {
    @Environment(\.dismiss) private var dismiss

    private var appVersion: String {
        Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "1.0"
    }

    private var buildNumber: String {
        Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "1"
    }

    var body: some View {
        VStack(spacing: 16) {
            Image(nsImage: NSApplication.shared.applicationIconImage)
                .resizable()
                .frame(width: 128, height: 128)

            Text("softKM")
                .font(.title)
                .fontWeight(.bold)

            Text("Version \(appVersion) (\(buildNumber))")
                .font(.caption)
                .foregroundColor(.secondary)

            Text("Software Keyboard/Mouse Switch for Haiku")
                .font(.body)
                .multilineTextAlignment(.center)

            Text("Share keyboard and mouse input between macOS and Haiku OS computers over a network.\n\nMove your mouse to the screen edge to seamlessly switch control between computers.")
                .font(.caption)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 280)

            Divider()
                .frame(width: 200)

            VStack(spacing: 4) {
                Text("© 2025 Microgeni AB")
                    .font(.caption)
                    .foregroundColor(.secondary)

                Text("Written by Daniel Benjaminsson")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }

            Button("OK") {
                dismiss()
            }
            .keyboardShortcut(.defaultAction)
            .padding(.top, 8)
        }
        .padding(24)
        .frame(width: 340)
    }
}

#Preview {
    AboutView()
}
