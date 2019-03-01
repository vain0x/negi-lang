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
        Str *str = negi_lang_tokenize_dump("(1 ++ 2)");
        assert(strcmp(str->data, "(,1,++,2,),,") == 0);
    }

    {
        Str *str = negi_lang_parse_dump(" 42 ");
        assert(strcmp(str->data, "42") == 0);
    }

    fprintf(stderr, "SUCCESS!\n");
    return 0;
}
