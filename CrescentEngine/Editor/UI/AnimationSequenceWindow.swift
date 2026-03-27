import SwiftUI
import Combine
import UniformTypeIdentifiers

struct AnimationSequenceWindow: View {
    @ObservedObject var editorState: EditorState
    @State private var clips: [AnimationClipInfo] = []
    @State private var selectedClipIndex: Int = 0
    @State private var events: [SequenceEvent] = []
    @State private var newEventType: String = "audio"
    @State private var newEventTag: String = ""
    @State private var newEventTime: Float = 0.0
    @State private var newEventPayload: String = ""
    @State private var showAudioImporter: Bool = false
    @State private var audioImportTargetIndex: Int? = nil
    @State private var activeUUID: String?
    @State private var isPreviewPlaying: Bool = false
    @State private var isPreviewLooping: Bool = true
    @State private var previewSpeed: Float = 1.0
    @State private var previewTime: Float = 0.0

    private let timer = Timer.publish(every: 0.8, on: .main, in: .common).autoconnect()
    private let previewTimer = Timer.publish(every: 1.0 / 30.0, on: .main, in: .common).autoconnect()
    private let audioTypes: [UTType] = {
        ["wav", "mp3", "ogg", "flac", "m4a", "aiff", "caf"].compactMap { UTType(filenameExtension: $0) }
    }()

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
        .onDisappear {
            CrescentEngineBridge.shared().setAnimationPreviewTarget(uuid: "")
        }
        .onReceive(timer) { _ in
            refreshEvents()
        }
        .onReceive(previewTimer) { _ in
            advancePreview()
        }
        .fileImporter(isPresented: $showAudioImporter, allowedContentTypes: audioTypes, allowsMultipleSelection: false) { result in
            handleAudioImport(result)
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

            previewViewport

            previewControls(uuid: uuid, duration: duration)

            TimelineView(events: events, duration: duration)
                .frame(height: 120)

            HStack(spacing: 8) {
                TextField("Type", text: $newEventType)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 90)
                    .font(EditorTheme.font(size: 10))

                TextField("Tag", text: $newEventTag)
                    .textFieldStyle(.roundedBorder)
                    .font(EditorTheme.font(size: 10))

                TextField("Time", value: $newEventTime, formatter: NumberFormatter.floatFormatter)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 80)
                    .font(EditorTheme.font(size: 10))

                TextField("Audio Clip Path", text: $newEventPayload)
                    .textFieldStyle(.roundedBorder)
                    .font(EditorTheme.font(size: 10))

