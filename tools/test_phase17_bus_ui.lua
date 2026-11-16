-- Executed by test_phase17_bus_render.py with production bindings and LVGL.
-- HTTP/state are deterministic adapters; list insertion and widget events are real.
local saved, requests, next_id, checkpoints = {}, {}, 0, {}
local original_print=print
print=function(...)
  local values={...}
  if values[1]=="BUS_RESOURCE" then checkpoints[values[2]]=true end
  original_print(...)
end
crystal = {ui=ui, state={get=function(k) return saved[k] end,
  set=function(k,value) saved[k]=value end, flush=function() return true end},
  json={decode=function(value) return value end}, service={http={
    fetch=function(url,_,callback)
      next_id=next_id+1; requests[next_id]={url=url,callback=callback}; return next_id
    end,
    cancel=function(id) requests[id].cancelled=true end,
  }, system={info=function() return {
    os_version='0.17.0',abi_version=1,
    lua_arena_kb=160,lua_used_kb=80,lua_peak_kb=96,lua_largest_free_kb=60,
    free_internal_kb=200,largest_internal_kb=150,native_internal_delta_kb=12,
    native_internal_low_kb=190,free_psram_kb=24000,largest_psram_kb=12000,
    native_psram_delta_kb=180,native_psram_low_kb=23800,http_peak_body_bytes=512,
    http_min_tls_internal_bytes=100000,http_min_tls_largest_bytes=64000,
    http_min_tls_psram_bytes=2000000,http_completed=12,http_cancellations=1,
    json_peak_internal_bytes=128,json_peak_psram_bytes=256,json_decode_count=12,
    json_largest_input_bytes=2048,app_tasks=0,app_timers=1,app_queues=0,
    app_allocations=2,app_objects=120,app_callbacks=64,app_http_callbacks=1,
    app_tracked_bytes=163840,app_storage_bytes=2048,free_storage_kb=3500,
  } end}}}
local function complete(body,code)
  local id=next_id; local r=requests[id]; requests[id]=nil
  r.callback(id,code or 200,body)
end
-- This fails with the old lua_replace implementation before the app is built.
local list=assert(ui.list(ui.screen(),{width=300,height=100}))
assert(ui.list_add(list,'String row'))
assert(ui.list_add(list,{primary='Primary row',secondary='Secondary row'}))
assert(ui.list_insert(list,1,'Inserted row'))
assert(ui.list_count(list)==3)
assert(ui.list_clear(list) and ui.list_count(list)==0)
assert(not ui.list_add(list,{}))
assert(ui.delete(list))
print("memory test: load Bus")
local app=assert(loadfile(package_path))()
print("memory test: create Bus")
assert(app.onCreate())
print("memory test: interact")
assert(has_text('No saved stops yet'))
snapshot('favorites-empty.ppm')
click('Search')
assert(has_text('Search by route number'))
assert(not has_text('Route number'))
snapshot('search-empty.ppm')
click('1');complete({routes={{route='1A',operator='KMB',origin='Sau Mau Ping',destination='Star Ferry',bound='O',service_type='1'}}})
assert(has_text('1A   KMB'))
click('2');complete({routes={}})
click(utf8.char(0xF55A));complete({routes={}})
assert(has_text('1') and not has_text('12'))
click(utf8.char(0xF00D))
snapshot('search-cleared.ppm')
click('1')
click('A') -- While first request is in flight: queue the latest query only.
local old=next_id
complete({routes={}})
assert(next_id==old+1 and requests[next_id].url:find('query=1A',1,true))
complete({routes={{route='1A',operator='KMB',origin='Sau Mau Ping',destination='Star Ferry',bound='O',service_type='1'}}})
assert(has_text('1A   KMB') and has_text('To Star Ferry'))
snapshot('search.ppm')
click('1A   KMB')
assert(#saved.recent_routes==1 and saved.recent_routes[1].route=='1A')
assert(requests[next_id].url:find('/api/stops?',1,true))
complete({stops={{seq=1,id='KMB-001',name_en='Central Market'}, {seq=2,id='KMB-002',name_en='City Hall'}}})
assert(has_text('1. Central Market') and has_text('2. City Hall'))
assert(not has_text('2 stops | fixture data') and not has_text('Refresh'))
snapshot('stops.ppm')
click('2. City Hall')
assert(requests[next_id].url:find('&stop=KMB-002',1,true), 'ETA must use selected stop')
complete({etas={{minutes=3,destination='Star Ferry',remark='Scheduled'}, {minutes=11,destination='Star Ferry',remark='Scheduled'}}})
assert(has_text('3 min') and has_text('Scheduled'))
snapshot('arrivals.ppm')
click('Save stop')
assert(saved.favorites[1].stop_id=='KMB-002')
assert(saved.recent_routes[1].route=='1A' and saved.recent_routes[1].destination=='Star Ferry')
click(utf8.char(0xF053));click(utf8.char(0xF053));click('Favorites')
assert(has_text('City Hall') and has_text('3 min  |  11 min'))
snapshot('favorites.ppm')
click('Search');click(utf8.char(0xF00D));complete({routes={{route='1A',operator='KMB',destination='Star Ferry'}}})
assert(has_text('Recent routes') and has_text('1A   KMB'))
snapshot('search-recents.ppm')
click('1A   KMB')
assert(requests[next_id].url:find('/api/stops?',1,true))
complete({stops={{seq=1,id='KMB-001',name_en='Central Market'}}})
click(utf8.char(0xF053));click('Favorites')
click('1A   KMB')
assert(requests[next_id].url:find('&stop=KMB-002',1,true))
complete({},503)
assert(has_text('Network error 503 - tap Refresh to retry'))
click(utf8.char(0xF021));complete({etas={}})
assert(has_text('No arrivals available'))
click('Remove');assert(#saved.favorites==0)
click(utf8.char(0xF053));click('Search');click('2');complete({routes={{route='2',operator='CTB',destination='Central'}}})
click('2   CTB');complete({stops={}})
assert(has_text('No stops found for this route'))
click(utf8.char(0xF021));local cancelled=next_id
assert(app.onPause() and requests[cancelled].cancelled)
assert(app.onResume())
requests[cancelled].callback(cancelled,200,{stops={{seq=9,id='late',name_en='Stale stop'}}})
assert(not has_text('9. Stale stop'))
click(utf8.char(0xF021));complete({stops={{seq=1,id='CTB-001',name_en='Central'}}})
assert(has_text('1. Central'))
-- Repeated route/stop refreshes reuse controls, keeping callback count bounded.
for i=1,1000 do
  click(utf8.char(0xF021));complete({stops={{seq=1,id='CTB-001',name_en='Central'}}})
end
assert(app.onDestroy())
assert(checkpoints.launch_begin and checkpoints.launch_ready and checkpoints.catalog_loaded)
assert(checkpoints.stops_loaded and checkpoints.eta_complete and checkpoints.teardown)
