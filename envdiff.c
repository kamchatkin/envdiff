#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef ENVDIFF_VERSION
#define ENVDIFF_VERSION "0.1.0"
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

static void out_of_memory(void)
{
    fputs("envdiff: недостаточно памяти\n", stderr);
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

/* Возвращается только имя слева от '='. Значение справа не извлекается. */
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
        fprintf(stderr, "envdiff: прочитать %s: %s\n", path, strerror(errno));
        return false;
    }

    char *buffer = NULL;
    size_t capacity = 0;
    ssize_t length;
    while ((length = getline(&buffer, &capacity, file)) >= 0) {
        if (memchr(buffer, '\0', (size_t)length) != NULL) {
            fprintf(stderr, "envdiff: %s содержит нулевой байт\n", path);
            free(buffer);
            fclose(file);
            return false;
        }
        if (length > 0 && buffer[length - 1] == '\n') {
            length--;
            int style = 1;
            if (length > 0 && buffer[length - 1] == '\r') {
                length--;
                style = 2;
            }
            if (*newline_style == 0) {
                *newline_style = style;
            }
        }
        strings_push_owned(lines, xstrndup(buffer, (size_t)length));
    }

    bool success = !ferror(file);
    if (!success) {
        fprintf(stderr, "envdiff: прочитать %s: %s\n", path, strerror(errno));
    }
    free(buffer);
    fclose(file);
    return success;
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

static bool parse_example(const Strings *lines, Entries *entries)
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
            fprintf(stderr, "envdiff: ключ %s повторяется в example\n", key);
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

static bool validate_current_keys(const Strings *lines)
{
    Strings keys = {0};
    for (size_t index = 0; index < lines->length; index++) {
        char *key = env_key(lines->items[index]);
        if (key == NULL) {
            continue;
        }
        if (strings_contains(&keys, key)) {
            fprintf(stderr, "envdiff: ключ %s повторяется в основном env\n", key);
            free(key);
            strings_clear(&keys);
            return false;
        }
        strings_push_owned(&keys, key);
    }
    strings_clear(&keys);
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

static char *directory_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL) {
        return xstrdup(".");
    }
    if (slash == path) {
        return xstrdup("/");
    }
    return xstrndup(path, (size_t)(slash - path));
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash == NULL ? path : slash + 1;
}

static bool write_atomic(
    const char *path,
    const Strings *lines,
    const char *newline,
    mode_t mode
)
{
    char *directory = directory_name(path);
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
        fprintf(stderr, "envdiff: создать временный файл: %s\n", strerror(errno));
        free(temporary_path);
        return false;
    }
    if (fchmod(descriptor, mode) != 0) {
        fprintf(stderr, "envdiff: установить права временного файла: %s\n", strerror(errno));
        close(descriptor);
        unlink(temporary_path);
        free(temporary_path);
        return false;
    }

    FILE *file = fdopen(descriptor, "wb");
    if (file == NULL) {
        fprintf(stderr, "envdiff: открыть временный файл: %s\n", strerror(errno));
        close(descriptor);
        unlink(temporary_path);
        free(temporary_path);
        return false;
    }

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
    if (success && fsync(descriptor) != 0) {
        success = false;
    }
    if (fclose(file) != 0) {
        success = false;
    }

    if (!success) {
        fprintf(stderr, "envdiff: записать временный файл: %s\n", strerror(errno));
        unlink(temporary_path);
        free(temporary_path);
        return false;
    }
    if (rename(temporary_path, path) != 0) {
        fprintf(stderr, "envdiff: заменить основной env: %s\n", strerror(errno));
        unlink(temporary_path);
        free(temporary_path);
        return false;
    }

    free(temporary_path);
    return true;
}

static void usage(FILE *stream)
{
    fputs("Использование: envdiff [--check] <example.env> <current.env>\n", stream);
    fputs("Добавляет отсутствующие ключи и новые комментарии.\n", stream);
    fputs("Существующие значения не извлекаются, не сравниваются и не изменяются.\n", stream);
}

int main(int argc, char **argv)
{
    bool check = false;
    const char *paths[2] = {0};
    size_t path_count = 0;

    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--check") == 0) {
            check = true;
        } else if (strcmp(argv[index], "--version") == 0 || strcmp(argv[index], "-V") == 0) {
            printf("envdiff %s\n", ENVDIFF_VERSION);
            return 0;
        } else if (strcmp(argv[index], "--help") == 0 || strcmp(argv[index], "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (argv[index][0] == '-') {
            fprintf(stderr, "envdiff: неизвестный параметр: %s\n", argv[index]);
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

    struct stat example_stat;
    struct stat current_stat;
    if (stat(paths[0], &example_stat) != 0) {
        fprintf(stderr, "envdiff: получить сведения о %s: %s\n", paths[0], strerror(errno));
        return 2;
    }
    if (stat(paths[1], &current_stat) != 0) {
        fprintf(stderr, "envdiff: получить сведения о %s: %s\n", paths[1], strerror(errno));
        return 2;
    }
    if (example_stat.st_dev == current_stat.st_dev && example_stat.st_ino == current_stat.st_ino) {
        fputs("envdiff: example и основной env должны быть разными файлами\n", stderr);
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

    Entries entries = {0};
    if (!parse_example(&example_lines, &entries) || !validate_current_keys(&current_lines)) {
        entries_clear(&entries);
        strings_clear(&example_lines);
        strings_clear(&current_lines);
        return 2;
    }

    size_t added_keys = 0;
    size_t added_comments = 0;
    merge_env(&entries, &current_lines, &added_keys, &added_comments);

    int result = 0;
    if (added_keys == 0 && added_comments == 0) {
        puts("изменений нет");
    } else if (check) {
        printf(
            "требуется добавить ключей: %zu, комментариев: %zu\n",
            added_keys,
            added_comments
        );
        result = 1;
    } else {
        int newline_style = current_newline != 0 ? current_newline : example_newline;
        const char *newline = newline_style == 2 ? "\r\n" : "\n";
        if (!write_atomic(paths[1], &current_lines, newline, current_stat.st_mode & 07777)) {
            result = 2;
        } else {
            printf("добавлено ключей: %zu, комментариев: %zu\n", added_keys, added_comments);
        }
    }

    entries_clear(&entries);
    strings_clear(&example_lines);
    strings_clear(&current_lines);
    return result;
}
