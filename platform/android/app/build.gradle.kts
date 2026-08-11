plugins { id("com.android.application") }

val hyperianKeystore = System.getenv("HYPERIAN_ANDROID_KEYSTORE")

android {
    namespace = "com.hyperian.generated"
    compileSdk = 37

    defaultConfig {
        applicationId = System.getenv("HYPERIAN_APPLICATION_ID") ?: "com.hyperian.generated"
        minSdk = 24
        targetSdk = 37
        versionCode = 1
        versionName = "1.0"
        externalNativeBuild { cmake { arguments += "-DANDROID_STL=c++_static" } }
    }

    signingConfigs {
        if (hyperianKeystore != null) {
            create("release") {
                storeFile = file(hyperianKeystore)
                storePassword = System.getenv("HYPERIAN_ANDROID_STORE_PASSWORD")
                keyAlias = System.getenv("HYPERIAN_ANDROID_KEY_ALIAS")
                keyPassword = System.getenv("HYPERIAN_ANDROID_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        getByName("release") {
            if (hyperianKeystore != null) signingConfig = signingConfigs.getByName("release")
            isMinifyEnabled = false
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}
