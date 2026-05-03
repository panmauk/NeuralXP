// Minimal dlfcn stub for MinGW static linking (OpenACC profiling needs these)
#include <windows.h>

void *dlopen(const char *filename, int flags) {
    (void)flags;
    if (!filename) return (void*)GetModuleHandle(NULL);
    return (void*)LoadLibraryA(filename);
}

void *dlsym(void *handle, const char *symbol) {
    return (void*)GetProcAddress((HMODULE)handle, symbol);
}

int dlclose(void *handle) {
    return FreeLibrary((HMODULE)handle) ? 0 : -1;
}

const char *dlerror(void) {
    return "dl not supported";
}
