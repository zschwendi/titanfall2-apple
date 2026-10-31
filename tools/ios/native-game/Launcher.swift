import SwiftUI
import GameController
import UIKit
import Combine

// Matches the library/card/settings structure of Dark Eco. Artwork and engine
// code remain independent; gameplay keeps Titanfall's positional XInput mapping.
@MainActor
private final class LauncherState: ObservableObject {
    @Published var ready = false
    @Published var detail = "Preparing game files…"
    @Published var controllerName: String?
    @Published var settings = false
    let engineReady: Bool
    let engineProblem: String?
    let launch: () -> Void
    private var previousCross = false
    private var previousMenu = false
    private var pendingLaunch = false
    private var previousController: ObjectIdentifier?

    init(engineReady: Bool, engineProblem: String?, launch: @escaping () -> Void) {
        self.engineReady = engineReady
        self.engineProblem = engineProblem
        self.launch = launch
    }

    func refresh() {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let filesReady = FileManager.default.fileExists(atPath: documents.appendingPathComponent("game-files-ready.json").path)
        let nextReady = engineReady && filesReady
        let nextDetail = nextReady ? "Ready to launch"
            : engineProblem ?? (filesReady ? "Preparing game…" : "Transferring game files…")
        if ready != nextReady { ready = nextReady }
        if detail != nextDetail { detail = nextDetail }
        let current = GCController.current
        let controller = current?.extendedGamepad != nil ? current : GCController.controllers().first { $0.extendedGamepad != nil }
        if controllerName != controller?.vendorName { controllerName = controller?.vendorName }
        let identity = controller.map(ObjectIdentifier.init)
        let cross = controller?.extendedGamepad?.buttonA.isPressed ?? false
        let menu = controller?.extendedGamepad?.buttonMenu.isPressed ?? false
        if identity != previousController {
            previousController = identity; pendingLaunch = false
            previousCross = cross; previousMenu = menu
            return
        }
        if menu && !previousMenu { settings.toggle(); pendingLaunch = false }
        if cross && !previousCross && ready && !settings { pendingLaunch = true }
        // Wait for release, so the launcher press is not carried into gameplay.
        if pendingLaunch && !cross && !settings { pendingLaunch = false; launch() }
        previousCross = cross; previousMenu = menu
    }

    func suspend() {
        pendingLaunch = false
        previousController = nil
    }
}

// Render sizes the game can draw at; main.m reads the same default. "native" is the
// panel's own size, which also fills the screen without letterboxing.
private struct RenderChoice: Identifiable { let id: String; let label: String }
@MainActor private func renderChoices() -> [RenderChoice] {
    let native = UIScreen.main.nativeBounds.size
    let width = Int(max(native.width, native.height)), height = Int(min(native.width, native.height))
    let sizes = [(1280, 720), (1600, 900), (1920, 1080), (2240, 1260), (2560, 1440)]
        .filter { $0.0 <= width && $0.1 <= height && ($0.0 != width || $0.1 != height) }
        .map { RenderChoice(id: "\($0.0)x\($0.1)", label: "\($0.0) × \($0.1)") }
    return sizes + [RenderChoice(id: "native", label: "Native \(width) × \(height)")]
}

private struct LauncherView: View {
    @ObservedObject var state: LauncherState
    @AppStorage("TFRenderResolution") private var resolution = "1920x1080"
    private let accent = Color(red: 0.35, green: 0.76, blue: 0.94)
    private let timer = Timer.publish(every: 0.05, on: .main, in: .common).autoconnect()

