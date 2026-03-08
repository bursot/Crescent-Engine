import SwiftUI
import Combine

struct AnimationSequenceWindow: View {
    @ObservedObject var editorState: EditorState
    @State private var clips: [AnimationClipInfo] = []
    @State private var selectedClipIndex: Int = 0
    @State private var events: [SequenceEvent] = []
    @State private var newEventName: String = ""
    @State private var newEventTime: Float = 0.0
    @State private var activeUUID: String?

    private let timer = Timer.publish(every: 0.8, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            LinearGradient(
                colors: [EditorTheme.backgroundTop, EditorTheme.backgroundBottom],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()

            VStack(alignment: .leading, spacing: 12) {
                header

                if let uuid = activeUUID, !uuid.isEmpty, !clips.isEmpty {
                    HStack(spacing: 16) {
                        clipList
                        timelinePanel(uuid: uuid)
                    }
                } else {
                    SequenceEmptyState(
                        title: "No Animation Clips",
                        subtitle: "Select an animated object to edit its clips."
                    )
                }
            }
            .padding(16)
        }
        .environment(\.colorScheme, .dark)
        .frame(minWidth: 860, minHeight: 620)
        .onAppear {
            refreshClips()
        }
        .onChange(of: editorState.selectedEntityUUIDs) { _ in
            refreshClips()
        }
        .onReceive(timer) { _ in
            refreshClips()
        }
    }

