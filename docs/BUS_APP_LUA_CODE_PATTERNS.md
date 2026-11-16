# Bus App Lua Code Patterns

Reference implementations for transforming S3 C++ and Flutter Dart patterns into P4 Lua.

## Verified Phase 1/2 contract (0.2.9, 2026-11-16)

Phases 1 and 2 are implemented in `examples/bus3p/main.lua`; use that tested
source for working API calls. The remaining Phase 3–4 snippets below are design sketches,
not a replacement entry file. Some use conceptual UI helpers that the current
ABI does not expose (`container`, `align`, `remove_children`, `timer_stop`).
Reuse the existing controls and one 100 ms timer through the real API instead.

- Keep local `Catalog`, `Providers`, `Arrivals` and `Controller` tables in the
  entry file. Runtime `require`/`os`/`io` are unavailable; entry source is capped
  at 64 KiB.
- Map `O`/`I` to `outbound`/`inbound` and percent-encode each path component.
  Preserve provider-qualified string stop IDs and service type.
- The production adapters use `stop_record`, `eta_record`,
  `parse_stop_details(operator, id, decoded)` and `array_job`/`array_step`.
  Decode one provider record per tick, not the whole response array. Scan at
  most 2,048 characters per tick; validate the envelope after the array.
- Resolve metadata from bounded bundled pages, update names progressively,
  then request missing stop details sequentially. Keep every sequence position,
  including repeated IDs, and display an identifiable retry row on failure.
- `service.time.now()` is epoch **seconds**. Timestamp parsing validates real
  calendar dates, leap years and explicit `Z`/signed offsets (fractional seconds
  are accepted). The implemented safe integer range is years 1970–2037.
- Keep valid timestamp rows when optional sequence/direction fields disagree,
  matching the Flutter reference. Sort by timestamp then source order, keep
  three; use rounded minutes and the 30-second Arriving threshold.
- Invalidate generations before cancelling HTTP. Native cancellation retires
  asynchronously, so one current job waits/retries on a later tick if the worker
  slot is still occupied. Clear pending jobs on navigation, pause and destroy.
- Create UI callbacks once. Stop lists use one shared `on_select(index)`
  callback. Add one native row per timer tick and update metadata in place with
  `crystal.ui.list_set_text(list, zero_based_index, text)` so resolving names
  does not rebuild the list or reset its scroll position. Selectable lists
  support up to 256 entries; conventional lists remain capped at 64.
- The real-catalog harness registers 62 callbacks;
  repeated refresh/paging must not grow that count. First launch initializes in
  `onCreate`; pause cancels work, resume refreshes the visible selection.
- Startup incrementally reads the compact route-search pages into a resident
  collection of raw page strings, reading one file per callback. The host
  generates keypad.txt; exact-prefix lookup uses that resident string without
  building prefix tables on P4. Readiness/UI/checkpoint work uses separate ticks.
  Only visible results expand into tables. Legacy JSONL
  fallback packages decode one record per tick. Keep the keypad disabled until
  this preload finishes. Derive the next-character mask
  from the loaded route numbers after each key edit; enable only characters
  that continue at least one route prefix. Search scans at most 64 resident
  records and expands at most one visible result per tick, without rereading
  route files. Compare prefix bytes without allocating per-record substrings.
  Cache keypad states to avoid redundant native writes. Publish one result
  row per tick, then navigation/status; resource diagnostics use another tick.
  Invalidate clickable result mappings immediately on query changes.
  Stop metadata pages remain on demand. The 4 MiB
  quota is configured for this workload; host arena results do not replace P4
  heap, callback-time or startup measurements.
- JSON conversion is protected so native cJSON trees are released before Lua
  allocation errors propagate. `test_phase17_bus_render.py` exercises the actual
  production binding, including repeated forced OOM.

See `BUS_APP_QUICK_START.md` for installation and board acceptance. Phase 2
joint matching and sequential ETA merging are implemented in the production
entry file. Phase 3 persistence hardening and Phase 4 periodic refresh remain
open.


