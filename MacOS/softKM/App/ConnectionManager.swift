import Foundation
import Combine
import AppKit

class ConnectionManager: ObservableObject {
    static let shared = ConnectionManager()

    @Published var isConnected = false
    @Published var connectionState: ConnectionState = .disconnected
    @Published var isCapturing = false

    // Screen dimensions
    var localScreenSize: CGSize = .zero
    var remoteScreenSize: CGSize = .zero

    private var networkClient: NetworkClient?
    private var cancellables = Set<AnyCancellable>()

    enum ConnectionState: Equatable {
        case disconnected
        case connecting
        case connected
        case error(String)

        static func == (lhs: ConnectionState, rhs: ConnectionState) -> Bool {
            switch (lhs, rhs) {
            case (.disconnected, .disconnected),
                 (.connecting, .connecting),
                 (.connected, .connected):
                return true
            case (.error(let l), .error(let r)):
                return l == r
            default:
                return false
            }
        }
    }

    private init() {
        networkClient = NetworkClient()
        setupBindings()
    }

    private func setupBindings() {
        networkClient?.$connectionState
            .receive(on: DispatchQueue.main)
            .sink { [weak self] state in
                guard let self = self else { return }
                let wasConnected = self.isConnected
                self.connectionState = state
                self.isConnected = (state == .connected)

                // Send screen info when newly connected
                if !wasConnected && self.isConnected {
                    self.sendScreenInfo()
                }
            }
            .store(in: &cancellables)
    }

    private func sendScreenInfo() {
        if let screen = NSScreen.main {
            localScreenSize = screen.frame.size
            let event = InputEvent.screenInfo(
                width: Float(localScreenSize.width),
                height: Float(localScreenSize.height)
            )
            send(event: event)
            LOG("Sent screen info: \(localScreenSize.width)x\(localScreenSize.height)")
        }
    }

    func setRemoteScreenSize(width: Float, height: Float) {
        remoteScreenSize = CGSize(width: CGFloat(width), height: CGFloat(height))
        LOG("Received remote screen size: \(width)x\(height)")
    }

    func connect() {
        let settings = SettingsManager.shared
        networkClient?.connect(
            to: settings.hostAddress,
            port: settings.port,
            useTLS: settings.useTLS
        )
    }

    func disconnect() {
        networkClient?.disconnect()
    }

    func send(event: InputEvent) {
        networkClient?.send(event: event)
    }

    func sendControlSwitch(toHaiku: Bool, yFromBottom: Float = 0.0) {
        let event = InputEvent.controlSwitch(toHaiku: toHaiku, yFromBottom: yFromBottom)
        send(event: event)
    }
}
