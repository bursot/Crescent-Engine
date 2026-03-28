import SwiftUI
import Combine
import UniformTypeIdentifiers
import AppKit

struct AnimationSequenceWindow: View {
    @ObservedObject var editorState: EditorState
    @State private var clips: [AnimationClipInfo] = []
    @State private var selectedClipIndex: Int = 0
    @State private var events: [SequenceEvent] = []
    @State private var newEventType: String = "audio"
    @State private var newEventTag: String = ""
    @State private var newEventTimeText: String = "0.0"
    @State private var newEventPayload: String = ""
    @State private var showAudioImporter: Bool = false
    @State private var audioImportTargetIndex: Int? = nil
    @State private var audioDurationCache: [String: Float] = [:]
    @State private var selectedEventIndex: Int? = nil
    @State private var draftEventType: String = "audio"
    @State private var draftEventTag: String = ""
    @State private var draftEventTimeText: String = "0.0"
    @State private var draftEventPayload: String = ""
    @State private var draftVolumeText: String = "1.0"
    @State private var draftPitchMinText: String = "1.0"
    @State private var draftPitchMaxText: String = "1.0"
    @State private var draftSpatial: Bool = true
    @State private var activeUUID: String?
    @State private var isPreviewPlaying: Bool = false
    @State private var isPreviewLooping: Bool = true
    @State private var previewSpeed: Float = 1.0
    @State private var previewTime: Float = 0.0
    @State private var isEditingText: Bool = false

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
        .onReceive(previewTimer) { _ in
            guard !isEditingText else { return }
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
                            syncPreviewToSelectedClip()
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

            TimelineView(events: events, duration: duration, currentTime: previewTime)
                .frame(height: 120)

            HStack(spacing: 8) {
                Button("Add Event...") {
                    presentAddEventDialog(uuid: uuid, duration: duration)
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 10, weight: .semibold))

                Button("Save Events") {
                    commitEvents(uuid: uuid)
                }
                .buttonStyle(.borderedProminent)
                .font(EditorTheme.font(size: 10, weight: .semibold))
            }

