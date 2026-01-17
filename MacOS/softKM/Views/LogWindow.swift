import SwiftUI

struct LogWindow: View {
    @ObservedObject var logger = Logger.shared
    @State private var autoScroll = true

    var body: some View {
        VStack(spacing: 0) {
            // Toolbar
            HStack {
                Text("Log (\(logger.logEntries.count) entries)")
                    .font(.headline)
                Spacer()
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
                        ForEach(Array(logger.logEntries.enumerated()), id: \.offset) { index, entry in
                            Text(entry)
                                .font(.system(.caption, design: .monospaced))
                                .textSelection(.enabled)
                                .id(index)
                        }
                    }
                    .padding(8)
                }
                .background(Color(NSColor.textBackgroundColor))
                .onChange(of: logger.logEntries.count) { _ in
                    if autoScroll, let lastIndex = logger.logEntries.indices.last {
                        withAnimation {
                            proxy.scrollTo(lastIndex, anchor: .bottom)
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
