/*
** $Id: lleaveslib.c $
** Leaves runtime support: classes, instances and inheritance.
** See Copyright Notice in lua.h
**
** The Leaves 'class' statement is pure syntactic sugar (see lparser.c):
** it hands the class name and an optional superclass to this library and
** stores the returned class object in a lexical local.  Everything else
** -- instantiation, method lookup, inheritance and 'isa' -- lives here,
** so the VM, the bytecode format and the GC need no changes at all.
**
** Classes and instances are tables. Instance lookup follows a separate
** member view chain, so static members never leak into instances. Class
** lookup checks static tables, member views, then superclass members.
** Internal __leaves_* fields are reserved implementation details.
*/

#define lleaveslib_c
#define LUA_LIB

#include "lprefix.h"

#include <stdio.h>
#include <string.h>

#include "lua.h"

#include "lauxlib.h"
#include "lualib.h"


/* internal field names used by the object model */
#define CLASS_MARK "__leaves_class"
#define CLASS_SUPER "__leaves_super"
#define CLASS_MT "__leaves_mt"
#define CLASS_STATICS "__leaves_statics"
#define CLASS_VIEW "__leaves_view"
#define CLASS_OWNER "__leaves_class"  /* instance metatable -> owning class */
#define VIEW_PARENT "__leaves_parent_view"




static int leaves_isa (lua_State *L);
static int class_index (lua_State *L);




/*
** Raw (metamethod-free) 't[name]': what "is this value itself a class?"
** must ask, because instances inherit their class' fields through
** __index and would otherwise look like classes too.
*/
static void rawgetfield (lua_State *L, int idx, const char *name) {
  idx = lua_absindex(L, idx);
  lua_pushstring(L, name);
  lua_rawget(L, idx);
}


/*
** Push the instance member named 'name' of class 'cls' (raw lookups, so
** no metamethod of ours can fire): own members live on the class table,
** inherited ones in the __leaves_view chain.  Leaves nil when absent.
*/
static void pushmember (lua_State *L, int cls, const char *name) {
  cls = lua_absindex(L, cls);
  /* Own instance members live directly on the class table. */
  rawgetfield(L, cls, name);
  if (!lua_isnil(L, -1))
    return;                       /* exactly one value pushed */
  lua_pop(L, 1);
  /* Inherited ones: walk the view chain with raw lookups only. */
  rawgetfield(L, cls, CLASS_VIEW);
  while (lua_istable(L, -1)) {
    lua_pushstring(L, name);
    lua_rawget(L, -2);            /* view, value */
    if (!lua_isnil(L, -1)) {
      lua_remove(L, -2);          /* keep the value alone */
      return;
    }
    lua_pop(L, 1);                /* drop nil */
    rawgetfield(L, -1, VIEW_PARENT);
    lua_remove(L, -2);            /* drop the view we just walked */
  }
  lua_pop(L, 1);                  /* drop the final non-table value */
  lua_pushnil(L);
}


/*
** Check that 'idx' holds a class and push it.  An instance is accepted
** too: its metatable records the owning class.
*/
static void pushclass (lua_State *L, int idx) {
  idx = lua_absindex(L, idx);
  if (!lua_istable(L, idx))
    luaL_error(L, "expected a class or an instance, got a %s",
                  luaL_typename(L, idx));
  /* The marker must be read *rawly*: an instance would otherwise see its
     class' marker through __index and be mistaken for a class. */
  rawgetfield(L, idx, CLASS_MARK);
  if (!lua_isnil(L, -1)) {
    lua_pop(L, 1);  /* remove the marker */
    lua_pushvalue(L, idx);  /* the class itself */
    return;
  }
  lua_pop(L, 1);
  if (!lua_getmetatable(L, idx))  /* plain table without metatable */
    luaL_error(L, "value is not a class or an instance of one");
  rawgetfield(L, -1, CLASS_OWNER);  /* instance metatable -> its class */
  lua_remove(L, -2);
  if (!lua_istable(L, -1))
    luaL_error(L, "value is not a class or an instance of one");
}


/*
** tostring() for instances: "ClassName: 0x..."
*/
static int instance_tostring (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  /* Find the class name: a class carries it directly, an instance finds it
     through its metatable's owner field. */
  rawgetfield(L, 1, CLASS_MARK);          /* is 'self' a class? */
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    if (lua_getmetatable(L, 1)) {
      rawgetfield(L, -1, CLASS_OWNER);    /* the instance's class */
      lua_remove(L, -2);
      if (lua_istable(L, -1)) {
        rawgetfield(L, -1, CLASS_MARK);   /* that class' name */
        lua_remove(L, -2);
      }
    }
  }
  if (lua_isnil(L, -1)) {  /* not a Leaves object: fall back */
    lua_pop(L, 1);
    lua_pushfstring(L, "table: %p", (void *)lua_topointer(L, 1));
    return 1;
  }
  lua_pushfstring(L, "%s: %p", lua_tostring(L, -1),
                                 (void *)lua_topointer(L, 1));
  return 1;
}


