c = coroutine.create(function(path)
    local e = vpi.handle_by_name(nil, path)
    vpi.wait(10)
    print("Putting 1 on event")
    e:put_value(1)
        
    vpi.wait(10)
    print("Putting 0 on event")
    e:put_value(0)
        
    vpi.wait(10)
    print("Putting 1 on event")
    e:put_value(1)

    yield() -- leave interactive prompt
end)
--print("Created coroutine: ", tostring(c))
assert(coroutine.resume(c,"tb.my_ev"))