            if events.isEmpty {
                Text("No notify events yet.")
                    .font(EditorTheme.font(size: 10))
                    .foregroundColor(EditorTheme.textMuted)
            } else {
                VStack(alignment: .leading, spacing: 6) {
                    ForEach(events.indices, id: \.self) { idx in
                        eventSummaryRow(index: idx, uuid: uuid)
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

    private func eventSummaryRow(index: Int, uuid: String) -> some View {
        let event = events[index]
        let selected = selectedEventIndex == index
        let payloadLabel = event.payload.isEmpty ? "No audio clip" : URL(fileURLWithPath: event.payload).lastPathComponent
        return VStack(alignment: .leading, spacing: 8) {
            HStack {
                Button {
                    loadDraft(from: index)
                } label: {
                    VStack(alignment: .leading, spacing: 4) {
                        HStack(spacing: 8) {
                            Text(event.eventTag.isEmpty ? "(no tag)" : event.eventTag)
                                .font(EditorTheme.font(size: 10, weight: .semibold))
                                .foregroundColor(EditorTheme.textPrimary)
                            Text(event.eventType.isEmpty ? "-" : event.eventType)
                                .font(EditorTheme.mono(size: 8))
                                .foregroundColor(EditorTheme.textMuted)
                            Text(String(format: "%.3fs", event.time))
                                .font(EditorTheme.mono(size: 8))
                                .foregroundColor(EditorTheme.textMuted)
                        }
                        Text(payloadLabel)
                            .font(EditorTheme.font(size: 9))
                            .foregroundColor(EditorTheme.textMuted)
                            .lineLimit(1)
                    }
                    Spacer()
                }
                .buttonStyle(.plain)

                if selected {
                    Text("Selected")
                        .font(EditorTheme.font(size: 8, weight: .semibold))
                        .foregroundColor(.teal)
                }

                Button("Preview") {
                    triggerPreviewAudio(for: event)
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 9, weight: .semibold))

                Button("Edit...") {
                    presentEditEventDialog(index: index, uuid: uuid)
                }
                .buttonStyle(.borderedProminent)
                .font(EditorTheme.font(size: 9, weight: .semibold))

                Button(role: .destructive) {
                    events.remove(at: index)
                    if selectedEventIndex == index {
                        clearDraft()
                    } else if let selectedEventIndex, index < selectedEventIndex {
                        self.selectedEventIndex = selectedEventIndex - 1
                    }
                    commitEvents(uuid: uuid)
                } label: {
                    Image(systemName: "trash")
                }
                .buttonStyle(.borderless)
            }
        }
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
            .allowsHitTesting(false)
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
            clearDraft()
            return
        }
        let clipIdx = clips[safe: selectedClipIndex]?.index ?? 0
        if let list = CrescentEngineBridge.shared().getAnimationEvents(uuid: uuid, clipIndex: clipIdx) as? [[String: Any]] {
            events = list.compactMap { SequenceEvent(dict: $0) }.sorted { $0.time < $1.time }
            hydrateAudioDurations()
            if newEventTimeText == "0.0", let duration = clips[safe: selectedClipIndex]?.duration {
                newEventTimeText = String(format: "%.3f", min(0.1, duration))
            }
            if let selectedEventIndex, events.indices.contains(selectedEventIndex) {
                loadDraft(from: selectedEventIndex)
            } else {
                clearDraft()
            }
        }
    }

    private func handleEditingChanged(_ editing: Bool) {
        isEditingText = editing
        if editing {
            isPreviewPlaying = false
        }
    }

    private func presentAddEventDialog(uuid: String, duration: Float) {
        let seed = SequenceEvent(
            eventType: newEventType,
            eventTag: newEventTag,
            time: clampedTime(from: newEventTimeText, duration: duration),
            payload: newEventPayload,
            volume: 1.0,
            pitchMin: 1.0,
            pitchMax: 1.0,
            spatial: true,
            audioDuration: resolvedAudioDuration(for: newEventPayload)
        )
        guard let edited = AnimationEventEditorDialog.runModal(
            title: "Add Animation Event",
            initial: seed,
            duration: duration
        ) else { return }

        events.append(edited)
        events.sort { $0.time < $1.time }
        if let idx = events.firstIndex(where: {
            $0.eventType == edited.eventType &&
            $0.eventTag == edited.eventTag &&
            abs($0.time - edited.time) < 0.0001 &&
            $0.payload == edited.payload
        }) {
            selectedEventIndex = idx
        }
        commitEvents(uuid: uuid)
    }

    private func presentEditEventDialog(index: Int, uuid: String) {
        guard events.indices.contains(index) else { return }
        let current = events[index]
        guard let edited = AnimationEventEditorDialog.runModal(
            title: "Edit Animation Event",
            initial: current,
            duration: clips[safe: selectedClipIndex]?.duration ?? current.time
        ) else { return }

        events[index] = edited
        events.sort { $0.time < $1.time }
        if let idx = events.firstIndex(where: {
            $0.eventType == edited.eventType &&
            $0.eventTag == edited.eventTag &&
            abs($0.time - edited.time) < 0.0001 &&
            $0.payload == edited.payload
        }) {
            selectedEventIndex = idx
        }
        commitEvents(uuid: uuid)
    }

    private func previewControls(uuid: String, duration: Float) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 8) {
                Button(isPreviewPlaying ? "Pause" : "Play") {
                    isPreviewPlaying.toggle()
                    if isPreviewPlaying {
                        triggerPreviewAudioEvents(from: previewTime - 0.0001,
                                                  to: previewTime,
                                                  duration: duration,
                                                  looped: false)
                    }
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
                        let previousTime = previewTime
                        previewTime = Float(newVal)
                        isPreviewPlaying = false
                        triggerPreviewAudioEvents(from: min(previousTime, previewTime),
                                                  to: max(previousTime, previewTime),
                                                  duration: duration,
                                                  looped: false)
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

    private func resolvedAudioDuration(for rawPath: String) -> Float? {
        let trimmed = rawPath.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return nil }
        if let cached = audioDurationCache[trimmed] {
            return cached > 0.0 ? cached : nil
        }
        let duration = CrescentEngineBridge.shared().getPreviewAudioDuration(path: trimmed)
        audioDurationCache[trimmed] = duration
        return duration > 0.0 ? duration : nil
    }

    private func hydrateAudioDurations() {
        for idx in events.indices {
            refreshAudioDuration(for: idx)
        }
    }

    private func refreshAudioDuration(for index: Int) {
        guard events.indices.contains(index) else { return }
        events[index].audioDuration = resolvedAudioDuration(for: events[index].payload)
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
        persistEventBackup(uuid: uuid, clipIndex: clipIdx, payload: payload)
    }

    private func persistEventBackup(uuid: String, clipIndex: Int, payload: [[String: Any]]) {
        guard let projectRoot = editorState.projectRootURL else { return }
        let backupDir = projectRoot
            .appendingPathComponent("Library", isDirectory: true)
            .appendingPathComponent("AnimationEventBackups", isDirectory: true)
        do {
            try FileManager.default.createDirectory(at: backupDir, withIntermediateDirectories: true)
            let clipName = clips[safe: selectedClipIndex]?.name ?? "clip_\(clipIndex)"
            let safeClipName = clipName.replacingOccurrences(of: "/", with: "_").replacingOccurrences(of: ":", with: "_")
            let url = backupDir.appendingPathComponent("\(uuid)_\(safeClipName).json")
            let root: [String: Any] = [
                "entityUUID": uuid,
                "clipIndex": clipIndex,
                "clipName": clipName,
                "events": payload
            ]
            let data = try JSONSerialization.data(withJSONObject: root, options: [.prettyPrinted, .sortedKeys])
            try data.write(to: url, options: .atomic)
        } catch {
            print("[AnimationSequence] Failed to write event backup: \(error)")
        }
    }

    private func addEvent(uuid: String, duration: Float) {
        let trimmedType = newEventType.trimmingCharacters(in: .whitespacesAndNewlines)
        let trimmedTag = newEventTag.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedTag.isEmpty else { return }
        let t = clampedTime(from: newEventTimeText, duration: duration)
        events.append(SequenceEvent(
            eventType: trimmedType.isEmpty ? "audio" : trimmedType,
            eventTag: trimmedTag,
            time: t,
            payload: newEventPayload,
            audioDuration: resolvedAudioDuration(for: newEventPayload)
        ))
        events.sort { $0.time < $1.time }
        if let idx = events.firstIndex(where: { $0.eventTag == trimmedTag && abs($0.time - t) < 0.0001 && $0.payload == newEventPayload }) {
            loadDraft(from: idx)
        }
        commitEvents(uuid: uuid)
        newEventType = "audio"
        newEventTag = ""
        newEventTimeText = String(format: "%.3f", min(0.1, duration))
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
            refreshAudioDuration(for: idx)
            if selectedEventIndex == idx {
                draftEventPayload = path
            }
            if let uuid = activeUUID, !uuid.isEmpty {
                commitEvents(uuid: uuid)
            }
        } else {
            newEventPayload = path
        }
    }

