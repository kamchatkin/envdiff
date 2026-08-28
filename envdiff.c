#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifndef ENVDIFF_VERSION
#define ENVDIFF_VERSION "0.5.0"
#endif

typedef struct {
    char **items;
    size_t length;
    size_t capacity;
} Strings;

typedef struct {
    char *key;
    Strings comments;
    char *assignment;
} Entry;

typedef struct {
    Entry *items;
    size_t length;
    size_t capacity;
} Entries;

typedef struct {
    size_t missing_keys;
    size_t only_current_keys;
    size_t missing_comments;
    size_t missing_comments_on_existing_keys;
} DiffStats;

typedef struct {
#ifdef _WIN32
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
#else
    dev_t device;
    ino_t inode;
    mode_t mode;
#endif
} FileInfo;

static void out_of_memory(void)
{
    fputs("envdiff: out of memory\n", stderr);
    exit(2);
}

static void *xrealloc(void *pointer, size_t size)
{
    void *result = realloc(pointer, size);
    if (result == NULL && size != 0) {
        out_of_memory();
    }
    return result;
}

static char *xstrndup(const char *value, size_t length)
{
    char *result = malloc(length + 1);
    if (result == NULL) {
        out_of_memory();
    }
    memcpy(result, value, length);
    result[length] = '\0';
    return result;
}

static char *xstrdup(const char *value)
{
    return xstrndup(value, strlen(value));
}

static void strings_reserve(Strings *strings, size_t required)
{
    if (required <= strings->capacity) {
        return;
    }
    size_t capacity = strings->capacity == 0 ? 16 : strings->capacity;
    while (capacity < required) {
        capacity *= 2;
    }
    strings->items = xrealloc(strings->items, capacity * sizeof(*strings->items));
    strings->capacity = capacity;
}

static void strings_push_owned(Strings *strings, char *value)
{
    strings_reserve(strings, strings->length + 1);
    strings->items[strings->length++] = value;
}

static void strings_push_copy(Strings *strings, const char *value)
{
    strings_push_owned(strings, xstrdup(value));
}

static bool strings_contains(const Strings *strings, const char *value)
{
    for (size_t index = 0; index < strings->length; index++) {
        if (strcmp(strings->items[index], value) == 0) {
            return true;
        }
    }
    return false;
}

static void strings_clear(Strings *strings)
{
    for (size_t index = 0; index < strings->length; index++) {
        free(strings->items[index]);
    }
    free(strings->items);
    *strings = (Strings){0};
}

static void strings_insert_move(Strings *target, size_t index, Strings *inserted)
{
    if (inserted->length == 0) {
        return;
    }
    strings_reserve(target, target->length + inserted->length);
    memmove(
        target->items + index + inserted->length,
        target->items + index,
        (target->length - index) * sizeof(*target->items)
    );
    memcpy(
        target->items + index,
        inserted->items,
        inserted->length * sizeof(*target->items)
    );
    target->length += inserted->length;
    free(inserted->items);
    *inserted = (Strings){0};
}

static void strings_erase(Strings *strings, size_t start, size_t count)
{
    for (size_t index = start; index < start + count; index++) {
        free(strings->items[index]);
    }
    memmove(
        strings->items + start,
        strings->items + start + count,
        (strings->length - start - count) * sizeof(*strings->items)
    );
    strings->length -= count;
}

static void entries_push(Entries *entries, Entry entry)
{
    if (entries->length == entries->capacity) {
        size_t capacity = entries->capacity == 0 ? 16 : entries->capacity * 2;
        entries->items = xrealloc(entries->items, capacity * sizeof(*entries->items));
        entries->capacity = capacity;
    }
    entries->items[entries->length++] = entry;
}

static void entries_clear(Entries *entries)
{
    for (size_t index = 0; index < entries->length; index++) {
        free(entries->items[index].key);
        strings_clear(&entries->items[index].comments);
        free(entries->items[index].assignment);
    }
    free(entries->items);
    *entries = (Entries){0};
}

static bool is_blank(const char *line)
{
    while (*line != '\0') {
        if (!isspace((unsigned char)*line)) {
            return false;
        }
        line++;
    }
    return true;
}

static bool is_comment(const char *line)
{
    while (isspace((unsigned char)*line)) {
        line++;
    }
    return *line == '#';
}

