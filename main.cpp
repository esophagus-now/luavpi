#include <iostream>
#include <cstring>
#include "lua.hpp"
#include <signal.h>

#include <vpi_user.h>
#include <cassert>

using namespace std;

ostream& el(ostream &o) {return o << "\n";}

int lua_repl();

//Continuation function that just returns the ctx number
//(so that we can indicate number of return values)
int no_more_questions_your_honour(
    [[maybe_unused]] lua_State *L, [[maybe_unused]] int status, lua_KContext ctx
) {
    return ctx;
}

void register_thread(lua_State *L, bool deregister=false) {
    lua_checkstack(L, 3);
    
    lua_pushlightuserdata(L,L);
    if (!deregister) {
        lua_pushthread(L);
    } else {
        lua_pushnil(L);
    }
    lua_rawset(L, LUA_REGISTRYINDEX);
}

#define deregister_thread(L) register_thread(L, true)

//If top-of-stack has an object of type thread, resumes
//the REPL in that thread. Otherwise, creates a new thread.
//TODO: find nice way for each thread to have a different
//prompt. Also, it would be nice to have built-in readline
//support, and while we're at it, steal some of the nice
//REPL features from the lua interpreter (support for multi-
//line input, automatically printing values from expressions),
//and autocomplete would be nice...
int lua_repl(lua_State *L) {
    char buff[80];
    int error;
    int rc, numresults;

    //Note: L can be equal to th
    lua_State *th = lua_tothread(L, -1);
    
    if (!th) {
        //Make a new thread for the user's input line
        th = lua_newthread(L);
        
        //Put this thread into the globals table so it doesn't
        //get garbage collected
        register_thread(th);
        
        //Now we can safely remove the new thread from the stack
        //(so that it does get garbage collected when we're done
        //with the REPL)
        lua_pop(L, 1);
    } else {
        //If resuming a coroutine, no need to ask for input
        goto run_the_thing;
    }
    
    while (printf("> "), fgets(buff, sizeof(buff), stdin) != NULL) {

        //Try compiling the user's string
        error = luaL_loadbuffer(th, buff, strlen(buff), "stdin");
        if (error) {
            fprintf(stderr, "%s\n", lua_tostring(th, -1));
            lua_pop(th, 1);  /* pop error message from the stack */
            continue;
        }

        //Call compiled string as a coroutine so it can call vpi.wait.
        //By the way, we'll jump straight to here if we were called
        //with a pre-made thread object (this is so resumed coroutines
        //keep executing rather than always asking for input)
        run_the_thing:
        
        //For some unknown reason pcallk doesn't actually run the
        //code in a protected context when you give it a continuation
        //function, so we're using lua_resume instead.
        //int rc = lua_pcallk(th,0,0,0,0,&lua_repl);
        rc = lua_resume(th, 0, 0, &numresults);
        
        if (rc == LUA_YIELD) {
            if (numresults == 0) {
                //Allow sim to continue on normal yields, and don't
                //take thread out of registry 
                return 0;
            } else {
                //Any nonzero number of yielded values tells us that
                //lua_leave_repl was invoked. For now, we will treat
                //this the same as normal yields, but maybe in the
                //future we'll do something different
                return 0;
            }
        } else if (rc != LUA_OK) {
            fprintf(stderr, "%s\n", lua_tostring(th, -1));
            lua_pop(th,1); //pop error message

            //Unfortunately for us, unlike pcallk, if an error
            //occurs inside lua_resume our thread is marked as
            //a dead coroutine and we have no choice but to make
            //a new thread. There must be a better way...
            deregister_thread(th);
            lua_settop(th,0); //Not sure if needed, but clear thread's stack
            
            th = lua_newthread(L);
            register_thread(th);
            lua_pop(L, 1);
        }
    }
        
    //Remove thread from registry
    deregister_thread(th);
    lua_settop(th,0); //Not sure if needed, but clear thread's stack

    return 0;
}

