-- A reconnect refresh with exactly one saved stop must finish its status pass.
local url='https://data.etabus.gov.hk/v1/transport/kmb/eta/K1/1A/1'
local clock=1794810600
local favorite={route='1A',operator='KMB',bound='O',service_type='1',destination='Central',
  parts={{operator='KMB',route='1A',bound='O',service_type='1'}},
  stop_id='K1',stop_operator='KMB',stop_seq=1,stop_name='Central Stop',
  endpoints={{operator='KMB',id='K1',url=url}}}
local saved={favorites={favorite},recent_routes={}}
local pending,timer,sequence=nil,nil,0
crystal={ui=ui,json={decode=decode_json},fs={read=function()return nil end},
  state={get=function(key)return saved[key]end,set=function(key,value)saved[key]=value;return true end},
  service={time={now=function()return clock end},http={
    fetch=function(request_url,options,callback)
      assert(not pending and request_url==url and options.timeout_sec==15)
      sequence=sequence+1;pending={id=sequence,callback=callback};return sequence
    end,
    cancel=function(id)if pending and pending.id==id then pending=nil;return true end;return false end
  }}}
crystal.service.net=net_api
for _,name in ipairs({'is_ready','on_ready','off_ready'}) do crystal.service.time[name]=clock_api[name] end
ui.timer=function(ms,callback)assert(ms==100);timer=callback;return 1 end
local function tick()run_callback(timer)end
local function complete()
  local job=assert(pending);pending=nil
  run_callback(function()job.callback(job.id,200,'{"data":[]}')end)
end
clock_signal(true);net_signal(false)
local app=assert(loadfile(package_path))();assert(app.onCreate())
assert(has_text('No connection') and not pending)
net_signal(true)
assert(has_text('Refreshing arrivals...'))
tick();assert(pending)
complete()
assert(not pending and has_text('Tap a saved stop for live arrivals'))
assert(not has_text('Refreshing arrivals...'))
assert(app.onDestroy())
print('PASS: single-favorite reconnect refresh clears its status after completion')
