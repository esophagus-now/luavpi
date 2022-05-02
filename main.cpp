#include <iostream>
#include <cstring>
#include "lua.hpp"
#include <signal.h>

#include <vpi_user.h>
#include <cassert>

using namespace std;

ostream& el(ostream &o) {return o << "\n";}

static int hello_compiletf([[maybe_unused]] char*user_data) {
    return 0;
}

lua_State *g_L;
int lua_repl();

//Continuation function that just returns the ctx number
//(so that we can indicate number of return values)
int no_more_questions_your_honour(
    [[maybe_unused]] lua_State *L, [[maybe_unused]] int status, lua_KContext ctx
) {
    return ctx;
}

static int lua_repl(lua_State *L) {
    char buff[80];
    int error;
    int rc, numresults;

    lua_State *th = lua_tothread(L, -1);
    
    if (th) {
        goto run_lua_thread;
    } 
    
    while (fgets(buff, sizeof(buff), stdin) != NULL) {
        //Call compiled string as a coroutine so it can call vpi.wait

        //Make a new thread for the user's input line
        th = lua_newthread(g_L);
        
        //Put this thread into the globals table so it doesn't
        //get garbage collected
        lua_pushlightuserdata(th,th);
        lua_pushthread(th);
        lua_rawset(th, LUA_REGISTRYINDEX);

        //Now we can safely remove the new thread from the main stack
        lua_pop(g_L, 1);

        //Try compiling the user's string
        error = luaL_loadbuffer(th, buff, strlen(buff), "stdin");
        if (error) {
            fprintf(stderr, "%s\n", lua_tostring(th, -1));
            lua_pop(th, 1);  /* pop error message from the stack */
            continue;
        }

        //If there was already a thread on the stack we'll jump
        //right to this label
        run_lua_thread:
        //For some unknown reason pcallk doesn't actually run the
        //code in a protected context when you give it a continuation
        //function
        //int rc = lua_pcallk(th,0,0,0,0,&lua_repl);
        rc = lua_resume(th, 0, 0, &numresults);
        
        if (rc == LUA_YIELD) break; //Allow sim to continue on yields, and also
                                    //don't take thread out of registry
        else if (rc != LUA_OK) {
            fprintf(stderr, "%s\n", lua_tostring(th, -1));
            lua_pop(th,1); //pop error message
        }
        
        //Remove thread from registry
        lua_settop(th,0);
        lua_pushlightuserdata(L, th);
        lua_pushnil(L);
        lua_rawset(L, LUA_REGISTRYINDEX);
        th = NULL;
    }

    return 0;
}

static int hello_calltf([[maybe_unused]] char*user_data) {
    lua_repl(g_L);
    return 0;
}

int vpi_handle_by_name_wrapper (lua_State *L) {
    vpiHandle h;
    if (lua_isnil(L, 1)) h = NULL;
    else h = (vpiHandle) luaL_checkudata(L,1,"vpiHandle");
    
    char const* what_to_get = luaL_checkstring(L,2);

    vpiHandle ret = vpi_handle_by_name(what_to_get, h);
    if (ret == NULL) {
        lua_pushnil(L);
    } else {
        lua_pushlightuserdata(L, ret);
        luaL_getmetatable(L, "vpiHandle");
        lua_setmetatable(L, -2);
    }
    
    return 1;
}

int vpi_get_str_wrapper(lua_State *L) {
    vpiHandle h = (vpiHandle) luaL_checkudata(L,1,"vpiHandle");
    int what_to_get = luaL_checkinteger(L,2);

    char *str = vpi_get_str(what_to_get, h);

    if (!str) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, str);
    }

    return 1;
}

int vpi_handle_to_string(lua_State *L) {
    vpiHandle h = (vpiHandle) luaL_checkudata(L,1,"vpiHandle");

    
    int tpid = vpi_get(vpiType, h);
    if (tpid == vpiModule) {
        char const* nm = vpi_get_str(vpiFullName, h);
        if (!nm) nm = "(no FullName property found)";
        nm = strdup(nm);
        
        char *modname = vpi_get_str(vpiDefName, h);
        lua_pushfstring(L, "Instance of module [%s] at [%s]", modname, nm);
        free((void*)nm); //C++ is so persnickety...
    } else {
        char const* nm = vpi_get_str(vpiName, h);
        if (!nm) nm = "(no Name property found)";
        nm = strdup(nm);
        
        char *tp = vpi_get_str(vpiType, h);
        lua_pushfstring(L, "Object of type [%s] named [%s]", tp, nm);
        free((void*)nm);
    }

    return 1;
}