    private func clampedTime(from text: String, duration: Float) -> Float {
        let parsed = Float(text.replacingOccurrences(of: ",", with: ".")) ?? 0.0
        return max(0.0, min(parsed, duration))
    }

    private func parsedFloat(_ text: String, fallback: Float) -> Float {
        Float(text.replacingOccurrences(of: ",", with: ".")) ?? fallback
    }

    private func loadDraft(from index: Int) {
        guard events.indices.contains(index) else { return }
        isEditingText = false
        isPreviewPlaying = false
        let event = events[index]
        selectedEventIndex = index
        draftEventType = event.eventType
        draftEventTag = event.eventTag
        draftEventTimeText = String(format: "%.3f", event.time)
        draftEventPayload = event.payload
        draftVolumeText = String(format: "%.2f", event.volume)
        draftPitchMinText = String(format: "%.2f", event.pitchMin)
        draftPitchMaxText = String(format: "%.2f", event.pitchMax)
        draftSpatial = event.spatial
    }

    private func clearDraft() {
        isEditingText = false
        selectedEventIndex = nil
        draftEventType = "audio"
        draftEventTag = ""
        draftEventTimeText = "0.0"
        draftEventPayload = ""
        draftVolumeText = "1.0"
        draftPitchMinText = "1.0"
        draftPitchMaxText = "1.0"
        draftSpatial = true
    }

