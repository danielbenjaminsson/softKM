import SwiftUI

enum LogCategory: String, CaseIterable {
    case mouse = "Mouse"
    case keys = "Keys"
    case comm = "Comm"
    case other = "Other"

    static func categorize(_ entry: String) -> LogCategory {
        let lower = entry.lowercased()
        if lower.contains("mouse") || lower.contains("scroll") || lower.contains("click") ||
           lower.contains("cursor") || lower.contains("wheel") {
            return .mouse
        } else if lower.contains("key") || lower.contains("keyboard") || lower.contains("modifier") {
            return .keys
        } else if lower.contains("connect") || lower.contains("disconnect") || lower.contains("send") ||
                  lower.contains("receive") || lower.contains("network") || lower.contains("client") ||
                  lower.contains("server") || lower.contains("socket") || lower.contains("tcp") ||
                  lower.contains("heartbeat") {
            return .comm
        } else {
            return .other
        }
    }
}

struct LogWindow: View {
    @ObservedObject var logger = Logger.shared
    @State private var autoScroll = true
    @State private var showMouse = true
    @State private var showKeys = true
    @State private var showComm = true
    @State private var showOther = true

    private var filteredEntries: [(Int, String)] {
        logger.logEntries.enumerated().filter { _, entry in
            let category = LogCategory.categorize(entry)
            switch category {
            case .mouse: return showMouse
            case .keys: return showKeys
            case .comm: return showComm
            case .other: return showOther
            }
        }.map { ($0.offset, $0.element) }
    }

    var body: some View {
        VStack(spacing: 0) {
            // Toolbar with filters
            HStack {
                Text("Log (\(filteredEntries.count)/\(logger.logEntries.count))")
                    .font(.headline)
                Spacer()
                Toggle("Mouse", isOn: $showMouse)
                    .toggleStyle(.checkbox)
                Toggle("Keys", isOn: $showKeys)
                    .toggleStyle(.checkbox)
                Toggle("Comm", isOn: $showComm)
                    .toggleStyle(.checkbox)
                Toggle("Other", isOn: $showOther)
                    .toggleStyle(.checkbox)
                Divider()
                    .frame(height: 16)
                Toggle("Auto-scroll", isOn: $autoScroll)
                    .toggleStyle(.checkbox)
                Button("Clear") {
                    logger.clear()
                }
            }
            .padding(8)
            .background(Color(NSColor.windowBackgroundColor))

            Divider()

            // Log content
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 2) {
                        ForEach(filteredEntries, id: \.0) { index, entry in
                            Text(entry)
                                .font(.system(size: 14, design: .monospaced))
                                .textSelection(.enabled)
                                .id(index)
                        }
                    }
                    .padding(8)
                }
                .background(Color(NSColor.textBackgroundColor))
                .onChange(of: logger.logEntries.count) { _ in
                    if autoScroll, let last = filteredEntries.last {
                        withAnimation {
                            proxy.scrollTo(last.0, anchor: .bottom)
                        }
                    }
                }
            }
        }
        .frame(minWidth: 600, minHeight: 300)
    }
}

class LogWindowController: NSWindowController {
    static let shared = LogWindowController()

    private init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 700, height: 400),
            styleMask: [.titled, .closable, .resizable, .miniaturizable],
            backing: .buffered,
            defer: false
        )
        window.title = "softKM Log"
        window.contentView = NSHostingView(rootView: LogWindow())
        window.center()
        window.setFrameAutosaveName("LogWindow")

        super.init(window: window)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func toggle() {
        if let window = window {
            if window.isVisible {
                window.orderOut(nil)
            } else {
                window.makeKeyAndOrderFront(nil)
                NSApp.activate(ignoringOtherApps: true)
            }
        }
    }

    func show() {
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }
}