int vpi_handle_wrapper(lua_State *L) {
    vpiHandle h;
    if (lua_isnil(L, 1)) h = NULL;
    else h = (vpiHandle) luaL_checkudata(L,1,"vpiHandle");
    
    int what_to_get = luaL_checkinteger(L,2);

    vpiHandle ret = vpi_handle(what_to_get, h);

    if (!ret) {
        lua_pushnil(L);
    } else {
        lua_pushlightuserdata(L, ret);
        luaL_getmetatable(L, "vpiHandle");
        lua_setmetatable(L, -2);
    }

    return 1;
}

int vpi_get_wrapper(lua_State *L) {
    vpiHandle h;
    if (lua_isnil(L, 1)) h = NULL;
    else h = (vpiHandle) luaL_checkudata(L,1,"vpiHandle");
    
    int what_to_get = luaL_checkinteger(L,2);

    uint32_t ret = vpi_get(what_to_get, h);

    lua_pushinteger(L, ret);

    return 1;
}

int vpi_get_all(lua_State *L) {
    vpiHandle h;
    if (lua_isnil(L, 1)) h = NULL;
    else h = (vpiHandle) luaL_checkudata(L,1,"vpiHandle");

    int what_to_get = luaL_checkinteger(L,2);

    lua_newtable(L);

    vpiHandle it = vpi_iterate(what_to_get, h);

    if (!it) return 1;

    vpiHandle el;
    int pos = 1;
    while ((el = vpi_scan(it)) != NULL) {
        lua_pushlightuserdata(L, el);
        luaL_getmetatable(L, "vpiHandle");
        lua_setmetatable(L, -2);
        lua_rawseti(L, -2, pos++);
    }

    return 1;
}

PLI_INT32 resume_lua_after_wait(s_cb_data *cbdat) {
    lua_State *L = (lua_State *) cbdat->user_data;

    lua_pushthread(L);
    return lua_repl(L);
}

//TODO: allow value change callbacks too
int vpi_wait(lua_State *L) {
    //Annoying... Icarus verilog doesn't support vpiScaledRealTime
    //double delay_time = luaL_checknumber(L, 1);
    lua_Integer delay_time = luaL_checkinteger(L, 1);
    if (delay_time < 0) {
        luaL_error(L, "Cannot wait for %ld; delay must be positive", delay_time);
    }

    lua_settop(L, 0); //Pop all arguments

    s_vpi_time delay;
    //delay.type = vpiScaledRealTime;
    //delay.real = delay_time;
    delay.type = vpiSimTime;
    delay.low = (delay_time) & 0xFFFFFFFF;
    delay.high = (delay_time >> 32) & 0xFFFFFFFF;

    s_cb_data cbdat;
    cbdat.reason = cbAfterDelay;
    cbdat.time = &delay;
    cbdat.cb_rtn = &resume_lua_after_wait;
    cbdat.user_data = (char*)L;

    //The version of iverilog I'm using must have been built
    //before this fix: https://github.com/steveicarus/iverilog/commit/580170d9745a0883b69c8d63d21553d9ad874698
    //I will put some arbitrary valid vpi handle in obj as a workaround
    vpiHandle it = vpi_iterate(vpiModule, NULL);
    vpiHandle el = vpi_scan(it);
    assert(el != NULL);
    if (it != NULL) vpi_free_object(it);

    cbdat.obj = el;
    
    vpi_register_cb(&cbdat);

    lua_yieldk(L, 0, 0, &no_more_questions_your_honour);

    puts("should never get here...");
    //Can never get here
    return 0;
}

