-- Real catalog files, production JSON conversion/allocator/UI; deterministic HTTP.
local timer, pending, sequence = nil, nil, 0
local saved={}
local reads,decodes=0,0
local preload=true
local native_set_enabled=ui.set_enabled
local enabled_states={}
local check_keypad=true
ui.set_enabled=function(handle,enabled)
  if check_keypad then assert(enabled_states[handle]~=enabled,'redundant enabled-state write') end
  enabled_states[handle]=enabled
  return native_set_enabled(handle,enabled)
end
local native_set_text=ui.set_text
local destinations=0
ui.set_text=function(handle,value)
  if value:sub(1,3)=='To ' then destinations=destinations+1 end
  return native_set_text(handle,value)
end
crystal={ui=ui,fs={read=function(path) reads=reads+1;return read_asset(path) end},
  json={decode=function(value) decodes=decodes+1;return decode_json(value) end},
  state={get=function(k)return saved[k]end,set=function(k,v)saved[k]=v;return true end,flush=function()return true end},
  service={time={now=function()return 1794810600 end},http={fetch=function(url,options,callback)
    assert(not pending);sequence=sequence+1;pending={id=sequence,url=url,callback=callback};return sequence
  end,cancel=function(id)if pending and pending.id==id then pending=nil end end}}}
ui.timer=function(ms,callback)assert(ms==100 and not timer);timer=callback;return 1 end
local function ticks(n)
  for i=1,n or 400 do
    reads,decodes,destinations=0,0,0
    run_callback(timer)
    if preload then assert(decodes==0,'preload expanded route records instead of keeping compact pages') end
  end
end
local function complete(body)
  local request=assert(pending);pending=nil
  run_callback(function() request.callback(request.id,200,body) end)
end
local function checkpoint(name)
  local m=memory_stats();print('CATALOG_MEMORY',name,m.used,m.peak,m.occupied,m.largest,m.capacity)
end
for _,name in ipairs({'is_ready','on_ready','off_ready'}) do crystal.service.time[name]=clock_api[name] end
local app=assert(loadfile(package_path))()
assert(app.onCreate());assert(timer);assert(decodes==0,'launch must not decode catalogs');checkpoint('launch')
local search_tab='Search'
assert(has_text('Favorites') and has_text(search_tab) and has_icon('Favorites') and has_icon(search_tab),
  'tabs retain their text with image icons')
click(search_tab);click('1');assert(not has_text('Catalog routes |'),'keypad must stay locked during preload')
assert(not is_enabled('1'),'route key must be disabled before preload finishes')
assert(app.onPause())
ticks(1);assert(not is_enabled('1'),'paused preload must not enable the keypad')
assert(app.onResume())
-- Load every compact route file and keypad index in the next callback,
-- without expanding the route records into tables.
local counts=decode_json(expected_counts)
ticks(1);assert(reads==counts['_preload_reads'],'preload still waits for a separate tick per file')
preload=false
assert(is_enabled('1') and has_text('0') and not is_enabled('0'),'unavailable numeric keys remain visible and disabled')
assert(not has_text('M') and not has_text('Z'),'letters without a matching route prefix must be hidden')
local ax,ay=position('A');local bx,by=position('B')
local ex,ey=position('E');local hx,hy=position('H')
assert(by==ay and bx>ax and ey>ay and ey==hy and hx>ex,
  'possible letters must compact alphabetically into two fixed columns')
assert(memory_stats().peak<768*1024,'preload must retain compact pages, not expand the full catalog')
local startup_reads=0
local original_read=crystal.fs.read
crystal.fs.read=function(path)
  if path:find('/route-search-',1,true) then startup_reads=startup_reads+1 end
  return original_read(path)
end
click('1');assert(is_enabled('A') and is_enabled('0') and is_enabled('9'))
assert(has_text('M') and not has_text('Z'),'valid letters must appear for the prefix')
ax,ay=position('A');local mx,my=position('M');local px,py=position('P')
assert(my==ay and mx>ax and py>ay and px==ax,
  'sparse prefix letters must remain in consecutive two-column rows')
click('M');assert(has_text('1M'),'reused letter slots must dispatch their current letter')
click(utf8.char(0xF00D));click('1');assert(has_text('M'))
click('A');assert(not has_text('M'),'letters must hide when the new prefix makes them impossible')
click(utf8.char(0xF00D));assert(not has_text('M'))
click('1');assert(has_text('M'),'valid letters must appear again when the prefix changes back')
-- The normal 200 ms debounce remains; scanning and all row publication then
-- finish in the same callback, with no result-row pacing.
ticks(1);assert(not has_text('CTB'),'debounce fired early')
ticks(1);assert(has_text('CTB'),'route search/publication still yields between ticks')
assert(not has_text('Choose a route and direction'))
assert(destinations==8,'result publication did not update all eight rows together')
checkpoint('prefix_1')
assert(not pending,'catalog search must be offline')
-- Every page remains reachable; counts come from an independent Python scan.
assert(text_count('Catalog routes | '..counts['1']..' matches')==1)
assert(text_font_size('Catalog routes | '..counts['1']..' matches')==20)
local pages=math.ceil(counts['1']/8)
assert(has_text('Page 1 / '..pages))
for page=2,pages do click('Next');ticks();assert(has_text('Page '..page..' / '..pages)) end
for page=pages-1,1,-1 do click('Previous');ticks();assert(has_text('Page '..page..' / '..pages)) end
for cycle=1,3 do
  for _,prefix in ipairs({'1','2','3','4','5','6','7','8','9','A','B','E','N'}) do
    click(utf8.char(0xF00D));click(prefix);ticks()
    assert(has_text('Catalog routes | '..counts[prefix]..' matches'))
  end
