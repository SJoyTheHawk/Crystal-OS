-- Real catalog files, production JSON conversion/allocator/UI; deterministic HTTP.
local timer, pending, sequence = nil, nil, 0
local saved={}
local reads,decodes=0,0
crystal={ui=ui,fs={read=function(path) reads=reads+1;return read_asset(path) end},
  json={decode=function(value) decodes=decodes+1;return decode_json(value) end},
  state={get=function(k)return saved[k]end,set=function(k,v)saved[k]=v;return true end,flush=function()return true end},
  service={time={now=function()return 1794810600 end},http={fetch=function(url,options,callback)
    assert(not pending);sequence=sequence+1;pending={id=sequence,url=url,callback=callback};return sequence
  end,cancel=function(id)if pending and pending.id==id then pending=nil end end}}}
ui.timer=function(ms,callback)assert(ms==100 and not timer);timer=callback;return 1 end
local function ticks(n)
  for i=1,n or 400 do
    reads,decodes=0,0
    timer()
    assert(reads+decodes<=1,'catalog tick combined file IO or multiple JSON decodes')
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
click('Search');click('1');ticks();checkpoint('prefix_1')
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
click(utf8.char(0xF00D));click('1');click('A');ticks();assert(has_text('1A   KMB'));checkpoint('route_1A')
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
