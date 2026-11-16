-- Production Lua allocator/JSON/UI with controlled provider, clock and storage.
local saved, pending, sequence, timer, retire_ticks = {}, nil, 0, nil, 0
local checkpoints={}
local original_print=print
print=function(...)
  local values={...};if values[1]=='BUS_RESOURCE' then checkpoints[values[2]]=true end
  original_print(...)
end
local kmb='https://data.etabus.gov.hk/v1/transport/kmb'
local ctb='https://rt.data.gov.hk/v2/transport/citybus'
local assets={
  ['assets/catalog/runtime.txt']='L A\nR routes.jsonl 12\n',
  ['assets/catalog/routes.jsonl']='{"route":"1A","operator":"KMB","bound":"O","service_type":"1","destination_en":"Star Ferry"}\n'..
    '{"route":"1A","operator":"KMB","bound":"I","service_type":"2","destination_en":"Sau Mau Ping"}\n'..
    '{"route":"2","operator":"CTB","bound":"O","service_type":"1","destination_en":"Central"}\n'..
    '{"route":"2","operator":"CTB","bound":"I","service_type":"1","destination_en":"Shau Kei Wan"}\n'
}
crystal={ui=ui,json={decode=decode_json},fs={read=function(path)return assets[path]end},
  state={get=function(k)return saved[k]end,set=function(k,value)saved[k]=value;return true end,flush=function()return true end},
  service={time={now=function()return 1794810600 end},http={
    fetch=function(url,options,callback)
      assert(options.timeout_sec==15 and not pending)
      if retire_ticks>0 then return nil,'worker quota exceeded' end
      sequence=sequence+1;pending={id=sequence,url=url,callback=callback};return sequence
    end,
    cancel=function(id)
      if pending and pending.id==id then pending=nil;retire_ticks=2;return true end
      return false
    end
  }}}
ui.timer=function(ms,callback)assert(ms==100 and not timer);timer=callback;return 1 end
local function ticks(n)
  for _=1,n or 30 do
    if retire_ticks>0 then retire_ticks=retire_ticks-1 end
    timer()
  end
end
local function complete(body,code)
  local job=assert(pending);pending=nil;job.callback(job.id,code or 200,body);ticks()
end
local function url(expected) assert(pending and pending.url==expected,'expected '..expected..', got '..(pending and pending.url or 'no request')) end
local function wait_url(expected,limit)
  for _=1,limit or 500 do
    if pending and pending.url==expected then return end
    ticks(1)
  end
  url(expected)
end
local clear,back,refresh=utf8.char(0xF00D),utf8.char(0xF053),utf8.char(0xF021)
-- Regression for the production list binding's item/sentinel handling.
local selected_index=0
local list=assert(ui.list(ui.screen(),{width=300,height=100,on_select=function(index) selected_index=index end}))
assert(ui.list_add(list,'String row'));assert(ui.list_insert(list,1,'Inserted row'))
assert(ui.list_count(list)==2);click('Inserted row');assert(selected_index==2)
assert(ui.list_clear(list));assert(ui.delete(list))