    private var header: some View {
        HStack {
            Label("Animation Sequence", systemImage: "film")
                .font(EditorTheme.font(size: 14, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)
            Spacer()
        }
    }

    private var clipList: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Clips")
                .font(EditorTheme.font(size: 11, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)

            ScrollView {
                VStack(alignment: .leading, spacing: 6) {
                    ForEach(clips.indices, id: \.self) { idx in
                        let clip = clips[idx]
                        Button(action: {
                            selectedClipIndex = idx
                            refreshEvents()
                        }) {
                            HStack {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(clip.name)
                                        .font(EditorTheme.font(size: 11, weight: .medium))
                                        .foregroundColor(EditorTheme.textPrimary)
                                    Text(String(format: "%.2fs", clip.duration))
                                        .font(EditorTheme.mono(size: 9))
                                        .foregroundColor(EditorTheme.textMuted)
                                }
                                Spacer()
                            }
                            .padding(.vertical, 6)
                            .padding(.horizontal, 8)
                            .background(selectedClipIndex == idx ? Color.teal.opacity(0.2) : Color.clear)
                            .cornerRadius(8)
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
        }
        .frame(maxWidth: 260)
        .padding(10)
        .background(EditorTheme.panelBackground)
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(EditorTheme.panelStroke, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private func timelinePanel(uuid: String) -> some View {
        let clip = clips[safe: selectedClipIndex]
        let duration = clip?.duration ?? 0.0

        return VStack(alignment: .leading, spacing: 12) {
            Text(clip?.name ?? "Clip")
                .font(EditorTheme.font(size: 12, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)

            TimelineView(events: events, duration: duration)
                .frame(height: 120)

            HStack(spacing: 8) {
                TextField("Event name", text: $newEventName)
                    .textFieldStyle(.roundedBorder)
                    .font(EditorTheme.font(size: 10))

                TextField("Time", value: $newEventTime, formatter: NumberFormatter.floatFormatter)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 80)
                    .font(EditorTheme.font(size: 10))

                Button("Add") {
                    addEvent(uuid: uuid, duration: duration)
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 10, weight: .semibold))
            }

            if events.isEmpty {
                Text("No notify events yet.")
                    .font(EditorTheme.font(size: 10))
                    .foregroundColor(EditorTheme.textMuted)
            } else {
                VStack(alignment: .leading, spacing: 6) {
                    ForEach(events.indices, id: \.self) { idx in
                        HStack(spacing: 8) {
                            TextField("Name", text: Binding(
                                get: { events[idx].name },
                                set: { newVal in
                                    events[idx].name = newVal
                                    commitEvents(uuid: uuid)
                                }
                            ))
                            .textFieldStyle(.roundedBorder)
                            .font(EditorTheme.font(size: 10))

                            TextField("Time", value: Binding(
                                get: { events[idx].time },
                                set: { newVal in
                                    events[idx].time = newVal
                                    commitEvents(uuid: uuid)
                                }
                            ), formatter: NumberFormatter.floatFormatter)
                            .textFieldStyle(.roundedBorder)
                            .frame(width: 80)
                            .font(EditorTheme.font(size: 10))

                            Button(action: {
                                events.remove(at: idx)
                                commitEvents(uuid: uuid)
                            }) {
                                Image(systemName: "trash")
                            }
                            .buttonStyle(.borderless)
                        }
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(EditorTheme.panelBackground)
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(EditorTheme.panelStroke, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private func refreshClips() {
        let uuid = editorState.selectedEntityUUIDs.first ?? ""
        activeUUID = uuid
        guard !uuid.isEmpty else {
            clips = []
            events = []
            return
        }
        if let info = CrescentEngineBridge.shared().getAnimationClipsInfo(uuid: uuid) as? [[String: Any]] {
            clips = info.compactMap { AnimationClipInfo(dict: $0) }
        } else {
            clips = []
        }
        if selectedClipIndex >= clips.count {
            selectedClipIndex = 0
        }
        refreshEvents()
    }

    private func refreshEvents() {
        guard let uuid = activeUUID, !uuid.isEmpty else {
            events = []
            return
        }
        let clipIdx = clips[safe: selectedClipIndex]?.index ?? 0
        if let list = CrescentEngineBridge.shared().getAnimationEvents(uuid: uuid, clipIndex: clipIdx) as? [[String: Any]] {
            events = list.compactMap { SequenceEvent(dict: $0) }.sorted { $0.time < $1.time }
            if newEventTime == 0.0, let duration = clips[safe: selectedClipIndex]?.duration {
                newEventTime = min(0.1, duration)
            }
        }
    }

    private func commitEvents(uuid: String) {
        let clipIdx = clips[safe: selectedClipIndex]?.index ?? 0
        let payload = events.map { ["name": $0.name, "time": $0.time] }
        _ = CrescentEngineBridge.shared().setAnimationEvents(uuid: uuid, clipIndex: clipIdx, events: payload)
    }

    private func addEvent(uuid: String, duration: Float) {
        let trimmed = newEventName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        let t = max(0.0, min(newEventTime, duration))
        events.append(SequenceEvent(name: trimmed, time: t))
        events.sort { $0.time < $1.time }
        commitEvents(uuid: uuid)
        newEventName = ""
    }
}

private struct TimelineView: View {
    let events: [SequenceEvent]
    let duration: Float

    var body: some View {
        GeometryReader { geo in
            let width = max(geo.size.width, 1)
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 8)
                    .fill(EditorTheme.surface)
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(EditorTheme.panelStroke, lineWidth: 1)
                    )
                Rectangle()
                    .fill(Color.white.opacity(0.08))
                    .frame(height: 2)
                    .padding(.horizontal, 12)
                    .position(x: width / 2, y: geo.size.height / 2)

                ForEach(events) { evt in
                    let t = duration > 0 ? CGFloat(evt.time / duration) : 0
                    let x = 12 + t * (width - 24)
                    VStack(spacing: 4) {
                        Circle()
                            .fill(Color.orange)
                            .frame(width: 8, height: 8)
                        Text(evt.name)
                            .font(EditorTheme.font(size: 8))
                            .foregroundColor(EditorTheme.textMuted)
                    }
                    .position(x: x, y: geo.size.height / 2 - 10)
                }
            }
        }
    }
}

private struct AnimationClipInfo: Identifiable {
    let id = UUID()
    let index: Int
    let name: String
    let duration: Float

    init?(dict: [String: Any]) {
        guard let index = (dict["index"] as? NSNumber)?.intValue else { return nil }
        let name = dict["name"] as? String ?? "Clip"
        let duration = (dict["duration"] as? NSNumber)?.floatValue ?? 0.0
        self.index = index
        self.name = name
        self.duration = duration
    }
}

private struct SequenceEvent: Identifiable {
    let id = UUID()
    var name: String
    var time: Float

    init(name: String, time: Float) {
        self.name = name
        self.time = time
    }

    init?(dict: [String: Any]) {
        guard let name = dict["name"] as? String else { return nil }
        let time = (dict["time"] as? NSNumber)?.floatValue ?? 0.0
        self.name = name
        self.time = time
    }
}

private struct SequenceEmptyState: View {
    let title: String
    let subtitle: String

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: "film")
                .font(EditorTheme.font(size: 40, weight: .semibold))
                .foregroundColor(EditorTheme.textMuted)
            Text(title)
                .font(EditorTheme.font(size: 14, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)
            Text(subtitle)
                .font(EditorTheme.font(size: 11))
                .foregroundColor(EditorTheme.textMuted)
        }
        .padding(20)
        .background(EditorTheme.panelBackground)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}