    private func applyDraftToSelectedEvent(uuid: String, duration: Float) {
        guard let selectedEventIndex, events.indices.contains(selectedEventIndex) else { return }
        isEditingText = false
        events[selectedEventIndex].eventType = draftEventType.trimmingCharacters(in: .whitespacesAndNewlines)
        events[selectedEventIndex].eventTag = draftEventTag.trimmingCharacters(in: .whitespacesAndNewlines)
        events[selectedEventIndex].time = clampedTime(from: draftEventTimeText, duration: duration)
        events[selectedEventIndex].payload = draftEventPayload.trimmingCharacters(in: .whitespacesAndNewlines)
        events[selectedEventIndex].volume = max(0.0, parsedFloat(draftVolumeText, fallback: 1.0))
        let pitchMin = max(0.01, parsedFloat(draftPitchMinText, fallback: 1.0))
        let pitchMax = max(pitchMin, parsedFloat(draftPitchMaxText, fallback: pitchMin))
        events[selectedEventIndex].pitchMin = pitchMin
        events[selectedEventIndex].pitchMax = pitchMax
        events[selectedEventIndex].spatial = draftSpatial
        events[selectedEventIndex].audioDuration = resolvedAudioDuration(for: events[selectedEventIndex].payload)
        let updatedEvent = events[selectedEventIndex]
        events.sort { $0.time < $1.time }
        if let newIndex = events.firstIndex(where: {
            $0.eventType == updatedEvent.eventType &&
            $0.eventTag == updatedEvent.eventTag &&
            abs($0.time - updatedEvent.time) < 0.0001 &&
            $0.payload == updatedEvent.payload
        }) {
            loadDraft(from: newIndex)
        }
        commitEvents(uuid: uuid)
    }
}

private struct TimelineView: View {
    let events: [SequenceEvent]
    let duration: Float
    let currentTime: Float