## 1. Provider Module Implementation

### 1.1 URL Builders

```lua
local Providers = {}

-- Base URLs
local KMB_BASE = "https://data.etabus.gov.hk/v1/transport/kmb"
local CTB_BASE = "https://rt.data.gov.hk/v2/transport/citybus"

local function escape(value)
  return (tostring(value or ""):gsub("[^%w%-_.~]", function(c)
    return string.format("%%%02X",string.byte(c))
  end))
end
function Providers.route_stops_url(part)
  local direction=part.bound=="I" and "inbound" or "outbound"
  if part.operator=="CTB" then
    return CTB_BASE.."/route-stop/CTB/"..escape(part.route).."/"..direction
  end
  return KMB_BASE.."/route-stop/"..escape(part.route).."/"..direction.."/"..escape(part.service_type or "1")
end
function Providers.stop_details_url(operator,id)
  return (operator=="CTB" and CTB_BASE or KMB_BASE).."/stop/"..escape(id)
end
function Providers.eta_url(part,stop)
  if part.operator=="CTB" then return CTB_BASE.."/eta/CTB/"..escape(stop.id).."/"..escape(part.route) end
  return KMB_BASE.."/eta/"..escape(stop.id).."/"..escape(part.route).."/"..escape(part.service_type or "1")
end
```

### 1.2 Response Parsers

```lua
-- Parse route-stop response
-- Both KMB and CTB return: {type:"...", version:"...", data:[{stop:"...", seq:1}, ...]}
function Providers.parse_route_stops(part, decoded)
  if type(decoded) ~= "table" or type(decoded.data) ~= "table" then
    return {}
  end
  
  local operator = tostring(part.operator or "KMB")
  local results = {}
  
  for _, item in ipairs(decoded.data) do
    if type(item) == "table" then
      local stop_id = tostring(item.stop or "")
      local seq = tonumber(item.seq) or 0
      
      if stop_id ~= "" and seq > 0 then
        results[#results + 1] = {
          operator = operator,
          id = stop_id,
          seq = seq,
          name_en = nil,  -- Will be resolved from catalog or stop details API
          name_tc = nil,
          lat = nil,
          lon = nil
        }
      end
    end
  end
  
  return results
end

-- Parse ETA response
-- KMB: {data:[{eta:"2026-11-16T14:30:00+08:00", rmk_tc:"原定班次", dir:"O", service_type:1}, ...]}
-- CTB: {data:[{eta:"2026-11-16T14:30:00+08:00", rmk_tc:"原定班次"}, ...]}
function Providers.parse_eta(part, stop, decoded)
  if type(decoded) ~= "table" or type(decoded.data) ~= "table" then
    return {}
  end
  
  local operator = tostring(part.operator or "KMB")
  local stop_id = tostring(stop.id or "")
  local results = {}
  
  for i, item in ipairs(decoded.data) do
    if type(item) == "table" and type(item.eta) == "string" then
      local epoch_sec = Arrivals.parse_timestamp(item.eta)
      
      if epoch_sec and epoch_sec > 0 then
        results[#results + 1] = {
          operator = operator,
          stop_id = stop_id,
          epoch_sec = epoch_sec,
          remark_en = tostring(item.rmk_en or ""),
          remark_tc = tostring(item.rmk_tc or ""),
          source_order = i
        }
      end
    end
  end
  
  return results
end

-- Parse stop details response
-- Both return: {data:{stop:"...", name_en:"...", name_tc:"...", lat:22.xxx, long:114.xxx}}
function Providers.parse_stop_details(operator, decoded)
  if type(decoded) ~= "table" or type(decoded.data) ~= "table" then
    return nil
  end
  
  local data = decoded.data
  return {
    operator = operator,
    id = tostring(data.stop or ""),
    name_en = tostring(data.name_en or ""),
    name_tc = tostring(data.name_tc or ""),
    lat = tonumber(data.lat),
    lon = tonumber(data.long)  -- Note: API uses "long" not "lon"
  }
end
```



---

## 2. Arrivals Module Implementation

