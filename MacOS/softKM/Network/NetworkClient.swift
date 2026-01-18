import Foundation
import Network

extension Notification.Name {
    static let switchToMac = Notification.Name("switchToMac")
}

struct SwitchToMacInfo {
    let yFromBottom: Float
}

class NetworkClient: ObservableObject {
    @Published var connectionState: ConnectionManager.ConnectionState = .disconnected

    private var connection: NWConnection?
    private let queue = DispatchQueue(label: "com.softkm.network")
    private var heartbeatTimer: Timer?

    func connect(to host: String, port: Int, useTLS: Bool) {
        disconnect()

        LOG("Connecting to \(host):\(port) (TLS: \(useTLS))")
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
            LOG("TCP_NODELAY enabled")
        } else {
            LOG("WARNING: Could not set TCP_NODELAY")
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
        LOG("Disconnecting from server...")
        heartbeatTimer?.invalidate()
        heartbeatTimer = nil

        // Force close the connection
        connection?.forceCancel()
        connection = nil
        connectionState = .disconnected
        LOG("Disconnected")
    }

    func send(event: InputEvent) {
        guard connectionState == .connected else {
            LOG("Cannot send - not connected")
            return
        }

        let data = Protocol.encode(event)
        // Use idempotent completion to avoid backpressure from waiting for completion
        connection?.send(content: data, completion: .idempotent)
    }

    private func handleStateChange(_ state: NWConnection.State) {
        LOG("Connection state: \(state)")

        switch state {
        case .ready:
            LOG("Connected successfully!")
            connectionState = .connected
            startHeartbeat()
            startReceiving()

        case .waiting(let error):
            LOG("Waiting: \(error)")
            connectionState = .error("Waiting: \(error.localizedDescription)")

        case .failed(let error):
            LOG("Failed: \(error)")
            connectionState = .error(error.localizedDescription)
            scheduleReconnect()

        case .cancelled:
            LOG("Cancelled")
            connectionState = .disconnected

        case .preparing:
            LOG("Preparing connection...")
            connectionState = .connecting

        default:
            LOG("Other state: \(state)")
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
        LOG("Received \(data.count) bytes from server")

        // Handle heartbeat ack or other responses from Haiku
        guard data.count >= 8 else {
            LOG("Data too short, ignoring")
            return
        }

        let magic = data.subdata(in: 0..<2).withUnsafeBytes { $0.load(as: UInt16.self) }
        guard magic == Protocol.magic.littleEndian else {
            LOG("Invalid magic in response")
            return
        }

        let eventType = data[3]
        if eventType == EventType.heartbeatAck.rawValue {
            LOG("Received heartbeat ACK")
        } else if eventType == EventType.controlSwitch.rawValue {
            // Haiku is telling us to switch control back to macOS
            guard data.count >= 9 else {
                LOG("CONTROL_SWITCH message too short")
                return
            }
            let direction = data[8]  // 0=toHaiku, 1=toMac

            // Extract yFromBottom if available (header=8 + direction=1 + yFromBottom=4)
            var yFromBottom: Float = 0.0
            if data.count >= 13 {
                yFromBottom = data.subdata(in: 9..<13).withUnsafeBytes { $0.load(as: Float.self) }
            }
            LOG("Received CONTROL_SWITCH direction=\(direction) yFromBottom=\(yFromBottom)")

            if direction == 1 {
                // Switch back to macOS
                let info = SwitchToMacInfo(yFromBottom: yFromBottom)
                DispatchQueue.main.async {
                    NotificationCenter.default.post(name: .switchToMac, object: info)
                }
            }
        } else if eventType == EventType.screenInfo.rawValue {
            // Haiku is sending its screen dimensions
            guard data.count >= 16 else {  // header(8) + width(4) + height(4)
                LOG("SCREEN_INFO message too short")
                return
            }
            let width = data.subdata(in: 8..<12).withUnsafeBytes { $0.load(as: Float.self) }
            let height = data.subdata(in: 12..<16).withUnsafeBytes { $0.load(as: Float.self) }
            LOG("Received remote screen info: \(width)x\(height)")

            DispatchQueue.main.async {
                ConnectionManager.shared.setRemoteScreenSize(width: width, height: height)
            }
        } else {
            LOG("Received event type: 0x\(String(format: "%02X", eventType))")
        }
    }

    private func startHeartbeat() {
        heartbeatTimer?.invalidate()
        heartbeatTimer = Timer.scheduledTimer(withTimeInterval: 5.0, repeats: true) { [weak self] _ in
            LOG("Sending heartbeat")
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
