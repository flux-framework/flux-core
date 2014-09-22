#ifndef HAVE_LUTIL_H
#define HAVE_LUTIL_H
int lua_pusherror (lua_State *L, char *fmt, ...);
int l_pushresult (lua_State *L, int rc);
int l_loadlibrary (lua_State *L, const char *name);
int l_format_args (lua_State *L, int index);
#endif /* !HAVE_LUTIL_H */