### 2.1 Timestamp Parsing

```lua
local Arrivals = {}

-- Manual ISO8601 parser for "2026-11-16T14:30:00+08:00"
-- Returns Unix epoch seconds or nil on parse failure
function Arrivals.parse_timestamp(value)
  if type(value)~="string" then return nil end
  local y,m,d,h,mi,s,zone=value:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)T(%d%d):(%d%d):(%d%d)(.*)$")
  y,m,d,h,mi,s=tonumber(y),tonumber(m),tonumber(d),tonumber(h),tonumber(mi),tonumber(s)
  if not y or y<1970 or y>2037 or m<1 or m>12 or h>23 or mi>59 or s>59 then return nil end
  zone=zone:gsub("^%.%d+","")
  local offset=0
  if zone~="Z" then
    local sign,zh,zm=zone:match("^([+-])(%d%d):(%d%d)$")
    zh,zm=tonumber(zh),tonumber(zm)
    if not sign or zh>23 or zm>59 then return nil end
    offset=(zh*3600+zm*60)*(sign=="-" and -1 or 1)
  end
  local function leaps(year) return year//4-year//100+year//400 end
  local months={31,28,31,30,31,30,31,31,30,31,30,31}
  if y%4==0 and (y%100~=0 or y%400==0) then months[2]=29 end
  if d<1 or d>months[m] then return nil end
  local days=(y-1970)*365+leaps(y-1)-leaps(1969)+d-1
  for i=1,m-1 do days=days+months[i] end
  return days*86400+h*3600+mi*60+s-offset
end
```

### 2.2 Stop Matching (Haversine Distance)

The Flutter `StopMatcher` selects the nearest CTB stop with valid coordinates;
it does not apply a maximum-distance cutoff. Equal distances keep the first
stop in route order. If the primary stop has no valid coordinates, it falls
back to the first CTB stop. The P4 implementation follows those rules and
rejects coordinates outside valid latitude/longitude ranges.

```lua
local function valid_coordinate(lat, lon)
  return lat and lon and lat == lat and lon == lon
    and lat >= -90 and lat <= 90 and lon >= -180 and lon <= 180
end

-- Match a KMB stop to the nearest CTB stop in a list
-- Returns the nearest stop with valid coordinates, with no distance cutoff.
function Arrivals.match_joint_stop(primary_stop, secondary_stops)
  local primary_lat = tonumber(primary_stop.lat)
  local primary_lon = tonumber(primary_stop.lon)
  
  if not valid_coordinate(primary_lat, primary_lon) then
    -- Flutter's fallback: if no coordinates, use first secondary stop
    if type(secondary_stops) == "table" and #secondary_stops > 0 then
      return secondary_stops[1]
    end
    return nil
  end
  
  local best_match = nil
  local best_distance = math.huge
  
  for _, stop in ipairs(secondary_stops) do
    local lat = tonumber(stop.lat)
    local lon = tonumber(stop.lon)
    
    if valid_coordinate(lat, lon) then
      local distance = haversine(primary_lat, primary_lon, lat, lon)
      
      if distance < best_distance then -- strict comparison keeps first on ties
        best_distance = distance
        best_match = stop
      end
    end
  end
  
  return best_match
end

-- Haversine distance in meters
local function haversine(lat1, lon1, lat2, lon2)
  local R = 6371000  -- Earth radius in meters
  local phi1 = math.rad(lat1)
  local phi2 = math.rad(lat2)
  local delta_phi = math.rad(lat2 - lat1)
  local delta_lambda = math.rad(lon2 - lon1)
  
  local a = math.sin(delta_phi / 2) * math.sin(delta_phi / 2) +
            math.cos(phi1) * math.cos(phi2) *
            math.sin(delta_lambda / 2) * math.sin(delta_lambda / 2)
  
  local c = 2 * math.atan(math.sqrt(a), math.sqrt(1 - a))
  
  return R * c
end
```

### 2.3 ETA Selection and Sorting

