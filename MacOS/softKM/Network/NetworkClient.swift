import Foundation
import Darwin

extension Notification.Name {
    static let switchToMac = Notification.Name("switchToMac")
}

struct SwitchToMacInfo {
    let yFromBottom: Float
}

class NetworkClient: ObservableObject {
    @Published var connectionState: ConnectionManager.ConnectionState = .disconnected

    private var socketFD: Int32 = -1
    private let queue = DispatchQueue(label: "com.softkm.network")
    private var heartbeatTimer: Timer?
    private var flushTimer: Timer?
    private var receiveThread: Thread?
    private var shouldRun = false
    private var sendCount = 0
    private var lastLogTime = Date()

    // Event batching for mouse moves
    private var pendingMouseDelta: (x: Float, y: Float) = (0, 0)
    private var pendingMouseModifiers: UInt32 = 0
    private var hasPendingMouse = false
    private let batchLock = NSLock()

    func connect(to host: String, port: Int, useTLS: Bool) {
        disconnect()

        LOG("Connecting to \(host):\(port) (TLS: \(useTLS))")
        connectionState = .connecting

        queue.async { [weak self] in
            guard let self = self else { return }

            // Resolve hostname
            var hints = addrinfo()
            hints.ai_family = AF_INET
            hints.ai_socktype = SOCK_STREAM
            hints.ai_protocol = IPPROTO_TCP

            var result: UnsafeMutablePointer<addrinfo>?
            let portStr = String(port)
            let status = getaddrinfo(host, portStr, &hints, &result)

            if status != 0 {
                let error = String(cString: gai_strerror(status))
                LOG("Failed to resolve hostname: \(error)")
                DispatchQueue.main.async {
                    self.connectionState = .error("DNS resolution failed: \(error)")
                }
                self.scheduleReconnect()
                return
            }

            defer { freeaddrinfo(result) }

            guard let addr = result else {
                LOG("No address found")
                DispatchQueue.main.async {
                    self.connectionState = .error("No address found")
                }
                self.scheduleReconnect()
                return
            }

            // Create socket
            self.socketFD = socket(AF_INET, SOCK_STREAM, 0)
            if self.socketFD < 0 {
                LOG("Failed to create socket")
                DispatchQueue.main.async {
                    self.connectionState = .error("Failed to create socket")
                }
                self.scheduleReconnect()
                return
            }

            // Set TCP_NODELAY for low latency
            var opt: Int32 = 1
            if setsockopt(self.socketFD, IPPROTO_TCP, TCP_NODELAY, &opt, socklen_t(MemoryLayout<Int32>.size)) < 0 {
                LOG("WARNING: Could not set TCP_NODELAY")
            } else {
                LOG("TCP_NODELAY enabled")
            }

            // Connect
            if Darwin.connect(self.socketFD, addr.pointee.ai_addr, addr.pointee.ai_addrlen) < 0 {
                let err = String(cString: strerror(errno))
                LOG("Failed to connect: \(err)")
                close(self.socketFD)
                self.socketFD = -1
                DispatchQueue.main.async {
                    self.connectionState = .error("Connection failed: \(err)")
                }
                self.scheduleReconnect()
                return
            }

            LOG("Connected successfully!")
            self.shouldRun = true

            DispatchQueue.main.async {
                self.connectionState = .connected
                self.startHeartbeat()
            }

            // Start receive loop on this queue
            self.receiveLoop()
        }
    }

    func disconnect() {
        LOG("Disconnecting from server...")
        heartbeatTimer?.invalidate()
        heartbeatTimer = nil
        flushTimer?.invalidate()
        flushTimer = nil
        shouldRun = false

        if socketFD >= 0 {
            // Shutdown to unblock recv
            shutdown(socketFD, SHUT_RDWR)
            close(socketFD)
            socketFD = -1
        }

        connectionState = .disconnected
        LOG("Disconnected")
    }

