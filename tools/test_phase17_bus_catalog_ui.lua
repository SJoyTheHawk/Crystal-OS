-- Real catalog files, production JSON conversion/allocator/UI; deterministic HTTP.
local timer, pending, sequence = nil, nil, 0
local saved={}
local reads,decodes=0,0
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
    assert(reads<=1 and decodes<=24,'route preload exceeded its bounded per-tick work')
    assert(reads==0 or decodes==0,'catalog tick combined file IO and JSON conversion')
    assert(destinations<=1,'search tick rendered more than one route row')
  end
end
local function complete(body)
  local request=assert(pending);pending=nil;request.callback(request.id,200,body)
end
local function checkpoint(name)
  local m=memory_stats();print('CATALOG_MEMORY',name,m.used,m.peak,m.occupied,m.largest,m.capacity)
end
local app=assert(loadfile(package_path))()
assert(app.onCreate());assert(timer);assert(decodes==0,'launch must not decode catalogs');checkpoint('launch')
click('Search');click('1');assert(not has_text('Catalog routes |'),'keypad must stay locked during preload')
assert(not is_enabled('1'),'route key must be disabled before preload finishes')
ticks(4);assert(app.onPause())
ticks(30);assert(not is_enabled('1'),'paused preload must not enable the keypad')
assert(app.onResume())
-- Nine route files plus one keypad file: announce/read on separate ticks,
-- followed by publish/UI/checkpoint. Startup must not scan 2,115 records.
ticks(24);assert(is_enabled('1') and has_text('0') and not is_enabled('0'),'unavailable numeric keys remain visible and disabled')
assert(not has_text('M') and not has_text('Z'),'letters without a matching route prefix must be hidden')
assert(memory_stats().peak<768*1024,'preload must retain compact pages, not expand the full catalog')
local startup_reads=0
local original_read=crystal.fs.read
crystal.fs.read=function(path)
  if path:find('/route-search-',1,true) then startup_reads=startup_reads+1 end
  return original_read(path)
end
click('1');assert(is_enabled('A') and is_enabled('0') and is_enabled('9'))
assert(has_text('M') and not has_text('Z'),'valid letters must appear for the prefix')
click('A');assert(not has_text('M'),'letters must hide when the new prefix makes them impossible')
click(utf8.char(0xF00D));assert(not has_text('M'))
click('1');assert(has_text('M'),'valid letters must appear again when the prefix changes back')
-- Two debounce ticks then one scan tick must count all 2,115 records.
ticks(3)
assert(has_text('Page 1 / '..math.ceil(decode_json(expected_counts)['1']/8)) == false,
  'publication remains separate from scanning')
-- The next tick starts publishing the completed page, without scan batches.
ticks(1);assert(has_text('1   CTB'),'full catalog scan did not finish in one callback')
ticks(10);checkpoint('prefix_1')
assert(has_text('Choose a route and direction'))
assert(not pending,'catalog search must be offline')
-- Every page remains reachable; counts come from an independent Python scan.
local counts=decode_json(expected_counts)
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
-- Edit while rows are being published: an old row must not open a route.
click(utf8.char(0xF00D));click('1')
for i=1,100 do
  ticks(1)
  if has_text('1   KMB') then break end
end
click(utf8.char(0xF00D));click('2');ticks()
assert(has_text('Catalog routes | '..counts['2']..' matches'))
assert(not has_text('1   KMB') and not pending)
assert(startup_reads==0,'search must reuse resident pages without rereading route assets')
click(utf8.char(0xF00D));click('1');click('A');ticks();assert(has_text('1A   KMB'));checkpoint('route_1A')
check_keypad=false
click('1A   KMB');assert(pending.url:find('/route-stop/1A/inbound/1',1,true))
complete('{"data":[{"stop":"00040ED8B61CA94B","seq":1},{"stop":"00040ED8B61CA94B","seq":2}]}')
ticks();checkpoint('metadata')
assert(has_text('1. PUBLIC SQUARE STREET YAU MA TEI (YT545)'))
assert(has_text('2. PUBLIC SQUARE STREET YAU MA TEI (YT545)'))
-- Long sequences retain every occurrence in one continuous scrolling list.
click(utf8.char(0xF021))
local rows={}
for i=1,35 do rows[i]='{"stop":"same-stop-'..i..'","seq":'..i..',"name_en":"Test stop '..i..'"}' end
complete('{"data":['..table.concat(rows,',')..']}');ticks()
assert(has_text('1. Test stop 1') and has_text('9. Test stop 9') and has_text('35. Test stop 35'))
assert(not has_text('Next') and not has_text('Page 1 /'))
assert(list_scroll_y('35. Test stop 35')>0,'last stop must be reachable by scrolling')
click('35. Test stop 35')
assert(pending.url:find('/eta/same-stop-35/1A/1',1,true));complete('{"data":[{"eta":"2026-11-16T14:33:00+08:00"}]}');ticks()
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