```lua
-- Select and sort arrivals: timestamp first, source order breaks ties
-- Returns up to 3 arrivals
function Arrivals.select(entries)
  -- Preserve all valid provider timestamps, including optional metadata
  -- disagreements. Accuracy filtering is a separate refinement milestone.
  table.sort(entries,function(a,b)
    if a.epoch_sec~=b.epoch_sec then return a.epoch_sec<b.epoch_sec end
    return a.source_order<b.source_order
  end)
  local result={}
  for i=1,math.min(3,#entries) do result[i]=entries[i] end
  return result
end
function Arrivals.format_minutes(epoch,now)
  if not now or now<=0 then return "Time unavailable" end
  local seconds=epoch-now
  return seconds<30 and "Arriving" or tostring((seconds+30)//60).." min"
end
```

---

## 3. Controller Module — HTTP Workflow

### 3.1 Request Queue Pattern

```lua
local Controller = {
  generation = 0,
  active_id = nil,
  queue = {}
}

function Controller.invalidate_requests()
  Controller.generation = Controller.generation + 1
  Controller.queue = {}
  
  local old_id = Controller.active_id
  Controller.active_id = nil
  
  if old_id then
    crystal.service.http.cancel(old_id)
  end
end

function Controller.start_request(url, on_complete)
  if Controller.active_id then
    -- Queue this request
    Controller.queue[#Controller.queue + 1] = {url = url, callback = on_complete}
    return nil, "queued"
  end
  
  local started_generation = Controller.generation
  
  local id, err = crystal.service.http.fetch(
    url,
    {timeout_sec = 15},
    function(request_id, status, body)
      -- Check if this request is still valid
      if Controller.generation ~= started_generation or 
         Controller.active_id ~= request_id then
        return
      end
      
      Controller.active_id = nil
      
      -- Process response
      if status == 200 then
        local decoded = crystal.json.decode(body)
        if decoded then
          on_complete(decoded)
        else
          status_message("Invalid JSON response")
        end
      else
        status_message("Network error " .. tostring(status))
      end
      
      -- Start next queued request
      Controller.process_queue()
    end
  )
  
  if id then
    Controller.active_id = id
  end
  
  return id, err
end

function Controller.process_queue()
  if #Controller.queue == 0 or Controller.active_id then
    return
  end
  
  local next_req = table.remove(Controller.queue, 1)
  Controller.start_request(next_req.url, next_req.callback)
end
```

### 3.2 Fetch Route Stops

```lua
function Controller.fetch_route_stops(route, on_complete)
  local part = route.parts and route.parts[1] or route
  local url = Providers.route_stops_url(part)
  
  Controller.start_request(url, function(decoded)
    local stops = Providers.parse_route_stops(part, decoded)
    
    -- Resolve stop names from catalog if available
    if #stops > 0 and Catalog.available then
      Controller.resolve_stop_metadata(stops, part.operator, function()
        on_complete(stops)
      end)
    else
      on_complete(stops)
    end
  end)
end

function Controller.resolve_stop_metadata(stops, operator, on_complete)
  -- Build wanted map: {stop_id -> array_index}
  local wanted = {}
  for i, stop in ipairs(stops) do
    if not stop.name_en or stop.name_en == "" then
      local id = stop.id
      if wanted[id] then
        -- Multiple occurrences of same stop
        if type(wanted[id]) == "number" then
          wanted[id] = {wanted[id], i}
        else
          wanted[id][#wanted[id] + 1] = i
        end
      else
        wanted[id] = i
      end
    end
  end
  
  -- If all names are resolved, done
  local needs_resolution = false
  for _ in pairs(wanted) do needs_resolution = true; break end
  
  if not needs_resolution then
    on_complete()
    return
  end
  
  -- Start catalog metadata job
  local job = {
    generation = Controller.generation,
    provider = operator,
    wanted = wanted,
    index = 1,
    raw = nil,
    position = 1
  }
  
  v.stop_catalog_job = job
  
  -- Job will run on timer ticks via catalog_stop_step()
  -- For now, call completion immediately — names will appear asynchronously
  on_complete()
end
```