    var body: some View {
        GeometryReader { geometry in
            let cardWidth = min(270.0, max(150.0, (geometry.size.height - 180) * 0.62))
            ZStack {
                LinearGradient(colors: [Color(red: 0.025, green: 0.045, blue: 0.065),
                                        Color(red: 0.06, green: 0.12, blue: 0.17), .black],
                               startPoint: .topLeading, endPoint: .bottomTrailing).ignoresSafeArea()
                VStack(spacing: 12) {
                    HStack {
                        Text("Library").font(.title3.weight(.semibold))
                        Spacer()
                        Text("TITANFALL 2").font(.caption.weight(.bold)).tracking(3)
                            .padding(.horizontal, 20).padding(.vertical, 10)
                            .background(.white.opacity(0.06), in: Capsule())
                        Spacer()
                        Button { state.settings = true } label: {
                            Image(systemName: "gearshape").frame(width: 44, height: 44)
                        }.accessibilityLabel("Settings")
                    }
                    Spacer(minLength: 0)
                    Button { state.launch() } label: {
                        VStack(alignment: .leading, spacing: 0) {
                            ZStack(alignment: .bottomLeading) {
                                LinearGradient(colors: [accent.opacity(0.24), .black], startPoint: .topTrailing, endPoint: .bottomLeading)
                                Image(systemName: "scope")
                                    .font(.system(size: cardWidth * 0.42, weight: .ultraLight))
                                    .foregroundStyle(accent.opacity(0.45))
                                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                                VStack(alignment: .leading, spacing: 5) {
                                    Text("PREPARE FOR").font(.system(size: 10, weight: .medium)).tracking(3)
                                    Text("TITANFALL").font(.system(size: 25, weight: .black, design: .rounded)).tracking(1)
                                    Text("2").font(.system(size: 47, weight: .light, design: .rounded)).foregroundStyle(accent)
                                }.padding(22)
                            }.frame(height: cardWidth * 1.18)
                            VStack(alignment: .leading, spacing: 8) {
                                Text("Titanfall 2").font(.headline)
                                Label(state.detail, systemImage: state.ready ? "play.fill" : "arrow.down.circle")
                                    .font(.caption).foregroundStyle(accent)
                            }.padding(16).frame(maxWidth: .infinity, alignment: .leading)
                                .background(.white.opacity(0.045))
                        }
                        .frame(width: cardWidth)
                        .clipShape(RoundedRectangle(cornerRadius: 18))
                        .overlay(RoundedRectangle(cornerRadius: 18).stroke(accent.opacity(state.ready ? 0.6 : 0.22), lineWidth: 1))
                    }
                    .buttonStyle(.plain).disabled(!state.ready)
                    .accessibilityLabel(state.ready ? "Play Titanfall 2" : "Titanfall 2, \(state.detail)")
                    Spacer(minLength: 0)
                    HStack {
                        Label(state.controllerName ?? "Connect a controller", systemImage: "gamecontroller")
                        Spacer()
                        if state.ready { Text(state.controllerName == nil ? "Tap the game to play" : "✕ Play    Options Settings") }
                    }.font(.caption).foregroundStyle(.white.opacity(0.65))
                }.padding(.horizontal, 30).padding(.vertical, 18)
            }
        }
        .foregroundStyle(.white).tint(accent).preferredColorScheme(.dark)
        // UIKit owns this scene; SwiftUI's scenePhase is not supplied by an App.
        .onReceive(timer) { _ in
            if UIApplication.shared.applicationState == .active { state.refresh() }
            else { state.suspend() }
        }
        .onDisappear { state.suspend() }
        .sheet(isPresented: $state.settings) {
            NavigationStack {
                Form {
                    Section("Controller") {
                        LabeledContent("Connected", value: state.controllerName ?? "None")
                        Text("DualSense uses the game's normal controller layout. Options opens the pause menu during play.")
                    }
                    Section("Display") {
                        Picker("Resolution", selection: $resolution) {
                            ForEach(renderChoices()) { Text($0.label).tag($0.id) }
                        }
                        Text("Applies the next time the game starts. Higher sizes cost GPU time; 16:9 sizes are letterboxed.")
                    }
                    Section("Game files") {
                        Text(state.detail)
                        Text("This first install includes the menu and pilot training. Later campaign levels are not installed yet.")
                    }
                }
                .navigationTitle("Settings")
                .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { state.settings = false } } }
            }.preferredColorScheme(.dark)
        }
    }
}

@objc(TFLauncherController)
public final class TFLauncherController: UIViewController {
    private let state: LauncherState
    private var hosting: UIHostingController<LauncherView>?

    @objc public init(engineReady: Bool, engineProblem: String?, launch: @escaping () -> Void) {
        state = LauncherState(engineReady: engineReady, engineProblem: engineProblem, launch: launch)
        super.init(nibName: nil, bundle: nil)
    }
    @objc public convenience init(engineReady: Bool, launch: @escaping () -> Void) {
        self.init(engineReady: engineReady, engineProblem: nil, launch: launch)
    }
    required public init?(coder: NSCoder) { nil }
    public override func viewDidLoad() {
        super.viewDidLoad()
        let hosting = UIHostingController(rootView: LauncherView(state: state))
        self.hosting = hosting
        addChild(hosting); hosting.view.frame = view.bounds
        hosting.view.autoresizingMask = [.flexibleWidth, .flexibleHeight]
        view.addSubview(hosting.view); hosting.didMove(toParent: self)
    }
}
