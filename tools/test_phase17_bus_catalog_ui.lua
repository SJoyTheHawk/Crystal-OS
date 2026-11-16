-- Real catalog files, production JSON conversion/allocator/UI; deterministic HTTP.
local timer, pending, sequence = nil, nil, 0
local saved={}
local reads,decodes=0,0
crystal={ui=ui,fs={read=function(path) reads=reads+1;return read_asset(path) end},
  json={decode=function(value) decodes=decodes+1;return decode_json(value) end},
  state={get=function(k)return saved[k]end,set=function(k,v)saved[k]=v;return true end,flush=function()return true end},
  service={http={fetch=function(url,options,callback)
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
click('1A   KMB');assert(pending.url:find('/api/stops?',1,true))
complete('{"stops":[{"id":"00040ED8B61CA94B","seq":1},{"id":"00040ED8B61CA94B","seq":2}]}')
ticks();checkpoint('metadata')
assert(has_text('1. PUBLIC SQUARE STREET YAU MA TEI (YT545)'))
assert(has_text('2. PUBLIC SQUARE STREET YAU MA TEI (YT545)'))
-- Long sequences retain both repeated occurrences and all three UI pages.
click(utf8.char(0xF021))
local rows={}
for i=1,17 do rows[i]='{"id":"same-stop","seq":'..i..',"name_en":"Test stop '..i..'"}' end
complete('{"stops":['..table.concat(rows,',')..']}')
assert(has_text('1. Test stop 1'));click('Next');assert(has_text('9. Test stop 9'))
click('Next');assert(has_text('17. Test stop 17'));click('17. Test stop 17')
assert(pending.url:find('stop=same-stop',1,true));complete('{"etas":[{"minutes":3}]}')
assert(has_text('3 min'));click('Save stop');assert(#saved.favorites==1)
click(utf8.char(0xF021));local late=pending
assert(app.onPause());assert(not pending);late.callback(late.id,200,'{"etas":[{"minutes":99}]}')
assert(not has_text('99 min'));assert(app.onResume());complete('{"etas":[]}')
assert(has_text('No arrivals available'))
click(utf8.char(0xF053));click(utf8.char(0xF053))
click(utf8.char(0xF00D));click('1');ticks(3);click(utf8.char(0xF00D));click('2');ticks()
assert(has_text('Catalog routes | '..counts['2']..' matches'))
checkpoint('navigation')
assert(app.onDestroy());checkpoint('destroy')
