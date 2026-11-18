-- Persisted endpoint refresh and one-time migration through production UI/JSON.
local kmb='https://data.etabus.gov.hk/v1/transport/kmb'
local ctb='https://rt.data.gov.hk/v2/transport/citybus'
local function favorite(id)
  return {route='101',operator='JOINT',bound='I',service_type='1',destination='Kwun Tong',
    parts={{operator='KMB',route='101',bound='I',service_type='1'},
      {operator='CTB',route='101',bound='O',service_type='1'}},
    stop_id=id,stop_operator='KMB',stop_seq=2,stop_name='Joint Stop',stop_lat=22.3,stop_lon=114.1}
end
local legacy=favorite('K1')
local ready=favorite('K2')
ready.endpoints={{operator='KMB',id='K2',url=kmb..'/eta/K2/101/1'},
  {operator='CTB',id='000002',url=ctb..'/eta/CTB/000002/101'}}
local bad=favorite('K3')
bad.endpoints={{operator='KMB',id='K3',url=kmb..'/eta/K3/101/1'},
  {operator='CTB',id='000003',url='https://untrusted.invalid/eta'}}
local saved={favorites={legacy,ready,bad}}
local pending,timer,seq,clock,fail_write=nil,nil,0,1794810600,true
local writes=0
crystal={ui=ui,json={decode=decode_json},fs={read=function()return nil end},
  state={get=function(key)return saved[key]end,set=function(key,value)
    writes=writes+1
    if fail_write then return false end
    saved[key]=decode_json(encode_json(value));return true
  end},service={time={now=function()return clock end},http={
    fetch=function(url,options,callback)
      assert(not pending);seq=seq+1;pending={url=url,id=seq,callback=callback};return seq
    end,cancel=function(id)if pending and pending.id==id then pending=nil;return true end;return false end
  }}}
ui.timer=function(ms,callback)assert(ms==100);timer=callback;return 1 end
local function tick()run_callback(timer)end
local function url(expected)assert(pending and pending.url==expected,'expected '..expected..', got '..(pending and pending.url or 'nil'))end
local function complete(body,code)
  local old=assert(pending);pending=nil
  run_callback(function()old.callback(old.id,code or 200,body or '{"data":[]}')end)
end
crystal.service.net=net_api
for _,name in ipairs({'is_ready','on_ready','off_ready'}) do crystal.service.time[name]=clock_api[name] end
clock_signal(false)
net_signal(false)
local app=assert(loadfile(package_path))();assert(app.onCreate())
for _=1,20 do tick() end
assert(not pending and has_text('No connection'))
net_signal(true)
assert(not pending)
tick();url(kmb..'/eta/K1/101/1')
local stale=pending
net_signal(false);assert(not pending and has_text('No connection'))
run_callback(function()stale.callback(stale.id,200,'{"data":[]}')end)
assert(not pending)
for _=1,20 do tick() end
assert(not pending)
net_signal(true);tick();url(kmb..'/eta/K1/101/1');complete()
url(ctb..'/route-stop/CTB/101/outbound')
complete('{"data":[{"stop":"000001","seq":2,"lat":22.3,"long":114.1}]}')
assert(saved.favorites[1].endpoints==nil and has_text('Could not save favorite endpoints'))
url(ctb..'/eta/CTB/000001/101');complete()
url(ready.endpoints[1].url);complete();url(ready.endpoints[2].url);complete()
assert(not pending and writes==1 and has_text('Tap a saved stop for live arrivals'))
-- A failed write does not publish the migrated record; next pass can retry.
fail_write=false;clock=clock+30;tick();url(kmb..'/eta/K1/101/1');complete()
url(ctb..'/route-stop/CTB/101/outbound')
complete('{"data":[{"stop":"000001","seq":2,"lat":22.3,"long":114.1}]}')
assert(#saved.favorites==2 and #saved.favorites[1].endpoints==2)
url(ctb..'/eta/CTB/000001/101');complete()
url(ready.endpoints[1].url);complete();url(ready.endpoints[2].url);complete()
assert(not pending and writes==2)
-- Later passes directly replay both URLs with no matching requests or writes.
clock=clock+30;tick();url(kmb..'/eta/K1/101/1');complete()
url(ctb..'/eta/CTB/000001/101');complete()
url(ready.endpoints[1].url);complete();url(ready.endpoints[2].url);complete()
assert(not pending and writes==2)
-- A small positive boot epoch must never be subtracted from a real ETA.
clock=120
assert(app.onPause() and app.onResume());tick()
url(kmb..'/eta/K1/101/1');complete('{"data":[{"eta":"2026-11-16T14:33:00+08:00"}]}')
url(ctb..'/eta/CTB/000001/101');complete()
url(ready.endpoints[1].url);complete('{"data":[{"eta":"2026-11-16T14:35:00+08:00"}]}')
url(ready.endpoints[2].url);complete()
for _=1,10 do tick() end
assert(has_text('--\n--\n--') and has_text('Waiting for device time'))
assert(not has_text('29913571 min'))
-- Clock sync repaints the cached favorites before the next network response.
clock=1794810600
clock_signal(true)
assert(has_text('3 min\n--\n--') and has_text('5 min\n--\n--'))
assert(not has_text('Waiting for device time'))
-- Resume still requests fresh arrivals after the clock notification.
assert(app.onPause() and app.onResume());tick()
url(kmb..'/eta/K1/101/1');complete();url(ctb..'/eta/CTB/000001/101');complete()
url(ready.endpoints[1].url);complete();url(ready.endpoints[2].url);complete()
-- Pause cancels the first provider; an abandoned callback cannot start the next.
clock=clock+30;tick();local late=assert(pending);assert(app.onPause() and not pending)
run_callback(function()late.callback(late.id,200,'{"data":[]}')end);assert(not pending)
assert(app.onResume());tick();url(kmb..'/eta/K1/101/1');complete()
url(ctb..'/eta/CTB/000001/101')
-- Offline favorite navigation never opens HTTP; reconnect refreshes the visible stop.
net_signal(false)
assert(has_text('--\n--\n--'))
click('Joint Stop');assert(not pending and has_text('No connection'))
for _=1,20 do tick() end
assert(not pending)
net_signal(true);tick();assert(pending)
local disconnected=pending
net_signal(false);assert(not pending and has_text('No connection'))
clock_signal(true);tick();assert(has_text('No connection'))
run_callback(function()disconnected.callback(disconnected.id,200,'{"data":[]}')end)
assert(not pending)
assert(app.onPause());clock_pause(true);net_signal(true);assert(not pending)
assert(app.onResume());clock_pause(false);tick();assert(pending)
assert(app.onDestroy() and not pending)
net_signal(false);net_signal(true);assert(not pending)
print('PASS: offline startup/detail, late response suppression, reconnect and paused recovery')
-- Eight joint favorites fit the production serializer node and storage limits.
local maximum={}
for i=1,8 do
  maximum[i]=decode_json(encode_json(ready))
  maximum[i].destination=string.rep('D',160);maximum[i].stop_name=string.rep('S',160)
end
local encoded=encode_json(maximum)
assert(#encoded<16*1024 and #decode_json(encoded)==8)
print('PASS: saved URLs, legacy migration, failed writes, cancellation, validation and eight joint favorites')
