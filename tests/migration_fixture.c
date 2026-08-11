#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_u32(FILE *file, uint32_t value) {
    unsigned char bytes[4] = {(unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    return fwrite(bytes, 1, 4, file) == 4;
}

static int write_text(FILE *file, const char *text) {
    return write_u32(file, (uint32_t)strlen(text)) && fwrite(text, 1, strlen(text), file) == strlen(text);
}

static int read_u32(FILE *file, uint32_t *value) {
    unsigned char bytes[4];
    if (fread(bytes, 1, 4, file) != 4) return 0;
    *value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    return 1;
}

static char *read_text(FILE *file) {
    uint32_t length;
    if (!read_u32(file, &length) || length > 1024) return NULL;
    char *text = malloc((size_t)length + 1);
    if (!text || fread(text, 1, length, file) != length) { free(text); return NULL; }
    text[length] = 0; return text;
}

static int create_legacy(const char *path) {
    FILE *file = fopen(path, "wb"); if (!file) return 1;
    int okay = fwrite("HDB1", 1, 4, file) == 4 && write_u32(file, 1) &&
        write_text(file, "Task") && write_u32(file, 2) &&
        write_text(file, "id") && write_text(file, "1") &&
        write_text(file, "title") && write_text(file, "Keep this value");
    return fclose(file) || !okay;
}

static int verify_migrated(const char *path) {
    FILE *file = fopen(path, "rb"); if (!file) return 1;
    char magic[4]; uint32_t version, records, fields;
    int okay = fread(magic, 1, 4, file) == 4 && !memcmp(magic, "HDB2", 4) &&
        read_u32(file, &version) && version == 2 && read_u32(file, &records) && records == 1;
    char *model = okay ? read_text(file) : NULL;
    okay = okay && model && !strcmp(model, "Task") && read_u32(file, &fields) && fields == 2;
    int found = 0;
    for (uint32_t i = 0; okay && i < fields; i++) {
        char *key = read_text(file), *value = read_text(file);
        if (!key || !value) okay = 0;
        else if (!strcmp(key, "name") && !strcmp(value, "Keep this value")) found = 1;
        free(key); free(value);
    }
    free(model); fclose(file); return !okay || !found;
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    if (!strcmp(argv[1], "create")) return create_legacy(argv[2]);
    if (!strcmp(argv[1], "verify")) return verify_migrated(argv[2]);
    return 2;
}