    var body: some View {
        GeometryReader { geo in
            let width = max(geo.size.width, 1)
            let innerWidth = max(width - 24, 1)
            let playheadRatio = duration > 0 ? CGFloat(min(max(currentTime / duration, 0.0), 1.0)) : 0
            let playheadX = 12 + playheadRatio * innerWidth
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 8)
                    .fill(EditorTheme.surface)
                    .overlay(
                        RoundedRectangle(cornerRadius: 8)
                            .stroke(EditorTheme.panelStroke, lineWidth: 1)
                    )

                ForEach(0..<5, id: \.self) { tick in
                    let ratio = CGFloat(tick) / 4.0
                    let x = 12 + ratio * innerWidth
                    Path { path in
                        path.move(to: CGPoint(x: x, y: 16))
                        path.addLine(to: CGPoint(x: x, y: geo.size.height - 16))
                    }
                    .stroke(Color.white.opacity(0.06), lineWidth: 1)

                    Text(String(format: "%.2f", duration * Float(ratio)))
                        .font(EditorTheme.mono(size: 8))
                        .foregroundColor(EditorTheme.textMuted)
                        .position(x: min(max(x, 24), width - 24), y: geo.size.height - 10)
                }

                Rectangle()
                    .fill(Color.white.opacity(0.08))
                    .frame(height: 2)
                    .padding(.horizontal, 12)
                    .position(x: width / 2, y: 34)

                Text("Audio")
                    .font(EditorTheme.font(size: 9, weight: .semibold))
                    .foregroundColor(EditorTheme.textMuted)
                    .position(x: 34, y: 62)

                ForEach(events) { evt in
                    if evt.isAudio {
                        let audioDuration = max(evt.audioDuration ?? 0.18, 0.18)
                        let startRatio = duration > 0 ? CGFloat(min(max(evt.time / duration, 0.0), 1.0)) : 0
                        let endRatio = duration > 0 ? CGFloat(min(max((evt.time + audioDuration) / duration, 0.0), 1.0)) : startRatio
                        let blockX = 12 + startRatio * innerWidth
                        let blockWidth = max((endRatio - startRatio) * innerWidth, 6)
                        let label = evt.payload.isEmpty ? evt.eventTag : URL(fileURLWithPath: evt.payload).lastPathComponent
                        RoundedRectangle(cornerRadius: 6)
                            .fill(Color.teal.opacity(0.35))
                            .overlay(
                                RoundedRectangle(cornerRadius: 6)
                                    .stroke(Color.teal.opacity(0.85), lineWidth: 1)
                            )
                            .frame(width: blockWidth, height: 22)
                            .position(x: blockX + blockWidth / 2, y: 62)

                        Text(label)
                            .font(EditorTheme.font(size: 8, weight: .medium))
                            .foregroundColor(EditorTheme.textPrimary)
                            .lineLimit(1)
                            .frame(width: max(blockWidth - 8, 10), alignment: .leading)
                            .position(x: blockX + blockWidth / 2, y: 62)
                    }
                }

                ForEach(events) { evt in
                    let t = duration > 0 ? CGFloat(evt.time / duration) : 0
                    let x = 12 + t * innerWidth
                    VStack(spacing: 4) {
                        Circle()
                            .fill(Color.orange)
                            .frame(width: 8, height: 8)
                        Text(evt.eventTag)
                            .font(EditorTheme.font(size: 8))
                            .foregroundColor(EditorTheme.textMuted)
                    }
                    .position(x: x, y: 26)
                }

                Rectangle()
                    .fill(Color.orange.opacity(0.9))
                    .frame(width: 2, height: geo.size.height - 18)
                    .position(x: playheadX, y: (geo.size.height - 4) / 2)
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
    var audioDuration: Float?

    init(eventType: String,
         eventTag: String,
         time: Float,
         payload: String = "",
         volume: Float = 1.0,
         pitchMin: Float = 1.0,
         pitchMax: Float = 1.0,
         spatial: Bool = true,
         audioDuration: Float? = nil) {
        self.eventType = eventType
        self.eventTag = eventTag
        self.time = time
        self.payload = payload
        self.volume = volume
        self.pitchMin = pitchMin
        self.pitchMax = pitchMax
        self.spatial = spatial
        self.audioDuration = audioDuration
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
        self.audioDuration = nil
    }

    var isAudio: Bool {
        let type = eventType.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        return type == "audio" || !payload.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
    }
}

private enum AnimationEventEditorDialog {
    static func runModal(title: String,
                         initial: SequenceEvent,
                         duration: Float) -> SequenceEvent? {
        let previousKeyWindow = NSApp.keyWindow
        let typeField = NSTextField(string: initial.eventType)
        let tagField = NSTextField(string: initial.eventTag)
        let timeField = NSTextField(string: String(format: "%.3f", initial.time))
        let payloadField = NSTextField(string: initial.payload)
        let volumeField = NSTextField(string: String(format: "%.2f", initial.volume))
        let pitchMinField = NSTextField(string: String(format: "%.2f", initial.pitchMin))
        let pitchMaxField = NSTextField(string: String(format: "%.2f", initial.pitchMax))
        let spatialToggle = NSButton(checkboxWithTitle: "Spatial", target: nil, action: nil)
        spatialToggle.state = initial.spatial ? .on : .off

        let panelBackground = NSColor(calibratedRed: 0.08, green: 0.10, blue: 0.16, alpha: 1.0)
        let fieldBackground = NSColor(calibratedRed: 0.13, green: 0.16, blue: 0.24, alpha: 1.0)
        let fieldBorder = NSColor(calibratedRed: 0.22, green: 0.27, blue: 0.38, alpha: 1.0)
        let primaryText = NSColor(calibratedWhite: 0.95, alpha: 1.0)
        let secondaryText = NSColor(calibratedWhite: 0.72, alpha: 1.0)

        func styleField(_ field: NSTextField) {
            field.isBordered = true
            field.isBezeled = true
            field.bezelStyle = .roundedBezel
            field.drawsBackground = true
            field.backgroundColor = fieldBackground
            field.textColor = primaryText
            field.focusRingType = .none
            field.font = NSFont.monospacedSystemFont(ofSize: 12, weight: .regular)
            field.wantsLayer = true
            field.layer?.cornerRadius = 6
            field.layer?.borderWidth = 1
            field.layer?.borderColor = fieldBorder.cgColor
        }

        [typeField, tagField, timeField, payloadField, volumeField, pitchMinField, pitchMaxField].forEach(styleField)
        spatialToggle.contentTintColor = primaryText

        let chooseAudioButton = NSButton(title: "Browse Audio...", target: nil, action: nil)
        chooseAudioButton.bezelStyle = .rounded
        chooseAudioButton.contentTintColor = primaryText
        chooseAudioButton.action = #selector(AudioBrowseTarget.chooseAudio(_:))

        let content = NSStackView()
        content.orientation = .vertical
        content.spacing = 10
        content.edgeInsets = NSEdgeInsets(top: 18, left: 18, bottom: 18, right: 18)
        content.translatesAutoresizingMaskIntoConstraints = false

        func row(_ label: String, _ control: NSView) -> NSView {
            let row = NSStackView()
            row.orientation = .horizontal
            row.spacing = 8
            let text = NSTextField(labelWithString: label)
            text.alignment = .right
            text.frame.size.width = 92
            text.font = NSFont.systemFont(ofSize: 12, weight: .semibold)
            text.textColor = secondaryText
            control.translatesAutoresizingMaskIntoConstraints = false
            control.setContentHuggingPriority(.defaultLow, for: .horizontal)
            control.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
            control.widthAnchor.constraint(greaterThanOrEqualToConstant: 280).isActive = true
            row.addArrangedSubview(text)
            row.addArrangedSubview(control)
            return row
        }

        let browseTarget = AudioBrowseTarget(payloadField: payloadField)
        chooseAudioButton.target = browseTarget

        let titleLabel = NSTextField(labelWithString: title)
        titleLabel.font = NSFont.systemFont(ofSize: 15, weight: .bold)
        titleLabel.textColor = primaryText
        let subtitleLabel = NSTextField(wrappingLabelWithString: "Edit event data here.")
        subtitleLabel.font = NSFont.systemFont(ofSize: 12)
        subtitleLabel.textColor = secondaryText
        content.addArrangedSubview(titleLabel)
        content.addArrangedSubview(subtitleLabel)
        content.addArrangedSubview(row("Type", typeField))
        content.addArrangedSubview(row("Tag", tagField))
        content.addArrangedSubview(row("Time", timeField))
        content.addArrangedSubview(row("Audio", payloadField))
        content.addArrangedSubview(row("", chooseAudioButton))
        content.addArrangedSubview(row("Volume", volumeField))
        content.addArrangedSubview(row("Pitch Min", pitchMinField))
        content.addArrangedSubview(row("Pitch Max", pitchMaxField))
        content.addArrangedSubview(row("", spatialToggle))

        let okButton = NSButton(title: "OK", target: nil, action: nil)
        okButton.bezelStyle = .rounded
        okButton.contentTintColor = primaryText
        okButton.keyEquivalent = "\r"
        let cancelButton = NSButton(title: "Cancel", target: nil, action: nil)
        cancelButton.bezelStyle = .rounded
        cancelButton.contentTintColor = primaryText
        cancelButton.keyEquivalent = "\u{1b}"
        let buttons = NSStackView(views: [cancelButton, okButton])
        buttons.orientation = .horizontal
        buttons.spacing = 8
        buttons.alignment = .trailing
        buttons.detachesHiddenViews = true
        content.addArrangedSubview(buttons)

        let panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 520, height: 420),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )
        panel.title = title
        panel.appearance = NSAppearance(named: .darkAqua)
        panel.isFloatingPanel = true
        panel.hidesOnDeactivate = false
        panel.level = .modalPanel
        panel.initialFirstResponder = tagField
        panel.center()