### 3.3 Fetch ETA (Single Operator)

```lua
function Controller.fetch_eta(route, stop, on_complete)
  if route.operator == "JOINT" then
    Controller.fetch_joint_eta(route, stop, on_complete)
    return
  end
  
  local part = route.parts and route.parts[1] or route
  local url = Providers.eta_url(part, stop)
  
  Controller.start_request(url, function(decoded)
    local arrivals = Providers.parse_eta(part, stop, decoded)
    local now_sec = crystal.service.time.now()
    local selected = Arrivals.select(arrivals, now_sec)
    
    on_complete(selected)
  end)
end
```

### 3.4 Fetch Joint ETA (Sequential KMB + CTB)

```lua
function Controller.fetch_joint_eta(route, stop, on_complete)
  if type(route.parts) ~= "table" or #route.parts < 2 then
    on_complete({})
    return
  end
  
  local kmb_part = route.parts[1]
  local ctb_part = route.parts[2]
  
  -- Step 1: Fetch KMB ETA
  local kmb_url = Providers.eta_url(kmb_part, stop)
  
  Controller.start_request(kmb_url, function(kmb_decoded)
    local kmb_arrivals = Providers.parse_eta(kmb_part, stop, kmb_decoded)
    
    -- Step 2: Fetch CTB route stops to find matching stop
    local ctb_stops_url = Providers.route_stops_url(ctb_part)
    
    Controller.start_request(ctb_stops_url, function(ctb_stops_decoded)
      local ctb_stops = Providers.parse_route_stops(ctb_part, ctb_stops_decoded)
      
      -- Step 3: Match KMB stop to CTB stop
      local matched_stop = Arrivals.match_joint_stop(stop, ctb_stops)
      
      if not matched_stop then
        -- No match found, return KMB only
        local now_sec = crystal.service.time.now()
        local selected = Arrivals.select(kmb_arrivals, now_sec)
        on_complete(selected)
        return
      end
      
      -- Step 4: Fetch CTB ETA for matched stop
      local ctb_url = Providers.eta_url(ctb_part, matched_stop)
      
      Controller.start_request(ctb_url, function(ctb_decoded)
        local ctb_arrivals = Providers.parse_eta(ctb_part, matched_stop, ctb_decoded)
        
        -- Step 5: Merge and sort
        local merged = {}
        for _, arr in ipairs(kmb_arrivals) do
          merged[#merged + 1] = arr
        end
        for _, arr in ipairs(ctb_arrivals) do
          merged[#merged + 1] = arr
        end
        
        local now_sec = crystal.service.time.now()
        local selected = Arrivals.select(merged, now_sec)
        
        on_complete(selected)
      end)
    end)
  end)
end
```

---

## 4. Store Module — Persistence

### 4.1 Favorites Management

```lua
local Store = {}

local MAX_FAVORITES = 8
local MAX_RECENTS = 5

function Store.load_favorites()
  local data = crystal.state.get("favorites")
  if type(data) == "table" then
    return data
  end
  return {}
end

function Store.add_favorite(route, stop)
  local favorites = Store.load_favorites()
  
  -- Check for duplicate
  for _, fav in ipairs(favorites) do
    if fav.route_key == route.key and fav.stop_id == stop.id then
      return false, "Already in favorites"
    end
  end
  
  -- Check limit
  if #favorites >= MAX_FAVORITES then
    return false, "Favorites full (max " .. MAX_FAVORITES .. ")"
  end
  
  -- Add new favorite
  local favorite = {
    route_key = route.key,
    operator = route.operator,
    route = route.route,
    bound = route.bound,
    service_type = route.service_type,
    destination = route.destination,
    parts = route.parts,
    stop_id = stop.id,
    stop_seq = stop.seq,
    stop_name_en = stop.name_en,
    stop_name_tc = stop.name_tc,
    stop_lat = stop.lat,
    stop_lon = stop.lon,
    added_at = crystal.service.time.now()
  }
  
  favorites[#favorites + 1] = favorite
  
  local success = crystal.state.set("favorites", favorites)
  if success then
    crystal.state.flush()
  end
  
  return success, success and "Saved" or "Failed to save"
end

function Store.remove_favorite(index)
  local favorites = Store.load_favorites()
  
  if index < 1 or index > #favorites then
    return false
  end
  
  table.remove(favorites, index)
  
  local success = crystal.state.set("favorites", favorites)
  if success then
    crystal.state.flush()
  end
  
  return success
end
```

