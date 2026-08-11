plugins { id("com.android.application") }

android {
    namespace = "com.hyperian.generated"
    compileSdk = 37

    defaultConfig {
        applicationId = "com.hyperian.generated"
        minSdk = 24
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_static" } }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
