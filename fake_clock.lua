-- Run this with dofile "fake_clock.lua"

function fake_clk(period, path) 
    local m = assert(
        vpi.handle_by_name(nil, path), 
        "Could not find [" .. path .. "]"
    )
    print("Making a clock with period " .. period .. " for " .. tostring(m))
    print("(inside " .. tostring(m:handle(vpi.Module)) .. ")")
    while true do
        m:put_value(0)
        --print("tick")
        vpi.wait(period/2)
        m:put_value(1)
        --print("tock")
        vpi.wait(period/2)
    end
end

c = coroutine.create(fake_clk)
assert(coroutine.resume(c, 100, "tb.DUT.i_clk"))