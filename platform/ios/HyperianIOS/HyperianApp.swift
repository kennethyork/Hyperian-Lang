import SwiftUI

@main
struct HyperianApp: App {
    @StateObject private var application = HyperianApplication()

    var body: some Scene {
        WindowGroup { ContentView(application: application) }
    }
}