static bool valid_key(const char *key, size_t length)
{
    if (length == 0 || !(isalpha((unsigned char)key[0]) || key[0] == '_')) {
        return false;
    }
    for (size_t index = 1; index < length; index++) {
        unsigned char character = (unsigned char)key[index];
        if (!(isalnum(character) || character == '_')) {
            return false;
        }
    }
    return true;
}

/* Returns only the name to the left of '='. The value is never extracted. */
static char *env_key(const char *line)
{
    while (isspace((unsigned char)*line)) {
        line++;
    }
    if (*line == '\0' || *line == '#') {
        return NULL;
    }
    if (strncmp(line, "export", 6) == 0 && isspace((unsigned char)line[6])) {
        line += 6;
        while (isspace((unsigned char)*line)) {
            line++;
        }
    }

    const char *equals = strchr(line, '=');
    if (equals == NULL) {
        return NULL;
    }
    const char *end = equals;
    while (end > line && isspace((unsigned char)end[-1])) {
        end--;
    }
    size_t length = (size_t)(end - line);
    if (!valid_key(line, length)) {
        return NULL;
    }
    return xstrndup(line, length);
}

static char *trimmed_copy(const char *value)
{
    while (isspace((unsigned char)*value)) {
        value++;
    }
    const char *end = value + strlen(value);
    while (end > value && isspace((unsigned char)end[-1])) {
        end--;
    }
    return xstrndup(value, (size_t)(end - value));
}

static bool read_lines(const char *path, Strings *lines, int *newline_style)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "envdiff: failed to read %s: %s\n", path, strerror(errno));
        return false;
    }

    char *content = NULL;
    size_t length = 0;
    size_t capacity = 0;
    unsigned char chunk[4096];
    size_t received;
    while ((received = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        if (length + received + 1 > capacity) {
            size_t next_capacity = capacity == 0 ? 8192 : capacity;
            while (next_capacity < length + received + 1) {
                next_capacity *= 2;
            }
            content = xrealloc(content, next_capacity);
            capacity = next_capacity;
        }
        memcpy(content + length, chunk, received);
        length += received;
    }

    bool success = !ferror(file);
    if (!success) {
        fprintf(stderr, "envdiff: failed to read %s: %s\n", path, strerror(errno));
    }
    fclose(file);
    if (!success) {
        free(content);
        return false;
    }
    if (length > 0 && memchr(content, '\0', length) != NULL) {
        fprintf(stderr, "envdiff: %s contains a NUL byte\n", path);
        free(content);
        return false;
    }

    size_t start = 0;
    for (size_t index = 0; index < length; index++) {
        if (content[index] != '\n') {
            continue;
        }
        size_t end = index;
        int style = 1;
        if (end > start && content[end - 1] == '\r') {
            end--;
            style = 2;
        }
        if (*newline_style == 0) {
            *newline_style = style;
        }
        strings_push_owned(lines, xstrndup(content + start, end - start));
        start = index + 1;
    }
    if (start < length) {
        strings_push_owned(lines, xstrndup(content + start, length - start));
    }

    free(content);
    return true;
}

