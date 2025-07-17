#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

Datum hello_world_c(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(hello_world_c);

Datum
hello_world_c(PG_FUNCTION_ARGS)
{
    text *input = PG_GETARG_TEXT_P(0);
    char *input_str = text_to_cstring(input);
    char *output_str = (char *) palloc(strlen(input_str) + 7+7 + 1); // "Hello from c ," + input + '\0'

    sprintf(output_str, "Hello from c, %s", input_str);
    PG_RETURN_TEXT_P(cstring_to_text(output_str));
}