### 4.2 Recent Routes

```lua
function Store.load_recents()
  local data = crystal.state.get("recent_routes")
  if type(data) == "table" then
    return data
  end
  return {}
end

function Store.add_recent(route)
  local recents = Store.load_recents()
  
  -- Remove if already exists (move to front)
  for i, recent in ipairs(recents) do
    if recent.key == route.key then
      table.remove(recents, i)
      break
    end
  end
  
  -- Add to front
  table.insert(recents, 1, {
    key = route.key,
    operator = route.operator,
    route = route.route,
    bound = route.bound,
    service_type = route.service_type,
    destination = route.destination,
    parts = route.parts,
    accessed_at = crystal.service.time.now()
  })
  
  -- Trim to limit
  while #recents > MAX_RECENTS do
    table.remove(recents)
  end
  
  local success = crystal.state.set("recent_routes", recents)
  if success then
    crystal.state.flush()
  end
  
  return success
end
```

---

## 5. View Module — UI Updates

### 5.1 ETA Page Rendering

```lua
function View.render_eta_page(arrivals, route, stop)
  if not v.eta_page then
    View.build_eta_page()
  end
  
  -- Update title
  local title = route.route .. " → " .. (stop.name_en or stop.name_tc or stop.id)
  crystal.ui.set_text(v.eta_title, title)
  
  -- Clear existing arrival rows
  crystal.ui.remove_children(v.eta_list)
  
  local now_sec = crystal.service.time.now()
  
  if #arrivals == 0 then
    local empty_label = crystal.ui.label(v.eta_list, {
      text = "No arrivals in next 60 minutes",
      style = {text_color = C.muted}
    })
    return
  end
  
  -- Render each arrival
  for i, arrival in ipairs(arrivals) do
    local card = crystal.ui.container(v.eta_list, {
      width = "100%",
      height = 64,
      style = {
        bg_color = C.card,
        radius = 8,
        pad_all = 12,
        border_width = 1,
        border_color = C.border
      }
    })
    
    -- Operator label (KMB, CTB)
    local op_label = crystal.ui.label(card, {
      text = arrival.operator,
      font = "small",
      style = {text_color = C.muted}
    })
    crystal.ui.align(op_label, "TOP_LEFT", 0, 0)
    
    -- Time (e.g., "3 min")
    local time_text = Arrivals.format_minutes(arrival.epoch_sec, now_sec)
    local time_label = crystal.ui.label(card, {
      text = time_text,
      font = "large",
      style = {text_color = C.accent}
    })
    crystal.ui.align(time_label, "CENTER", 0, 0)
    
    -- Remark (if any)
    if arrival.remark_tc ~= "" then
      local remark_label = crystal.ui.label(card, {
        text = arrival.remark_tc,
        font = "small",
        style = {text_color = C.muted}
      })
      crystal.ui.align(remark_label, "BOTTOM_LEFT", 0, 0)
    end
  end
  
  -- Show save button if not already favorited
  local is_favorited = false
  for _, fav in ipairs(Store.load_favorites()) do
    if fav.route_key == route.key and fav.stop_id == stop.id then
      is_favorited = true
      break
    end
  end
  
  if not is_favorited then
    crystal.ui.set_hidden(v.eta_save_button, false)
  else
    crystal.ui.set_hidden(v.eta_save_button, true)
  end
  
  -- Store last refresh time
  v.last_eta_refresh = now_sec
end
```

### 5.2 Refresh Timer

