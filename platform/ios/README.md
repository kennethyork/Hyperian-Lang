# Generated Hyperian iOS application

Open `HyperianIOS.xcodeproj` in Xcode, choose your development team and a unique bundle identifier, and run the `HyperianIOS` scheme on a simulator or device.

The project embeds `application.hyc` and Hyperian's C runtime. An Objective-C bridge exposes the runtime to Swift through the project's bridging header, and SwiftUI renders the current MVC view using native controls.

Hyperian can invoke Xcode’s archive and export workflow automatically. Set `HYPERIAN_APPLICATION_ID` and `HYPERIAN_IOS_TEAM`, then run `hyperian build app.hyp for ios as App.ipa`. `HYPERIAN_IOS_DISTRIBUTION` may be `app-store-connect`, `ad-hoc`, `development`, or `enterprise`; it defaults to `app-store-connect`. Set `HYPERIAN_XCODEBUILD` when `xcodebuild` is not on `PATH`. Xcode still requires a valid Apple Developer identity and provisioning access on macOS.
