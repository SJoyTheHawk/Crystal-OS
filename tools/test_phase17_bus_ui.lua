-- Production Lua allocator/JSON/UI with controlled provider, clock and storage.
local saved, pending, sequence, timer, retire_ticks = {}, nil, 0, nil, 0
local failed_writes={}
local checkpoints={}
local original_print=print
print=function(...)
  local values={...};if values[1]=='BUS_RESOURCE' then checkpoints[values[2]]=true end
  original_print(...)
end
local kmb='https://data.etabus.gov.hk/v1/transport/kmb'
local ctb='https://rt.data.gov.hk/v2/transport/citybus'
local assets={
  ['assets/catalog/runtime.txt']='L A\nC 5\nQ route-search.txt\nR routes.jsonl 12\n',
  ['assets/catalog/route-search.txt']='1A\tKMB\tO\t1\tStar%20Ferry\tKMB%7C1A%7CO%7C1\t\n'..
    '1A\tKMB\tI\t2\tSau%20Mau%20Ping\tKMB%7C1A%7CI%7C2\t\n'..
    '2\tCTB\tO\t1\tCentral\tCTB%7C2%7CO%7C1\t\n'..
    '2\tCTB\tI\t1\tShau%20Kei%20Wan\tCTB%7C2%7CI%7C1\t\n'..
    '101\tJOINT\tI\t1\tKwun%20Tong\tJOINT%7CKMB%3A101%3AI%3A1%7CCTB%3A101%3AO%3A2\tKMB%3A101%3AI%3A1%3BCTB%3A101%3AO%3A2\n',
  ['assets/catalog/routes.jsonl']='{"route":"1A","operator":"KMB","bound":"O","service_type":"1","destination_en":"Star Ferry"}\n'..
    '{"route":"1A","operator":"KMB","bound":"I","service_type":"2","destination_en":"Sau Mau Ping"}\n'..
    '{"route":"2","operator":"CTB","bound":"O","service_type":"1","destination_en":"Central"}\n'..
    '{"route":"2","operator":"CTB","bound":"I","service_type":"1","destination_en":"Shau Kei Wan"}\n'..
    '{"route":"101","operator":"JOINT","bound":"I","service_type":"1","destination_en":"Kwun Tong","parts":[{"operator":"KMB","route":"101","bound":"I","service_type":"1"},{"operator":"CTB","route":"101","bound":"O","service_type":"2"}]}\n'
}
crystal={ui=ui,json={decode=decode_json},fs={read=function(path)return assets[path]end},
  state={get=function(k)return saved[k]end,set=function(k,value)
    if failed_writes[k] then return false end
    saved[k]=value;return true
  end,flush=function()return true end},
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
    run_callback(timer)
  end
end
local function complete(body,code)
  local job=assert(pending);pending=nil
  run_callback(function() job.callback(job.id,code or 200,body) end)
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
snapshot('favorites-empty.ppm');click('Search');click('1')
assert(not has_text('1A   KMB'),'keypad must ignore input before route preload finishes')
assert(not is_enabled('1'),'numeric keypad must remain disabled while catalog loads')
ticks(1);assert(has_text('Enter a route number'),'catalog preload still waits between files')
assert(text_font_size('Enter a route number')==20,'search status must use the shared larger font')
assert(is_enabled('1') and has_text('9') and not is_enabled('9'),'numeric keys stay visible and disable when unavailable')
assert(not has_text('A') and not has_text('Z'),'letters without a matching route prefix must be hidden')
assert(not has_text('Route number'));snapshot('search-empty.ppm')
click('1');ticks(1);assert(not has_text('1A   KMB'),'debounce fired early')
assert(is_enabled('A') and is_enabled('0') and has_text('9') and not is_enabled('9'),
  'after prefix 1 only characters present in route numbers should be enabled')
assert(has_text('A') and not has_text('Z'),'valid letters reappear after the prefix changes')
click('A');ticks();assert(has_text('1A   KMB'));assert(not pending)
assert(text_font_size('1A   KMB')==28,'route results should use the larger route-title size')
assert(not has_text('A'),'letter keys hide when the completed route has no next character')
snapshot('search.ppm');click('1A   KMB')
assert(text_font_size('1A  |  KMB')==28,'detail route title should match the back icon size')
local _,back_y=position(back);local _,title_y=position('1A  |  KMB')
assert(title_y==back_y,'detail route title should align vertically with the back button')
local _,header_y,_,header_height=widget_metrics('1A  |  KMB')
assert(header_height==math.floor(48*screen_width/480),
  'detail header should use the compact 48 px logical height')
assert(header_y<=math.floor(16*screen_width/480)+1,
  'detail header should align with the standard page padding')
url(kmb..'/route-stop/1A/outbound/1')
assert(not has_text('Choose a route and direction') and not has_text('Loading bus stops...'))
assert(text_count('Loading stops...')==1 and text_font_size('Loading stops...')==20)
assert(#saved.recent_routes==1)
-- Provider sequence strings, repeated ID occurrences and metadata API fallback.
complete('{"type":"RouteStop","data":[{"stop":"000001","seq":"1"},{"stop":"000001","seq":"2"}]}')
url(kmb..'/stop/000001')
complete('{"data":{"stop":"000001","name_en":"City Hall","lat":"22.3","long":"114.1"}}')
assert(has_text('1. City Hall') and has_text('2. City Hall'));assert(not pending)
assert(text_count('Tap a stop for arrivals')==1,'keep only the bottom stop guidance')
assert(text_font_size('Tap a stop for arrivals')==20)
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
assert(text_font_size('Updated just now')==20,'arrival status must use the shared larger font')
snapshot('arrivals.ppm');failed_writes.favorites=true;click('Save stop')
assert(saved.favorites==nil and has_text('Could not save favorite'))
failed_writes.favorites=nil;click('Save stop')
assert(saved.favorites[1].stop_id=='000001' and saved.favorites[1].stop_seq==2)
assert(saved.favorites[1].eta_text==nil)
click(back);click(back);click('Favorites')
assert(text_font_size('Tap a saved stop for live arrivals')==20,'favorites status must use the shared larger font')
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
    '{"data":[','{"data":[{"eta":"unfinished','{"data":[{"eta":"x"}}]}',
    '{"data":[]} garbage','{"data":[]}{"data":[]}'}) do
  click(refresh);complete(bad);assert(has_text('Invalid provider response'))
