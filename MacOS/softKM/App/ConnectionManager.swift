import Foundation
import Combine

class ConnectionManager: ObservableObject {
    static let shared = ConnectionManager()

    @Published var isConnected = false
    @Published var connectionState: ConnectionState = .disconnected
    @Published var isCapturing = false

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
                self?.connectionState = state
                self?.isConnected = (state == .connected)
            }
            .store(in: &cancellables)
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

    func sendControlSwitch(toHaiku: Bool) {
        let event = InputEvent.controlSwitch(toHaiku: toHaiku)
        send(event: event)
    }
}