                Button("Browse") {
                    audioImportTargetIndex = nil
                    showAudioImporter = true
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 10, weight: .semibold))

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
                        VStack(alignment: .leading, spacing: 8) {
                            HStack(spacing: 8) {
                                TextField("Type", text: Binding(
                                    get: { events[idx].eventType },
                                    set: { newVal in
                                        events[idx].eventType = newVal
                                        commitEvents(uuid: uuid)
                                    }
                                ))
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 90)
                                .font(EditorTheme.font(size: 10))

                                TextField("Tag", text: Binding(
                                    get: { events[idx].eventTag },
                                    set: { newVal in
                                        events[idx].eventTag = newVal
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

                            TextField("Audio Clip Path", text: Binding(
                                get: { events[idx].payload },
                                set: { newVal in
                                    events[idx].payload = newVal
                                    commitEvents(uuid: uuid)
                                }
                            ))
                            .textFieldStyle(.roundedBorder)
                            .font(EditorTheme.font(size: 10))

                            Button("Browse") {
                                audioImportTargetIndex = idx
                                showAudioImporter = true
                            }
                            .buttonStyle(.bordered)
                            .font(EditorTheme.font(size: 10, weight: .semibold))

                            HStack(spacing: 8) {
                                Text("Vol")
                                    .font(EditorTheme.font(size: 9, weight: .medium))
                                    .foregroundColor(EditorTheme.textMuted)

                                TextField("1.0", value: Binding(
                                    get: { events[idx].volume },
                                    set: { newVal in
                                        events[idx].volume = newVal
                                        commitEvents(uuid: uuid)
                                    }
                                ), formatter: NumberFormatter.floatFormatter)
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 70)
                                .font(EditorTheme.font(size: 10))

                                Text("Pitch")
                                    .font(EditorTheme.font(size: 9, weight: .medium))
                                    .foregroundColor(EditorTheme.textMuted)

                                TextField("Min", value: Binding(
                                    get: { events[idx].pitchMin },
                                    set: { newVal in
                                        events[idx].pitchMin = newVal
                                        commitEvents(uuid: uuid)
                                    }
                                ), formatter: NumberFormatter.floatFormatter)
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 70)
                                .font(EditorTheme.font(size: 10))

                                TextField("Max", value: Binding(
                                    get: { events[idx].pitchMax },
                                    set: { newVal in
                                        events[idx].pitchMax = newVal
                                        commitEvents(uuid: uuid)
                                    }
                                ), formatter: NumberFormatter.floatFormatter)
                                .textFieldStyle(.roundedBorder)
                                .frame(width: 70)
                                .font(EditorTheme.font(size: 10))

                                Toggle("Spatial", isOn: Binding(
                                    get: { events[idx].spatial },
                                    set: { newVal in
                                        events[idx].spatial = newVal
                                        commitEvents(uuid: uuid)
                                    }))
                                .font(EditorTheme.font(size: 9, weight: .medium))
                            }
                        }
                        .padding(8)
                        .background(EditorTheme.surface.opacity(0.45))
                        .overlay(RoundedRectangle(cornerRadius: 8).stroke(EditorTheme.panelStroke, lineWidth: 1))
                        .clipShape(RoundedRectangle(cornerRadius: 8))
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

    private var previewViewport: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Preview")
                .font(EditorTheme.font(size: 10, weight: .semibold))
                .foregroundColor(EditorTheme.textMuted)

            MetalView(
                viewKind: .preview,
                isActive: false,
                drivesLoop: false,
                onEngineReady: {
                    syncPreviewTarget()
                    if let uuid = activeUUID, !uuid.isEmpty {
                        pushPreviewState(uuid: uuid, timeOnly: false)
                    }
                }
            )
            .frame(height: 280)
            .background(Color.black.opacity(0.55))
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .overlay(
                RoundedRectangle(cornerRadius: 10)
                    .stroke(EditorTheme.panelStroke, lineWidth: 1)
            )
        }
    }

    private func refreshClips() {
        let previousUUID = activeUUID
        let previousClipSignature = clips.map { "\($0.index):\($0.name):\($0.duration)" }
        let uuid = editorState.selectedEntityUUIDs.first ?? ""
        activeUUID = uuid
        guard !uuid.isEmpty else {
            syncPreviewTarget()
            clips = []
            events = []
            return
        }
        if let info = CrescentEngineBridge.shared().getAnimationClipsInfo(uuid: uuid) as? [[String: Any]] {
            clips = info.compactMap { AnimationClipInfo(dict: $0) }
        } else {
            clips = []
        }
        let newClipSignature = clips.map { "\($0.index):\($0.name):\($0.duration)" }
        let shouldResetPreview = previousUUID != uuid || previousClipSignature != newClipSignature
        if selectedClipIndex >= clips.count {
            selectedClipIndex = 0
        }
        syncPreviewTarget()
        if shouldResetPreview {
            syncPreviewToSelectedClip()
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

    private func previewControls(uuid: String, duration: Float) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Button(isPreviewPlaying ? "Pause" : "Play") {
                    isPreviewPlaying.toggle()
                    pushPreviewState(uuid: uuid, timeOnly: false)
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 10, weight: .semibold))

                Button("Restart") {
                    previewTime = 0.0
                    pushPreviewState(uuid: uuid, timeOnly: false)
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 10, weight: .semibold))

                Toggle("Loop", isOn: Binding(
                    get: { isPreviewLooping },
                    set: { newVal in
                        isPreviewLooping = newVal
                        pushPreviewState(uuid: uuid, timeOnly: false)
                    }))
                .font(EditorTheme.font(size: 10, weight: .medium))
            }

            HStack(spacing: 12) {
                Text("Speed")
                    .font(EditorTheme.font(size: 10, weight: .medium))
                    .foregroundColor(EditorTheme.textMuted)
                    .frame(width: 42, alignment: .leading)

                Slider(value: Binding(
                    get: { Double(previewSpeed) },
                    set: { newVal in
                        previewSpeed = Float(newVal)
                        pushPreviewState(uuid: uuid, timeOnly: false)
                    }), in: 0.1...2.5)

                Text(String(format: "%.2fx", previewSpeed))
                    .font(EditorTheme.mono(size: 9))
                    .foregroundColor(EditorTheme.textMuted)
                    .frame(width: 42, alignment: .trailing)
            }

            HStack(spacing: 12) {
                Text("Time")
                    .font(EditorTheme.font(size: 10, weight: .medium))
                    .foregroundColor(EditorTheme.textMuted)
                    .frame(width: 42, alignment: .leading)

                Slider(value: Binding(
                    get: { Double(previewTime) },
                    set: { newVal in
                        previewTime = Float(newVal)
                        isPreviewPlaying = false
                        pushPreviewState(uuid: uuid, timeOnly: true)
                    }), in: 0.0...Double(max(duration, 0.01)))

                Text(String(format: "%.2f / %.2f", previewTime, duration))
                    .font(EditorTheme.mono(size: 9))
                    .foregroundColor(EditorTheme.textMuted)
                    .frame(width: 90, alignment: .trailing)
            }
        }
        .padding(10)
        .background(EditorTheme.surface.opacity(0.5))
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(EditorTheme.panelStroke, lineWidth: 1)
        )
        .clipShape(RoundedRectangle(cornerRadius: 8))
    }

    private func syncPreviewToSelectedClip() {
        previewTime = 0.0
        isPreviewPlaying = false
        guard let uuid = activeUUID, !uuid.isEmpty else { return }
        pushPreviewState(uuid: uuid, timeOnly: false)
    }

    private func syncPreviewTarget() {
        CrescentEngineBridge.shared().setAnimationPreviewTarget(uuid: activeUUID ?? "")
    }

    private func pushPreviewState(uuid: String, timeOnly: Bool) {
        let clipIdx = clips[safe: selectedClipIndex]?.index ?? 0
        var payload: [String: Any] = [
            "clipIndex": clipIdx,
            "time": previewTime
        ]
        if !timeOnly {
            payload["playing"] = isPreviewPlaying
            payload["looping"] = isPreviewLooping
            payload["speed"] = previewSpeed
        }
        CrescentEngineBridge.shared().setAnimationPreviewPlayback(info: payload)
    }

    private func isAudioEvent(_ event: SequenceEvent) -> Bool {
        let type = event.eventType.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        return type == "audio" || (!event.payload.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
    }

    private func triggerPreviewAudio(for event: SequenceEvent) {
        let rawPath = event.payload.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !rawPath.isEmpty else { return }

        let minPitch = max(0.01, min(event.pitchMin, event.pitchMax))
        let maxPitch = max(minPitch, max(event.pitchMin, event.pitchMax))
        let pitch = minPitch == maxPitch ? minPitch : Float.random(in: minPitch...maxPitch)
        let volume = max(0.0, event.volume)
        _ = CrescentEngineBridge.shared().playPreviewAudio(path: rawPath, volume: volume, pitch: pitch)
    }

    private func triggerPreviewAudioEvents(from startTime: Float,
                                           to endTime: Float,
                                           duration: Float,
                                           looped: Bool) {
        guard !events.isEmpty else { return }

        let epsilon: Float = 0.0001
        func fire(rangeStart: Float, rangeEnd: Float) {
            for event in events where isAudioEvent(event) {
                if event.time > rangeStart + epsilon && event.time <= rangeEnd + epsilon {
                    triggerPreviewAudio(for: event)
                }
            }
        }

        if looped {
            fire(rangeStart: startTime, rangeEnd: duration)
            fire(rangeStart: 0.0, rangeEnd: endTime)
        } else {
            fire(rangeStart: startTime, rangeEnd: endTime)
        }
    }

    private func advancePreview() {
        guard isPreviewPlaying, let uuid = activeUUID, !uuid.isEmpty else {
            return
        }
        let duration = clips[safe: selectedClipIndex]?.duration ?? 0.0
        guard duration > 0.0 else {
            return
        }

        let previousTime = previewTime
        previewTime += (1.0 / 30.0) * previewSpeed
        var looped = false
        if previewTime >= duration {
            if isPreviewLooping {
                previewTime.formTruncatingRemainder(dividingBy: duration)
                looped = true
            } else {
                previewTime = duration
                isPreviewPlaying = false
            }
        }
        triggerPreviewAudioEvents(from: previousTime, to: previewTime, duration: duration, looped: looped)
        pushPreviewState(uuid: uuid, timeOnly: true)
    }

    private func commitEvents(uuid: String) {
        let clipIdx = clips[safe: selectedClipIndex]?.index ?? 0
        let payload = events.map {
            [
                "name": $0.eventTag,
                "eventType": $0.eventType,
                "eventTag": $0.eventTag,
                "time": $0.time,
                "payload": $0.payload,
                "volume": $0.volume,
                "pitchMin": $0.pitchMin,
                "pitchMax": $0.pitchMax,
                "spatial": $0.spatial
            ] as [String : Any]
        }
        _ = CrescentEngineBridge.shared().setAnimationEvents(uuid: uuid, clipIndex: clipIdx, events: payload)
    }

    private func addEvent(uuid: String, duration: Float) {
        let trimmedType = newEventType.trimmingCharacters(in: .whitespacesAndNewlines)
        let trimmedTag = newEventTag.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedTag.isEmpty else { return }
        let t = max(0.0, min(newEventTime, duration))
        events.append(SequenceEvent(
            eventType: trimmedType.isEmpty ? "audio" : trimmedType,
            eventTag: trimmedTag,
            time: t,
            payload: newEventPayload
        ))
        events.sort { $0.time < $1.time }
        commitEvents(uuid: uuid)
        newEventType = "audio"
        newEventTag = ""
        newEventPayload = ""
    }

    private func handleAudioImport(_ result: Result<[URL], Error>) {
        guard case .success(let urls) = result, let url = urls.first else { return }
        applyAudioURL(url)
    }

    private func applyAudioURL(_ url: URL) {
        var resolvedURL = url
        let accessed = resolvedURL.startAccessingSecurityScopedResource()
        if accessed {
            resolvedURL = resolvedURL.standardizedFileURL
        }

        defer {
            if accessed {
                resolvedURL.stopAccessingSecurityScopedResource()
            }
        }

        var path = resolvedURL.path
        let imported = CrescentEngineBridge.shared().importAsset(path: path, type: "audio")
        if !imported.isEmpty {
            path = imported
        }

        if let idx = audioImportTargetIndex, events.indices.contains(idx) {
            events[idx].payload = path
            if let uuid = activeUUID, !uuid.isEmpty {
                commitEvents(uuid: uuid)
            }
        } else {
            newEventPayload = path
        }
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
                        Text(evt.eventTag)
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
    var eventType: String
    var eventTag: String
    var time: Float
    var payload: String
    var volume: Float
    var pitchMin: Float
    var pitchMax: Float
    var spatial: Bool

    init(eventType: String,
         eventTag: String,
         time: Float,
         payload: String = "",
         volume: Float = 1.0,
         pitchMin: Float = 1.0,
         pitchMax: Float = 1.0,
         spatial: Bool = true) {
        self.eventType = eventType
        self.eventTag = eventTag
        self.time = time
        self.payload = payload
        self.volume = volume
        self.pitchMin = pitchMin
        self.pitchMax = pitchMax
        self.spatial = spatial
    }

    init?(dict: [String: Any]) {
        let tag = (dict["eventTag"] as? String) ?? (dict["name"] as? String) ?? ""
        guard !tag.isEmpty else { return nil }
        let time = (dict["time"] as? NSNumber)?.floatValue ?? 0.0
        self.eventType = (dict["eventType"] as? String) ?? ((dict["payload"] as? String)?.isEmpty == false ? "audio" : "")
        self.eventTag = tag
        self.time = time
        self.payload = dict["payload"] as? String ?? ""
        self.volume = (dict["volume"] as? NSNumber)?.floatValue ?? 1.0
        self.pitchMin = (dict["pitchMin"] as? NSNumber)?.floatValue ?? 1.0
        self.pitchMax = (dict["pitchMax"] as? NSNumber)?.floatValue ?? 1.0
        self.spatial = (dict["spatial"] as? NSNumber)?.boolValue ?? true
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