end
failed_writes.favorites=true;click('Remove')
assert(#saved.favorites==1 and has_text('Could not remove favorite'))
failed_writes.favorites=nil;click('Remove');assert(#saved.favorites==0)
click(back);click('Search');click(clear);ticks();assert(has_text('Recent routes saved on this device'))
assert(has_text('Recent:') and has_text('1A   KMB'))
local _,recent_heading_y=position('Recent:');local _,recent_route_y=position('1A   KMB')
assert(recent_route_y>recent_heading_y,'Recent: should appear above the recent route records')
assert(has_text('1A   KMB') and not has_text('2   CTB'))
snapshot('search-recents.ppm');failed_writes.recent_routes=true;click('2');ticks();click('2   CTB')
assert(#saved.recent_routes==1 and saved.recent_routes[1].route=='1A')
assert(has_text('Recent route not saved'));failed_writes.recent_routes=nil
url(ctb..'/route-stop/CTB/2/outbound')
complete('{"data":[{"stop":"002737","seq":1}]}');wait_url(ctb..'/stop/002737')
complete('{"data":{}}');assert(has_text('Loading...'))
assert(not has_text('1. 002737 - Retry name') and not has_text('1. Loading...'))
snapshot('stops-loading.ppm')
click('Loading...');wait_url(ctb..'/stop/002737')
assert(has_text('Loading...'),'unresolved selected-stop heading must keep the loading placeholder')
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
-- Repeated responses decode without per-record timer delays; controls are reused.
for _=1,100 do click(refresh);complete('{"data":[{"eta":"2026-11-16T14:33:00+08:00"}]}') end
-- Replay recorded official responses through the same UI, not a shadow parser.
-- These files are test inputs only; production packages contain no recorded ETA.
local sample='../../tools/fixtures/bus-phase1-providers/'
click(back);click(back);click(clear);click('1');click('A');ticks();click('1A   KMB')
url(kmb..'/route-stop/1A/outbound/1');complete(read_asset(sample..'kmb_stops.json'))
url(kmb..'/stop/A3ADFCDF8487ADB9');complete(read_asset(sample..'kmb_detail.json'))
assert(has_text('1. SAU MAU PING (CENTRAL)'))
assert(has_text('Loading...'))
-- The first stop is resolved; the remaining 34 identical placeholders keep
-- their own occurrence identities, including the last stop in the list.
assert(list_scroll_y('Loading...',34)>0,'last stop must be reachable by scrolling')
click('Loading...',34);ticks(3);url(kmb..'/stop/DCFF4041D0C0ACF8')
complete('{"data":{"stop":"DCFF4041D0C0ACF8","name_en":"STAR FERRY, HARBOUR CITY (YT901)"}}')
url(kmb..'/eta/DCFF4041D0C0ACF8/1A/1')
complete(read_asset(sample..'kmb_eta.json'));assert(has_text('Updated just now'))
click(back);click(back);click(clear);click('2');ticks();click('2   CTB')
url(ctb..'/route-stop/CTB/2/outbound');complete(read_asset(sample..'ctb_stops.json'))
wait_url(ctb..'/stop/003155');complete(read_asset(sample..'ctb_detail.json'))
assert(has_text('1. Sai Wan Ho (Grand Promenade)'))
click('1. Sai Wan Ho (Grand Promenade)');ticks(3);url(ctb..'/eta/CTB/003155/2')
complete(read_asset(sample..'ctb_eta.json'));assert(has_text('Updated just now'))
-- Phase 2 joint route: KMB is the primary stop sequence, then the nearest
-- CTB stop is selected (even when it is over 500m away) and both provider ETA
-- lists are merged by timestamp.
click(back);click(back);click(clear);click('1');click('0');click('1');ticks();assert(has_text('101   KMB/CTB'))
click('101   KMB/CTB');url(kmb..'/route-stop/101/inbound/1')
complete('{"data":[{"stop":"KMB-101-001","seq":1},{"stop":"KMB-101-002","seq":2}]}')
wait_url(kmb..'/stop/KMB-101-001');complete('{"data":{"stop":"KMB-101-001","name_en":"Joint KMB One","lat":"22.3000","long":"114.1000"}}')
wait_url(kmb..'/stop/KMB-101-002');complete('{"data":{"stop":"KMB-101-002","name_en":"Joint KMB Two","lat":"22.3100","long":"114.1100"}}');ticks()
click('1. Joint KMB One');url(kmb..'/eta/KMB-101-001/101/1')
complete('{"data":[{"eta":"2026-11-16T14:33:00+08:00","co":"KMB","rmk_en":"KMB scheduled"}]}')
url(ctb..'/route-stop/CTB/101/outbound');complete('{"data":[{"stop":"CTB-101-001","seq":1},{"stop":"CTB-101-002","seq":2}]}')
url(ctb..'/stop/CTB-101-001');complete('{"data":{"stop":"CTB-101-001","name_en":"Joint CTB One","lat":"22.3300","long":"114.1300"}}')
url(ctb..'/stop/CTB-101-002');complete('{"data":{"stop":"CTB-101-002","name_en":"Joint CTB Two","lat":"22.3100","long":"114.1100"}}')
url(ctb..'/eta/CTB/CTB-101-002/101');complete('{"data":[{"eta":"2026-11-16T14:35:00+08:00","co":"CTB","rmk_en":"CTB scheduled"}]}')
assert(has_text('KMB | KMB scheduled') and has_text('CTB | CTB scheduled'))
-- A failed KMB ETA must leave CTB arrivals usable after matching metadata is
-- loaded through the provider fallback path.
click(refresh);url(kmb..'/eta/KMB-101-001/101/1');complete('{}',503)
url(ctb..'/route-stop/CTB/101/outbound');complete('{"data":[{"stop":"CTB-101-001","seq":1},{"stop":"CTB-101-002","seq":2}]}')
url(ctb..'/stop/CTB-101-001');complete('{"data":{"stop":"CTB-101-001","name_en":"Joint CTB One","lat":"22.3300","long":"114.1300"}}')
url(ctb..'/stop/CTB-101-002');complete('{"data":{"stop":"CTB-101-002","name_en":"Joint CTB Two","lat":"22.3100","long":"114.1100"}}')
url(ctb..'/eta/CTB/CTB-101-002/101');complete('{"data":[{"eta":"2026-11-16T14:35:00+08:00","co":"CTB","rmk_en":"CTB partial"}]}')
assert(has_text('CTB | CTB partial') and has_text('Partial arrivals: CTB only'))
-- The other single-source failure direction preserves KMB results; two failed
-- sources produce a retryable combined error.
click(refresh);url(kmb..'/eta/KMB-101-001/101/1')
complete('{"data":[{"eta":"2026-11-16T14:34:00+08:00","co":"KMB","rmk_en":"KMB partial"}]}')
url(ctb..'/route-stop/CTB/101/outbound');complete('{}',503)
assert(has_text('KMB | KMB partial') and has_text('Partial arrivals: KMB only'))
click(refresh);url(kmb..'/eta/KMB-101-001/101/1');complete('{}',503)
url(ctb..'/route-stop/CTB/101/outbound');complete('{}',503)
assert(has_text('Unable to load arrivals - tap Refresh to retry'))
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