int vpi_get_value_wrapper(lua_State *L) {
    vpiHandle h = (vpiHandle) luaL_checkudata(L,1,"vpiHandle");
    
    int what_to_get = luaL_checkinteger(L,2);
    
    s_vpi_value val;
    val.format = what_to_get;
    
    switch (what_to_get) {
    case vpiBinStrVal:
    case vpiOctStrVal:
    case vpiDecStrVal:
    case vpiHexStrVal:
    case vpiStringVal: 
        //Initialize the string pointer to 0 just in
        //case vpi_get_value fails
        val.value.str = NULL;
        vpi_get_value(h, &val);
        lua_pushstring(L, val.value.str);
        break;
    case vpiIntVal:
        vpi_get_value(h, &val);
        lua_pushinteger(L, val.value.integer);
        break;
    case vpiTimeVal: {
        //Initialize the time pointer to 0 just in
        //case vpi_get_value fails
        val.value.time = NULL;
        vpi_get_value(h, &val);
        //idk what lua does about unsigned vs. signed
        //but I doubt anyone would let a simulation
        //max out a 64-bit int anyway
        if (val.value.time == NULL) {
            lua_pushnil(L);
            break;
        }
        lua_Integer tm = 
            ((lua_Integer)val.value.time->high << 32) | 
            val.value.time->low
        ;
        lua_pushinteger(L, tm);
        break;
    }
    default:
        luaL_error(L, "Sorry, this value type is unsupported");
    }

    return 1;
}

//TODO: handle the other value types
int vpi_put_value_wrapper(lua_State *L) {
    vpiHandle h = (vpiHandle) luaL_checkudata(L,1,"vpiHandle");
    lua_Integer val_in = luaL_checkinteger(L,2);

    s_vpi_value val;
    val.format = vpiIntVal;
    val.value.integer = (val_in & 0xFFFFFFFF);

    vpiHandle ev = vpi_put_value(h, &val, NULL, vpiForceFlag);
    if (ev) {
        //Try to prevent memory leaks
        vpi_free_object(ev);
    }

    return 0;
}