end
checkpoint('repeated_prefixes')
-- Replace a pending query before publication; stale rows must not remain.
click(utf8.char(0xF00D));click('1')
ticks(1)
click(utf8.char(0xF00D));click('2');ticks()
assert(has_text('Catalog routes | '..counts['2']..' matches'))
assert(text_count('1')==1 and not pending)
assert(startup_reads==0,'search must reuse resident pages without rereading route assets')
click(utf8.char(0xF00D));click('1');click('A');ticks();assert(has_text('KMB'));checkpoint('route_1A')
check_keypad=false
click('KMB',1,1);assert(pending.url:find('/route-stop/1A/inbound/1',1,true))
complete('{"data":[{"stop":"00040ED8B61CA94B","seq":1},{"stop":"00040ED8B61CA94B","seq":2}]}')
assert(has_text('1. PUBLIC SQUARE STREET YAU MA TEI (YT545)'))
assert(has_text('2. PUBLIC SQUARE STREET YAU MA TEI (YT545)'))
assert(not has_text('Loading...') and not pending,'bundled names must resolve in the response callback')
checkpoint('metadata')
-- Look up stops across the first and last catalog files in the same callback.
click(utf8.char(0xF021))
complete('{"data":[{"stop":"00040ED8B61CA94B","seq":1},{"stop":"FFFB0F940CA7BEA7","seq":2}]}')
assert(not has_text('Loading...') and not pending,'stop lookup still waits between catalog pages')
assert(has_text('Tap a stop for arrivals'))
local expected_margin=math.floor(16*screen_width/480)
local _,status_y,_,status_h=label_bounds('Tap a stop for arrivals')
local status_bottom_margin=screen_height-(status_y+status_h)
assert(status_bottom_margin>=-1 and status_bottom_margin<=expected_margin+2,
  'stop list should extend to the bottom status with the standard page padding')
-- More than 64 missing names spread over 40 packaged files must all resolve
-- before this single response callback returns.
click(utf8.char(0xF021))
local metadata_rows,expected_names={},{}
for page=0,39 do
  local page_text=assert(read_asset(string.format('assets/catalog/stops-kmb-%03d.jsonl',page)))
  local taken=0
  for line in page_text:gmatch('[^\r\n]+') do
    local stop=assert(decode_json(line))
    local seq=#metadata_rows+1
    metadata_rows[seq]='{"stop":"'..stop.id..'","seq":'..seq..'}'
    expected_names[seq]=seq..'. '..(stop.name_en or stop.name_tc)
    taken=taken+1;if taken==2 then break end
  end
end
reads,decodes=0,0
complete('{"data":['..table.concat(metadata_rows,',')..']}')
assert(reads==40 and decodes>=80,'metadata resolution still yields between records or source files')
for _,name in ipairs(expected_names) do assert(has_text(name),'unresolved bundled stop: '..name) end
assert(not pending and not has_text('Loading...'))
checkpoint('eighty_stop_metadata')
-- Long sequences retain every occurrence in one continuous scrolling list.
click(utf8.char(0xF021))
local rows={}
for i=1,80 do
  local name=i==1 and 'A very long stop name that should wrap naturally onto two lines in this viewport' or 'Test stop '..i
  rows[i]='{"stop":"same-stop-'..i..'","seq":'..i..',"name_en":"'..name..'"}'
end
complete('{"data":['..table.concat(rows,',')..']}')
local first_name='1. A very long stop name that should wrap naturally onto two lines in this viewport'
assert(has_text(first_name) and has_text('65. Test stop 65') and has_text('80. Test stop 80'),
  'full response/list must complete immediately, including more than 64 stops')
local list_x,list_w=list_bounds(first_name)
assert(list_x==expected_margin and screen_width-list_x-list_w==expected_margin,
  'detail stop list margins must match Favorites and Search')
local first_y,first_h=list_row_metrics(first_name)
local second_y,second_h=list_row_metrics('2. Test stop 2')
local expected_gap=math.floor(8*screen_width/480)
assert(first_h>second_h and second_h<math.floor(88*screen_width/480) and
  second_y-first_y-first_h>=expected_gap-1,
  'wrapped rows should grow to fit text while shorter rows stay compact and separated')
assert(not has_text('Next') and not has_text('Page 1 /'))
assert(list_scroll_y('80. Test stop 80')>0,'last stop must be reachable by scrolling')
click('80. Test stop 80')
local eta_x,_,eta_w=label_bounds('NEXT ARRIVALS')
assert(eta_x==expected_margin and eta_w==screen_width-2*expected_margin,
  'ETA page margins must match Favorites and Search')
assert(pending.url:find('/eta/same-stop-80/1A/1',1,true));complete('{"data":[{"eta":"2026-11-16T14:33:00+08:00"}]}');ticks()
assert(has_text('3 min'));click('Save stop');assert(#saved.favorites==1)
click(utf8.char(0xF021));local late=pending
assert(app.onPause());assert(not pending);late.callback(late.id,200,'{"data":[{"eta":"2026-11-16T16:09:00+08:00"}]}')
assert(not has_text('99 min'));assert(app.onResume());complete('{"data":[]}');ticks()
assert(has_text('No arrivals available'))
click(utf8.char(0xF053));click(utf8.char(0xF053))
click(utf8.char(0xF00D));click('1');ticks(3);click(utf8.char(0xF00D));click('2');ticks()
assert(has_text('Catalog routes | '..counts['2']..' matches'))
checkpoint('navigation')
assert(app.onDestroy());checkpoint('destroy')
