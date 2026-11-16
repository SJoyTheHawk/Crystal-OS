-- Bus package: native-reference layout with bounded catalog-backed search.
local ui, http, state, json = crystal.ui, crystal.service.http, crystal.state, crystal.json
local fs = crystal.fs
local BASE = "http://phase17.bus.fixture:8000"
local C = {bg=0x0F172A, card=0x1E293B, text=0xF8FAFC, muted=0x94A3B8, border=0x334155, accent=0x38BDF8}
-- Built-in LVGL symbols, available in the firmware's Montserrat fonts.
local ICON={clear=utf8.char(0xF00D),backspace=utf8.char(0xF55A),back=utf8.char(0xF053),refresh=utf8.char(0xF021)}
local M, v = {}, {}
local Catalog = {manifest=nil, route_pages=nil, stop_pages={}, available=false}
local favorites = state.get("favorites") or {}
local recents = {}
local metric_marks = {}
local selected, selected_stop, routes, stops = {}, nil, {}, {}
local route_results, stop_results = {}, {}
local route_page, stop_page = 1, 1
local query, search_pending, return_page = "", false, "search"
local search_routes, open_route, fetch_eta, render_favorites, show_page, refresh
local render_route_page, render_stop_page
local status, resource_checkpoint
local route_total=0
local catalog_search

local function catalog_decode(text)
  if not text or not json or not json.decode then return nil end
  local value = json.decode(text)
  return type(value) == "table" and value or nil
end

local function catalog_part_identity(part)
  return table.concat({tostring(part.operator or ""), tostring(part.route or ""),
    tostring(part.bound or "O"), tostring(part.service_type or "1")}, ":")
end