static ptrdiff_t entry_index(const Entries *entries, const char *key)
{
    for (size_t index = 0; index < entries->length; index++) {
        if (strcmp(entries->items[index].key, key) == 0) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}

static bool parse_entries(
    const Strings *lines,
    Entries *entries,
    const char *file_description
)
{
    Strings pending_comments = {0};
    for (size_t index = 0; index < lines->length; index++) {
        const char *line = lines->items[index];
        if (is_comment(line)) {
            strings_push_copy(&pending_comments, line);
            continue;
        }
        if (is_blank(line)) {
            continue;
        }

        char *key = env_key(line);
        if (key == NULL) {
            strings_clear(&pending_comments);
            continue;
        }
        if (entry_index(entries, key) >= 0) {
            fprintf(stderr, "envdiff: duplicate key %s in %s\n", key, file_description);
            free(key);
            strings_clear(&pending_comments);
            return false;
        }

        Entry entry = {
            .key = key,
            .comments = pending_comments,
            .assignment = xstrdup(line),
        };
        pending_comments = (Strings){0};
        entries_push(entries, entry);
    }
    strings_clear(&pending_comments);
    return true;
}

static ptrdiff_t find_key(const Strings *lines, const char *wanted)
{
    for (size_t index = 0; index < lines->length; index++) {
        char *key = env_key(lines->items[index]);
        if (key == NULL) {
            continue;
        }
        bool matches = strcmp(key, wanted) == 0;
        free(key);
        if (matches) {
            return (ptrdiff_t)index;
        }
    }
    return -1;
}

static void collect_comments(const Strings *lines, Strings *known)
{
    for (size_t index = 0; index < lines->length; index++) {
        if (!is_comment(lines->items[index])) {
            continue;
        }
        char *identity = trimmed_copy(lines->items[index]);
        if (strings_contains(known, identity)) {
            free(identity);
        } else {
            strings_push_owned(known, identity);
        }
    }
}

static Strings new_comments(const Strings *comments, Strings *known, size_t *added)
{
    Strings result = {0};
    for (size_t index = 0; index < comments->length; index++) {
        char *identity = trimmed_copy(comments->items[index]);
        if (strings_contains(known, identity)) {
            free(identity);
            continue;
        }
        strings_push_owned(known, identity);
        strings_push_copy(&result, comments->items[index]);
        (*added)++;
    }
    return result;
}

static DiffStats calculate_diff(
    const Entries *example_entries,
    const Entries *current_entries,
    const Strings *current_lines
)
{
    DiffStats stats = {0};
    Strings known_comments = {0};
    collect_comments(current_lines, &known_comments);

    for (size_t index = 0; index < example_entries->length; index++) {
        const Entry *entry = &example_entries->items[index];
        if (entry_index(current_entries, entry->key) < 0) {
            stats.missing_keys++;
        }
        Strings comments = new_comments(
            &entry->comments,
            &known_comments,
            &stats.missing_comments
        );
        if (entry_index(current_entries, entry->key) >= 0) {
            stats.missing_comments_on_existing_keys += comments.length;
        }
        strings_clear(&comments);
    }

    for (size_t index = 0; index < current_entries->length; index++) {
        if (entry_index(example_entries, current_entries->items[index].key) < 0) {
            stats.only_current_keys++;
        }
    }

    strings_clear(&known_comments);
    return stats;
}

static bool diff_has_changes(const DiffStats *stats)
{
    return stats->missing_keys != 0
        || stats->only_current_keys != 0
        || stats->missing_comments != 0;
}

static void print_example_diff_section(
    const Entries *example_entries,
    const Entries *current_entries,
    const Strings *current_lines,
    bool print_missing_keys
)
{
    Strings known_comments = {0};
    collect_comments(current_lines, &known_comments);

    for (size_t index = 0; index < example_entries->length; index++) {
        const Entry *entry = &example_entries->items[index];
        bool key_is_missing = entry_index(current_entries, entry->key) < 0;
        size_t ignored_count = 0;
        Strings comments = new_comments(
            &entry->comments,
            &known_comments,
            &ignored_count
        );

        if (print_missing_keys && key_is_missing) {
            for (size_t comment = 0; comment < comments.length; comment++) {
                printf("+ %s\n", comments.items[comment]);
            }
            printf("+ %s\n\n", entry->assignment);
        } else if (!print_missing_keys && !key_is_missing && comments.length != 0) {
            for (size_t comment = 0; comment < comments.length; comment++) {
                printf("+ %s\n", comments.items[comment]);
            }
            printf("  %s (existing value is not compared)\n\n", entry->key);
        }

        strings_clear(&comments);
    }

    strings_clear(&known_comments);
}

static void print_check_diff(
    const char *current_path,
    const Entries *example_entries,
    const Entries *current_entries,
    const Strings *current_lines,
    const DiffStats *stats
)
{
    printf(
        "missing keys: %zu, only in current: %zu, new comments: %zu\n\n",
        stats->missing_keys,
        stats->only_current_keys,
        stats->missing_comments
    );

    if (stats->missing_keys != 0) {
        printf("Missing from %s:\n\n", current_path);
        print_example_diff_section(
            example_entries,
            current_entries,
            current_lines,
            true
        );
    }

    if (stats->missing_comments_on_existing_keys != 0) {
        printf("Comments missing from %s:\n\n", current_path);
        print_example_diff_section(
            example_entries,
            current_entries,
            current_lines,
            false
        );
    }

    if (stats->only_current_keys != 0) {
        printf("Only in %s (review before removing):\n\n", current_path);
        for (size_t index = 0; index < current_entries->length; index++) {
            const Entry *entry = &current_entries->items[index];
            if (entry_index(example_entries, entry->key) >= 0) {
                continue;
            }
            for (size_t comment = 0; comment < entry->comments.length; comment++) {
                printf("- %s\n", entry->comments.items[comment]);
            }
            printf("- %s=<value hidden>\n\n", entry->key);
        }
    }
}

static size_t preceding_comment_start(const Strings *lines, size_t key_index)
{
    size_t start = key_index;
    while (start > 0) {
        const char *line = lines->items[start - 1];
        if (!is_comment(line) && !is_blank(line)) {
            break;
        }
        start--;
    }
    return start;
}

static bool remove_current_only_entries(
    const Entries *example_entries,
    Strings *current_lines
)
{
    bool changed = false;
    size_t index = current_lines->length;

    while (index > 0) {
        index--;
        char *key = env_key(current_lines->items[index]);
        if (key == NULL) {
            continue;
        }
        bool current_only = entry_index(example_entries, key) < 0;
        free(key);
        if (!current_only) {
            continue;
        }

        size_t block_start = preceding_comment_start(current_lines, index);
        strings_erase(current_lines, block_start, index - block_start + 1);
        index = block_start;
        changed = true;
    }

    return changed;
}

static void merge_env(
    const Entries *entries,
    Strings *current,
    size_t *added_keys,
    size_t *added_comments
)
{
    Strings known_comments = {0};
    collect_comments(current, &known_comments);

    for (size_t entry_number = 0; entry_number < entries->length; entry_number++) {
        const Entry *entry = &entries->items[entry_number];
        ptrdiff_t current_index = find_key(current, entry->key);
        Strings block = new_comments(&entry->comments, &known_comments, added_comments);

        if (current_index >= 0) {
            strings_insert_move(current, (size_t)current_index, &block);
            continue;
        }

        strings_push_copy(&block, entry->assignment);
        size_t insert_at = current->length;
        for (size_t next = entry_number + 1; next < entries->length; next++) {
            ptrdiff_t next_index = find_key(current, entries->items[next].key);
            if (next_index >= 0) {
                insert_at = preceding_comment_start(current, (size_t)next_index);
                break;
            }
        }
        strings_insert_move(current, insert_at, &block);
        (*added_keys)++;
    }

    strings_clear(&known_comments);
}

static const char *last_path_separator(const char *path)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) {
        slash = backslash;
    }