void hello_register()
{
    s_vpi_systf_data tf_data;

    tf_data.type      = vpiSysTask;
    tf_data.tfname    = "$hello";
    tf_data.calltf    = hello_calltf;
    tf_data.compiletf = hello_compiletf;
    tf_data.sizetf    = 0;
    tf_data.user_data = 0;
    vpi_register_systf(&tf_data);

    
    g_L = luaL_newstate();
    if (!g_L) {
        cerr << "oops" << el;
        vpi_control(vpiFinish, -1);
        return;
    }

    luaL_openlibs(g_L);

    lua_newtable(g_L);
    { //For code folding
    #define pushstr(str) lua_pushlstring(g_L, str, sizeof(str) - 1);
    #define mkthing(thing) lua_pushlstring(g_L, #thing, sizeof(#thing)-1); lua_pushinteger(g_L, vpi##thing); lua_settable(g_L, -3);
    //Don't worry... I paste from vpi_user.h and used regex
    //find-replace
    mkthing(Constant);
    mkthing(Function);
    mkthing(IntegerVar);
    mkthing(Iterator);
    mkthing(Memory);
    mkthing(MemoryWord);
    mkthing(ModPath);
    mkthing(Module);
    mkthing(NamedBegin);
    mkthing(NamedEvent);
    mkthing(NamedFork);
    mkthing(Net);
    mkthing(NetBit);
    mkthing(Parameter);
    mkthing(PartSelect);
    mkthing(PathTerm);
    mkthing(Port);
    mkthing(RealVar);
    mkthing(Reg);
    mkthing(RegBit);
    mkthing(SysFuncCall);
    mkthing(SysTaskCall);
    mkthing(Task);
    mkthing(TimeVar);
    mkthing(UdpDefn);
    mkthing(UserSystf);
    mkthing(NetArray);
    mkthing(Index);
    mkthing(LeftRange);
    mkthing(Parent);
    mkthing(RightRange);
    mkthing(Scope);
    mkthing(SysTfCall);
    mkthing(Argument);
    mkthing(InternalScope);
    mkthing(ModPathIn);
    mkthing(ModPathOut);
    mkthing(Variables);
    mkthing(Expr);
    mkthing(Callback);
    mkthing(RegArray);
    mkthing(GenScope);
    mkthing(Undefined);
    mkthing(Type);
    mkthing(Name);
    mkthing(FullName);
    mkthing(Size);
    mkthing(File);
    mkthing(LineNo);
    mkthing(TopModule);
    mkthing(CellInstance);
    mkthing(DefName);
    mkthing(TimeUnit);
    mkthing(TimePrecision);
    mkthing(DefFile);
    mkthing(DefLineNo);
    mkthing(Scalar);
    mkthing(Vector);
    mkthing(Direction);
    mkthing(Input);
    mkthing(Output);
    mkthing(Inout);
    mkthing(MixedIO);
    mkthing(NoDirection);
    mkthing(NetType);
    mkthing(Wire);
    mkthing(Wand);
    mkthing(Wor);
    mkthing(Tri);
    mkthing(Tri0);
    mkthing(Tri1);
    mkthing(TriReg);
    mkthing(TriAnd);
    mkthing(TriOr);
    mkthing(Supply1);
    mkthing(Supply0);
    mkthing(Array);
    mkthing(PortIndex);
    mkthing(Edge);
    mkthing(NoEdge);
    mkthing(Edge01);
    mkthing(Edge10);
    mkthing(Edge0x);
    mkthing(Edgex1);
    mkthing(Edge1x);
    mkthing(Edgex0);
    mkthing(Posedge);
    mkthing(Negedge);
    mkthing(AnyEdge);
    mkthing(ConstType);
    mkthing(DecConst);
    mkthing(RealConst);
    mkthing(BinaryConst);
    mkthing(OctConst);
    mkthing(HexConst);
    mkthing(StringConst);
    mkthing(FuncType);
    mkthing(IntFunc);
    mkthing(RealFunc);
    mkthing(TimeFunc);
    mkthing(SizedFunc);
    mkthing(SizedSignedFunc);
    mkthing(SysFuncType);
    mkthing(SysFuncInt);
    mkthing(SysFuncReal);
    mkthing(SysFuncTime);
    mkthing(SysFuncSized);
    mkthing(UserDefn);
    mkthing(Automatic);
    mkthing(ConstantSelect);
    mkthing(Signed);
    mkthing(LocalParam);
    mkthing(BinStrVal);
    mkthing(OctStrVal);
    mkthing(DecStrVal);
    mkthing(HexStrVal);
    mkthing(ScalarVal);
    mkthing(IntVal);
    mkthing(RealVal);
    mkthing(StringVal);
    mkthing(VectorVal);
    mkthing(StrengthVal);
    mkthing(TimeVal);
    mkthing(ObjTypeVal);
    mkthing(SuppressVal);
    }

    pushstr("handle");
    lua_pushcfunction(g_L, &vpi_handle_wrapper);
    lua_settable(g_L, -3);
    
    pushstr("handle_by_name");
    lua_pushcfunction(g_L, &vpi_handle_by_name_wrapper);
    lua_settable(g_L, -3);
    
    pushstr("get");
    lua_pushcfunction(g_L, &vpi_get_wrapper);
    lua_settable(g_L, -3);
    
    pushstr("get_str");
    lua_pushcfunction(g_L, &vpi_get_str_wrapper);
    lua_settable(g_L, -3);
    
    pushstr("get_all");
    lua_pushcfunction(g_L, &vpi_get_all);
    lua_settable(g_L, -3);
    
    pushstr("wait");
    lua_pushcfunction(g_L, &vpi_wait);
    lua_settable(g_L, -3);
    
    pushstr("get_value");
    lua_pushcfunction(g_L, &vpi_get_value_wrapper);
    lua_settable(g_L, -3);
    
    pushstr("put_value");
    lua_pushcfunction(g_L, &vpi_put_value_wrapper);
    lua_settable(g_L, -3);
    
    luaL_newmetatable(g_L, "vpiHandle");
    pushstr("__index");
    lua_pushvalue(g_L, -3);
    lua_settable(g_L, -3);

    pushstr("__tostring");
    lua_pushcfunction(g_L, &vpi_handle_to_string);
    lua_settable(g_L,-3);

    lua_pop(g_L, 1);
    
    lua_setglobal(g_L, "vpi");
}

void (*vlog_startup_routines[])() = {
    hello_register,
    0
};