local app=assert(loadfile(package_path))()
assert(app.onCreate() and timer);assert(has_text('No saved stops yet'))
snapshot('favorites-empty.ppm');click('Search');assert(has_text('Search by route number'))
assert(not has_text('Route number'));snapshot('search-empty.ppm')
click('1');ticks(1);assert(not has_text('1A   KMB'),'debounce fired early')
click('A');ticks();assert(has_text('1A   KMB'));assert(not pending)
snapshot('search.ppm');click('1A   KMB');url(kmb..'/route-stop/1A/outbound/1')
assert(#saved.recent_routes==1)
-- Provider sequence strings, repeated ID occurrences and metadata API fallback.
complete('{"type":"RouteStop","data":[{"stop":"000001","seq":"1"},{"stop":"000001","seq":"2"}]}')
url(kmb..'/stop/000001')
complete('{"data":{"stop":"000001","name_en":"City Hall","lat":"22.3","long":"114.1"}}')
assert(has_text('1. City Hall') and has_text('2. City Hall'));assert(not pending)
assert(not has_text('Page 1 /') and not has_text('Next'))
snapshot('stops.ppm');click('2. City Hall');url(kmb..'/eta/000001/1A/1')
-- Different offsets, fractional seconds, null/empty timestamps, optional metadata
-- disagreements and stable ties. Retain timestamps and show the earliest three.
complete('{"data":[{"eta":"2026-11-16T14:41:00+08:00","rmk_en":"Later"},'..
 '{"eta":null},{"eta":""},{"eta":"2026-11-16T06:33:00.500Z","dir":"I","seq":99,"rmk_en":"First tie"},'..
 '{"eta":"2026-11-16T01:33:00-05:00","rmk_en":"Second tie"},'..
 '{"eta":"2026-11-16T14:32:00+08:00","rmk_en":"Earliest"}]}')
assert(has_text('2 min') and has_text('3 min') and not has_text('11 min'))
assert(has_text('KMB | Earliest') and has_text('KMB | First tie') and has_text('KMB | Second tie'))
snapshot('arrivals.ppm');click('Save stop')
assert(saved.favorites[1].stop_id=='000001' and saved.favorites[1].stop_seq==2)
assert(saved.favorites[1].eta_text==nil)
click(back);click(back);click('Favorites')
assert(has_text('City Hall') and has_text('Tap to load live arrivals'));snapshot('favorites.ppm')
click('1A   KMB');url(kmb..'/eta/000001/1A/1');complete('{}',503)
assert(has_text('Network error 503 - tap Refresh to retry'))
click(refresh);complete('{"data":[]}');assert(has_text('No arrivals available'))
click(refresh);complete('{"data":[{"eta":"2026-11-16T14:30:29+08:00"}]}');assert(has_text('Arriving'))
click(refresh);complete('{"data":[{"eta":"2026-11-16T14:30:30+08:00"}]}');assert(has_text('1 min'))
-- Independent known epoch for a valid leap day (UTC): 2000-02-29 12:30.
local live_clock=crystal.service.time.now
crystal.service.time.now=function()return 951827400 end
click(refresh);complete('{"data":[{"eta":"2000-02-29T12:33:00Z"}]}');assert(has_text('3 min'))
crystal.service.time.now=function()return 0 end
click(refresh);complete('{"data":[{"eta":"2026-11-16T14:33:00+08:00"}]}');assert(has_text('Time unavailable'))
crystal.service.time.now=live_clock
-- Bad calendar dates and missing timezones never produce an arrival.
click(refresh);complete('{"data":[{"eta":"2026-02-29T14:30:00+08:00"},{"eta":"2026-11-16T14:30:00"}]}')
assert(has_text('No arrivals available'))
for _,bad in ipairs({'{}','{"data":{}}','{"data":[{},]}','{"data":[{"eta":"x"}]',
    '{"data":[]} garbage','{"data":[]}{"data":[]}'}) do
  click(refresh);complete(bad);assert(has_text('Invalid provider response'))
end
click('Remove');assert(#saved.favorites==0)
click(back);click('Search');click(clear);ticks();assert(has_text('Recent routes'))
snapshot('search-recents.ppm');click('2');ticks();click('2   CTB')
url(ctb..'/route-stop/CTB/2/outbound')
complete('{"data":[{"stop":"002737","seq":1}]}');wait_url(ctb..'/stop/002737')
complete('{"data":{}}');assert(has_text('1. 002737 - Retry name'))
click('1. 002737 - Retry name');wait_url(ctb..'/stop/002737')
complete('{"data":{"stop":"002737","name_en":"Central","lat":"22.28","long":"114.15"}}')
url(ctb..'/eta/CTB/002737/2');complete('{"data":[{"eta":"2026-11-16T14:33:00+08:00"}]}')
assert(has_text('3 min') and has_text('CTB | Scheduled'))
-- Late callbacks cannot change a newer selection; cancelled native work may
-- retain its worker slot until a later dispatcher tick.
click(refresh);local late=assert(pending);assert(app.onPause());assert(not pending)
late.callback(late.id,200,'{"data":[{"eta":"2026-11-16T15:00:00+08:00"}]}');ticks()
assert(not pending and not has_text('30 min'));assert(app.onResume());ticks()
url(ctb..'/eta/CTB/002737/2');complete('{"data":[]}')
click(refresh);local old=assert(pending);click(refresh);assert(not pending);ticks(1);assert(not pending)
ticks(1);url(ctb..'/eta/CTB/002737/2')
old.callback(old.id,200,'{"data":[{"eta":"2026-11-16T15:00:00+08:00"}]}')
complete('{"data":[]}');assert(not has_text('30 min'))
-- A long response is framed over ticks; controls and callbacks are reused.
for _=1,100 do click(refresh);complete('{"data":[{"eta":"2026-11-16T14:33:00+08:00"}]}') end
-- Replay recorded official responses through the same UI, not a shadow parser.
-- These files are test inputs only; production packages contain no recorded ETA.
local sample='../../tools/fixtures/bus-phase1-providers/'
click(back);click(back);click(clear);click('1');click('A');ticks();click('1A   KMB')
url(kmb..'/route-stop/1A/outbound/1');complete(read_asset(sample..'kmb_stops.json'));ticks(1200)
url(kmb..'/stop/A3ADFCDF8487ADB9');complete(read_asset(sample..'kmb_detail.json'))
assert(has_text('1. SAU MAU PING (CENTRAL)'))
assert(has_text('35. DCFF4041D0C0ACF8 - Retry name'))
assert(list_scroll_y('35. DCFF4041D0C0ACF8 - Retry name')>0,'last stop must be reachable by scrolling')
click('35. DCFF4041D0C0ACF8 - Retry name');ticks(3);url(kmb..'/stop/DCFF4041D0C0ACF8')
complete('{"data":{"stop":"DCFF4041D0C0ACF8","name_en":"STAR FERRY, HARBOUR CITY (YT901)"}}')
url(kmb..'/eta/DCFF4041D0C0ACF8/1A/1')
complete(read_asset(sample..'kmb_eta.json'));assert(has_text('Updated just now'))
click(back);click(back);click(clear);click('2');ticks();click('2   CTB')
url(ctb..'/route-stop/CTB/2/outbound');complete(read_asset(sample..'ctb_stops.json'));ticks(30)
wait_url(ctb..'/stop/003155');complete(read_asset(sample..'ctb_detail.json'))
assert(has_text('1. Sai Wan Ho (Grand Promenade)'))
click('1. Sai Wan Ho (Grand Promenade)');ticks(3);url(ctb..'/eta/CTB/003155/2')
complete(read_asset(sample..'ctb_eta.json'));assert(has_text('Updated just now'))
-- Direction and actual service identity survive selecting the second variant.
click(back);click(back);click(clear);click('1');click('A');ticks();click('1A   KMB',2)
url(kmb..'/route-stop/1A/inbound/2')
complete('{"data":[{"stop":"leading/00%01?","seq":"1","name_en":"Encoded stop"}]}')
click('1. Encoded stop');url(kmb..'/eta/leading%2F00%2501%3F/1A/2')
complete('{"data":[]}');click(back);click(back);click(clear);click('2');ticks();click('2   CTB',2)
url(ctb..'/route-stop/CTB/2/inbound');complete('{"data":[]}')
assert(has_text('No stops found for this route'))
print('OFFICIAL_RESPONSE_MEMORY',memory_stats().peak,memory_stats().capacity)
assert(app.onDestroy());ticks();assert(not pending)
assert(checkpoints.launch_begin and checkpoints.launch_ready and checkpoints.catalog_loaded)
assert(checkpoints.stops_loaded and checkpoints.eta_complete and checkpoints.teardown)
print('PASS: KMB/CTB live endpoint workflow, metadata, timestamps, ETA, favorites and cancellation')
