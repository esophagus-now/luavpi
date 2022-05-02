-- Run this with dofile "fake_clock.lua"

function fake_clk(period) 
    while true do
        vpi.wait(period/2)
        print("tick")
        vpi.wait(period/2)
        print("tock")
    end
end

c = coroutine.create(fake_clk)
coroutine.resume(c, 100)