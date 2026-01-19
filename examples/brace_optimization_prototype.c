/**
 * ブレース展開最適化のプロトタイプ実装
 *
 * 機能:
 *  - パターン解析
 *  - 共通部分抽出
 *  - 最適化されたマッチング
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// ========================================
// データ構造
// ========================================

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} string_list_t;

typedef struct
{
    char **strings;
    uint32_t *hashes;
    size_t count;
} string_set_t;

typedef struct
{
    char *common_prefix;
    char *common_suffix;
    char **alternatives;
    size_t alt_count;
    string_set_t *alt_set;
    bool is_simple;
} brace_info_t;

// ========================================
// ユーティリティ関数
// ========================================

static uint32_t hash_string(const char *str)
{
    uint32_t hash = 2166136261u;
    while (*str)
    {
        hash ^= (uint8_t)*str++;
        hash *= 16777619u;
    }
    return hash;
}

static string_set_t *create_string_set(char **strings, size_t count)
{
    string_set_t *set = malloc(sizeof(*set));
    set->strings = strings;
    set->count = count;
    set->hashes = malloc(sizeof(uint32_t) * count);

    for (size_t i = 0; i < count; i++)
    {
        set->hashes[i] = hash_string(strings[i]);
    }

    return set;
}

static bool string_set_contains(const string_set_t *set, const char *str)
{
    uint32_t hash = hash_string(str);

    for (size_t i = 0; i < set->count; i++)
    {
        if (set->hashes[i] == hash && strcmp(set->strings[i], str) == 0)
        {
            return true;
        }
    }
    return false;
}

static void free_string_set(string_set_t *set)
{
    if (set)
    {
        free(set->hashes);
        free(set);
    }
}

// ========================================
// ブレース展開パーサー
// ========================================

static const char *find_matching_brace(const char *pattern)
{
    int depth = 1;
    const char *p = pattern + 1; // '{' の次から

    while (*p && depth > 0)
    {
        if (*p == '\\' && *(p + 1))
        {
            p += 2;
            continue;
        }
        if (*p == '{')
            depth++;
        else if (*p == '}')
            depth--;

        if (depth == 0)
            return p;
        p++;
    }
    return NULL;
}

static char **parse_alternatives(const char *start, const char *end, size_t *count)
{
    // カンマで分割
    size_t capacity = 4;
    char **alts = malloc(sizeof(char *) * capacity);
    size_t n = 0;

    const char *chunk_start = start;
    const char *p = start;
    int depth = 0;

    while (p < end)
    {
        if (*p == '\\' && *(p + 1))
        {
            p += 2;
            continue;
        }

        if (*p == '{')
            depth++;
        else if (*p == '}')
            depth--;
        else if (*p == ',' && depth == 0)
        {
            // 分割点
            size_t len = p - chunk_start;
            alts[n] = malloc(len + 1);
            memcpy(alts[n], chunk_start, len);
            alts[n][len] = '\0';
            n++;

            if (n >= capacity)
            {
                capacity *= 2;
                alts = realloc(alts, sizeof(char *) * capacity);
            }

            chunk_start = p + 1;
        }
        p++;
    }

    // 最後のチャンク
    size_t len = end - chunk_start;
    alts[n] = malloc(len + 1);
    memcpy(alts[n], chunk_start, len);
    alts[n][len] = '\0';
    n++;

    *count = n;
    return alts;
}

static brace_info_t *analyze_brace_pattern(const char *pattern)
{
    // ブレースを探す
    const char *brace_open = strchr(pattern, '{');
    if (!brace_open)
        return NULL;

    const char *brace_close = find_matching_brace(brace_open);
    if (!brace_close)
        return NULL;

    brace_info_t *info = malloc(sizeof(*info));

    // プレフィックス
    size_t prefix_len = brace_open - pattern;
    info->common_prefix = malloc(prefix_len + 1);
    memcpy(info->common_prefix, pattern, prefix_len);
    info->common_prefix[prefix_len] = '\0';

    // サフィックス
    const char *suffix_start = brace_close + 1;
    info->common_suffix = strdup(suffix_start);

    // 代替文字列
    info->alternatives = parse_alternatives(
        brace_open + 1, brace_close, &info->alt_count);

    // ハッシュセット構築
    info->alt_set = create_string_set(info->alternatives, info->alt_count);

    // 単純性チェック（ワイルドカード検査）
    info->is_simple = (strchr(info->common_prefix, '*') == NULL) &&
                      (strchr(info->common_prefix, '?') == NULL) &&
                      (strchr(info->common_suffix, '*') == NULL) &&
                      (strchr(info->common_suffix, '?') == NULL);

    for (size_t i = 0; i < info->alt_count && info->is_simple; i++)
    {
        if (strchr(info->alternatives[i], '*') ||
            strchr(info->alternatives[i], '?'))
        {
            info->is_simple = false;
        }
    }

    return info;
}

static void free_brace_info(brace_info_t *info)
{
    if (!info)
        return;

    free(info->common_prefix);
    free(info->common_suffix);

    for (size_t i = 0; i < info->alt_count; i++)
    {
        free(info->alternatives[i]);
    }
    free(info->alternatives);

    free_string_set(info->alt_set);
    free(info);
}

// ========================================
// ディレクトリからファイル名部分を抽出
// ========================================

static const char *extract_filename_part(const char *path)
{
    const char *last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

static void extract_directory_part(const char *path, char *dir_out)
{
    const char *last_slash = strrchr(path, '/');
    if (last_slash)
    {
        size_t len = last_slash - path;
        memcpy(dir_out, path, len);
        dir_out[len] = '\0';
    }
    else
    {
        strcpy(dir_out, ".");
    }
}

// ========================================
// 最適化されたマッチング
// ========================================

static void match_simple_brace(
    const brace_info_t *info,
    const char *base_path,
    string_list_t *results)
{
    // ディレクトリパス構築
    char dir_path[PATH_MAX];
    if (info->common_prefix[0] == '\0')
    {
        strcpy(dir_path, base_path ? base_path : ".");
    }
    else
    {
        extract_directory_part(info->common_prefix, dir_path);
        if (base_path && base_path[0])
        {
            char temp[PATH_MAX];
            snprintf(temp, PATH_MAX, "%s/%s", base_path, dir_path);
            strcpy(dir_path, temp);
        }
    }

    // ファイル名プレフィックス
    const char *filename_prefix = extract_filename_part(info->common_prefix);
    size_t prefix_len = strlen(filename_prefix);
    size_t suffix_len = strlen(info->common_suffix);

    printf("  [最適化] ディレクトリスキャン: %s\n", dir_path);
    printf("  [最適化] プレフィックス: '%s', サフィックス: '%s'\n",
           filename_prefix, info->common_suffix);
    printf("  [最適化] 代替: {");
    for (size_t i = 0; i < info->alt_count; i++)
    {
        printf("%s%s", info->alternatives[i],
               i < info->alt_count - 1 ? ", " : "");
    }
    printf("}\n\n");

    // ディレクトリを1回だけ開く
    DIR *dir = opendir(dir_path);
    if (!dir)
    {
        printf("  [エラー] ディレクトリが開けません: %s\n", dir_path);
        return;
    }

    size_t matched = 0;
    size_t scanned = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        const char *name = entry->d_name;
        scanned++;

        // ドットファイルをスキップ
        if (name[0] == '.')
            continue;

        size_t name_len = strlen(name);

        // 長さチェック
        if (name_len < prefix_len + suffix_len)
        {
            continue;
        }

        // プレフィックスマッチ
        if (memcmp(name, filename_prefix, prefix_len) != 0)
        {
            continue;
        }

        // サフィックスマッチ
        if (memcmp(name + name_len - suffix_len,
                   info->common_suffix, suffix_len) != 0)
        {
            continue;
        }

        // 中間部分抽出
        size_t middle_len = name_len - prefix_len - suffix_len;
        char middle[middle_len + 1];
        memcpy(middle, name + prefix_len, middle_len);
        middle[middle_len] = '\0';

        // ハッシュセットチェック
        if (string_set_contains(info->alt_set, middle))
        {
            printf("  ✓ マッチ: %s (中間='%s')\n", name, middle);

            // 結果に追加
            if (results->count >= results->capacity)
            {
                results->capacity *= 2;
                results->items = realloc(results->items,
                                         sizeof(char *) * results->capacity);
            }

            char full_path[PATH_MAX];
            snprintf(full_path, PATH_MAX, "%s/%s", dir_path, name);
            results->items[results->count++] = strdup(full_path);
            matched++;
        }
    }

    closedir(dir);

    printf("\n  [統計] スキャン: %zu ファイル, マッチ: %zu\n",
           scanned, matched);
}

// ========================================
// メイン関数（デモ）
// ========================================

int main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("ブレース展開最適化プロトタイプ\n");
    printf("========================================\n\n");

    const char *pattern = argc > 1 ? argv[1] : "tests/fixtures/test_{a,b,c}.txt";
    const char *base_path = argc > 2 ? argv[2] : NULL;

    printf("パターン: %s\n", pattern);
    printf("ベースパス: %s\n\n", base_path ? base_path : "(なし)");

    // パターン解析
    printf("--- パターン解析 ---\n");
    brace_info_t *info = analyze_brace_pattern(pattern);

    if (!info)
    {
        printf("ブレース展開が見つかりません。\n");
        return 1;
    }

    printf("プレフィックス: '%s'\n", info->common_prefix);
    printf("サフィックス: '%s'\n", info->common_suffix);
    printf("代替数: %zu\n", info->alt_count);
    printf("単純パターン: %s\n\n", info->is_simple ? "はい" : "いいえ");

    // マッチング実行
    printf("--- マッチング実行 ---\n");

    string_list_t results = {
        .items = malloc(sizeof(char *) * 16),
        .count = 0,
        .capacity = 16};

    if (info->is_simple)
    {
        match_simple_brace(info, base_path, &results);
    }
    else
    {
        printf("複雑なパターンはフォールバックが必要です。\n");
    }

    // 結果表示
    printf("\n--- 結果 ---\n");
    printf("マッチ数: %zu\n", results.count);
    for (size_t i = 0; i < results.count; i++)
    {
        printf("  %zu: %s\n", i + 1, results.items[i]);
        free(results.items[i]);
    }

    // クリーンアップ
    free(results.items);
    free_brace_info(info);

    return 0;
}
