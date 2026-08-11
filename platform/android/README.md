# Generated Hyperian Android application

Open this `android` folder in Android Studio, allow Gradle to synchronize, and run the `app` configuration on an emulator or phone.

The project uses Android Gradle Plugin 9.3, API level 37, CMake, and the Android NDK. JDK 17 or newer is required. Gradle builds the included Hyperian C runtime as a native shared library and packages `application.hyc` as an application asset.

`MainActivity` renders headings, text, values, inputs, text areas, checkboxes, buttons, links, and images as Android widgets. It synchronizes input values before English-named controller actions, dispatches recurring timer events, follows Hyperian view navigation, and keeps model data in the application's private storage. English web requests use Android's native HTTP/HTTPS client. SQLite is compiled into the native runtime, while HDB remains available as the dependency-free storage choice; both use the private `hyperian-data.db` path.

Hyperian can invoke this project automatically and sign a release APK or Android App Bundle. Set `HYPERIAN_APPLICATION_ID`, `HYPERIAN_ANDROID_KEYSTORE`, `HYPERIAN_ANDROID_KEY_ALIAS`, `HYPERIAN_ANDROID_STORE_PASSWORD`, and `HYPERIAN_ANDROID_KEY_PASSWORD`, then run `hyperian build app.hyp for android as App.apk` or use an `.aab` output. Set `HYPERIAN_GRADLE` when the Gradle executable is not on `PATH`. Passwords are read from the environment and are not placed in command arguments or generated files.
