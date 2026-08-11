#include "hyperian.h"

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HyperianMobile *mobile;
static char last_error[512];
static JavaVM *java_vm;
static jclass activity_class;
static jmethodID fetch_method;
static const char *utf(JNIEnv *environment, jstring value);
static void release_utf(JNIEnv *environment, jstring value, const char *text);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *virtual_machine, void *reserved) {
    (void)reserved; java_vm = virtual_machine; return JNI_VERSION_1_6;
}

static int android_http_get(const char *url, char *body, size_t body_size, long *status, char *error, size_t error_size) {
    JNIEnv *environment = NULL; int detach = 0;
    if (!java_vm || !activity_class || !fetch_method) { snprintf(error, error_size, "Android HTTPS is not connected"); return 0; }
    jint environment_state = (*java_vm)->GetEnv(java_vm, (void **)&environment, JNI_VERSION_1_6);
    if (environment_state == JNI_EDETACHED) {
        if ((*java_vm)->AttachCurrentThread(java_vm, (void **)&environment, NULL) != JNI_OK) {
            snprintf(error, error_size, "Android could not start its HTTPS client"); return 0;
        }
        detach = 1;
    } else if (environment_state != JNI_OK) { snprintf(error, error_size, "Android could not access its HTTPS client"); return 0; }
    jstring address = (*environment)->NewStringUTF(environment, url);
    jobjectArray response = address ? (jobjectArray)(*environment)->CallStaticObjectMethod(environment, activity_class, fetch_method, address, (jint)body_size) : NULL;
    if (address) (*environment)->DeleteLocalRef(environment, address);
    if ((*environment)->ExceptionCheck(environment)) { (*environment)->ExceptionClear(environment); response = NULL; }
    int okay = response != NULL;
    jstring status_value = okay ? (jstring)(*environment)->GetObjectArrayElement(environment, response, 0) : NULL;
    jstring body_value = okay ? (jstring)(*environment)->GetObjectArrayElement(environment, response, 1) : NULL;
    jstring error_value = okay ? (jstring)(*environment)->GetObjectArrayElement(environment, response, 2) : NULL;
    const char *status_text = utf(environment, status_value), *body_text = utf(environment, body_value), *error_text = utf(environment, error_value);
    if (!okay || !status_text || !body_text || !error_text) {
        snprintf(error, error_size, "Android could not finish its HTTPS request"); okay = 0;
    } else if (*error_text) {
        snprintf(error, error_size, "%s", error_text); okay = 0;
    } else if (strlen(body_text) >= body_size) {
        snprintf(error, error_size, "the web response is larger than %zu characters", body_size - 1); okay = 0;
    } else {
        *status = strtol(status_text, NULL, 10); snprintf(body, body_size, "%s", body_text);
    }
    release_utf(environment, status_value, status_text); release_utf(environment, body_value, body_text); release_utf(environment, error_value, error_text);
    if (status_value) (*environment)->DeleteLocalRef(environment, status_value);
    if (body_value) (*environment)->DeleteLocalRef(environment, body_value);
    if (error_value) (*environment)->DeleteLocalRef(environment, error_value);
    if (response) (*environment)->DeleteLocalRef(environment, response);
    if (detach) (*java_vm)->DetachCurrentThread(java_vm);
    return okay;
}

static const char *utf(JNIEnv *environment, jstring value) {
    return value ? (*environment)->GetStringUTFChars(environment, value, NULL) : NULL;
}

static void release_utf(JNIEnv *environment, jstring value, const char *text) {
    if (value && text) (*environment)->ReleaseStringUTFChars(environment, value, text);
}

JNIEXPORT jstring JNICALL Java_com_hyperian_generated_MainActivity_openMobile(
    JNIEnv *environment, jobject object, jstring bytecode, jstring data) {
    const char *bytecode_path = utf(environment, bytecode), *data_path = utf(environment, data);
    if (mobile) hyperian_mobile_close(mobile);
    if (activity_class) (*environment)->DeleteGlobalRef(environment, activity_class);
    jclass local_class = (*environment)->GetObjectClass(environment, object);
    activity_class = local_class ? (*environment)->NewGlobalRef(environment, local_class) : NULL;
    if (local_class) (*environment)->DeleteLocalRef(environment, local_class);
    fetch_method = activity_class ? (*environment)->GetStaticMethodID(environment, activity_class, "fetchFromInternet", "(Ljava/lang/String;I)[Ljava/lang/String;") : NULL;
    if ((*environment)->ExceptionCheck(environment)) { (*environment)->ExceptionClear(environment); fetch_method = NULL; }
    hyperian_set_http_handler(fetch_method ? android_http_get : NULL);
    if (data_path) setenv("HYPERIAN_DATA", data_path, 1);
    mobile = hyperian_mobile_open(bytecode_path, last_error, sizeof(last_error));
    int okay = mobile && hyperian_mobile_start(mobile, last_error, sizeof(last_error));
    release_utf(environment, bytecode, bytecode_path); release_utf(environment, data, data_path);
    return (*environment)->NewStringUTF(environment, okay ? "" : last_error);
}

JNIEXPORT void JNICALL Java_com_hyperian_generated_MainActivity_setMobileValue(
    JNIEnv *environment, jobject object, jstring name, jstring value) {
    (void)object; const char *native_name = utf(environment, name), *native_value = utf(environment, value);
    if (mobile) hyperian_mobile_set(mobile, native_name, native_value, last_error, sizeof(last_error));
    release_utf(environment, name, native_name); release_utf(environment, value, native_value);
}

JNIEXPORT jstring JNICALL Java_com_hyperian_generated_MainActivity_runMobileAction(
    JNIEnv *environment, jobject object, jstring action) {
    (void)object; const char *native_action = utf(environment, action);
    int okay = mobile && hyperian_mobile_run_action(mobile, native_action, NULL, last_error, sizeof(last_error));
    release_utf(environment, action, native_action);
    return (*environment)->NewStringUTF(environment, okay ? "" : last_error);
}

JNIEXPORT jstring JNICALL Java_com_hyperian_generated_MainActivity_sendMobileEvent(
    JNIEnv *environment, jobject object, jstring event) {
    (void)object; const char *native_event = utf(environment, event);
    int okay = mobile && hyperian_mobile_send_event(mobile, native_event, last_error, sizeof(last_error));
    release_utf(environment, event, native_event);
    return (*environment)->NewStringUTF(environment, okay ? "" : last_error);
}

JNIEXPORT jstring JNICALL Java_com_hyperian_generated_MainActivity_renderMobile(JNIEnv *environment, jobject object) {
    (void)object; char *json = malloc(262144);
    if (!json) return (*environment)->NewStringUTF(environment, "{\"error\":\"not enough memory\"}");
    if (!mobile || !hyperian_mobile_render_json(mobile, json, 262144, last_error, sizeof(last_error))) {
        free(json); return (*environment)->NewStringUTF(environment, "{\"error\":\"mobile view could not be rendered\"}");
    }
    jstring result = (*environment)->NewStringUTF(environment, json); free(json); return result;
}

JNIEXPORT void JNICALL Java_com_hyperian_generated_MainActivity_closeMobile(JNIEnv *environment, jobject object) {
    (void)object; hyperian_mobile_close(mobile); mobile = NULL; hyperian_set_http_handler(NULL); fetch_method = NULL;
    if (activity_class) { (*environment)->DeleteGlobalRef(environment, activity_class); activity_class = NULL; }
}