local function catalog_normalize(record)
  if type(record) ~= "table" then return nil end
  local route = tostring(record.route or "")
  if route == "" then return nil end
  local parts
  if type(record.parts)=="table" then
    parts={}
    for _, part in ipairs(record.parts) do
      if type(part)=="table" then
        -- Keep only identity fields in the bounded search result. The
        -- catalog's localized origin/destination fields are not needed after
        -- the card is rendered and would multiply Lua arena usage on broad
        -- prefixes; provider display data remains in the source pages.
        parts[#parts+1]={operator=tostring(part.operator or ""),route=tostring(part.route or ""),
          bound=tostring(part.bound or "O"),service_type=tostring(part.service_type or "1")}
      end
    end
  end
  local destination=tostring(record.destination_en or record.destination_tc or "")
  local result = {
    key=tostring(record.key or ""), operator=tostring(record.operator or ""),
    route=route, bound=tostring(record.bound or "O"),
    service_type=tostring(record.service_type or "1"),
    destination=destination
  }
  result.parts=parts
  if result.key=="" then
    if type(result.parts)=="table" then
      local keys={}
      for _, part in ipairs(result.parts) do keys[#keys+1]=catalog_part_identity(part) end
      result.key=table.concat({"JOINT",table.concat(keys,"|")},"|")
    else
      result.key=table.concat({result.operator,result.route,result.bound,result.service_type},"|")
    end
  end
  return result
end

local function catalog_load()
  if Catalog.manifest then return Catalog.available end
  local directory=fs and fs.read and fs.read("assets/catalog/runtime.txt")
  Catalog.manifest={}; Catalog.route_pages={}; Catalog.stop_pages={}
  if not directory then return false end
  -- Retain strings, not decoded manifest/index trees. No catalog JSON at launch.
  for line in directory:gmatch("[^\r\n]+") do
    local kind, rest=line:match("^(%u) (.+)$")
    if kind=="L" then Catalog.letters=rest
    elseif kind=="R" then Catalog.route_pages[#Catalog.route_pages+1]=rest
    elseif kind=="S" then
      local provider, descriptor=rest:match("^(%u+) (.+)$")
      if provider then
        Catalog.stop_pages[provider]=Catalog.stop_pages[provider] or {}
        local pages=Catalog.stop_pages[provider];pages[#pages+1]=descriptor
      end
    end
  end
  Catalog.available=#Catalog.route_pages>0
  return Catalog.available
end

local function catalog_letters()
  return Catalog.letters or "ABCDEFGHKMNPRSTWX"
end

-- One file read OR one JSON record conversion per timer callback.
-- Even a page with hundreds of records never runs a full decode loop.
local function catalog_line(job, pages)
  if not job.raw then
    local descriptor=pages[job.index]
    if not descriptor then return nil,true end
    job.index=job.index+1
    local filename, rest=descriptor:match("^(%S+) (.+)$")
    if job.prefix then
      if not rest:find(job.prefix:sub(1,1),1,true) then return nil,false end
    else
      local first,last=rest:match("^(%S+) (%S+)$")
      local needed=false
      for id in pairs(job.wanted) do if id>=first and id<=last then needed=true;break end end
      if not needed then return nil,false end
    end
    job.raw=fs.read("assets/catalog/"..filename);job.position=1
    return nil,false
  end
  -- Cheap field checks are bounded too. Generated JSON uses canonical keys;
  -- provider parts preserve the same route number as their parent record.
  for _=1,16 do
    local start=job.position
    if start>#job.raw then job.raw=nil;return nil,false end
    local finish=job.raw:find("\n",start,true) or (#job.raw+1)
    job.position=finish+1
    local line=job.raw:sub(start,finish-1)
    if job.prefix then
      local route=line:match('"route":"([A-Z0-9]+)"')
      if route and route:sub(1,#job.prefix)==job.prefix then
        job.count=job.count+1
        if job.count>job.offset and #job.results<8 then return line,false end
      end
    else
      local id=line:match('"id":"([^"\\]+)"')
      if id and job.wanted[id] then return line,false end
    end
  end
  return nil,false
end

local function catalog_search_step(job)
  if not job or job.generation~=v.catalog_generation then
    if v.catalog_job==job then v.catalog_job=nil end
    return true
  end
  local line,done=catalog_line(job,Catalog.route_pages)
  if done then
    route_results=job.results; route_total=job.count; route_page=job.page
    render_route_page()
    ui.set_text(v.results_hint,#route_results>0 and "Choose a route and direction" or "No matching routes")
    status(#route_results>0 and ("Catalog routes | "..route_total.." matches") or "No matching routes")
    v.catalog_job=nil
    resource_checkpoint("search_ready")
    return true
  end
  if line then
    local record=catalog_normalize(catalog_decode(line))
    if record then job.results[#job.results+1]=record end
  end
  return false
end

local function catalog_stop_step(job)
  if not job or job.generation~=v.catalog_generation then
    if v.stop_catalog_job==job then v.stop_catalog_job=nil end
    return true
  end
  local line,done=catalog_line(job,Catalog.stop_pages[job.provider] or {})
  if done then
    render_stop_page(); v.stop_catalog_job=nil
    status(#stop_results>0 and "Tap a stop for arrivals" or "No stops found for this route")
    resource_checkpoint("stops_loaded")
    return true
  end
  if line then
    local stop=catalog_decode(line)
    local id=type(stop)=="table" and tostring(stop.id or "")
    local wanted=id and job.wanted[id]
    if wanted then
      local targets=type(wanted)=="table" and wanted or {wanted}
      for _, index in ipairs(targets) do
        local target=stop_results[index]
        if target then
          target.name_en=target.name_en or stop.name_en;target.name_tc=target.name_tc or stop.name_tc
          target.lat=target.lat or stop.lat;target.lon=target.lon or stop.lon
        end
      end
      job.wanted[id]=nil
    end
  end
  return false
end

-- Called at explicit workflow checkpoints only. Keep idle serial output quiet.
resource_checkpoint = function(name)
  if metric_marks[name] then return end
  metric_marks[name] = true
  local service = crystal.service and crystal.service.system
  local info = service and service.info and service.info()
  if type(info) ~= "table" then
    print("BUS_RESOURCE", name, "metrics_unavailable")
    return
  end
  print("BUS_RESOURCE", name,
    "lua_kb="..tostring(info.lua_used_kb).."/"..tostring(info.lua_arena_kb),
    "lua_peak_kb="..tostring(info.lua_peak_kb),
    "lua_largest_free_kb="..tostring(info.lua_largest_free_kb),
    "internal_free_kb="..tostring(info.free_internal_kb),
    "internal_largest_kb="..tostring(info.largest_internal_kb),
    "internal_low_kb="..tostring(info.native_internal_low_kb),
    "internal_delta_kb="..tostring(info.native_internal_delta_kb),
    "psram_free_kb="..tostring(info.free_psram_kb),
    "psram_largest_kb="..tostring(info.largest_psram_kb),
    "psram_low_kb="..tostring(info.native_psram_low_kb),
    "psram_delta_kb="..tostring(info.native_psram_delta_kb),
    "http_peak_body_bytes="..tostring(info.http_peak_body_bytes),
    "http_tls_min_internal_bytes="..tostring(info.http_min_tls_internal_bytes),
    "http_tls_min_largest_bytes="..tostring(info.http_min_tls_largest_bytes),
    "http_tls_min_psram_bytes="..tostring(info.http_min_tls_psram_bytes),
    "http_completed="..tostring(info.http_completed),
    "http_cancellations="..tostring(info.http_cancellations),
    "json_peak_native_bytes="..tostring(info.json_peak_internal_bytes).."/"..tostring(info.json_peak_psram_bytes),
    "ui_objects="..tostring(info.app_objects),
    "callbacks="..tostring(info.app_callbacks),
    "http_callbacks="..tostring(info.app_http_callbacks),
    "resources="..table.concat({tostring(info.app_tasks),tostring(info.app_timers),
      tostring(info.app_queues),tostring(info.app_allocations),tostring(info.app_objects)}, "/"),
    "tracked_bytes="..tostring(info.app_tracked_bytes),
    "app_storage_bytes="..tostring(info.app_storage_bytes),
    "json_decodes="..tostring(info.json_decode_count),
    "json_largest_input_bytes="..tostring(info.json_largest_input_bytes))
end

local function text(parent, value, width, font, color)
  return assert(ui.label(parent, {text=value, width=width, font=font or "normal",
    style={text_color=color or C.text}}))
end
status = function(message)
  local label=v.status and v.status[v.page]
  if label then ui.set_text(label,message); ui.set_hidden(label,message=="") end
end
local function escape(value)
  return (tostring(value or ""):gsub("[^%w%-_.]", ""))
end
local function identity(route)
  if type(route.parts)=="table" then
    local keys={}
    for _, part in ipairs(route.parts) do keys[#keys+1]=catalog_part_identity(part) end
    return "JOINT:"..table.concat(keys,"|")
  end
  return table.concat({route.operator or "KMB", route.route or "", route.bound or "O", route.service_type or "1"}, ":")
end
local function request_part(route)
  if type(route)=="table" and route.operator=="JOINT" and type(route.parts)=="table" then
    return route.parts[1] or route.parts[2] or route
  end
  return route or {}
end
local function operator_label(route)
  if type(route) == "table" and route.operator == "JOINT" and type(route.parts) == "table" then
    return "KMB/CTB"
  end
  return tostring((type(route) == "table" and route.operator) or "KMB")
end
local function parameters()
  local part=request_part(selected)
  return "route=" .. escape(part.route) .. "&operator=" .. escape(part.operator) ..
    "&bound=" .. escape(part.bound or "O") .. "&service_type=" .. escape(part.service_type or "1")
end
local function request(path, callback, checkpoint)
  if v.request_id then status("Please wait for the current request"); return false end
  local id, err = http.fetch(BASE .. path, {timeout_sec=15}, function(req, code, body)
    if req ~= v.request_id then return end
    v.request_id = nil
    if code ~= 200 then
      if checkpoint then resource_checkpoint(checkpoint) end
      status("Network error " .. tostring(code) .. " - tap Refresh to retry")
    else
      local data = json.decode(body)
      if type(data) ~= "table" then
        if checkpoint then resource_checkpoint(checkpoint) end
        status("Invalid bus response")
      else
        if checkpoint then resource_checkpoint(checkpoint) end
        callback(data)
      end
    end
    if search_pending and v.page == "search" then search_pending=false; search_routes() end
  end)
  if not id then status("Request refused: " .. tostring(err)); return false end
  v.request_id = id
  status("Loading...")
  return true
end
local function save_state()
  state.set("favorites", favorites)
  state.flush()
end
local function save_recents()
  state.set("recent_routes", recents)
  state.flush()
end
local function remember_route(route)
  if type(route)~="table" or type(route.route)~="string" or route.route=="" then return end
  local recent={route=route.route, operator=tostring(route.operator or "KMB"),
    bound=tostring(route.bound or "O"), service_type=tostring(route.service_type or "1"),
    destination=tostring(route.destination or "selected destination"), parts=route.parts}
  for i=#recents,1,-1 do
    if type(recents[i])~="table" or identity(recents[i])==identity(recent) then table.remove(recents,i) end
  end
  table.insert(recents,1,recent)
  while #recents>5 do table.remove(recents) end
  save_recents()
end
render_route_page = function()
  local first=1
  routes={}
  for i, row in ipairs(v.routes) do
    local route=route_results[first+i-1]
    routes[i]=route
    ui.set_hidden(row.box, not route)
    if route then
      ui.set_text(row.route, route.route.."   "..operator_label(route))
      ui.set_text(row.destination, "To "..(route.destination or "selected destination"))
    end
  end
  local pages=math.max(1,math.ceil(route_total/8))
  ui.set_hidden(v.route_nav, route_total<=8)
  ui.set_hidden(v.route_prev, route_page<=1)
  ui.set_hidden(v.route_next, route_page>=pages)
  ui.set_text(v.route_page, #route_results>0 and ("Page "..route_page.." / "..pages) or "")
end
local function render_route_rows(rows, hint, message)
  route_results=rows or {}; route_total=#route_results; route_page=math.max(1,math.min(route_page,math.max(1,math.ceil(route_total/8))))
  render_route_page()
  ui.set_text(v.results_hint,hint)
  status(message)
end
local function show_recent_routes()
  local rows={}
  for i=1,math.min(#recents,5) do rows[i]=recents[i] end
  render_route_rows(rows,#rows>0 and "Recent routes" or "Search by route number",
    #rows>0 and "Recent routes saved on this device" or "Enter a route number")
end
local function button(parent, title, width, height, callback)
  return assert(ui.button(parent, {text=title, width=width, height=height, on_click=callback,
    style={bg_color=C.card, text_color=C.text, border_width=1, border_color=C.border, radius=8}}))
end
local function card(parent, callback)
  return assert(ui.col(parent, {gap=6, on_click=callback, style={padding=16, radius=8,
    bg_color=C.card, bg_opa=255, border_width=1, border_color=C.border}}))
end
local function eta_summary(rows)
  local result = {}
  for i=1, math.min(#rows, 3) do
    result[#result+1] = tonumber(rows[i].minutes) and
      (tonumber(rows[i].minutes)<1 and "Due" or tostring(rows[i].minutes).." min") or "--"
  end
  return #result>0 and table.concat(result, "  |  ") or "No arrivals available"
end
render_favorites = function()
  for i, row in ipairs(v.favorites) do
    local favorite = favorites[i]
    ui.set_hidden(row.box, not favorite)
    if favorite then
      ui.set_text(row.route, (favorite.route or "?").."   "..(favorite.operator or "KMB"))
      ui.set_text(row.destination, "To "..(favorite.destination or "selected destination"))
      ui.set_text(row.stop, favorite.stop_name or "Choose a stop for this saved route")
      ui.set_text(row.eta, favorite.eta_text or "Tap to load arrivals")
    end
  end
  ui.set_hidden(v.empty, #favorites>0)
end
local function is_saved()
  for i, favorite in ipairs(favorites) do
    if selected_stop and identity(favorite)==identity(selected) and favorite.stop_id==selected_stop.id then return i end
  end
end
local function update_actions()
  local saved = is_saved()
  ui.set_enabled(v.save, selected_stop~=nil)
  ui.set_text(v.save, saved and "Remove" or "Save stop")
  ui.set_style(v.save, {bg_color=saved and C.card or C.accent, text_color=saved and C.muted or C.bg})
end
local function save_stop()
  if not selected_stop then status("Choose a stop first"); return end
  if is_saved() then status("Stop already saved"); return end
  if #favorites>=8 then status("Eight saved stops maximum"); return end
  favorites[#favorites+1] = {route=selected.route, operator=selected.operator, bound=selected.bound,
    service_type=selected.service_type, destination=selected.destination, stop_id=selected_stop.id,
    stop_name=selected_stop.name_en or selected_stop.name_tc, eta_text=v.eta_text, parts=selected.parts}
  save_state(); render_favorites(); update_actions(); status("Stop saved to Favorites")
end
local function remove_stop()
  local index = is_saved()
  if index then table.remove(favorites, index); save_state(); render_favorites(); update_actions(); status("Removed from Favorites") end
end
render_stop_page = function()
  local first=(stop_page-1)*8+1
  stops={}
  for i, row in ipairs(v.stops) do
    local stop=stop_results[first+i-1]
    stops[i]=stop
    ui.set_hidden(row, not stop)
    ui.set_style(row, {border_color=C.border, border_width=1})
    if stop then
      local label=stop.name_en or stop.name_tc or "Metadata unavailable"
      if label=="" then label="Metadata unavailable" end
      if not stop.name_en and not stop.name_tc then label=label.." - Retry" end
      ui.set_text(row, tostring(stop.seq or first+i-1)..". "..label)
    end
  end
  local pages=math.max(1,math.ceil(#stop_results/8))
  ui.set_hidden(v.stop_nav, #stop_results<=8)
  ui.set_hidden(v.stop_prev, stop_page<=1)
  ui.set_hidden(v.stop_next, stop_page>=pages)
  ui.set_text(v.stop_page, #stop_results>0 and ("Page "..stop_page.." / "..pages) or "")
end
local function move_route_page(delta)
  local pages=math.max(1,math.ceil(route_total/8))
  local target=math.max(1,math.min(pages,route_page+delta))
  if Catalog.available and query~="" then catalog_search(target)
  else route_page=target; render_route_page() end
end
local function move_stop_page(delta)
  local pages=math.max(1,math.ceil(#stop_results/8))
  stop_page=math.max(1,math.min(pages,stop_page+delta)); render_stop_page()
end
fetch_eta = function()
  if not selected_stop then status("Choose a stop to see arrivals"); return end
  request("/api/eta?"..parameters().."&stop="..escape(selected_stop.id), function(data)
    if type(data.etas)~="table" then status("Invalid arrival response"); return end
    for _, row in ipairs(v.arrivals) do ui.set_hidden(row.box,true) end
    local rows = data.etas
    for i=1, math.min(#rows, 3) do
      local item = rows[i]
      local row=v.arrivals[i]
      ui.set_hidden(row.box,false)
      ui.set_text(row.minutes,eta_summary({item}))
      ui.set_text(row.remark,tostring(item.remark or "Scheduled"))
    end
    ui.set_hidden(v.no_eta,#rows>0)
    -- Arrival minutes are transient. Do not write state or rebuild the
    -- Favorites page from inside the HTTP callback; both operations can
    -- exceed the 50 ms app callback budget on the P4.
    v.eta_text = eta_summary(rows)
    status(#rows>0 and "Updated just now | fixture data" or "No arrivals available")
  end, "eta_complete")
end
local function select_stop(index)
  if v.request_id then status("Please wait for the current request"); return end
  local absolute=(stop_page-1)*8+index
  selected_stop=stop_results[absolute]
  if not selected_stop then return end
  for i, row in ipairs(v.stops) do
    ui.set_style(row, {border_color=i==index and C.accent or C.border, border_width=i==index and 2 or 1})
  end
  ui.set_text(v.stop_name, selected_stop.name_en or selected_stop.id)
  v.eta_text=nil
  for _, row in ipairs(v.arrivals) do ui.set_hidden(row.box,true) end
  ui.set_hidden(v.no_eta,true)
  update_actions()
  v.board=true
  ui.set_hidden(v.stop_section,true); ui.set_hidden(v.eta_section,false)
  ui.scroll_to(v.detail_header, {align="top"})
  if not selected_stop.name_en and not selected_stop.name_tc then
    status("Stop metadata unavailable - tap Refresh to retry")
  end
  fetch_eta()
end
local function fetch_stops()
  request("/api/stops?"..parameters(), function(data)
    if type(data.stops)~="table" then status("Invalid stop response"); return end
    stop_results=data.stops
    stop_page=1
    render_stop_page()
    ui.set_text(v.stops_hint, #stop_results>0 and "Tap a stop for arrivals" or "No stops found for this route")
    if catalog_load() and #stop_results>0 then
      v.catalog_generation=(v.catalog_generation or 0)+1
      local wanted, needs={},false
      local provider=tostring(request_part(selected).operator or ""):upper()
      for i, stop in ipairs(stop_results) do
        if type(stop)=="table" and not stop.name_en and not stop.name_tc then
          local id=tostring(stop.id or "")
          if wanted[id] then
            if type(wanted[id])=="table" then wanted[id][#wanted[id]+1]=i
            else wanted[id]={wanted[id],i} end
          else wanted[id]=i end
          needs=true
        end
      end
      if needs then
        v.stop_catalog_job={provider=provider,index=1,wanted=wanted,generation=v.catalog_generation}
        status("Loading stop metadata...")
      else
        resource_checkpoint("stops_loaded")
        status("")
      end
    else
      resource_checkpoint("stops_loaded")
      status("")
    end
  end)
end
open_route = function(route, favorite)
  if v.request_id then status("Please wait for the current request"); return end
  -- Reopening a saved stop must not perform another synchronous state write.
  if not favorite then remember_route(route) end
  return_page=v.page
  v.catalog_generation=(v.catalog_generation or 0)+1
  v.catalog_job=nil; v.stop_catalog_job=nil
  selected=route; selected_stop=nil; stops={}; stop_results={}; stop_page=1; v.eta_text=nil; v.board=false
  ui.set_hidden(v.stop_section,false); ui.set_hidden(v.eta_section,true)
  ui.set_text(v.route_title, tostring(route.route).."  |  "..operator_label(route))
  ui.set_text(v.destination, "To "..tostring(route.destination or "selected destination"))
  ui.set_text(v.stop_name, "Choose a stop")
  ui.set_text(v.stops_hint, "Loading bus stops...")
  for _, row in ipairs(v.stops) do ui.set_hidden(row, true) end
  for _, row in ipairs(v.arrivals) do ui.set_hidden(row.box,true) end
  ui.set_hidden(v.no_eta,true)
  update_actions(); show_page("detail")
  if favorite and favorite.stop_id then
    selected_stop={id=favorite.stop_id, name_en=favorite.stop_name}
    ui.set_text(v.stop_name, favorite.stop_name or favorite.stop_id)
    ui.set_text(v.stops_hint, "Saved stop")
    v.board=true
    ui.set_hidden(v.stop_section,true); ui.set_hidden(v.eta_section,false)
    update_actions(); fetch_eta()
  else fetch_stops() end
end
catalog_search = function(page)
  v.catalog_generation=(v.catalog_generation or 0)+1
  v.catalog_job={prefix=query,index=1,results={},count=0,offset=(page-1)*8,
    page=page,generation=v.catalog_generation}
  routes={}
  for _, row in ipairs(v.routes) do ui.set_hidden(row.box,true) end
  ui.set_hidden(v.route_nav,true)
  status("Loading catalog...")
end
search_routes = function()
  if v.request_id then search_pending=true; return end
  if query=="" then show_recent_routes(); return end
  if catalog_load() then
    route_results={}; route_total=0; route_page=1
    catalog_search(1)
    resource_checkpoint("catalog_loaded")
    search_pending=false
    return
  end
  local submitted=query
  request("/api/routes?query="..escape(query), function(data)
    if submitted~=query then search_pending=true; return end
    if type(data.routes)~="table" then status("Invalid route response"); return end
    resource_checkpoint("catalog_loaded")
    routes={}
    local rows=data.routes
    for i=1, math.min(#rows, 8) do routes[i]=rows[i] end
    render_route_rows(routes,#routes>0 and "Choose a route and direction" or "No matching routes",
      "Fixture routes | "..#routes.." matches")
  end)
end
local function key(value)
  if value=="Clear" then query=""
  elseif value=="<" then query=query:sub(1,-2)
  elseif tostring(value):match("^[A-Za-z0-9]$") and #query<6 then query=(query..value):upper() end
  ui.set_text(v.query,query)
  if fs and fs.read then
    v.catalog_generation=(v.catalog_generation or 0)+1
    v.catalog_job=nil; v.stop_catalog_job=nil
    search_pending=true; v.search_due=2
  else
    search_routes()
  end
end
local function search_tick()
  if v.search_due and v.search_due>0 then
    v.search_due=v.search_due-1
    if v.search_due==0 and search_pending and v.page=="search" then
      search_pending=false; search_routes()
    end
  end
  if v.catalog_job then
    catalog_search_step(v.catalog_job)
  elseif v.stop_catalog_job then
    catalog_stop_step(v.stop_catalog_job)
  end
end
show_page = function(name)
  v.page=name
  ui.set_hidden(v.nav, name=="detail")
  for page, object in pairs(v.pages) do ui.set_hidden(object, page~=name) end
  for page, line in pairs(v.tab_lines) do
    ui.set_style(line, {bg_color=page==name and C.accent or C.bg})
    ui.set_style(v.tab_buttons[page], {text_color=page==name and C.accent or C.muted})
  end
  if name=="favorites" then
    render_favorites()
    status(#favorites>0 and "Saved stops - sample data" or "Sample data - not live")
  elseif name=="search" and query=="" then
    show_recent_routes()
  end
end
refresh = function()
  if selected_stop then fetch_eta() else fetch_stops() end
end
function M.onCreate()
  metric_marks={}
  resource_checkpoint("launch_begin")
  local stored_recents=state.get("recent_routes")
  if type(stored_recents)=="table" then
    for i=1,math.min(#stored_recents,5) do
      local route=stored_recents[i]
      if type(route)=="table" and type(route.route)=="string" and route.route~="" then
        recents[#recents+1]={route=route.route,operator=tostring(route.operator or "KMB"),
          bound=tostring(route.bound or "O"),service_type=tostring(route.service_type or "1"),
          destination=tostring(route.destination or "selected destination"),parts=route.parts}
      end
    end
  end
  v={pages={}, status={}, favorites={}, routes={}, stops={}, arrivals={}, tab_lines={}, tab_buttons={}}
  catalog_load()
  local screen=ui.screen()
  local size=ui.get_size(screen)
  local W,H=size.w,size.h
  local scale=W/480
  local pad,gap=math.floor(16*scale),math.floor(5*scale)
  local tab_h,key_h=math.floor(48*scale),math.floor(40*scale)
  local width=W-2*pad
  local readable_font=W>=600 and "large" or "normal"
  local readable_size=W>=600 and 28 or 20
  assert(ui.set_style(screen, {padding=0, bg_color=C.bg}))
  local root=assert(ui.col(screen, {width=W,height=H,gap=0}))
  v.nav=assert(ui.row(root, {height=tab_h,gap=0}))
  for _, name in ipairs({"favorites","search"}) do
    local slot=assert(ui.col(v.nav, {width=W//2,gap=0}))
    v.tab_buttons[name]=button(slot,name=="favorites" and "Favorites" or "Search",W//2,tab_h-3,function()
      show_page(name)
      if name=="search" then search_routes() end
    end)
    ui.set_style(v.tab_buttons[name], {radius=0,border_width=0,bg_color=C.bg})
    v.tab_lines[name]=assert(ui.col(slot, {height=3,style={bg_color=C.bg,bg_opa=255}}))
  end
  for _, name in ipairs({"favorites","search","detail"}) do
    local page=assert(ui.scroll_view(root, {height=name=="detail" and H or H-tab_h,gap=math.floor(12*scale),style={padding=pad}}))
    v.pages[name]=page
  end
  local page=v.pages.favorites
  v.empty=assert(ui.col(page,{gap=math.floor(16*scale),style={padding=math.floor(32*scale)}}))
  local empty_w=width-2*math.floor(32*scale)
  ui.col(v.empty,{height=math.floor(38*scale)})
  local mark=text(v.empty,"BUS",empty_w,"large",C.accent)
  ui.set_style(mark,{align="center"})
  local title=text(v.empty,"No saved stops yet",empty_w,"large")
  ui.set_style(title,{align="center"})
  local hint=text(v.empty,"Find a route and save the stops you check often.",empty_w,"small",C.muted)
  ui.set_style(hint,{align="center"})
  text(v.empty,"Use Search above to find a route.",empty_w,"small",C.muted)
  for i=1,8 do
    local box=card(page,function() if favorites[i] then open_route(favorites[i],favorites[i]) end end)
    v.favorites[i]={box=box,route=text(box,"",width-32,"large"),destination=text(box,"",width-32,readable_font,C.muted),
      stop=text(box,"",width-32,"normal"),eta=text(box,"",width-32,"normal",C.accent)}
  end
  page=v.pages.search
  v.query=assert(ui.label(page,{text="",width=width,height=58,font="large"}))
  ui.set_style(v.query,{font_size=48,align="center"})
  -- Native reference: a centered keypad occupying three quarters of the screen.
  local keypad_w=math.floor(W*3/4)
  local surround=assert(ui.row(page,{gap=0}))
  ui.col(surround,{width=(width-keypad_w)//2,height=1})
  local keypad=assert(ui.row(surround,{width=keypad_w,gap=gap}))
  local numeric_w=math.floor(keypad_w*2/3)
  local numeric=assert(ui.col(keypad,{width=numeric_w,gap=gap}))
  for _, keys in ipairs({{"1","2","3"},{"4","5","6"},{"7","8","9"},{"Clear","0","<"}}) do
    local row=assert(ui.row(numeric,{gap=gap}))
    for _, value in ipairs(keys) do
      local caption=value=="Clear" and ICON.clear or value=="<" and ICON.backspace or value
      local key_button=button(row,caption,(numeric_w-2*gap)//3,key_h,function() key(value) end)
      ui.set_style(key_button,{border_width=0})
    end
  end
  local letters=catalog_letters()
  local bank_w=keypad_w-numeric_w-gap
  local bank=assert(ui.scroll_view(keypad,{width=bank_w,height=4*key_h+3*gap,gap=gap}))
  for i=1,#letters,2 do
    local row=assert(ui.row(bank,{gap=gap}))
    for j=i,math.min(i+1,#letters) do
      local value=letters:sub(j,j)
      local key_button=button(row,value,(bank_w-gap)//2,key_h,function() key(value) end)
      ui.set_style(key_button,{border_width=0})
    end
  end
  v.results_hint=text(page,"Matching routes will appear here",width,"small",C.muted)
  for i=1,8 do
    local box=assert(ui.row(page,{gap=12,on_click=function() if routes[i] then open_route(routes[i]) end end,
      style={padding=16,radius=8,bg_color=C.card,bg_opa=255,border_width=1,border_color=C.border}}))
    local row=box
    v.routes[i]={box=box,route=text(row,"",math.floor(108*scale),readable_font),
      destination=text(row,"",width-44-math.floor(108*scale),readable_font,C.muted)}
    ui.set_hidden(box,true)
  end
  v.route_nav=assert(ui.row(page,{gap=gap}))
  v.route_prev=button(v.route_nav,"Previous",math.floor(width/3),key_h,function() move_route_page(-1) end)
  v.route_page=text(v.route_nav,"",math.floor(width/3),"small",C.muted)
  ui.set_style(v.route_page,{align="center"})
  v.route_next=button(v.route_nav,"Next",math.floor(width/3),key_h,function() move_route_page(1) end)
  ui.set_hidden(v.route_nav,true)
  page=v.pages.detail
  local header=assert(ui.row(page,{gap=gap}))
  v.detail_header=header
  local icon_w=key_h
  local back=button(header,ICON.back,icon_w,key_h,function() M.onBack() end)
  ui.set_style(back,{bg_color=C.bg,border_width=0,text_color=C.accent})
  v.route_title=text(header,"",width-2*icon_w-2*gap,"large")
  local refresh_button=button(header,ICON.refresh,icon_w,key_h,refresh)
  ui.set_style(refresh_button,{bg_color=C.bg,border_width=0,text_color=C.accent,font_size=28})
  ui.set_style(back,{font_size=28})
  v.destination=text(page,"",width,readable_font,C.muted)
  v.stop_section=assert(ui.col(page,{gap=12}))
  v.stops_hint=text(v.stop_section,"Choose a route",width,"small",C.muted)
  local stop_list=assert(ui.scroll_view(v.stop_section,{height=H-math.floor(210*scale),gap=math.floor(6*scale)}))
  for i=1,8 do
    v.stops[i]=assert(ui.button(stop_list,{text="",width=width,min_height=key_h,
      on_click=function() select_stop(i) end,
      style={bg_color=C.card,text_color=C.text,border_width=1,border_color=C.border,radius=8,
        align="left",padding=10,font_size=readable_size}}))
    ui.set_hidden(v.stops[i],true)
  end
  v.stop_nav=assert(ui.row(v.stop_section,{gap=gap}))
  v.stop_prev=button(v.stop_nav,"Previous",math.floor(width/3),key_h,function() move_stop_page(-1) end)
  v.stop_page=text(v.stop_nav,"",math.floor(width/3),"small",C.muted)
  ui.set_style(v.stop_page,{align="center"})
  v.stop_next=button(v.stop_nav,"Next",math.floor(width/3),key_h,function() move_stop_page(1) end)
  ui.set_hidden(v.stop_nav,true)
  -- ETA is a separate visual board, not a second list below the stop picker.
  v.eta_section=assert(ui.col(page,{gap=math.floor(16*scale)}))
  v.stop_name=text(v.eta_section,"Choose a stop",width,"large")
  text(v.eta_section,"NEXT ARRIVALS",width,"small",C.muted)
  local board=card(v.eta_section,nil)
  for i=1,3 do
    local row=assert(ui.row(board,{gap=12}))
    v.arrivals[i]={box=row,minutes=text(row,"",math.floor(118*scale),"large",i==1 and C.accent or C.text),
      remark=text(row,"",width-44-math.floor(118*scale),"small",C.muted)}
    ui.set_hidden(row,true)
  end
  v.no_eta=text(board,"No arrivals available",width-32,"normal",C.muted)
  ui.set_hidden(v.no_eta,true)
  v.save=button(v.eta_section,"Save stop",width,key_h,function() if is_saved() then remove_stop() else save_stop() end end)
  -- The header refresh action is shared by the stop picker and ETA board.
  for _, name in ipairs({"favorites","search","detail"}) do
    v.status[name]=text(v.pages[name],"Sample data - not live",width,"small",C.muted)
  end
  if fs and fs.read then v.search_timer=ui.timer(100,search_tick) end
  update_actions(); show_page("favorites")
  resource_checkpoint("launch_ready")
  return true
end
function M.onPause()
  save_state(); search_pending=false
  v.catalog_generation=(v.catalog_generation or 0)+1
  v.catalog_job=nil; v.stop_catalog_job=nil; v.search_due=nil
  if v.request_id then http.cancel(v.request_id); v.request_id=nil end
  return true
end
function M.onResume()
  if v.page=="detail" then
    if selected_stop then fetch_eta()
    elseif selected and selected.route then fetch_stops() end
  end
  return true
end
function M.onDestroy()
  v.catalog_generation=(v.catalog_generation or 0)+1
  v.catalog_job=nil; v.stop_catalog_job=nil
  if v.request_id then http.cancel(v.request_id); v.request_id=nil end
  resource_checkpoint("teardown"); v={}; return true
end
function M.onBack()
  if v.page=="detail" then
    v.catalog_generation=(v.catalog_generation or 0)+1
    v.catalog_job=nil; v.stop_catalog_job=nil
    if v.request_id then http.cancel(v.request_id); v.request_id=nil end
    if v.board and return_page=="search" then
      v.board=false; selected_stop=nil
      ui.set_hidden(v.stop_section,false); ui.set_hidden(v.eta_section,true)
      ui.scroll_to(v.detail_header,{align="top"}); status("Tap a stop for arrivals")
    else show_page(return_page or "search") end
    return true
  elseif v.page=="search" and query~="" then key("Clear"); return true end
  return false
end
return M
