import SwiftUI

struct GameRuntimeView: View {
    @ObservedObject var runtimeState: GameRuntimeState

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            MetalView(
                viewKind: .game,
                isActive: true,
                drivesLoop: true,
                onEngineReady: {
                    runtimeState.bootstrapIfNeeded()
                }
            )
            .ignoresSafeArea()

            if runtimeState.isRunning {
                RuntimeCrosshair()
                    .allowsHitTesting(false)
            }

            if runtimeState.isLoading || runtimeState.errorMessage != nil {
                RuntimeOverlay(
                    title: runtimeState.errorMessage == nil ? runtimeState.gameTitle : "Launch Failed",
                    message: runtimeState.errorMessage ?? "Loading game"
                )
            }
        }
        .frame(minWidth: 960, minHeight: 540)
    }
}

private struct RuntimeOverlay: View {
    let title: String
    let message: String

    var body: some View {
        VStack(spacing: 12) {
            if message == "Loading game" {
                ProgressView()
                    .controlSize(.large)
                    .tint(.white)
            }

            Text(title)
                .font(.system(size: 20, weight: .semibold, design: .rounded))
                .foregroundColor(.white)

            Text(message)
                .font(.system(size: 13, weight: .medium, design: .rounded))
                .foregroundColor(Color.white.opacity(0.76))
                .multilineTextAlignment(.center)
        }
        .padding(.horizontal, 24)
        .padding(.vertical, 20)
        .background(Color.black.opacity(0.68))
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
    }
}

private struct RuntimeCrosshair: View {
    var body: some View {
        ZStack {
            Capsule()
                .fill(Color.white.opacity(0.92))
                .frame(width: 20, height: 2)
            Capsule()
                .fill(Color.white.opacity(0.92))
                .frame(width: 2, height: 20)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
