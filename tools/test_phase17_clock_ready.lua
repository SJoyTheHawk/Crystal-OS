-- Production core subscription dispatch plus protected Lua bindings.
local calls=0
clock_signal(false)
assert(not clock_api.is_ready())
assert(clock_api.on_ready(function(event)assert(event=='ready');calls=calls+1 end))
clock_dispatch();assert(calls==0)
-- Numeric clock plausibility is not a substitute for the OS ready event.
clock_signal(true);assert(clock_api.is_ready() and calls==1)
clock_dispatch();assert(calls==1)
-- SNTP/manual corrections notify again, but ordinary UI ticks do not.
clock_signal(true);assert(calls==2)
-- Replacing one subscriber does not consume another callback slot.
for i=1,100 do assert(clock_api.on_ready(function()calls=calls+1 end)) end
assert(calls==2 and clock_callback_count()==1);clock_dispatch();assert(calls==3)
-- Paused apps receive one coalesced notification on resume.
clock_pause(true);clock_signal(true);clock_signal(true);assert(calls==3)
clock_pause(false);assert(calls==4)
clock_dispatch();assert(calls==4)
-- A subscriber may remove itself safely from inside its own callback.
assert(clock_api.on_ready(function()calls=calls+1;assert(clock_api.off_ready())end))
clock_dispatch();assert(calls==5)
clock_signal(true);assert(calls==5 and not clock_api.off_ready())
-- Already ready at subscription time: replay is deferred, not synchronous.
assert(clock_api.on_ready(function()calls=calls+1 end));assert(calls==5)
clock_dispatch();assert(calls==6)
assert(clock_api.off_ready());clock_signal(true);assert(calls==6)
assert(not clock_api.on_ready('invalid'))
print('PASS: OS clock-ready replay, updates, replacement, pause/resume and unsubscribe')
-- Runtime cleanup cancels a pending paused event even without app cooperation.
assert(clock_api.on_ready(function()error('late callback after teardown')end))
clock_pause(true);clock_signal(true)
clock_reset();assert(clock_callback_count()==0)
clock_pause(false);clock_dispatch()
