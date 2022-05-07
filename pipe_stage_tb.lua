-- Meant to be a relatively complete testbench for the pipe stage
-- example. There are a few ideas in here that would be nice to
-- pull out into a common library

-- This function just watches the clock and reset given as the
-- two handle arguments, and on rising edges it resumes all the
-- coroutines in the at_posedge table. For extra niceness, each
-- entry in this table has both the coroutine (in field c) and
-- an optional args table (in field args)
at_posedge = {}
function clk_watch(clk, rstn)
    local cb = vpi.register_cb({reason=vpi.cbValueChange, obj=rstn})
    while true do
        coroutine.yield()
        rval = rstn:get_value(vpi.IntVal)
        if (rval ~= 0) then
            print("Out of reset")
            break
        end
    end

    vpi.remove_cb(cb)
    
    cb = vpi.register_cb({reason=vpi.cbValueChange, obj=clk})

    while true do
        yield()
        if (clk:get_value(vpi.IntVal) ~= 0) then
            for _,r in ipairs(at_posedge) do
                assert(coroutine.resume(r.c, r.args))
            end
        end
    end
end

-- Example of a property checker coroutine
-- FIXME: because I'm using this like an ordinary lua
-- coroutine, I need to call coroutine.yield instead of
-- my custom yield that just leaves the lua repl. I should
-- try to improve this so you can just use coroutine.yield
-- for everything (I did try it and it kind of works, but
-- after a few iterations it crashes lua and I don't know
-- why yet)
function prop_checker(args) 
    local m = args.m
    local wrsig = m:handle_by_name("wr_sig")
    local rdsig = m:handle_by_name("rd_sig")
    local wrptr = m:handle_by_name("wr_ptr")
    local rdptr = m:handle_by_name("rd_ptr")
    
    while true do
        if (wrsig and rdsig) then
            assert(
                wrptr ~= rdptr, 
                "Property check failed: read and write to same pipe stage slot"
            )
        end
        coroutine.yield()
    end
end

function random_driver(args)
    local m = assert(args.m, "No module given to random driver")
    local l = args.l or 0
    local h = args.h or 1
    local wait_NBA = {
        reason = vpi.cbReadWriteSynch
    }
    while true do
        -- Need to be careful to only put values as if we
        -- had written Verilog with non-blocking assignments
        vpi.register_cb(wait_NBA)
        -- ugly... need to use different yields for different 
        -- reasons...
        yield()
        m:put_value(math.random(l,h))
        coroutine.yield()
    end
end

function clock_driver(clk, period)
    print("Making a clock with period " .. period .. " for " .. tostring(clk))
    print("(inside " .. tostring(clk:handle(vpi.Module)) .. ")")
    while true do
        clk:put_value(0)
        vpi.wait(period/2)
        clk:put_value(1)
        vpi.wait(period/2)
    end
end

function reset_driver(rstn)
    rstn:put_value(0)
    vpi.wait(1000)
    rstn:put_value(1)
    yield() -- Leave the REPL
end

function hs_watch(args)
    local d = assert(args.d, "No handle for data given")
    local v = assert(args.v, "No handle for vld given")
    local r = assert(args.r, "No handle for rdy given")

    while true do
        if (v:get_value(vpi.IntVal) ~= 0 and r:get_value(vpi.IntVal) ~= 0) then
            print("Observed datum 0x"..d:get_value(vpi.HexStrVal))
        end
        coroutine.yield()
    end
end

-- Pushes sequential integers into an RTS/RTR interface
-- and waits for random times in between valid pulses
function hs_seq_driver(args)
    local d = assert(args.d, "No handle for data given")
    local v = assert(args.v, "No handle for vld given")
    local r = assert(args.r, "No handle for rdy given")
    
    local l = args.l or 0
    local h = args.h or 3
    assert(l>=0, "Lower bound must be nonnegative")
    assert(h>=l, "Invalid low/high bounds given")

    local wait_NBA = {
        reason = vpi.cbReadWriteSynch
    }
    
    -- initialize
    local cnt = 1
    d:put_value("0xxx")
    v:put_value(0)
    local time_until_vld = math.random(l,h)

    local vld = 0
    
    while true do
        -- I'm finding myself writing pseudo-verilog
        -- in Lua... I wanted the API to be less 
        -- cumbersome than Verilog but it's certainly
        -- no better... I need to do some real soul-
        -- searching and design something more usable
        if (time_until_vld > 0) then
            time_until_vld = time_until_vld - 1
        end
        local rdy = r:get_value(vpi.IntVal)
        if (vld ~= 0) then
            if (rdy ~= 0) then
                cnt = cnt + 1
                time_until_vld = math.random(l,h)
                if (time_until_vld == 0) then
                    vld = 1
                else 
                    vld = 0
                end
            end
        elseif (time_until_vld == 0) then
            vld = 1
        end

        -- Need to be careful to only put values as if we
        -- had written Verilog with non-blocking assignments
        vpi.register_cb(wait_NBA)
        -- ugly... need to use different yields for different 
        -- reasons...
        yield()
        v:put_value(vld)
        if (v == 0) then
            d:put_value("0xxx")
        else 
            d:put_value(cnt)
        end

        -- Wait for next posedge_clk
        coroutine.yield()
    end
end

DUT = vpi.handle_by_name(nil, "tb.DUT")
clk = DUT:handle_by_name("clk")
rstn = DUT:handle_by_name("i_reset_n")
irdy = DUT:handle_by_name("i_rdy")
ivld = DUT:handle_by_name("i_vld")
-- Give ivld initial value to prevent xprop
ivld:put_value(0)

table.insert(at_posedge, {c = coroutine.create(prop_checker), args={m=DUT}})
table.insert(at_posedge, {c = coroutine.create(random_driver), args={m=irdy}})
table.insert(
    at_posedge, {
        c = coroutine.create(hs_seq_driver), 
        args={
            d = DUT:handle_by_name("i_data"),
            v = DUT:handle_by_name("i_vld"),
            r = DUT:handle_by_name("o_rdy")
        }
    }
)
table.insert(
    at_posedge, {
        c = coroutine.create(hs_watch), 
        args={
            d = DUT:handle_by_name("o_data"),
            v = DUT:handle_by_name("o_vld"),
            r = DUT:handle_by_name("i_rdy")
        }
    }
)

c = coroutine.create(clock_driver)
assert(coroutine.resume(c, clk, 200))

c = coroutine.create(reset_driver)
assert(coroutine.resume(c, rstn))

c = coroutine.create(clk_watch)
assert(coroutine.resume(c, clk, rstn))
