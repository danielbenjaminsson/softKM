import SwiftUI
import AppKit

// NSTextView wrapper for selectable log text
struct SelectableLogView: NSViewRepresentable {
    let entries: [String]
    let autoScroll: Bool

    func makeNSView(context: Context) -> NSScrollView {
        let scrollView = NSScrollView()
        let textView = NSTextView()

        textView.isEditable = false
        textView.isSelectable = true
        textView.font = NSFont.monospacedSystemFont(ofSize: 14, weight: .regular)
        textView.backgroundColor = NSColor.textBackgroundColor
        textView.textContainerInset = NSSize(width: 8, height: 8)
        textView.autoresizingMask = [.width]
        textView.isVerticallyResizable = true
        textView.isHorizontallyResizable = false
        textView.textContainer?.widthTracksTextView = true

        scrollView.documentView = textView
        scrollView.hasVerticalScroller = true
        scrollView.hasHorizontalScroller = false
        scrollView.autohidesScrollers = true

        return scrollView
    }

    func updateNSView(_ scrollView: NSScrollView, context: Context) {
        guard let textView = scrollView.documentView as? NSTextView else { return }

        let newText = entries.joined(separator: "\n")
        let currentText = textView.string

        if newText != currentText {
            let wasAtBottom = isScrolledToBottom(scrollView)
            textView.string = newText

            if autoScroll && (wasAtBottom || entries.count <= 1) {
                DispatchQueue.main.async {
                    textView.scrollToEndOfDocument(nil)
                }
            }
        }
    }

    private func isScrolledToBottom(_ scrollView: NSScrollView) -> Bool {
        guard let documentView = scrollView.documentView else { return true }
        let visibleRect = scrollView.contentView.bounds
        let documentHeight = documentView.frame.height
        return visibleRect.maxY >= documentHeight - 50
    }
}

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

            // Log content - using NSTextView for proper text selection
            SelectableLogView(
                entries: filteredEntries.map { $0.1 },
                autoScroll: autoScroll
            )
        }
        .frame(minWidth: 600, minHeight: 300)
    }
}

class LogWindowController: NSWindowController, NSWindowDelegate, ObservableObject {
    static let shared = LogWindowController()

    @Published var isVisible: Bool = false

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

        window.delegate = self
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func toggle() {
        if let window = window {
            if window.isVisible {
                window.orderOut(nil)
                isVisible = false
            } else {
                window.makeKeyAndOrderFront(nil)
                NSApp.activate(ignoringOtherApps: true)
                isVisible = true
            }
        }
    }

    func show() {
        window?.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
        isVisible = true
    }

    func hide() {
        window?.orderOut(nil)
        isVisible = false
    }

    // MARK: - NSWindowDelegate

    func windowWillClose(_ notification: Notification) {
        isVisible = false
    }
}
