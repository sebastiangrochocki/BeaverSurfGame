/* Replays every golden trace in fixtures/ through the C rules and compares each frame's state
 * byte for byte with what the Rust crate recorded. Usage: trace_test <fixtures dir> */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/rules.h"

#define TRACE_HEADER_LEN 12
#define RECORD_LEN (1 + STATE_LEN)

static unsigned char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *data;
    long size;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size);
    if (fread(data, 1, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return data;
}

static int replay(const char *path) {
    size_t len;
    unsigned char *bytes = read_file(path, &len);
    unsigned long frames, index;
    Events events;
    unsigned char state[STATE_LEN];
    if (!bytes) {
        printf("%s: cannot read\n", path);
        return 1;
    }
    if (len < TRACE_HEADER_LEN || memcmp(bytes, "BVRT", 4) != 0 || bytes[4] != 1 ||
        bytes[5] != STATE_LEN) {
        printf("%s: bad header\n", path);
        free(bytes);
        return 1;
    }
    frames = (unsigned long)bytes[8] | ((unsigned long)bytes[9] << 8) |
             ((unsigned long)bytes[10] << 16) | ((unsigned long)bytes[11] << 24);
    if (len != TRACE_HEADER_LEN + frames * RECORD_LEN) {
        printf("%s: bad length\n", path);
        free(bytes);
        return 1;
    }
    world_new();
    for (index = 0; index < frames; index++) {
        const unsigned char *record = bytes + TRACE_HEADER_LEN + index * RECORD_LEN;
        if (index > 0) world_step(record[0], &events);
        world_encode(state);
        if (memcmp(state, record + 1, STATE_LEN) != 0) {
            size_t at = 0;
            while (at < STATE_LEN && state[at] == record[1 + at]) at++;
            printf("%s: diverges at record %lu, byte %zu (got %u, want %u)\n", path, index, at,
                   state[at], record[1 + at]);
            free(bytes);
            return 1;
        }
    }
    printf("%s: %lu frames match\n", path, frames);
    free(bytes);
    return 0;
}

static int check_saves(const char *dir) {
    char path[1024];
    size_t len;
    unsigned char *bytes;
    unsigned char encoded[SAVE_LEN];
    uint32_t best = 0;
    int failures = 0;

    snprintf(path, sizeof path, "%s/save_best_123456.bin", dir);
    bytes = read_file(path, &len);
    if (!bytes || len != SAVE_LEN || !save_decode(bytes, &best) || best != 123456UL) {
        printf("%s: decode failed\n", path);
        failures++;
    } else {
        save_encode(123456UL, encoded);
        if (memcmp(encoded, bytes, SAVE_LEN) != 0) {
            printf("%s: re-encode differs\n", path);
            failures++;
        } else {
            printf("%s: ok\n", path);
        }
    }
    free(bytes);

    snprintf(path, sizeof path, "%s/save_damaged.bin", dir);
    bytes = read_file(path, &len);
    if (!bytes || len != SAVE_LEN || save_decode(bytes, &best)) {
        printf("%s: damaged save was accepted\n", path);
        failures++;
    } else {
        printf("%s: rejected as expected\n", path);
    }
    free(bytes);
    return failures;
}

static int by_name(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "fixtures";
    DIR *d = opendir(dir);
    struct dirent *entry;
    char *names[64];
    int count = 0, i, failures = 0;
    if (!d) {
        printf("cannot open %s\n", dir);
        return 1;
    }
    while ((entry = readdir(d)) != NULL && count < 64) {
        size_t n = strlen(entry->d_name);
        if (n > 6 && strcmp(entry->d_name + n - 6, ".trace") == 0) {
            names[count] = malloc(strlen(dir) + n + 2);
            sprintf(names[count], "%s/%s", dir, entry->d_name);
            count++;
        }
    }
    closedir(d);
    qsort(names, (size_t)count, sizeof(char *), by_name);
    if (count == 0) {
        printf("no traces in %s\n", dir);
        return 1;
    }
    for (i = 0; i < count; i++) {
        failures += replay(names[i]);
        free(names[i]);
    }
    failures += check_saves(dir);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all %d traces and the save fixtures match\n", count);
    return 0;
}