#endif
    return slash;
}

static bool filename_contains_example(const char *path)
{
    static const char needle[] = "example";
    const char *separator = last_path_separator(path);
    const char *filename = separator == NULL ? path : separator + 1;
    size_t filename_length = strlen(filename);
    size_t needle_length = sizeof(needle) - 1;

    for (size_t start = 0; start + needle_length <= filename_length; start++) {
        bool matches = true;
        for (size_t index = 0; index < needle_length; index++) {
            unsigned char character = (unsigned char)filename[start + index];
            if (character >= 'A' && character <= 'Z') {
                character = (unsigned char)(character - 'A' + 'a');
            }
            if (character != (unsigned char)needle[index]) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

static char *directory_name(const char *path)
{
    const char *slash = last_path_separator(path);
    if (slash == NULL) {
        return xstrdup(".");
    }
    if (slash == path) {
        return xstrdup("/");
    }
#ifdef _WIN32
    if (slash == path + 2 && path[1] == ':') {
        return xstrndup(path, 3);
    }
#endif
    return xstrndup(path, (size_t)(slash - path));
}

#ifndef _WIN32
static const char *base_name(const char *path)
{
    const char *slash = last_path_separator(path);
    return slash == NULL ? path : slash + 1;
}
#endif

static bool read_file_info_if_exists(const char *path, FileInfo *info, bool *exists)
{
    *exists = false;
#ifdef _WIN32
    HANDLE file = CreateFileA(
        path,
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        fprintf(stderr, "envdiff: failed to inspect %s: Windows error %lu\n", path, error);
        return false;
    }

    BY_HANDLE_FILE_INFORMATION details;
    if (!GetFileInformationByHandle(file, &details)) {
        DWORD error = GetLastError();
        CloseHandle(file);
        fprintf(stderr, "envdiff: failed to inspect %s: Windows error %lu\n", path, error);
        return false;
    }
    CloseHandle(file);
    if ((details.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        fprintf(stderr, "envdiff: %s is not a regular file\n", path);
        return false;
    }

    info->volume_serial = details.dwVolumeSerialNumber;
    info->file_index_high = details.nFileIndexHigh;
    info->file_index_low = details.nFileIndexLow;
#else
    struct stat details;
    if (stat(path, &details) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return true;
        }
        fprintf(stderr, "envdiff: failed to inspect %s: %s\n", path, strerror(errno));
        return false;
    }
    if (!S_ISREG(details.st_mode)) {
        fprintf(stderr, "envdiff: %s is not a regular file\n", path);
        return false;
    }

    info->device = details.st_dev;
    info->inode = details.st_ino;
    info->mode = details.st_mode;
#endif
    *exists = true;
    return true;
}

static bool read_file_info(const char *path, FileInfo *info)
{
    bool exists;
    if (!read_file_info_if_exists(path, info, &exists)) {
        return false;
    }
    if (!exists) {
        fprintf(stderr, "envdiff: %s does not exist\n", path);
        return false;
    }
    return true;
}

static bool same_file(const FileInfo *first, const FileInfo *second)
{
#ifdef _WIN32
    return first->volume_serial == second->volume_serial
        && first->file_index_high == second->file_index_high
        && first->file_index_low == second->file_index_low;
#else
    return first->device == second->device && first->inode == second->inode;
#endif
}

static bool write_atomic(
    const char *path,
    const Strings *lines,
    const char *newline,
    const FileInfo *output_info,
    bool target_exists
)
{
    char *directory = directory_name(path);
#ifdef _WIN32
    (void)output_info;
    char temporary_buffer[MAX_PATH + 1];
    if (GetTempFileNameA(directory, "env", 0, temporary_buffer) == 0) {
        DWORD error = GetLastError();
        fprintf(stderr, "envdiff: failed to create temporary file: Windows error %lu\n", error);
        free(directory);
        return false;
    }
    char *temporary_path = xstrdup(temporary_buffer);
    free(directory);

    FILE *file = fopen(temporary_path, "wb");
    if (file == NULL) {
        fprintf(stderr, "envdiff: failed to open temporary file: %s\n", strerror(errno));
        DeleteFileA(temporary_path);
        free(temporary_path);
        return false;
    }
#else
    (void)target_exists;
    const char *base = base_name(path);
    size_t template_length = strlen(directory) + strlen(base) + 20;
    char *temporary_path = malloc(template_length);
    if (temporary_path == NULL) {
        free(directory);
        out_of_memory();
    }
    snprintf(temporary_path, template_length, "%s/.%s.envdiff.XXXXXX", directory, base);
    free(directory);

    int descriptor = mkstemp(temporary_path);
    if (descriptor < 0) {
        fprintf(stderr, "envdiff: failed to create temporary file: %s\n", strerror(errno));
        free(temporary_path);
        return false;
    }
    if (fchmod(descriptor, output_info->mode & 07777) != 0) {
        fprintf(stderr, "envdiff: failed to set temporary file permissions: %s\n", strerror(errno));
        close(descriptor);
        unlink(temporary_path);
        free(temporary_path);
        return false;
    }

    FILE *file = fdopen(descriptor, "wb");
    if (file == NULL) {
        fprintf(stderr, "envdiff: failed to open temporary file: %s\n", strerror(errno));
        close(descriptor);
        unlink(temporary_path);
        free(temporary_path);
        return false;
    }
#endif

    bool success = true;
    for (size_t index = 0; index < lines->length; index++) {
        if (fputs(lines->items[index], file) == EOF || fputs(newline, file) == EOF) {
            success = false;
            break;
        }
    }
    if (success && fflush(file) != 0) {
        success = false;
    }
#ifdef _WIN32
    if (success && _commit(_fileno(file)) != 0) {
#else
    if (success && fsync(fileno(file)) != 0) {
#endif
        success = false;
    }
    if (fclose(file) != 0) {
        success = false;
    }

    if (!success) {
        fprintf(stderr, "envdiff: failed to write temporary file: %s\n", strerror(errno));
#ifdef _WIN32
        DeleteFileA(temporary_path);
#else
        unlink(temporary_path);
#endif
        free(temporary_path);
        return false;
    }
#ifdef _WIN32
    BOOL replaced = target_exists
        ? ReplaceFileA(path, temporary_path, NULL, REPLACEFILE_WRITE_THROUGH, NULL, NULL)
        : MoveFileExA(
            temporary_path,
            path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        );
    if (!replaced) {
        DWORD error = GetLastError();
        fprintf(stderr, "envdiff: failed to update output file: Windows error %lu\n", error);
        DeleteFileA(temporary_path);
        free(temporary_path);
        return false;
    }
#else
    if (rename(temporary_path, path) != 0) {
        fprintf(stderr, "envdiff: failed to update output file: %s\n", strerror(errno));
        unlink(temporary_path);
        free(temporary_path);
        return false;
    }
#endif

    free(temporary_path);
    return true;
}

static void usage(FILE *stream)
{
    fputs(
        "Usage: envdiff [-c] [-f] [-r] "
        "[-i | -o FILE] <example.env> <current.env>\n",
        stream
    );
    fputs("Writes the merged environment to standard output by default.\n", stream);
    fputs("Existing values are never extracted, compared, or changed.\n", stream);
    fputs("\nOptions:\n", stream);
    fputs("  -o, --output FILE  Atomically write the result to FILE\n", stream);
    fputs("  -i, --in-place     Atomically update the current file\n", stream);
    fputs("  -c, --check        Show the structural difference without writing files\n", stream);
    fputs("  -r, --remove       Remove keys absent from the example during merge\n", stream);
    fputs("  -f, --force        Allow 'example' in the current file name\n", stream);
    fputs("  -h, --help         Show this help\n", stream);
    fputs("  -V, --version      Show the version\n", stream);
}

int main(int argc, char **argv)
{
    bool check = false;
    bool force = false;
    bool remove_extra = false;
    bool in_place = false;
    bool parse_options = true;
    const char *output_path = NULL;
    const char *paths[2] = {0};
    size_t path_count = 0;

    for (int index = 1; index < argc; index++) {
        if (parse_options && strcmp(argv[index], "--") == 0) {
            parse_options = false;
        } else if (parse_options
            && (strcmp(argv[index], "-c") == 0 || strcmp(argv[index], "--check") == 0)) {
            check = true;
        } else if (parse_options
            && (strcmp(argv[index], "-f") == 0 || strcmp(argv[index], "--force") == 0)) {
            force = true;
        } else if (parse_options
            && (strcmp(argv[index], "-r") == 0 || strcmp(argv[index], "--remove") == 0)) {
            remove_extra = true;
        } else if (parse_options
            && (strcmp(argv[index], "-i") == 0
                || strcmp(argv[index], "--in-place") == 0)) {
            in_place = true;
        } else if (parse_options
            && (strcmp(argv[index], "-o") == 0 || strcmp(argv[index], "--output") == 0)) {
            if (output_path != NULL) {
                fputs("envdiff: output option specified more than once\n", stderr);
                return 2;
            }
            if (++index >= argc) {
                fputs("envdiff: output option requires a file path\n", stderr);
                usage(stderr);
                return 2;
            }
            output_path = argv[index];
            if (*output_path == '\0') {
                fputs("envdiff: output option requires a file path\n", stderr);
                return 2;
            }
        } else if (parse_options && strncmp(argv[index], "--output=", 9) == 0) {
            if (output_path != NULL) {
                fputs("envdiff: output option specified more than once\n", stderr);
                return 2;
            }
            output_path = argv[index] + 9;
            if (*output_path == '\0') {
                fputs("envdiff: output option requires a file path\n", stderr);
                return 2;
            }
        } else if (parse_options
            && (strcmp(argv[index], "--version") == 0 || strcmp(argv[index], "-V") == 0)) {
            printf("envdiff %s\n", ENVDIFF_VERSION);
            return 0;
        } else if (parse_options
            && (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0)) {
            usage(stdout);
            return 0;
        } else if (parse_options && argv[index][0] == '-'
            && argv[index][1] != '\0' && argv[index][1] != '-') {
            for (const char *option = argv[index] + 1; *option != '\0'; option++) {
                switch (*option) {
                    case 'c':
                        check = true;
                        break;
                    case 'f':
                        force = true;
                        break;
                    case 'r':
                        remove_extra = true;
                        break;
                    case 'i':
                        in_place = true;
                        break;
                    case 'h':
                        usage(stdout);
                        return 0;
                    case 'V':
                        printf("envdiff %s\n", ENVDIFF_VERSION);
                        return 0;
                    default:
                        fprintf(stderr, "envdiff: unknown option: -%c\n", *option);
                        usage(stderr);
                        return 2;
                }
            }
        } else if (parse_options && argv[index][0] == '-') {
            fprintf(stderr, "envdiff: unknown option: %s\n", argv[index]);
            usage(stderr);
            return 2;
        } else if (path_count < 2) {
            paths[path_count++] = argv[index];
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (path_count != 2) {
        usage(stderr);
        return 2;
    }
    if (check && output_path != NULL) {
        fputs("envdiff: --check and --output cannot be used together\n", stderr);
        return 2;
    }
    if (check && in_place) {
        fputs("envdiff: --check and --in-place cannot be used together\n", stderr);
        return 2;
    }
    if (in_place && output_path != NULL) {
        fputs("envdiff: --in-place and --output cannot be used together\n", stderr);
        return 2;
    }
    if (!force && filename_contains_example(paths[1])) {
        fprintf(
            stderr,
            "envdiff: current file name contains 'example': %s\n",
            paths[1]
        );
        fputs(
            "envdiff: refusing to continue; use -f or --force to override\n",
            stderr
        );
        return 2;
    }
    if (in_place) {
        output_path = paths[1];
    }

    FileInfo example_info;
    FileInfo current_info;
    if (!read_file_info(paths[0], &example_info)
        || !read_file_info(paths[1], &current_info)) {
        return 2;
    }
    if (same_file(&example_info, &current_info)) {
        fputs("envdiff: example and current env must be different files\n", stderr);
        return 2;
    }

    Strings example_lines = {0};
    Strings current_lines = {0};
    int example_newline = 0;
    int current_newline = 0;
    if (!read_lines(paths[0], &example_lines, &example_newline)
        || !read_lines(paths[1], &current_lines, &current_newline)) {
        strings_clear(&example_lines);
        strings_clear(&current_lines);
        return 2;
    }

    Entries example_entries = {0};
    Entries current_entries = {0};
    if (!parse_entries(&example_lines, &example_entries, "example file")
        || !parse_entries(&current_lines, &current_entries, "current env file")) {
        entries_clear(&example_entries);
        entries_clear(&current_entries);
        strings_clear(&example_lines);
        strings_clear(&current_lines);
        return 2;
    }

    if (check) {
        DiffStats stats = calculate_diff(
            &example_entries,
            &current_entries,
            &current_lines
        );
        int result = 0;
        if (diff_has_changes(&stats)) {
            print_check_diff(
                paths[1],
                &example_entries,
                &current_entries,
                &current_lines,
                &stats
            );
            result = 1;
            if (fflush(stdout) != 0) {
                fprintf(
                    stderr,
                    "envdiff: failed to write check output: %s\n",
                    strerror(errno)
                );
                result = 2;
            }
        }
        entries_clear(&example_entries);
        entries_clear(&current_entries);
        strings_clear(&example_lines);
        strings_clear(&current_lines);
        return result;
    }

    size_t added_keys = 0;
    size_t added_comments = 0;
    bool removed_entries = remove_extra
        && remove_current_only_entries(&example_entries, &current_lines);
    merge_env(&example_entries, &current_lines, &added_keys, &added_comments);

    int newline_style = current_newline != 0 ? current_newline : example_newline;
    const char *newline = newline_style == 2 ? "\r\n" : "\n";

    int result = 0;
    if (output_path == NULL) {
#ifdef _WIN32
        if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
            fprintf(stderr, "envdiff: failed to set binary output mode: %s\n", strerror(errno));
            result = 2;
        } else
#endif
        {
            for (size_t index = 0; index < current_lines.length; index++) {
                if (fputs(current_lines.items[index], stdout) == EOF
                    || fputs(newline, stdout) == EOF) {
                    result = 2;
                    break;
                }
            }
            if (result == 0 && fflush(stdout) != 0) {
                result = 2;
            }
            if (result != 0) {
                fprintf(stderr, "envdiff: failed to write standard output: %s\n", strerror(errno));
            }
        }
    } else {
        FileInfo output_info = current_info;
        bool output_exists = false;
        if (!read_file_info_if_exists(output_path, &output_info, &output_exists)) {
            result = 2;
        } else if ((added_keys != 0 || added_comments != 0 || removed_entries
                || !output_exists || !same_file(&output_info, &current_info))
            && !write_atomic(
                output_path,
                &current_lines,
                newline,
                &output_info,
                output_exists
            )) {
            result = 2;
        }
    }

    entries_clear(&example_entries);
    entries_clear(&current_entries);
    strings_clear(&example_lines);
    strings_clear(&current_lines);
    return result;
}
