import Foundation
import SwiftUI
import UIKit

struct HyperianScreen: Decodable {
    var view: String
    var controls: [HyperianControl]
    var timers: [Int]
}

struct HyperianControl: Decodable {
    var kind: String
    var text: String?
    var label: String?
    var name: String?
    var value: String?
    var action: String?
    var destination: String?
    var source: String?
    var description: String?
    var required: Bool?
    var changeEvent: String?
    var submitEvent: String?
}

@MainActor final class HyperianApplication: ObservableObject {
    @Published var screen = HyperianScreen(view: "", controls: [], timers: [])
    @Published var message = ""
    var values: [String: String] = [:]
    private let bridge = HyperianBridge()
    private var scheduled: Set<Int> = []

    init() {
        guard let bytecode = Bundle.main.path(forResource: "application", ofType: "hyc", inDirectory: "Resources") else {
            message = "The compiled Hyperian application is missing."; return
        }
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let error = bridge.openBytecode(bytecode, dataPath: documents.appendingPathComponent("hyperian-data.hdb").path)
        if !error.isEmpty { message = error } else { refresh() }
    }

    func binding(for control: HyperianControl) -> Binding<String> {
        let name = control.name ?? ""
        return Binding(get: { self.values[name] ?? control.value ?? "" }, set: {
            self.values[name] = $0
            if let event = control.changeEvent { DispatchQueue.main.async { self.sendInputEvent(event) } }
        })
    }

    func booleanBinding(for control: HyperianControl) -> Binding<Bool> {
        let value = binding(for: control)
        return Binding(get: { value.wrappedValue == "true" }, set: { value.wrappedValue = $0 ? "true" : "false" })
    }

    func run(_ action: String) {
        for (name, value) in values { bridge.setValue(value, named: name) }
        let error = bridge.runAction(action)
        if !error.isEmpty { message = error }
        refresh()
    }

    func submit(_ control: HyperianControl) {
        if let event = control.submitEvent { sendInputEvent(event) }
    }

    private func sendInputEvent(_ event: String) {
        for (name, value) in values { bridge.setValue(value, named: name) }
        let error = bridge.sendEvent(event)
        if !error.isEmpty { message = error }
        refresh()
    }

    private func refresh() {
        do {
            screen = try JSONDecoder().decode(HyperianScreen.self, from: Data(bridge.render().utf8))
            for control in screen.controls where control.name != nil {
                if values[control.name!] == nil { values[control.name!] = control.value ?? "" }
            }
            for interval in screen.timers where !scheduled.contains(interval) { schedule(interval) }
        } catch { message = "Hyperian could not render this view: \(error.localizedDescription)" }
    }

    private func schedule(_ milliseconds: Int) {
        scheduled.insert(milliseconds)
        Timer.scheduledTimer(withTimeInterval: Double(milliseconds) / 1000.0, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                let error = self.bridge.sendEvent("TIMER:\(milliseconds)")
                if !error.isEmpty { self.message = error }
                self.refresh()
            }
        }
    }
}

struct ContentView: View {
    @ObservedObject var application: HyperianApplication

    var body: some View {
        NavigationStack {
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 14) {
                    ForEach(Array(application.screen.controls.enumerated()), id: \.offset) { _, control in
                        controlView(control)
                    }
                    if !application.message.isEmpty { Text(application.message).foregroundStyle(.red) }
                }.padding()
            }
        }
    }

    @ViewBuilder private func controlView(_ control: HyperianControl) -> some View {
        switch control.kind {
        case "heading": Text(control.text ?? "").font(.largeTitle).bold()
        case "text", "value": Text(control.text ?? "")
        case "input": TextField(control.label ?? "", text: application.binding(for: control)).textFieldStyle(.roundedBorder).onSubmit { application.submit(control) }
        case "textarea": TextEditor(text: application.binding(for: control)).frame(minHeight: 120).overlay(RoundedRectangle(cornerRadius: 8).stroke(.secondary))
        case "checkbox": Toggle(control.label ?? "", isOn: application.booleanBinding(for: control))
        case "button": Button(control.label ?? "") { application.run(control.action ?? "") }.buttonStyle(.borderedProminent).disabled((control.action ?? "").isEmpty)
        case "link": if let address = control.destination, let url = URL(string: address) { Link(control.label ?? address, destination: url) }
        case "image": if let source = control.source, let image = UIImage(contentsOfFile: Bundle.main.path(forResource: source, ofType: nil, inDirectory: "Resources") ?? "") {
            Image(uiImage: image).resizable().scaledToFit().accessibilityLabel(control.description ?? "")
        } else { Text(control.description ?? control.source ?? "Image") }
        default: EmptyView()
        }
    }
}