    func send(event: InputEvent) {
        // Batch mouse moves together
        if case let .mouseMove(x, y, relative, modifiers) = event, relative {
            batchLock.lock()
            pendingMouseDelta.x += x
            pendingMouseDelta.y += y
            pendingMouseModifiers = modifiers
            hasPendingMouse = true
            batchLock.unlock()
            return  // Will be flushed by timer
        }

        // All other events sent immediately
        sendDirect(event: event)
    }

    func flushPendingMouse() {
        batchLock.lock()
        guard hasPendingMouse else {
            batchLock.unlock()
            return
        }
        let delta = pendingMouseDelta
        let modifiers = pendingMouseModifiers
        pendingMouseDelta = (0, 0)
        pendingMouseModifiers = 0
        hasPendingMouse = false
        batchLock.unlock()

        sendDirect(event: .mouseMove(x: delta.x, y: delta.y, relative: true, modifiers: modifiers))
    }

    private func sendDirect(event: InputEvent) {
        let fd = socketFD
        guard connectionState == .connected, fd >= 0 else {
            return
        }

        let data = Protocol.encode(event)
        sendCount += 1
        let now = Date()
        if now.timeIntervalSince(lastLogTime) >= 1.0 {
            LOG("Send rate: \(sendCount) sends, fd=\(fd)")
            sendCount = 0
            lastLogTime = now
        }

        data.withUnsafeBytes { ptr in
            let sent = Darwin.send(fd, ptr.baseAddress, data.count, 0)
            if sent < 0 {
                LOG("Send error: \(String(cString: strerror(errno)))")
                DispatchQueue.main.async { [weak self] in
                    self?.disconnect()
                }
            }
        }
    }

    private func receiveLoop() {
        var buffer = [UInt8](repeating: 0, count: 4096)
        var accumulated = Data()

        while shouldRun && socketFD >= 0 {
            let bytesRead = recv(socketFD, &buffer, buffer.count, 0)

            if bytesRead <= 0 {
                if bytesRead < 0 && errno == EINTR {
                    continue
                }
                // Connection closed or error
                LOG("Receive error or connection closed")
                DispatchQueue.main.async { [weak self] in
                    self?.disconnect()
                    self?.scheduleReconnect()
                }
                break
            }

            accumulated.append(contentsOf: buffer[0..<bytesRead])

            // Process complete messages
            while accumulated.count >= 8 {
                // Check magic
                let magic = accumulated.subdata(in: 0..<2).withUnsafeBytes { $0.load(as: UInt16.self) }
                if magic != Protocol.magic.littleEndian {
                    LOG("Invalid magic in response, clearing buffer")
                    accumulated.removeAll()
                    break
                }

                // Get length
                let length = accumulated.subdata(in: 4..<8).withUnsafeBytes { $0.load(as: UInt32.self) }
                let messageSize = 8 + Int(length)

                if accumulated.count < messageSize {
                    break  // Wait for more data
                }

                // Process complete message
                let messageData = accumulated.subdata(in: 0..<messageSize)
                handleReceivedData(messageData)
                accumulated.removeSubrange(0..<messageSize)
            }
        }
    }

    private func handleReceivedData(_ data: Data) {
        guard data.count >= 8 else {
            LOG("Data too short, ignoring")
            return
        }

        let eventType = data[3]
        if eventType == EventType.heartbeatAck.rawValue {
            // Heartbeat ACK - silent
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
            self?.sendDirect(event: .heartbeat)
        }

        // Flush pending mouse moves every 16ms (~60Hz)
        flushTimer?.invalidate()
        flushTimer = Timer.scheduledTimer(withTimeInterval: 0.016, repeats: true) { [weak self] _ in
            self?.flushPendingMouse()
        }
    }

    private func scheduleReconnect() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 5.0) { [weak self] in
            guard self?.connectionState != .connected else { return }
            let settings = SettingsManager.shared
            self?.connect(to: settings.hostAddress, port: settings.port, useTLS: settings.useTLS)
        }
    }
}