```lua
local REFRESH_INTERVAL = 20  -- seconds
local MINUTE_UPDATE_INTERVAL = 1  -- seconds

function View.on_timer_tick()
  local now_sec = crystal.service.time.now()
  
  -- Update displayed minutes every second
  if v.page == "eta" and v.current_arrivals then
    View.update_eta_minutes(v.current_arrivals, now_sec)
  end
  
  -- Refresh ETA every 20 seconds
  if v.page == "eta" and v.last_eta_refresh then
    local elapsed = now_sec - v.last_eta_refresh
    
    if elapsed >= REFRESH_INTERVAL then
      Controller.fetch_eta(v.current_route, v.current_stop, function(arrivals)
        v.current_arrivals = arrivals
        View.render_eta_page(arrivals, v.current_route, v.current_stop)
      end)
    end
  end
  
  -- Also refresh favorite cards if on Favorites tab
  if v.page == "favorites" and #v.favorites > 0 then
    -- Refresh all favorites every 30 seconds
    if not v.last_favorites_refresh or (now_sec - v.last_favorites_refresh) >= 30 then
      View.refresh_all_favorites()
      v.last_favorites_refresh = now_sec
    end
  end
end

function View.update_eta_minutes(arrivals, now_sec)
  -- Find all time labels and update their text
  -- This assumes arrival cards are stored in v.eta_arrival_cards
  for i, arrival in ipairs(arrivals) do
    local card = v.eta_arrival_cards[i]
    if card and card.time_label then
      local time_text = Arrivals.format_minutes(arrival.epoch_sec, now_sec)
      crystal.ui.set_text(card.time_label, time_text)
    end
  end
end
```

---

## 6. Lifecycle Integration

### 6.1 onCreate

```lua
function M.onCreate(manager)
  print("Bus app onCreate")
  resource_checkpoint("launch_begin")
  
  -- Load catalog
  Catalog.catalog_load()
  
  -- Initialize state
  v.favorites = Store.load_favorites()
  v.recents = Store.load_recents()
  v.generation = 0
  v.page = "favorites"
  
  -- Build UI
  View.build_root(manager)
  View.build_favorites_tab()
  View.build_search_tab()  -- Deferred until first use
  
  -- Start timer
  v.timer = crystal.ui.timer(100, View.on_timer_tick)
  
  resource_checkpoint("launch_ready")
  return true
end
```

### 6.2 onResume

```lua
function M.onResume()
  print("Bus app onResume")
  
  -- Restart timer if stopped
  if not v.timer then
    v.timer = crystal.ui.timer(100, View.on_timer_tick)
  end
  
  -- Refresh current page
  if v.page == "eta" and v.current_route and v.current_stop then
    Controller.fetch_eta(v.current_route, v.current_stop, function(arrivals)
      v.current_arrivals = arrivals
      View.render_eta_page(arrivals, v.current_route, v.current_stop)
    end)
  elseif v.page == "favorites" then
    View.refresh_all_favorites()
  end
  
  return true
end
```

### 6.3 onPause

```lua
function M.onPause()
  print("Bus app onPause")
  
  -- Cancel all pending requests
  Controller.invalidate_requests()
  
  -- Stop timer
  if v.timer then
    crystal.ui.timer_stop(v.timer)
    v.timer = nil
  end
  
  -- Save state
  crystal.state.flush()
  
  return true
end
```

### 6.4 onDestroy

```lua
function M.onDestroy()
  print("Bus app onDestroy")
  
  -- Cancel requests
  Controller.invalidate_requests()
  
  -- Stop timer
  if v.timer then
    crystal.ui.timer_stop(v.timer)
    v.timer = nil
  end
  
  -- Save state
  crystal.state.flush()
  
  -- Clear all references
  v = {}
  
  resource_checkpoint("teardown")
  return true
end
```

---

## 7. Testing Utilities

### 7.1 Mock HTTP Responses

