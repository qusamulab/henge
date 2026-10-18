import ReplayKit
import SwiftUI
import UIKit

/// The OpenGL ES surface a NativeActivity draws into, and its touches.
final class GameSurfaceView: UIView {
    override class var layerClass: AnyClass { CAEAGLLayer.self }

    /// First layout with a real size: (CAEAGLLayer, width, height) in pixels.
    var onReady: ((UnsafeMutableRawPointer, Int, Int) -> Void)?
    var onResize: ((Int, Int) -> Void)?
    var onTouch: ((henge_touch_phase, Int32, Float, Float) -> Void)?

    private var reported = false
    private var fingers: [ObjectIdentifier: Int32] = [:]
    private var nextFinger: Int32 = 0

    override init(frame: CGRect) {
        super.init(frame: frame)
        isOpaque = true
        isMultipleTouchEnabled = true
        backgroundColor = .black
        let eagl = layer as! CAEAGLLayer
        eagl.isOpaque = true
        eagl.contentsScale = UIScreen.main.scale
        eagl.drawableProperties = [kEAGLDrawablePropertyRetainedBacking: false,
                                   kEAGLDrawablePropertyColorFormat: kEAGLColorFormatRGBA8]
    }

    @available(*, unavailable) required init?(coder: NSCoder) { fatalError() }

    private var pixelSize: (Int, Int) {
        let s = layer.contentsScale
        return (Int(bounds.width * s), Int(bounds.height * s))
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        let (w, h) = pixelSize
        guard w > 0, h > 0 else { return }
        if reported {
            onResize?(w, h)
        } else {
            reported = true
            onReady?(Unmanaged.passUnretained(layer).toOpaque(), w, h)
        }
    }

    private func send(_ touches: Set<UITouch>, _ phase: henge_touch_phase) {
        let scale = Float(layer.contentsScale)
        for t in touches {
            let key = ObjectIdentifier(t)
            if phase == HENGE_TOUCH_BEGAN {
                fingers[key] = nextFinger
                nextFinger += 1
            }
            guard let id = fingers[key] else { continue }
            let p = t.location(in: self)
            onTouch?(phase, id, Float(p.x) * scale, Float(p.y) * scale)
            if phase == HENGE_TOUCH_ENDED || phase == HENGE_TOUCH_CANCELLED { fingers[key] = nil }
        }
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) { send(touches, HENGE_TOUCH_BEGAN) }
    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) { send(touches, HENGE_TOUCH_MOVED) }
    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) { send(touches, HENGE_TOUCH_ENDED) }
    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) { send(touches, HENGE_TOUCH_CANCELLED) }
}

private struct GameSurface: UIViewRepresentable {
    let runner: ConvertRunner

    func makeUIView(context: Context) -> GameSurfaceView {
        let v = GameSurfaceView()
        v.onReady = { [weak runner] layer, w, h in runner?.startActivity(layer: layer, width: w, height: h) }
        v.onResize = { [weak runner] w, h in runner?.resize(width: w, height: h) }
        v.onTouch = { [weak runner] phase, id, x, y in runner?.touch(phase, id: id, x: x, y: y) }
        return v
    }

    func updateUIView(_ uiView: GameSurfaceView, context: Context) {}
}

/// Records the screen (and the app's audio) to Documents/henge-capture.mp4 with ReplayKit.
@MainActor
final class ScreenRecorder: ObservableObject {
    @Published private(set) var recording = false
    @Published private(set) var message: String?

    static var outputURL: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("henge-capture.mp4")
    }

    func toggle() {
        let recorder = RPScreenRecorder.shared()
        if recording {
            try? FileManager.default.removeItem(at: Self.outputURL)
            recorder.stopRecording(withOutput: Self.outputURL) { [weak self] error in
                DispatchQueue.main.async {
                    self?.recording = false
                    self?.message = error.map { "Recording failed: \($0.localizedDescription)" } ?? "Saved henge-capture.mp4"
                }
            }
        } else {
            recorder.isMicrophoneEnabled = false
            recorder.startRecording { [weak self] error in
                DispatchQueue.main.async {
                    self?.recording = error == nil
                    self?.message = error.map { "Could not record: \($0.localizedDescription)" }
                }
            }
        }
    }
}

/// Full-screen host for a running NativeActivity.
struct GameScreen: View {
    @ObservedObject var runner: ConvertRunner
    @StateObject private var recorder = ScreenRecorder()

    var body: some View {
        ZStack(alignment: .topLeading) {
            GameSurface(runner: runner).ignoresSafeArea()
            HStack {
                Spacer()
                Button {
                    recorder.toggle()
                } label: {
                    Image(systemName: recorder.recording ? "stop.circle.fill" : "record.circle")
                        .font(.title)
                        .foregroundStyle(recorder.recording ? .red : .white.opacity(0.8))
                        .shadow(radius: 3)
                        .padding(16)
                }
            }
            Button {
                runner.stop()
            } label: {
                Label("Close", systemImage: "xmark.circle.fill")
                    .labelStyle(.iconOnly)
                    .font(.title)
                    .foregroundStyle(.white.opacity(0.8))
                    .shadow(radius: 3)
                    .padding(16)
            }
        }
        .statusBarHidden()
    }
}
