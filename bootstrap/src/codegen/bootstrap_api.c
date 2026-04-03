/*
 * bootstrap_api.c
 * ================
 * Implements bootstrap_compile() by exec-ing the bootstrap binary.
 * No %s format strings in snprintf — uses strcpy/strcat instead to avoid
 * fprintf(wf, ...) escaping issues when embedded in the C wrapper.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static const char* find_bootstrap(void) {
    const char* env = getenv("CPRIME_BOOTSTRAP");
    if (env && access(env, X_OK) == 0) return env;
    if (access("/usr/bin/cpc-bootstrap", X_OK) == 0) return "/usr/bin/cpc-bootstrap";
    if (access("./build/bootstrap/cpc-bootstrap", X_OK) == 0)
        return "./build/bootstrap/cpc-bootstrap";
    if (access("../build/bootstrap/cpc-bootstrap", X_OK) == 0)
        return "../build/bootstrap/cpc-bootstrap";
    return "cpc-bootstrap";
}

/* Build command without snprintf format strings to avoid escaping issues */
static void build_cmd(char* cmd, size_t cap,
                      const char* bootstrap,
                      const char* input, const char* output,
                      int dump_tokens, int dump_ast, int dump_asm, int opt)
{
    cmd[0] = '\0';
    /* bootstrap "input" -o "output" */
    strncat(cmd, bootstrap, cap - strlen(cmd) - 1);
    strncat(cmd, " \"",     cap - strlen(cmd) - 1);
    strncat(cmd, input,     cap - strlen(cmd) - 1);
    strncat(cmd, "\" -o \"",cap - strlen(cmd) - 1);
    strncat(cmd, output,    cap - strlen(cmd) - 1);
    strncat(cmd, "\"",      cap - strlen(cmd) - 1);
    if (dump_tokens) strncat(cmd, " --dump-tokens", cap - strlen(cmd) - 1);
    if (dump_ast)    strncat(cmd, " --dump-ast",    cap - strlen(cmd) - 1);
    if (dump_asm)    strncat(cmd, " --dump-asm",    cap - strlen(cmd) - 1);
    if (opt)         strncat(cmd, " -O",            cap - strlen(cmd) - 1);
}

int bootstrap_compile(const char* input_file,
                      const char* output_file,
                      int dump_tokens,
                      int dump_ast,
                      int dump_asm,
                      int optimize_flag)
{
    const char* bootstrap = find_bootstrap();
    char cmd[4096];
    build_cmd(cmd, sizeof(cmd), bootstrap, input_file, output_file,
              dump_tokens, dump_ast, dump_asm, optimize_flag);
    int rc = system(cmd);
    if (rc == -1) return 1;
    return WEXITSTATUS(rc);
}