        let rootView = NSView(frame: NSRect(x: 0, y: 0, width: 520, height: 420))
        rootView.wantsLayer = true
        rootView.layer?.backgroundColor = panelBackground.cgColor
        rootView.addSubview(content)
        NSLayoutConstraint.activate([
            content.leadingAnchor.constraint(equalTo: rootView.leadingAnchor),
            content.trailingAnchor.constraint(equalTo: rootView.trailingAnchor),
            content.topAnchor.constraint(equalTo: rootView.topAnchor),
            content.bottomAnchor.constraint(equalTo: rootView.bottomAnchor)
        ])
        panel.contentView = rootView
        panel.makeKeyAndOrderFront(nil)
        panel.makeFirstResponder(tagField)
        typeField.currentEditor()?.selectAll(nil)
        tagField.currentEditor()?.selectAll(nil)

        DispatchQueue.main.async {
            panel.makeFirstResponder(tagField)
            if let editor = tagField.currentEditor() {
                editor.selectAll(nil)
            }
        }

        var accepted = false
        final class ModalButtonTarget: NSObject {
            var onPress: (() -> Void)?
            @objc func press(_ sender: Any?) { onPress?() }
        }
        let okTarget = ModalButtonTarget()
        okTarget.onPress = {
            accepted = true
            panel.makeFirstResponder(nil)
            NSApp.stopModal()
            panel.close()
        }
        let cancelTarget = ModalButtonTarget()
        cancelTarget.onPress = {
            accepted = false
            panel.makeFirstResponder(nil)
            NSApp.stopModal()
            panel.close()
        }
        okButton.target = okTarget
        okButton.action = #selector(ModalButtonTarget.press(_:))
        cancelButton.target = cancelTarget
        cancelButton.action = #selector(ModalButtonTarget.press(_:))

