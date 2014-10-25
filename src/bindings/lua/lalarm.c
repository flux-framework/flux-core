/*
* lalarm.c
* an alarm library for Lua 5.1 based on signal
* Luiz Henrique de Figueiredo <lhf@tecgraf.puc-rio.br>
* 01 May 2009 10:40:56
* This code is hereby placed in the public domain.
*/

#include <signal.h>
#include <unistd.h>

#include "lua.h"
#include "lauxlib.h"

#define NAME	"alarm handler"

static lua_State *LL=NULL;
static lua_Hook oldhook=NULL;
static int oldmask=0;
static int oldcount=0;

static void l_handler(lua_State *L, lua_Debug *ar)
{
 L=LL;
 lua_sethook(L,oldhook,oldmask,oldcount);
 lua_pushliteral(L,NAME);
 lua_gettable(L,LUA_REGISTRYINDEX);
 lua_call(L,0,0);
}

static void l_signal(int i)
{						/* assert(i==SIGALRM); */
 signal(i,SIG_DFL);
 oldhook=lua_gethook(LL);
 oldmask=lua_gethookmask(LL);
 oldcount=lua_gethookcount(LL);
 lua_sethook(LL,l_handler,LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT,1);
}

static int l_alarm(lua_State *L)		/** alarm([secs,[func]]) */
{
 LL=L;
 switch (lua_gettop(L))
 {
  case 0:
	break;
  case 1:
	lua_pushliteral(L,NAME);
	lua_gettable(L,LUA_REGISTRYINDEX);
	if (lua_isnil(L,-1)) luaL_error(L,"no alarm handler set");
	break;
  default:
	luaL_checktype(L,2,LUA_TFUNCTION);
	lua_pushliteral(L,NAME);
	lua_pushvalue(L,2);
	lua_settable(L,LUA_REGISTRYINDEX);
	break;
 }
 if (signal(SIGALRM,l_signal)==SIG_ERR)
  lua_pushnil(L);
 else
  lua_pushinteger(L,alarm(luaL_optinteger(L,1,0)));
 return 1;
}

LUALIB_API int luaopen_lalarm(lua_State *L)
{
 lua_pushcfunction (L, l_alarm);
 return 1;
}
