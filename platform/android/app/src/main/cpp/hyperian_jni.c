#include "hyperian.h"

#include <jni.h>
#include <stdlib.h>

static HyperianMobile *mobile;
static char last_error[512];

static const char *utf(JNIEnv *environment, jstring value) {
    return value ? (*environment)->GetStringUTFChars(environment, value, NULL) : NULL;
}

static void release_utf(JNIEnv *environment, jstring value, const char *text) {
    if (value && text) (*environment)->ReleaseStringUTFChars(environment, value, text);
}

JNIEXPORT jstring JNICALL Java_com_hyperian_generated_MainActivity_openMobile(
    JNIEnv *environment, jobject object, jstring bytecode, jstring data) {
    (void)object; const char *bytecode_path = utf(environment, bytecode), *data_path = utf(environment, data);
    if (mobile) hyperian_mobile_close(mobile);
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
    (void)environment; (void)object; hyperian_mobile_close(mobile); mobile = NULL;
}
