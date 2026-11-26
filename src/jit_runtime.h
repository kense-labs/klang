#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const void *address;
} KlangJitSymbol;

const KlangJitSymbol *klang_jit_symbols(void);
int klang_jit_run(int (*entry)(int, char **), void (*cleanup)(void),
                  int argc, char **argv, const char *resource_dir);

#ifdef __cplusplus
}
#endif
