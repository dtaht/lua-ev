/**
 * Create a table for ev.Loop that gives access to the constructor for
 * loop objects and the "default" event loop object instance. 
 *
 * [-0, +1, ?]
 */
static int luaopen_ev_loop(lua_State *L) {
    lua_pop(L, create_loop_mt(L));

    lua_createtable(L, 0, 1);

    lua_pushcfunction(L, loop_new);
    lua_setfield(L, -2, "new");

    *loop_alloc(L) = UNINITIALIZED_DEFAULT_LOOP;
    lua_setfield(L, -2, "default");

    return 1;
}

/**
 * Create the loop metatable in the registry.
 *
 * [-0, +1, ?]
 */
static int create_loop_mt(lua_State *L) {

    static luaL_reg fns[] = {
        { "is_default", loop_is_default },
        { "count",      loop_count },
        { "now",        loop_now },
        { "update_now", loop_update_now },
        { "loop",       loop_loop },
        { "unloop",     loop_unloop },
        { "__gc",       loop_delete },
        { NULL, NULL }
    };
    luaL_newmetatable(L, LOOP_MT);
    luaL_register(L, NULL, fns);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");

    return 1;
}

/**
 * Create a table intended as the loop object, sets the metatable,
 * registers it, creates the evlua_loop struct appropriately, and sets
 * the userdata fenv to an empty table.  This table is used to store
 * references to active watchers so the garbage collector does not
 * prematurely collect the watchers.
 *
 * [-0, +1, v]
 */
static struct ev_loop** loop_alloc(lua_State *L) {
    int              result;
    struct ev_loop** loop = (struct ev_loop**)
        obj_new(L, sizeof(struct ev_loop*), LOOP_MT);

    lua_newtable(L);
    result = lua_setfenv(L, -2);
    assert(result == 1 /* setfenv() was successful */);

    return loop;
}

/**
 * Validates that loop_i is a loop object and checks if it is the
 * special UNINITIALIZED_DEFAULT_LOOP token, and if so it initializes
 * the default loop.  If everything checks out fine, then a pointer to
 * the ev_loop object is returned.
 */
static struct ev_loop** check_loop_and_init(lua_State *L, int loop_i) {
    struct ev_loop** loop_r = check_loop(L, loop_i);
    if ( UNINITIALIZED_DEFAULT_LOOP == *loop_r ) {
        *loop_r = ev_default_loop(EVFLAG_AUTO);
        if ( NULL == *loop_r ) {
            luaL_error(L,
                       "libev init failed, perhaps LIBEV_FLAGS environment variable "
                       " is causing it to select a bad backend?");
        }
        register_obj(L, loop_i, *loop_r);
    }
    return loop_r;
}



/**
 * Create a new non-default loop instance.
 *
 * [-0, +1, ?]
 */
static int loop_new(lua_State *L) {
    struct ev_loop** loop_r = loop_alloc(L);
    *loop_r = ev_loop_new(EVFLAG_AUTO);
    register_obj(L, -1, *loop_r);
    return 1;
}

/**
 * Delete a loop instance.  Default event loop is ignored.
 */
static int loop_delete(lua_State *L) {
    struct ev_loop* loop = *check_loop(L, 1);

    if ( UNINITIALIZED_DEFAULT_LOOP == loop ||
         ev_is_default_loop(loop)           ) return 0;

    ev_loop_destroy(loop);
    return 0;
}

/**
 * Must be called aftert start()ing a watcher.  This is necessary so
 * that the watcher is not prematurely garbage collected, and if the
 * watcher is "marked as a daemon", then ev_unref() is called so that
 * this watcher does not prevent the event loop from terminating.
 *
 * is_daemon may be -1 to specify that if this watcher is already
 * registered in the event loop, then use the current is_daemon value,
 * otherwise set is_daemon to false.
 *
 * [-0, +0, m]
 */
static void loop_start_watcher(lua_State* L, int loop_i, int watcher_i, int is_daemon) {
    int current_is_daemon = -1;

    loop_i    = abs_index(L, loop_i);
    watcher_i = abs_index(L, watcher_i);

    /* Check that watcher isn't already registered: */
    lua_getfenv(L,     loop_i);
    lua_pushvalue(L,   watcher_i);
    lua_rawget(L,      -2);

    if ( ! lua_isnil(L, -1) ) {
        current_is_daemon = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);

    /* Currently not initialized, or daemon status change? */
    if ( -1 == current_is_daemon ||
         current_is_daemon ^ is_daemon )
    {
        lua_pushvalue(L,   watcher_i);
        lua_pushboolean(L, is_daemon);
        lua_rawset(L,      -3);
        lua_pop(L,         1);

        if ( ! is_daemon && -1 == current_is_daemon ) {
            /* Do nothing, we are initializing a non-daemon */
        } else if ( is_daemon ) {
            /* unref() so that we are a "daemon" */
            ev_unref(*check_loop_and_init(L, loop_i));
        } else {
            /* ref() so that we are no longer a "daemon" */
            ev_ref(*check_loop_and_init(L, loop_i));
        }
    }
}

/**
 * Must be called aftert stop()ing a watcher, or after a non-repeating
 * timer expires.  This is necessary so that the watcher is not
 * prematurely garbage collected, and if the watcher is "marked as a
 * daemon", then ev_ref() is called in order to "undo" what was done
 * in loop_add_watcher().
 *
 * [-0, +0, m]
 */
static void loop_stop_watcher(lua_State* L, int loop_i, int watcher_i) {
    loop_i    = abs_index(L, loop_i);
    watcher_i = abs_index(L, watcher_i);

    lua_getfenv(L,     loop_i);
    lua_pushvalue(L,   watcher_i);
    lua_rawget(L,      -2);

    if ( lua_toboolean(L, -1) ) ev_ref(*check_loop_and_init(L, loop_i));
    lua_pop(L, 1);

    lua_pushvalue(L,   watcher_i);
    lua_pushnil(L);
    lua_rawset(L,      -3);
    lua_pop(L,         1);
}

/**
 * Check if this is the default event loop.
 */
static int loop_is_default(lua_State *L) {
    struct ev_loop* loop = *check_loop(L, 1);
    lua_pushboolean(L,
                    UNINITIALIZED_DEFAULT_LOOP == loop ||
                    ev_is_default_loop(loop)           );
    return 1;
}

/**
 * How many times have we iterated though the event loop?
 */
static int loop_count(lua_State *L) {
    struct ev_loop* loop = *check_loop(L, 1);
    lua_pushinteger(L,
                    UNINITIALIZED_DEFAULT_LOOP == loop ?
                    0 : ev_loop_count(loop));
    return 1;
}

/**
 * The current event loop time.
 */
static int loop_now(lua_State *L) {
    lua_pushnumber(L, ev_now(*check_loop_and_init(L, 1)));
    return 1;
}

/**
 * Sync the event loop time with "real" time and return the "real"
 * time.
 */
static int loop_update_now(lua_State *L) {
    struct ev_loop* loop = *check_loop_and_init(L, 1);
    ev_now_update(loop);
    lua_pushnumber(L, ev_now(loop));
    return 1;
}

/**
 * Actually do the event loop.
 */
static int loop_loop(lua_State *L) {
    ev_loop(*check_loop_and_init(L, 1), 0);
    return 0;
}

/**
 * "Quit" out of the event loop.
 */
static int loop_unloop(lua_State *L) {
    ev_unloop(*check_loop_and_init(L, 1), EVUNLOOP_ALL);
    return 0;
}

