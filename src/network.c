#include "hyperian.h"

#include <stdio.h>
#include <string.h>

#ifdef HYPERIAN_HAVE_CURL
#include <curl/curl.h>

typedef struct { char *body; size_t size, used; int overflow; } HttpBuffer;

static size_t receive_body(char *data, size_t size, size_t count, void *context) {
    HttpBuffer *buffer = context; size_t bytes = size * count;
    if (bytes > buffer->size - buffer->used - 1) { buffer->overflow = 1; return 0; }
    memcpy(buffer->body + buffer->used, data, bytes); buffer->used += bytes; buffer->body[buffer->used] = 0; return bytes;
}

int hyperian_http_get(const char *url, char *body, size_t body_size, long *status, char *error, size_t error_size) {
    CURL *curl = curl_easy_init(); if (!curl) { snprintf(error, error_size, "could not initialize the HTTP client"); return 0; }
    HttpBuffer buffer = {body, body_size, 0, 0}; body[0] = 0;
    curl_easy_setopt(curl, CURLOPT_URL, url); curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L); curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Hyperian/" HYPERIAN_VERSION); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer); curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https,file");
    CURLcode result = curl_easy_perform(curl); curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status); curl_easy_cleanup(curl);
    if (buffer.overflow) { snprintf(error, error_size, "the web response is larger than %zu characters", body_size - 1); return 0; }
    if (result != CURLE_OK) { snprintf(error, error_size, "web request failed: %s", curl_easy_strerror(result)); return 0; }
    return 1;
}
#else
int hyperian_http_get(const char *url, char *body, size_t body_size, long *status, char *error, size_t error_size) {
    (void)url; (void)body; (void)body_size; (void)status;
    snprintf(error, error_size, "this Hyperian build does not include the HTTP backend"); return 0;
}
#endif