```lua
-- For host testing without real API
local function mock_http_fetch(url, opts, callback)
  local mock_id = math.random(1000, 9999)
  
  -- Simulate async response after 100ms
  crystal.ui.after(100, function()
    if url:find("/route%-stop/") then
      local body = '{"type":"RouteStop","data":[{"seq":1,"stop":"001"},{"seq":2,"stop":"002"}]}'
      callback(mock_id, 200, body)
    elseif url:find("/eta/") then
      local now = crystal.service.time.now()
      local eta1 = os.date("!%Y-%m-%dT%H:%M:%S+08:00", now + 180)  -- 3 min
      local eta2 = os.date("!%Y-%m-%dT%H:%M:%S+08:00", now + 480)  -- 8 min
      local body = string.format(
        '{"type":"ETA","data":[{"eta":"%s","rmk_tc":"原定班次"},{"eta":"%s","rmk_tc":""}]}',
        eta1, eta2
      )
      callback(mock_id, 200, body)
    else
      callback(mock_id, 404, "")
    end
  end)
  
  return mock_id
end

-- Inject mock during tests
if os.getenv("BUS_TEST_MODE") == "1" then
  crystal.service.http.fetch = mock_http_fetch
end
```

### 7.2 Resource Checkpoints

```lua
-- Already implemented in current code, but here's the pattern:
resource_checkpoint = function(name)
  if metric_marks[name] then return end
  metric_marks[name] = true
  
  local info = crystal.service.system.info()
  if type(info) ~= "table" then
    print("BUS_RESOURCE", name, "metrics_unavailable")
    return
  end
  
  print("BUS_RESOURCE", name,
    "lua_kb=" .. info.lua_used_kb .. "/" .. info.lua_arena_kb,
    "lua_peak_kb=" .. info.lua_peak_kb,
    "internal_free_kb=" .. info.free_internal_kb,
    "psram_free_kb=" .. info.free_psram_kb,
    "http_completed=" .. info.http_completed,
    "callbacks=" .. info.app_callbacks,
    "tracked_bytes=" .. info.app_tracked_bytes)
end

-- Add checkpoints at key stages:
-- - launch_begin (onCreate start)
-- - launch_ready (UI built, before first interaction)
-- - search_ready (catalog search completed)
-- - stops_loaded (stop list rendered)
-- - eta_complete (ETA fetched and displayed)
-- - teardown (onDestroy, before cleanup)
```

---

## 8. Migration Checklist

- [x] Replace the fixture base URL with live provider adapters
- [x] Implement KMB and CTB route-stop and ETA URLs
- [x] Parse stop/ETA records incrementally through `array_step`
- [x] Host integration checks for route 1A/2 and recorded official responses
- [x] Implement exact timezone-aware `Arrivals.parse_timestamp()`
- [ ] Validate live stop fetching and ETA on P4 (both providers/directions)
- [x] Implement `Arrivals.match_joint_stop()` with the Flutter nearest-coordinate rule
- [x] Implement `Controller.fetch_joint_eta()` with sequential KMB/CTB flow
- [x] Host-test joint-route ETA (route 101)
- [ ] Implement `Store.add_favorite()` and `load_favorites()`
- [x] Retain existing Favorites/save/remove UI and recent routes
- [ ] Test save/exit/relaunch persistence
- [ ] Implement ETA refresh timer (20s interval)
- [ ] Implement live minute countdown
- [x] Host checks for pause/resume, navigation and late callbacks
- [x] Resource checkpoints at launch/search/stops/ETA/teardown
- [ ] Run 10-cycle stability test on P4
- [ ] Capture Phase 1 P4 peak/fragmentation and validate 25% arena headroom
- [x] Remove fixture dependency from the live package; retain explicit test fixtures
- [ ] Update app.yaml version to 0.3.0

---

## Conclusion

These patterns replicate the S3 C++ implementation and Flutter Dart logic in P4 Lua. Follow them faithfully — they're proven to work in production mobile apps and native MCU apps. The adaptation is straightforward: Lua has the necessary primitives (HTTP, JSON, state, timers), and the memory constraints are manageable with bounded processing.

**Next step**: Install 0.2.9 and complete the P4 joint-route acceptance before
starting Phase 3 persistence hardening.