        NSApp.runModal(for: panel)
        previousKeyWindow?.makeKeyAndOrderFront(nil)
        guard accepted else { return nil }

        let time = max(0.0, min(Float(timeField.stringValue.replacingOccurrences(of: ",", with: ".")) ?? initial.time, duration))
        let volume = max(0.0, Float(volumeField.stringValue.replacingOccurrences(of: ",", with: ".")) ?? initial.volume)
        let pitchMin = max(0.01, Float(pitchMinField.stringValue.replacingOccurrences(of: ",", with: ".")) ?? initial.pitchMin)
        let pitchMax = max(pitchMin, Float(pitchMaxField.stringValue.replacingOccurrences(of: ",", with: ".")) ?? max(initial.pitchMax, pitchMin))
        let payload = payloadField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        return SequenceEvent(
            eventType: typeField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines),
            eventTag: tagField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines),
            time: time,
            payload: payload,
            volume: volume,
            pitchMin: pitchMin,
            pitchMax: pitchMax,
            spatial: spatialToggle.state == .on,
            audioDuration: payload.isEmpty ? nil : CrescentEngineBridge.shared().getPreviewAudioDuration(path: payload)
        )
    }
}

private final class AudioBrowseTarget: NSObject {
    private weak var payloadField: NSTextField?

    init(payloadField: NSTextField) {
        self.payloadField = payloadField
    }

    @objc func chooseAudio(_ sender: Any?) {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedFileTypes = ["wav", "mp3", "ogg", "flac", "m4a", "aiff", "caf"]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        payloadField?.stringValue = url.path
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

private struct StableTextField: NSViewRepresentable {
    final class Coordinator: NSObject, NSTextFieldDelegate {
        var parent: StableTextField

        init(parent: StableTextField) {
            self.parent = parent
        }

        func controlTextDidBeginEditing(_ obj: Notification) {
            parent.onEditingChanged?(true)
        }

        func controlTextDidChange(_ obj: Notification) {
            guard let field = obj.object as? NSTextField else { return }
            if parent.text != field.stringValue {
                parent.text = field.stringValue
            }
        }

        func controlTextDidEndEditing(_ obj: Notification) {
            guard let field = obj.object as? NSTextField else { return }
            parent.text = field.stringValue
            parent.onEditingChanged?(false)
        }
    }

    let placeholder: String
    @Binding var text: String
    var onEditingChanged: ((Bool) -> Void)? = nil

    init(_ placeholder: String, text: Binding<String>, onEditingChanged: ((Bool) -> Void)? = nil) {
        self.placeholder = placeholder
        self._text = text
        self.onEditingChanged = onEditingChanged
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(parent: self)
    }

    func makeNSView(context: Context) -> NSTextField {
        let field = NSTextField(string: text)
        field.delegate = context.coordinator
        field.isBordered = true
        field.isBezeled = true
        field.isEditable = true
        field.isSelectable = true
        field.focusRingType = .default
        field.drawsBackground = true
        field.backgroundColor = NSColor.controlBackgroundColor
        field.textColor = NSColor.labelColor
        field.placeholderString = placeholder
        return field
    }

    func updateNSView(_ nsView: NSTextField, context: Context) {
        context.coordinator.parent = self
        if nsView.stringValue != text {
            nsView.stringValue = text
        }
        nsView.placeholderString = placeholder
    }
}
