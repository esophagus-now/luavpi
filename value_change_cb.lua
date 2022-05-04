c = coroutine.create(function(path)
    local e = vpi.handle_by_name(nil, path)

    local cbdat = {
        reason = vpi.cbValueChange,
        obj = e
    }

    -- Because the __gc field is set on vpiHandle
    -- userdata, by ignoring the return value the
    -- handle is automatically released and we
    -- prevent a memory leak
    vpi.register_cb(cbdat)

    while true do
        yield() -- wait for event
        print(path .. " changed! Its value is now: " .. e:get_value(vpi.DecStrVal))
    end
end)
assert(coroutine.resume(c,"tb.thing"))