import Foundation
import Network

class NetworkClient: ObservableObject {
    @Published var connectionState: ConnectionManager.ConnectionState = .disconnected

    private var connection: NWConnection?
    private let queue = DispatchQueue(label: "com.softkm.network")
    private var heartbeatTimer: Timer?

    func connect(to host: String, port: Int, useTLS: Bool) {
        disconnect()

        connectionState = .connecting

        let endpoint = NWEndpoint.hostPort(
            host: NWEndpoint.Host(host),
            port: NWEndpoint.Port(integerLiteral: UInt16(clamping: port))
        )

        let parameters: NWParameters
        if useTLS {
            parameters = NWParameters(tls: createTLSOptions())
        } else {
            parameters = NWParameters.tcp
        }

        // Configure TCP options for low latency
        if let tcpOptions = parameters.defaultProtocolStack.transportProtocol as? NWProtocolTCP.Options {
            tcpOptions.noDelay = true
        }

        connection = NWConnection(to: endpoint, using: parameters)

        connection?.stateUpdateHandler = { [weak self] state in
            DispatchQueue.main.async {
                self?.handleStateChange(state)
            }
        }

        connection?.start(queue: queue)
    }

    func disconnect() {
        heartbeatTimer?.invalidate()
        heartbeatTimer = nil
        connection?.cancel()
        connection = nil
        connectionState = .disconnected
    }

    func send(event: InputEvent) {
        guard connectionState == .connected else { return }

        let data = Protocol.encode(event)
        connection?.send(content: data, completion: .contentProcessed { [weak self] error in
            if let error = error {
                DispatchQueue.main.async {
                    self?.connectionState = .error(error.localizedDescription)
                }
            }
        })
    }

    private func handleStateChange(_ state: NWConnection.State) {
        switch state {
        case .ready:
            connectionState = .connected
            startHeartbeat()
            startReceiving()

        case .waiting(let error):
            connectionState = .error("Waiting: \(error.localizedDescription)")

        case .failed(let error):
            connectionState = .error(error.localizedDescription)
            scheduleReconnect()

        case .cancelled:
            connectionState = .disconnected

        case .preparing:
            connectionState = .connecting

        default:
            break
        }
    }

    private func startReceiving() {
        connection?.receive(minimumIncompleteLength: 1, maximumLength: 1024) { [weak self] data, _, isComplete, error in
            if let data = data, !data.isEmpty {
                self?.handleReceivedData(data)
            }

            if let error = error {
                DispatchQueue.main.async {
                    self?.connectionState = .error(error.localizedDescription)
                }
                return
            }

            if !isComplete {
                self?.startReceiving()
            }
        }
    }

    private func handleReceivedData(_ data: Data) {
        // Handle heartbeat ack or other responses from Haiku
        guard data.count >= 8 else { return }

        let magic = data.subdata(in: 0..<2).withUnsafeBytes { $0.load(as: UInt16.self) }
        guard magic == Protocol.magic.littleEndian else { return }

        let eventType = data[3]
        if eventType == EventType.heartbeatAck.rawValue {
            // Heartbeat acknowledged
        }
    }

    private func startHeartbeat() {
        heartbeatTimer?.invalidate()
        heartbeatTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            self?.send(event: .heartbeat)
        }
    }

    private func scheduleReconnect() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 5.0) { [weak self] in
            guard self?.connectionState != .connected else { return }
            let settings = SettingsManager.shared
            self?.connect(to: settings.hostAddress, port: settings.port, useTLS: settings.useTLS)
        }
    }

    private func createTLSOptions() -> NWProtocolTLS.Options {
        let options = NWProtocolTLS.Options()
        // For development, allow self-signed certificates
        sec_protocol_options_set_verify_block(options.securityProtocolOptions, { _, _, completion in
            completion(true)
        }, queue)
        return options
    }
}