int lua_leave_repl(lua_State *L) {
    //cerr << "yielding " << L << endl;
    
    lua_yieldk(L, 0, 0, &no_more_questions_your_honour);

    puts("should never get here...");
    //Can never get here
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
    //cerr << "resuming " << L << endl;

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
    
    return lua_leave_repl(L); //Should never return
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

//Load the VPI table into the given lua_State
void luaopen_vpi(lua_State *L) {
    lua_checkstack(L, 5);

    lua_register(L, "repl", lua_repl);
    lua_register(L, "yield", lua_leave_repl);
    
    lua_newtable(L);
    
    #define pushconst(thing) \
        lua_pushlstring(L, #thing, sizeof(#thing)-1); \
        lua_pushinteger(L, vpi##thing); \
        lua_settable(L, -3);
    
    //Don't worry... I paste from vpi_user.h and used regex
    //find-replace
    pushconst(Constant);
    pushconst(Function);
    pushconst(IntegerVar);
    pushconst(Iterator);
    pushconst(Memory);
    pushconst(MemoryWord);
    pushconst(ModPath);
    pushconst(Module);
    pushconst(NamedBegin);
    pushconst(NamedEvent);
    pushconst(NamedFork);
    pushconst(Net);
    pushconst(NetBit);
    pushconst(Parameter);
    pushconst(PartSelect);
    pushconst(PathTerm);
    pushconst(Port);
    pushconst(RealVar);
    pushconst(Reg);
    pushconst(RegBit);
    pushconst(SysFuncCall);
    pushconst(SysTaskCall);
    pushconst(Task);
    pushconst(TimeVar);
    pushconst(UdpDefn);
    pushconst(UserSystf);
    pushconst(NetArray);
    pushconst(Index);
    pushconst(LeftRange);
    pushconst(Parent);
    pushconst(RightRange);
    pushconst(Scope);
    pushconst(SysTfCall);
    pushconst(Argument);
    pushconst(InternalScope);
    pushconst(ModPathIn);
    pushconst(ModPathOut);
    pushconst(Variables);
    pushconst(Expr);
    pushconst(Callback);
    pushconst(RegArray);
    pushconst(GenScope);
    pushconst(Undefined);
    pushconst(Type);
    pushconst(Name);
    pushconst(FullName);
    pushconst(Size);
    pushconst(File);
    pushconst(LineNo);
    pushconst(TopModule);
    pushconst(CellInstance);
    pushconst(DefName);
    pushconst(TimeUnit);
    pushconst(TimePrecision);
    pushconst(DefFile);
    pushconst(DefLineNo);
    pushconst(Scalar);
    pushconst(Vector);
    pushconst(Direction);
    pushconst(Input);
    pushconst(Output);
    pushconst(Inout);
    pushconst(MixedIO);
    pushconst(NoDirection);
    pushconst(NetType);
    pushconst(Wire);
    pushconst(Wand);
    pushconst(Wor);
    pushconst(Tri);
    pushconst(Tri0);
    pushconst(Tri1);
    pushconst(TriReg);
    pushconst(TriAnd);
    pushconst(TriOr);
    pushconst(Supply1);
    pushconst(Supply0);
    pushconst(Array);
    pushconst(PortIndex);
    pushconst(Edge);
    pushconst(NoEdge);
    pushconst(Edge01);
    pushconst(Edge10);
    pushconst(Edge0x);
    pushconst(Edgex1);
    pushconst(Edge1x);
    pushconst(Edgex0);
    pushconst(Posedge);
    pushconst(Negedge);
    pushconst(AnyEdge);
    pushconst(ConstType);
    pushconst(DecConst);
    pushconst(RealConst);
    pushconst(BinaryConst);
    pushconst(OctConst);
    pushconst(HexConst);
    pushconst(StringConst);
    pushconst(FuncType);
    pushconst(IntFunc);
    pushconst(RealFunc);
    pushconst(TimeFunc);
    pushconst(SizedFunc);
    pushconst(SizedSignedFunc);
    pushconst(SysFuncType);
    pushconst(SysFuncInt);
    pushconst(SysFuncReal);
    pushconst(SysFuncTime);
    pushconst(SysFuncSized);
    pushconst(UserDefn);
    pushconst(Automatic);
    pushconst(ConstantSelect);
    pushconst(Signed);
    pushconst(LocalParam);
    pushconst(BinStrVal);
    pushconst(OctStrVal);
    pushconst(DecStrVal);
    pushconst(HexStrVal);
    pushconst(ScalarVal);
    pushconst(IntVal);
    pushconst(RealVal);
    pushconst(StringVal);
    pushconst(VectorVal);
    pushconst(StrengthVal);
    pushconst(TimeVal);
    pushconst(ObjTypeVal);
    pushconst(SuppressVal);
    #undef pushconst

    #define pushcfn(name,fn) \
        lua_pushlstring(L, name, sizeof(name) - 1); \
        lua_pushcfunction(L, &fn); \
        lua_settable(L, -3);
    
    pushcfn("handle", vpi_handle_wrapper);
    pushcfn("handle_by_name", vpi_handle_by_name_wrapper);
    pushcfn("get", vpi_get_wrapper);
    pushcfn("get_str", vpi_get_str_wrapper);
    pushcfn("get_all", vpi_get_all);
    pushcfn("wait", vpi_wait);
    pushcfn("get_value", vpi_get_value_wrapper);
    pushcfn("put_value", vpi_put_value_wrapper);
    #undef pushcfn
    
    luaL_newmetatable(L, "vpiHandle");
    lua_pushlstring(L, "__index", sizeof("__index") -1);
    lua_pushvalue(L, -3); //Copy VPI table to top of stack
    lua_settable(L, -3);  //Set __index for the metatable

    lua_pushlstring(L, "__tostring", sizeof("__tostring") -1);
    lua_pushcfunction(L, &vpi_handle_to_string);
    lua_settable(L,-3); //Set __tostring for the metatatable

    lua_pop(L, 1); //Pop the metatable (safe, because the metatable
                   //is saved in the registry)
    
    lua_setglobal(L, "vpi"); //Finally, save the table of VPI functions
                             //to a global var
}

static int lua_repl_calltf([[maybe_unused]] char*user_data) {
    lua_State *L = luaL_newstate();
    if (!L) {
        cerr << "oops" << el;
        vpi_control(vpiFinish, -1);
        return -1;
    }

    luaL_openlibs(L);
    luaopen_vpi(L);
    lua_repl(L);
    return 0;
}

static int lua_repl_compiletf([[maybe_unused]] char*user_data) {
    return 0;
}

void lua_repl_task_register() {
    s_vpi_systf_data tf_data;

    tf_data.type      = vpiSysTask;
    tf_data.tfname    = "$lua_repl";
    tf_data.calltf    = lua_repl_calltf;
    tf_data.compiletf = lua_repl_compiletf;
    tf_data.sizetf    = 0;
    tf_data.user_data = 0;
    vpi_register_systf(&tf_data);
}

void (*vlog_startup_routines[])() = {
    lua_repl_task_register,
    0
};