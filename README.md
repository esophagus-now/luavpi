### luavpi
---

Project started on May 31 / 2022

I wanted an easy way to create/debug verilog simulations. Because Lua is (probably) my favourite programming language, and because it is so easy to add it as an extension language, it made sense to try this out.

## Quickstart
_In case you are looking at this on github, please navigate to [https://replit.com/@Mahkoe/Embedded-lua-in-verilog](https://replit.com/@Mahkoe/Embedded-lua-in-verilog) to run this in the online IDE. I have not gone to the trouble to make this repo work on any platform besides repl.it_

Run `go.sh`. This will compile the C++ code, then compile the Verilog code, and then it will start the simulation. Try the following code snippet:

```lua
tb = vpi.handle_by_name(nil, "tb")
print(tb)
print(tb:get_str(vpi.File) .. ":" .. tb:get(vpi.LineNo))

DUT = vpi.handle_by_name(nil, "tb.DUT")
print(DUT)

-- Can also get modules hierarchically
DUT = vpi.handle_by_name(tb, "DUT")
print(DUT)

-- Unlike the VPI spec, the vpiHandle is always the first
-- argument. This is so you can use the following kind
-- of shorthand (equivalent to the above line)
DUT = tb:handle_by_name("DUT")

-- Instead of vpi_iterate and vpi_scan, luavpi provides
-- vpi.get_all(handle, type). Or, using the shorthand,
-- handle:get_all(type)
submodules = tb:get_all(vpi.Module)
for i,v in ipairs(submodules) do print(i,v) end

DUT = m[1]
print(DUT)

i_clk = DUT:handle_by_name("i_clk")
print(i_clk:get_value(vpi.DecStrVal))

-- Notice in the Verilog code that there is a $monitor
-- watching i_clk
i_clk:put_value(1)
-- TODO: put_value currently only accepts integers, need
-- to support the other types

-- Blocking call that waits for the given number of sim
-- time units. hello.v has a timescale of 1ns/1ps, so this
-- waits for n*1ps
vpi.wait(100)
-- TODO: need to support waiting for value-change, waiting
-- for RO_sync, etc.

-- We can use Lua coroutines to do interesting things with
-- vpi.wait (did I mention Lua is my favourite language?)
dofile "fake_clock.lua"

-- Notice that the fake_clock immediately sets the clock to 0,
-- but then stops...

-- The fake_clock coroutine is started, but our interactive
-- REPL in the main thread is preventing the simulation from
-- continuing. So let our main thread block for a while; it
-- will automatically be resumed, and in the meantime, the
-- fake clock will also be periodically resumed
vpi.wait(1234)

-- If you hit CTRL-D to end interactive input, the main thread
-- will return, but the fake_clock coroutine will continue to be
-- periodically resumed until $finish is called
```

## API description
_This is subject to change since I'm still bringing up this project in the first place_

All functionality is made available in the global `vpi` table. VPI handle objects in Lua have a metatable with a `__tostring` pretty-printer. The `h:get_value(type)` shorthand shown in the quickstart is possible because the `__index` field of the VPI handle metatable points to the `vpi` table.

Most VPI constants are available in the `vpi` table. If the constant was called `vpiXyzAbc` in C code, then it should be available in Lua with `vpi.XyzAbc`. 

Likewise, a subset of the available VPI functions (listed below) can also be found in this table; if the function is called `vpi_xyz_abc` in C then it would be available in Lua as `vpi.xyz_abc`. Keep in mind that I always place the vpiHandle argument as the first argument, even when this contradicts the VPI spec.

The available functions are:
 - `vpi.handle(h, type)` and `vpi.handle_by_name(h,type)`. You can use `nil` as the first argument (this will pass `NULL` as the first argument in C). If the C call returns `NULL`, then the call returns `nil` in Lua
 - `vpi.get(h, type)` and `vpi.get_str(h,type)`
 - `vpi.get_value(h,type)`. Currently supports `vpi[Bin,Oct,Dec,Hex]StrVal`, `vpiIntVal`, `vpiStringVal`, and `vpiTimeVal`
 - `vpi.put_value(h, val)`. Only supports putting integer values.
 - `vpi.wait(delay)`. Blocks current coroutine until `delay` simulation time units have elapsed. This uses `cbAfterDelay` behind the scenes so that the coroutine is automatically resumed at the right time.
 - `vpi.get_all(h, type)`. Behind the scenes this uses `vpi_iterate` and `vpi_scan` to fill an array with handles to all instances of `type` in `h`. This array is returned as a Lua table (and it can be empty)