/* Build an instance of the class at 'clsidx' and leave it on the stack. */
static void push_instance (lua_State *L, int clsidx) {
  clsidx = lua_absindex(L, clsidx);
  lua_getfield(L, clsidx, CLASS_MT);   /* the shared instance metatable */
  lua_newtable(L);                     /* the instance */
  lua_pushvalue(L, -2);                /* the metatable */
  lua_setmetatable(L, -2);
  lua_remove(L, -2);                   /* drop the metatable */
}


/* Keep the result below the constructor call frame: no registry roots are
** needed, including when init raises an error or recursively constructs. */
static int class_call (lua_State *L) {
  int nargs = lua_gettop(L) - 1;
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_checkstack(L, 6, "too many constructor arguments");
  pushmember(L, 1, "init");           /* class, args..., init */
  push_instance(L, 1);                /* class, args..., init, inst */
  if (lua_isnil(L, -2))
    return 1;
  lua_insert(L, 1);                   /* inst, class, args..., init */
  lua_insert(L, 2);                   /* inst, init, class, args... */
  lua_copy(L, 1, 3);                  /* inst, init, inst, args... */
  lua_call(L, nargs + 1, 0);          /* inst survives below the frame */
  return 1;
}




/*
** __index for a class table.  A class must answer:
**   Cls.stat     -> static members (own, then inherited)
**   Cls.method   -> instance methods, so 'Base.init(self, ...)' works
** Class/view probes are raw; static tables use their separate table-only
** inheritance chain, which never re-enters class_index.
*/
static int class_index (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_settop(L, 2);

  /* 1. static members, own and inherited */
  rawgetfield(L, 1, CLASS_STATICS);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);          /* statics chain uses a real __index */
    if (!lua_isnil(L, -1))
      return 1;
  }
  lua_pop(L, 1);

  /* 2. instance methods through the view chain */
  rawgetfield(L, 1, CLASS_VIEW);
  while (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1))
      return 1;
    lua_pop(L, 1);
    rawgetfield(L, -1, VIEW_PARENT);
    lua_remove(L, -2);
  }

  /* 3. last resort: the superclass' own members */
  rawgetfield(L, 1, CLASS_SUPER);
  if (lua_istable(L, -1)) {
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    return 1;
  }
  lua_pushnil(L);
  return 1;
}


/*
** leaves.class(name [, superclass]) -> class table
**
** Called by the code generated for a 'class' statement.  Also usable
** directly from Lua for metaprogramming.
*/
static int leaves_class (lua_State *L) {
  const char *name = luaL_checkstring(L, 1);
  int has_super = !lua_isnoneornil(L, 2);

  lua_settop(L, 2);                          /* 1: name (any), 2: super */
  luaL_checkstack(L, 8, "too many superclasses");
  if (has_super) {
    luaL_checktype(L, 2, LUA_TTABLE);
    rawgetfield(L, 2, CLASS_MARK);           /* really a class? */
    if (lua_isnil(L, -1))
      luaL_error(L, "cannot extend a value that is not a class");
    lua_pop(L, 1);
  }

  lua_newtable(L);                          /* 3: the class table C */

  /* ---- the instance member view -------------------------------------
     A plain table holding C's instance members, linked to the
     superclass' view through a real metatable.  Instances index this,
     never the class table, so static members stay invisible to them. */
  lua_newtable(L);                          /* 4: view */
  if (has_super) {
    lua_getfield(L, 2, CLASS_VIEW);         /* superclass' view? */
    if (lua_isnil(L, -1)) {                 /* plain superclass: use it */
      lua_pop(L, 1);
      lua_pushvalue(L, 2);
    }
    /* Record the parent view *as data* (VIEW_PARENT) and also as the
       metatable's __index, so ordinary Lua code sees inheritance too.
       Our own lookups use the data field, which needs no metamethods. */
    lua_pushvalue(L, -1);
    lua_setfield(L, 4, VIEW_PARENT);
    lua_newtable(L);                        /* metatable for the view */
    lua_insert(L, -2);                      /* mt, superview */
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, 4);                 /* view -> superview -> ... */
  }
  lua_setfield(L, 3, CLASS_VIEW);            /* C.__leaves_view = view */

  /* ---- statics ------------------------------------------------------
     Static members live in their own table, never on the class table
     itself.  Instances index the *view*, so they cannot see statics; and
     because the statics table inherits from the superclass' statics table,
     'Sub.stat' finds 'Base.stat'. */
  lua_newtable(L);                          /* 4: statics table */
  if (has_super) {
    lua_getfield(L, 2, CLASS_STATICS);      /* superclass' statics */
    if (lua_istable(L, -1)) {
      lua_createtable(L, 0, 1);             /* its metatable */
      lua_insert(L, -2);                    /* mt, superstats */
      lua_setfield(L, -2, "__index");
      lua_setmetatable(L, 4);
    }
    else
      lua_pop(L, 1);
  }
  lua_setfield(L, 3, CLASS_STATICS);

  /* ---- markers and identity ------------------------------------------ */
  lua_pushstring(L, name);
  lua_setfield(L, 3, CLASS_MARK);
  if (has_super) {
    lua_pushvalue(L, 2);
    lua_setfield(L, 3, CLASS_SUPER);
  }
  /* 'isa' must be reachable from classes *and* from instances.  Put it on
     the class (for 'Cls:isa(...)') and in the view (what instances index). */
  lua_pushcfunction(L, leaves_isa);
  lua_setfield(L, 3, "isa");
  lua_getfield(L, 3, CLASS_VIEW);
  lua_pushcfunction(L, leaves_isa);
  lua_setfield(L, -2, "isa");
  lua_pop(L, 1);

  /* Class indexing searches static tables and instance member views;
     calling the class constructs an instance. */
  lua_newtable(L);                            /* 4: class metatable */
  lua_pushcfunction(L, class_index);
  lua_setfield(L, -2, "__index");
  lua_pushcfunction(L, class_call);
  lua_setfield(L, -2, "__call");
  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");
  lua_setmetatable(L, 3);

  /* ---- metatable shared by every instance --------------------------- */
  lua_newtable(L);                            /* 4: instance metatable */
  lua_getfield(L, 3, CLASS_VIEW);
  lua_setfield(L, -2, "__index");             /* instance -> view */
  lua_pushstring(L, name);
  lua_setfield(L, -2, "__name");
  lua_pushvalue(L, 3);
  lua_setfield(L, -2, CLASS_OWNER);   /* instance metatable -> its class */
  lua_pushcfunction(L, instance_tostring);
  lua_setfield(L, -2, "__tostring");
  lua_setfield(L, 3, CLASS_MT);               /* C.__leaves_mt = mt */

  lua_settop(L, 3);                           /* drop the inputs */
  return 1;                                   /* C */
}


