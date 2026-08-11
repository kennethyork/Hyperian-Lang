#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

static int write_file(const char *path, const unsigned char *data, size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    int okay = fwrite(data, 1, size, file) == size;
    if (fclose(file)) okay = 0;
    return okay;
}

int main(void) {
    static const unsigned char bitmap[] = {
        'B','M',58,0,0,0, 0,0,0,0, 54,0,0,0,
        40,0,0,0, 1,0,0,0, 1,0,0,0, 1,0, 24,0,
        0,0,0,0, 4,0,0,0, 0,0,0,0, 0,0,0,0,
        0,0,0,0, 0,0,0,0, 255,0,255,0
    };
    static const unsigned char wave[] = {
        'R','I','F','F',44,0,0,0,'W','A','V','E',
        'f','m','t',' ',16,0,0,0,1,0,1,0,64,31,0,0,64,31,0,0,1,0,8,0,
        'd','a','t','a',8,0,0,0,128,128,128,128,128,128,128,128
    };
    if (mkdir("assets", 0755) && errno != EEXIST) return 1;
    if (!write_file("assets/player.bmp", bitmap, sizeof(bitmap)) || !write_file("assets/jump.wav", wave, sizeof(wave))) return 1;
    puts("Created game media fixtures."); return 0;
}
