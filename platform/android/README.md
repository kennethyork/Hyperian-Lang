# Generated Hyperian Android application

Open this `android` folder in Android Studio, allow Gradle to synchronize, and run the `app` configuration on an emulator or phone.

The project uses Android Gradle Plugin 9.3, API level 37, CMake, and the Android NDK. JDK 17 or newer is required. Gradle builds the included Hyperian C runtime as a native shared library and packages `application.hyc` as an application asset.

`MainActivity` renders headings, text, values, inputs, text areas, checkboxes, buttons, links, and images as Android widgets. It synchronizes input values before English-named controller actions, dispatches recurring timer events, follows Hyperian view navigation, and keeps model data in the application's private storage.

Change the generated `applicationId` and namespace before publishing multiple Hyperian applications. A release APK or Android App Bundle must be signed with your own Android signing key.
