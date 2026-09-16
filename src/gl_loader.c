#include "gl_loader.h"
#include "platform.h"
#include <SDL3/SDL.h>

#define BH_GL_DEFINE(type, name) type name = 0;
BH_GL_FUNCS(BH_GL_DEFINE)
#undef BH_GL_DEFINE

static const char *strip_prefix(const char *name) {
    return (name[0] == 'b' && name[1] == 'h' && name[2] == '_') ? name + 3 : name;
}

const char *gl_load_functions(void) {
#define BH_GL_LOAD(type, name) \
    name = (type)SDL_GL_GetProcAddress(strip_prefix(#name)); \
    if (!name) return strip_prefix(#name);
    BH_GL_FUNCS(BH_GL_LOAD)
#undef BH_GL_LOAD
    return 0;
}

int gl_check(const char *where) {
    GLenum e = glGetError();
    if (e == GL_NO_ERROR) return 0;
    do {
        plat_log("GL error 0x%04X at %s", (unsigned)e, where);
        e = glGetError();
    } while (e != GL_NO_ERROR);
    return 1;
}
