
#ifndef HAVE_JSON_LUA
#define HAVE_JSON_LUA
#include <json/json.h>

int json_object_to_lua (lua_State *L, json_object *o);
json_object * lua_value_to_json (lua_State *L, int index);

#endif /* HAVE_JSON_LUA */
