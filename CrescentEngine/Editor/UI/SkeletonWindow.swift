import SwiftUI
import Combine

struct SkeletonWindow: View {
    @ObservedObject var editorState: EditorState
    @State private var searchText: String = ""
    @State private var bones: [BoneEntry] = []
    @State private var selectedBone: BoneEntry?
    @State private var ikInfo: IKInfo = IKInfo()
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

                if let uuid = activeUUID, !uuid.isEmpty, !bones.isEmpty {
                    HStack(spacing: 16) {
                        boneList
                        ikControls(uuid: uuid)
                    }
                } else {
                    SkeletonEmptyState(
                        title: "No Skeleton Found",
                        subtitle: "Select an animated object to view its bones."
                    )
                }
            }
            .padding(16)
        }
        .environment(\.colorScheme, .dark)
        .frame(minWidth: 780, minHeight: 620)
        .onAppear {
            refreshSkeleton()
        }
        .onChange(of: editorState.selectedEntityUUIDs) { _ in
            refreshSkeleton()
        }
        .onReceive(timer) { _ in
            refreshSkeleton()
        }
    }

    private var header: some View {
        HStack {
            Label("Skeleton", systemImage: "figure.stand")
                .font(EditorTheme.font(size: 14, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)

            Spacer()

            HStack(spacing: 6) {
                Image(systemName: "magnifyingglass")
                    .font(EditorTheme.font(size: 11))
                    .foregroundColor(EditorTheme.textMuted)
                TextField("Search bones", text: $searchText)
                    .textFieldStyle(.plain)
                    .font(EditorTheme.font(size: 11))
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(EditorTheme.surface)
            .cornerRadius(8)
        }
    }

    private var boneList: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Bones")
                .font(EditorTheme.font(size: 11, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)

            ScrollView {
                LazyVStack(alignment: .leading, spacing: 4) {
                    ForEach(filteredBones) { bone in
                        Button(action: { selectedBone = bone }) {
                            HStack(spacing: 8) {
                                Text(bone.name)
                                    .font(EditorTheme.font(size: 11, weight: .medium))
                                    .foregroundColor(EditorTheme.textPrimary)
                                    .padding(.leading, CGFloat(bone.depth * 12))
                                Spacer()
                                if bone.index == ikInfo.rootIndex {
                                    badge("Root")
                                } else if bone.index == ikInfo.midIndex {
                                    badge("Mid")
                                } else if bone.index == ikInfo.endIndex {
                                    badge("End")
                                }
                            }
                            .padding(.vertical, 4)
                            .padding(.horizontal, 6)
                            .background(selectedBone?.id == bone.id ? Color.teal.opacity(0.2) : Color.clear)
                            .cornerRadius(6)
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
        }
        .frame(maxWidth: 420)
        .padding(10)
        .background(EditorTheme.panelBackground)
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(EditorTheme.panelStroke, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private func ikControls(uuid: String) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("IK Helper")
                .font(EditorTheme.font(size: 11, weight: .semibold))
                .foregroundColor(EditorTheme.textPrimary)

            if let selected = selectedBone {
                HStack(spacing: 8) {
                    Button("Set Root") { setIKBone(selected, role: .root, uuid: uuid) }
                    Button("Set Mid") { setIKBone(selected, role: .mid, uuid: uuid) }
                    Button("Set End") { setIKBone(selected, role: .end, uuid: uuid) }
                }
                .buttonStyle(.bordered)
                .font(EditorTheme.font(size: 10, weight: .semibold))
            } else {
                Text("Select a bone to assign IK.")
                    .font(EditorTheme.font(size: 10))
                    .foregroundColor(EditorTheme.textMuted)
            }

            Divider()
                .overlay(EditorTheme.panelStroke)

            Text("Auto Detect")
                .font(EditorTheme.font(size: 10, weight: .semibold))
                .foregroundColor(EditorTheme.textMuted)

            HStack(spacing: 8) {
                Button("Left Arm") { autoDetectIK(uuid: uuid, side: .left, chain: .arm) }
                Button("Right Arm") { autoDetectIK(uuid: uuid, side: .right, chain: .arm) }
            }
            .buttonStyle(.bordered)
            .font(EditorTheme.font(size: 10, weight: .semibold))

            HStack(spacing: 8) {
                Button("Left Leg") { autoDetectIK(uuid: uuid, side: .left, chain: .leg) }
                Button("Right Leg") { autoDetectIK(uuid: uuid, side: .right, chain: .leg) }
            }
            .buttonStyle(.bordered)
            .font(EditorTheme.font(size: 10, weight: .semibold))

            Divider()
                .overlay(EditorTheme.panelStroke)

            VStack(alignment: .leading, spacing: 6) {
                Text("Current IK")
                    .font(EditorTheme.font(size: 10, weight: .semibold))
                    .foregroundColor(EditorTheme.textMuted)
                Text("Root: \(ikInfo.rootName)")
                    .font(EditorTheme.font(size: 10))
                Text("Mid: \(ikInfo.midName)")
                    .font(EditorTheme.font(size: 10))
                Text("End: \(ikInfo.endName)")
                    .font(EditorTheme.font(size: 10))
            }
        }
        .frame(maxWidth: .infinity, alignment: .topLeading)
        .padding(12)
        .background(EditorTheme.panelBackground)
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(EditorTheme.panelStroke, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 10))
    }

    private var filteredBones: [BoneEntry] {
        let trimmed = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty {
            return bones
        }
        return bones.filter { $0.name.localizedCaseInsensitiveContains(trimmed) }
    }

    private func badge(_ title: String) -> some View {
        Text(title.uppercased())
            .font(EditorTheme.mono(size: 8))
            .foregroundColor(Color.white.opacity(0.9))
            .padding(.horizontal, 6)
            .padding(.vertical, 2)
            .background(Color.blue.opacity(0.6))
            .cornerRadius(4)
    }

    private func refreshSkeleton() {
        let uuid = editorState.selectedEntityUUIDs.first ?? ""
        activeUUID = uuid
        guard !uuid.isEmpty else {
            bones = []
            selectedBone = nil
            ikInfo = IKInfo()
            return
        }

        if let info = CrescentEngineBridge.shared().getSkeletonInfo(uuid: uuid) as? [String: Any],
           let boneList = info["bones"] as? [[String: Any]] {
            let rawBones = boneList.compactMap { BoneInfo(dict: $0) }
            bones = flattenBones(rawBones)
            if selectedBone != nil, !bones.contains(where: { $0.id == selectedBone?.id }) {
                selectedBone = nil
            }
        } else {
            bones = []
        }

        if let ik = CrescentEngineBridge.shared().getIKConstraintInfo(uuid: uuid) as? [String: Any] {
            ikInfo = IKInfo(info: ik, bones: bones)
        }
    }

    private func flattenBones(_ boneList: [BoneInfo]) -> [BoneEntry] {
        var children: [Int: [BoneInfo]] = [:]
        for bone in boneList {
            children[bone.parent, default: []].append(bone)
        }
        var result: [BoneEntry] = []

        func walk(_ bone: BoneInfo, depth: Int) {
            result.append(BoneEntry(index: bone.index, name: bone.name, depth: depth))
            for child in children[bone.index] ?? [] {
                walk(child, depth: depth + 1)
            }
        }

        let roots = boneList.filter { $0.parent < 0 }
        for root in roots {
            walk(root, depth: 0)
        }
        return result
    }

    private func setIKBone(_ bone: BoneEntry, role: IKRole, uuid: String) {
        var root = ikInfo.rootName
        var mid = ikInfo.midName
        var end = ikInfo.endName
        switch role {
        case .root: root = bone.name
        case .mid: mid = bone.name
        case .end: end = bone.name
        }
        let info: [String: Any] = [
            "root": root,
            "mid": mid,
            "end": end,
            "target": [0.0, 0.0, 0.0],
            "world": true,
            "weight": 1.0
        ]
        _ = CrescentEngineBridge.shared().setIKConstraintInfo(uuid: uuid, info: info)
        refreshSkeleton()
    }

    private func autoDetectIK(uuid: String, side: IKSide, chain: IKChain) {
        guard !bones.isEmpty else { return }
        let names = bones.map { $0.name }
        let root = findBoneName(names: names, side: side, patterns: chain.rootPatterns)
        let mid = findBoneName(names: names, side: side, patterns: chain.midPatterns)
        let end = findBoneName(names: names, side: side, patterns: chain.endPatterns)
        guard let rootName = root, let midName = mid, let endName = end else { return }
        let info: [String: Any] = [
            "root": rootName,
            "mid": midName,
            "end": endName,
            "target": [0.0, 0.0, 0.0],
            "world": true,
            "weight": 1.0
        ]
        _ = CrescentEngineBridge.shared().setIKConstraintInfo(uuid: uuid, info: info)
        refreshSkeleton()
    }

    private func findBoneName(names: [String], side: IKSide, patterns: [String]) -> String? {
        let candidates = names.filter { matchesSide($0, side: side) }
        if let found = findMatch(in: candidates, patterns: patterns) {
            return found
        }
        return findMatch(in: names, patterns: patterns)
    }

    private func findMatch(in names: [String], patterns: [String]) -> String? {
        for name in names {
            let lower = name.lowercased()
            for pattern in patterns {
                if lower.contains(pattern) {
                    return name
                }
            }
        }
        return nil
    }

    private func matchesSide(_ name: String, side: IKSide) -> Bool {
        let lower = name.lowercased()
        let tokens = side == .left ? ["left", "_l", ".l", "l_", " l"] : ["right", "_r", ".r", "r_", " r"]
        return tokens.contains { lower.contains($0) }
    }
}

private enum IKRole {
    case root
    case mid
    case end
}

private enum IKSide {
    case left
    case right
}

private enum IKChain {
    case arm
    case leg

    var rootPatterns: [String] {
        switch self {
        case .arm: return ["upperarm", "arm", "shoulder"]
        case .leg: return ["thigh", "upleg", "leg"]
        }
    }

    var midPatterns: [String] {
        switch self {
        case .arm: return ["lowerarm", "forearm", "elbow"]
        case .leg: return ["calf", "lowerleg", "knee"]
        }
    }

    var endPatterns: [String] {
        switch self {
        case .arm: return ["hand", "wrist"]
        case .leg: return ["foot", "ankle"]
        }
    }
}

private struct BoneInfo {
    let index: Int
    let name: String
    let parent: Int

    init?(dict: [String: Any]) {
        guard let index = (dict["index"] as? NSNumber)?.intValue else { return nil }
        let name = dict["name"] as? String ?? "Bone"
        let parent = (dict["parent"] as? NSNumber)?.intValue ?? -1
        self.index = index
        self.name = name
        self.parent = parent
    }
}

private struct BoneEntry: Identifiable, Hashable {
    let index: Int
    let name: String
    let depth: Int
    var id: Int { index }
}

private struct IKInfo {
    var rootName: String = "-"
    var midName: String = "-"
    var endName: String = "-"
    var rootIndex: Int = -1
    var midIndex: Int = -1
    var endIndex: Int = -1

    init() {}

    init(info: [String: Any], bones: [BoneEntry]) {
        rootName = info["root"] as? String ?? "-"
        midName = info["mid"] as? String ?? "-"
        endName = info["end"] as? String ?? "-"
        rootIndex = bones.first(where: { $0.name == rootName })?.index ?? -1
        midIndex = bones.first(where: { $0.name == midName })?.index ?? -1
        endIndex = bones.first(where: { $0.name == endName })?.index ?? -1
    }
}

private struct SkeletonEmptyState: View {
    let title: String
    let subtitle: String

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: "bone")
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
