#include "negi_lang.h"
#include "negi_lang_internals.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    Ctx *ctx = (Ctx *)malloc(sizeof(Ctx));
    memset(ctx, 0, sizeof(Ctx));

    negi_lang_test_util();

    {
        const char *str = negi_lang_tokenize_dump("(1 ++ 2)");
        assert(strcmp(str, "(,1,++,2,),,") == 0);
    }

    {
        const char *str = negi_lang_parse_dump(" 42 ");
        assert(strcmp(str, "42") == 0);
    }

    {
        const char *str = negi_lang_parse_dump("1 + 2 * (3 / 4)");
        assert(strcmp(str, "(+ 1 (* 2 (/ 3 4)))") == 0);
    }

    {
        const char *str = negi_lang_parse_dump("let x = fs[0]() < -1");
        assert(strcmp(str, "(let (< (paren (bracket fs 0)) (- 0 1)))") == 0);
    }

    {
        const char *str = negi_lang_parse_dump(
            "if (p1) { q1 } else if (p2) { q2 } else { if (p3) {} }");
        assert(strcmp(str, "(if p1 (; q1 0) (if p2 (; q2 0) (; (if p3 (; 0 0) 0) 0)))") == 0);
    }

    {
        const char *str = negi_lang_gen_dump(
            "let a = 1; a += 1; if (a % 2 == 0) { a /= 2 } else { a *= 3 } a"
        );
        fprintf(stderr, "%s\n", str);
    }

    fprintf(stderr, "SUCCESS!\n");
    return 0;
}
