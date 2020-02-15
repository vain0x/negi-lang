// LICENSE: CC0-1.0 <https://creativecommons.org/publicdomain/zero/1.0/deed.ja>

#ifndef NEIG_LANG_H
#define NEGI_LANG_H

// ネギ言語処理系 ヘッダー

struct NegiLangContext;

typedef struct NegiLangExternals {
    const char *src;
    const char **output;
    int *exit_code;

    const char *(*stdin_to_str)();
} NegiLangExternals;

#endif