/*
** Cls:isa(other) / instance:isa(other) -> boolean
**   true when the receiver is, or inherits from, 'other'.
*/
static int leaves_isa (lua_State *L) {
  int depth = 0;
  pushclass(L, 1);  /* the class of the receiver */
  luaL_checktype(L, 2, LUA_TTABLE);
  while (lua_istable(L, -1)) {
    int found = lua_rawequal(L, -1, 2);
    if (found) {
      lua_pushboolean(L, 1);
      return 1;
    }
    rawgetfield(L, -1, CLASS_SUPER);
    lua_remove(L, -2);
    if (++depth > 1000)
      luaL_error(L, "inheritance chain is too deep (cycle?)");
  }
  lua_pushboolean(L, 0);
  return 1;
}


/*
** leaves.new(Cls, ...) -> instance
**   Same as calling the class, exposed for uniformity.
*/
static int leaves_new (lua_State *L) {
  luaL_checkany(L, 1);
  lua_call(L, lua_gettop(L) - 1, 1);
  return 1;
}


/*
** leaves.typeof(x) -> class of x, or nil when x is not a Leaves object.
*/
static int leaves_typeof (lua_State *L) {
  luaL_checkany(L, 1);
  if (!lua_istable(L, 1)) {
    lua_pushnil(L);          /* not a table: not a Leaves object */
    return 1;
  }
  if (lua_getmetatable(L, 1)) {          /* instances have one */
    rawgetfield(L, -1, CLASS_OWNER);
    if (lua_istable(L, -1))
      return 1;                          /* its class, by identity */
    lua_pop(L, 1);
    lua_pop(L, 1);
  }
  /* Not an instance: is it a class itself? */
  rawgetfield(L, 1, CLASS_MARK);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_pushnil(L);                      /* a plain table */
  }
  else {
    lua_pop(L, 1);
    lua_pushvalue(L, 1);
  }
  return 1;
}


/*
** leaves.super(Cls) -> superclass (or nil)
*/
static int leaves_super (lua_State *L) {
  pushclass(L, 1);
  rawgetfield(L, -1, CLASS_SUPER);
  return 1;
}


static const luaL_Reg leaves_funcs[] = {
  {"class", leaves_class},
  {"new", leaves_new},
  {"typeof", leaves_typeof},
  {"super", leaves_super},
  {"isa", leaves_isa},
  {NULL, NULL}
};


LUAMOD_API int luaopen_leaves (lua_State *L) {
  luaL_newlib(L, leaves_funcs);
  lua_pushliteral(L, "Leaves 1.0 (on Lua 5.5.1)");
  lua_setfield(L, -2, "_VERSION");
  lua_pushliteral(L, "Leaves");
  lua_setfield(L, -2, "_NAME");

  /* Install the hidden helper used by code generated for 'class'. */
  lua_pushcfunction(L, leaves_class);
  lua_setglobal(L, "__leaves_class");

  /* Make 'isa' reachable from every class and instance.  It is looked up
  ** through the class chain, so it must live on the class tables; the
  ** compiler does not emit it, so we expose it as a global helper used by
  ** the library's own metatables.  Classes created later get it through
  ** leaves.class. */
  return 1;
}
