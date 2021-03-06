// LICENSE: CC0-1.0 <https://creativecommons.org/publicdomain/zero/1.0/deed.ja>

#include "negi_lang.h"
#include "negi_lang_internals.h"
#include "tomlc99/toml.h"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct EvalTestCase {
    const char *name;
    const char *src;
    const char *err;
    int exit;
    bool skip;
    bool only;
} EvalTestCase;

static EvalTestCase eval_tests[1024];
static int eval_test_len;

static const char *stdin_buffer = "";

static const char *stdin_to_str() { return stdin_buffer; }

static bool str_roughly_equals(const char *s, const char *t) {
    int si = 0;
    int ti = 0;
    int sn = strlen(s);
    int tn = strlen(t);

    while (si < sn || ti < tn) {
        if (si < sn && isspace(s[si])) {
            si++;
            continue;
        }
        if (ti < tn && isspace(t[ti])) {
            ti++;
            continue;
        }
        if (si < sn && ti < tn && s[si] == t[ti]) {
            si++;
            ti++;
            continue;
        }
        return false;
    }
    return true;
}

static char *file_read_all(const char *file_name) {
    FILE *file = fopen(file_name, "r");
    if (!file) {
        fprintf(stderr, "File '%s' not found.", file_name);
        abort();
    }

    fseek(file, 0, SEEK_END);
    int size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *content = calloc(size + 1, sizeof(char));
    fread(content, 1, size, file);

    fclose(file);
    return content;
}

static void parse_tests() {
    const int toml_success = 0;
    const char *file_name = "tests.toml";

    char err_buf[1024];

    char *toml = file_read_all(file_name);
    toml_table_t *top = toml_parse(toml, err_buf, sizeof(err_buf));
    if (top == NULL) {
        fprintf(stderr, "Error in '%s':\n%s\n", file_name, err_buf);
        abort();
    }

    toml_array_t *evals = toml_array_in(top, "eval");
    for (int i = 0; i < toml_array_nelem(evals); i++) {
        toml_table_t *eval = toml_table_at(evals, i);

        char *name;
        if (toml_rtos(toml_raw_in(eval, "name"), &name) != toml_success) {
            fprintf(stderr, "eval[%d].name is missing\n", i);
            name = "anonymous";
        }

        char *src;
        if (toml_rtos(toml_raw_in(eval, "src"), &src) != toml_success) {
            fprintf(stderr, "eval[%d].src is missing (name = %s)\n", i, name);
            src = "";
        }

        char *err;
        if (toml_rtos(toml_raw_in(eval, "err"), &err) != toml_success) {
            err = "";
        }

        int64_t exit;
        if (toml_rtoi(toml_raw_in(eval, "exit"), &exit) != toml_success) {
            exit = strlen(err) == 0 ? 0 : 1;
        }

        int skip;
        if (toml_rtob(toml_raw_in(eval, "skip"), &skip) != toml_success) {
            skip = 0;
        }

        int only;
        if (toml_rtob(toml_raw_in(eval, "only"), &only) != toml_success) {
            only = 0;
        }

        eval_tests[eval_test_len++] = (EvalTestCase){
            .src = src,
            .name = name,
            .err = err,
            .exit = (int)exit,
            .skip = skip != 0,
            .only = only != 0,
        };
    }
};

void some_tests() { negi_lang_test_util(); }

void eval_test_print_heading(int i, bool ok) {
    if (!ok)
        return;

    fprintf(stderr, "[[eval]] %s\nsrc = %s\n", eval_tests[i].name,
            eval_tests[i].src);

    const char *tokenize_dump = negi_lang_tokenize_dump(eval_tests[i].src);
    fprintf(stderr, "tokenize_dump: %s\n", tokenize_dump);

    const char *parse_dump = negi_lang_parse_dump(eval_tests[i].src);
    fprintf(stderr, "parse_dump: %s\n", parse_dump);
}

int main() {
    some_tests();

    int pass_count = 0;
    int fail_count = 0;
    int skip_count = 0;

    parse_tests();

    bool only;
    for (int i = 0; i < eval_test_len; i++) {
        if (eval_tests[i].only) {
            only = true;
        }
    }

    for (int i = 0; i < eval_test_len; i++) {
        EvalTestCase *eval = &eval_tests[i];

        if (eval->skip || (only && !eval->only)) {
            skip_count++;
            continue;
        }

        // eval_test_print_heading(i, true);

        bool ok = true;
        int exit;
        const char *err;
        NegiLangExternals externals = (NegiLangExternals){
            .src = eval->src,
            .exit_code = &exit,
            .output = &err,
            .stdin_to_str = stdin_to_str,
        };
        negi_lang_eval_for_testing(&externals);

        if (exit != eval->exit) {
            eval_test_print_heading(i, ok);
            ok = false;

            fprintf(stderr, "Exit Code:\n  Expected = %d\n  Actual = %d\n",
                    eval->exit, exit);
        }

        if (!str_roughly_equals(err, eval->err)) {
            eval_test_print_heading(i, ok);
            ok = false;

            fprintf(stderr,
                    "Error output:\n  Expected = \"\"\"\n%s\n\"\"\"\n  Actual "
                    "= \"\"\"\n%s\n\"\"\"\n",
                    eval->err, err);
        }

        if (ok) {
            pass_count++;
        } else {
            fail_count++;
        }
    }

    {
        const char *negi = file_read_all("negi_lang.negi");

        int exit;
        const char *err;
        NegiLangExternals externals = (NegiLangExternals){
            .src = negi,
            .exit_code = &exit,
            .output = &err,
            .stdin_to_str = stdin_to_str,
        };

        stdin_buffer = "0";
        negi_lang_eval_for_testing(&externals);

        if (exit == 0) {
            pass_count++;
        } else {
            fail_count++;

            fprintf(stderr, "Exit = %d\nError = %s\n", exit, err);
        }
    }

    bool ok = fail_count == 0;
    const char *status = ok ? "SUCCESS" : "FAILURE";

    fprintf(stderr, "Result: %d pass / %d fail / %d skip\nStatus: %s\n",
            pass_count, fail_count, skip_count, status);

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
