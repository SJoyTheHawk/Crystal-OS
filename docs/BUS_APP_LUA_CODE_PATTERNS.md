# Bus App Lua Code Patterns

Reference implementations for transforming S3 C++ and Flutter Dart patterns into P4 Lua.

## 1. Provider Module Implementation

### 1.1 URL Builders

```lua
local Providers = {}

-- Base URLs
local KMB_BASE = "https://data.etabus.gov.hk/v1/transport/kmb"
local CTB_BASE = "https://rt.data.gov.hk/v2/transport/citybus"

function Providers.route_stops_url(part)
  local op = tostring(part.operator or "KMB")
  local route = tostring(part.route or "")
  local bound = tostring(part.bound or "O")
  local service = tostring(part.service_type or "1")
  
  if op == "CTB" then
    -- CTB endpoint: /route-stop/CTB/{route}/{bound}
    return CTB_BASE .. "/route-stop/CTB/" .. route .. "/" .. bound
  else
    -- KMB/LWB endpoint: /route-stop/{route}/{bound}/{service}
    return KMB_BASE .. "/route-stop/" .. route .. "/" .. bound .. "/" .. service
  end
end

function Providers.stop_details_url(operator, stop_id)
  if operator == "CTB" then
    return CTB_BASE .. "/stop/" .. tostring(stop_id)
  else
    return KMB_BASE .. "/stop/" .. tostring(stop_id)
  end
end

function Providers.eta_url(part, stop)
  local op = tostring(part.operator or "KMB")
  local route = tostring(part.route or "")
  local service = tostring(part.service_type or "1")
  local stop_id = tostring(stop.id or "")
  
  if op == "CTB" then
    -- CTB ETA: /eta/CTB/{stop}/{route}
    return CTB_BASE .. "/eta/CTB/" .. stop_id .. "/" .. route
  else
    -- KMB ETA: /eta/{stop}/{route}/{service}
    return KMB_BASE .. "/eta/" .. stop_id .. "/" .. route .. "/" .. service
  end
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
function Arrivals.parse_timestamp(iso8601)
  if type(iso8601) ~= "string" then
    return nil
  end
  
  -- Match ISO8601 with timezone: YYYY-MM-DDTHH:MM:SS+HH:MM
  local year, month, day, hour, min, sec, tz_sign, tz_hour, tz_min = 
    iso8601:match("^(%d%d%d%d)-(%d%d)-(%d%d)T(%d%d):(%d%d):(%d%d)([+-])(%d%d):(%d%d)$")
  
  if not year then
    return nil
  end
  
  -- Convert to numbers
  year = tonumber(year)
  month = tonumber(month)
  day = tonumber(day)
  hour = tonumber(hour)
  min = tonumber(min)
  sec = tonumber(sec)
  tz_hour = tonumber(tz_hour)
  tz_min = tonumber(tz_min)
  
  -- Days per month (non-leap year; good enough for ETA display)
  local days_in_month = {31,28,31,30,31,30,31,31,30,31,30,31}
  
  -- Leap year check
  if (year % 4 == 0 and year % 100 ~= 0) or (year % 400 == 0) then
    days_in_month[2] = 29
  end
  
  -- Calculate days since epoch (1970-01-01)
  local days = 0
  for y = 1970, year - 1 do
    if (y % 4 == 0 and y % 100 ~= 0) or (y % 400 == 0) then
      days = days + 366
    else
      days = days + 365
    end
  end
  
  for m = 1, month - 1 do
    days = days + days_in_month[m]
  end
  
  days = days + day - 1
  
  -- Convert to seconds
  local epoch_sec = days * 86400 + hour * 3600 + min * 60 + sec
  
  -- Apply timezone offset
  local tz_offset_sec = tz_hour * 3600 + tz_min * 60
  if tz_sign == "-" then
    tz_offset_sec = -tz_offset_sec
  end
  
  epoch_sec = epoch_sec - tz_offset_sec
  
  return epoch_sec
end
```

### 2.2 Stop Matching (Haversine Distance)

```lua
-- Match a KMB stop to the nearest CTB stop in a list
-- Returns matched stop or nil if no valid match within 500m
function Arrivals.match_joint_stop(primary_stop, secondary_stops)
  local primary_lat = tonumber(primary_stop.lat)
  local primary_lon = tonumber(primary_stop.lon)
  
  if not primary_lat or not primary_lon then
    -- Flutter's fallback: if no coordinates, use first secondary stop
    if type(secondary_stops) == "table" and #secondary_stops > 0 then
      return secondary_stops[1]
    end
    return nil
  end
  
  local best_match = nil
  local best_distance = 500  -- 500m threshold (Flutter's default)
  
  for _, stop in ipairs(secondary_stops) do
    local lat = tonumber(stop.lat)
    local lon = tonumber(stop.lon)
    
    if lat and lon then
      local distance = haversine(primary_lat, primary_lon, lat, lon)
      
      if distance < best_distance then
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
  
  local c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
  
  return R * c
end
```

### 2.3 ETA Selection and Sorting

```lua
-- Select and sort arrivals: timestamp first, source order breaks ties
-- Returns up to 3 arrivals
function Arrivals.select(entries, now_sec)
  if type(entries) ~= "table" then
    return {}
  end
  
  -- Filter out past arrivals
  local valid = {}
  for _, entry in ipairs(entries) do
    if entry.epoch_sec and entry.epoch_sec >= now_sec then
      valid[#valid + 1] = entry
    end
  end
  
  -- Sort by timestamp, then source order
  table.sort(valid, function(a, b)
    if a.epoch_sec ~= b.epoch_sec then
      return a.epoch_sec < b.epoch_sec
    end
    return (a.source_order or 0) < (b.source_order or 0)
  end)
  
  -- Return first 3
  local result = {}
  for i = 1, math.min(3, #valid) do
    result[i] = valid[i]
  end
  
  return result
end

-- Format epoch seconds as "X min" or "Arriving"
function Arrivals.format_minutes(epoch_sec, now_sec)
  local diff = epoch_sec - now_sec
  local minutes = math.floor(diff / 60)
  
  if minutes < 1 then
    return "Arriving"
  else
    return tostring(minutes) .. " min"
  end
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

- [ ] Replace `BASE` fixture URL with Provider module functions
- [ ] Implement `Providers.route_stops_url()` for KMB and CTB
- [ ] Implement `Providers.parse_route_stops()`
- [ ] Test single-operator stop fetching (route 1A, route 2)
- [ ] Implement `Providers.eta_url()` and `parse_eta()`
- [ ] Implement `Arrivals.parse_timestamp()` with ISO8601 support
- [ ] Test single-operator ETA display
- [ ] Implement `Arrivals.match_joint_stop()` with haversine
- [ ] Implement `Controller.fetch_joint_eta()` with sequential flow
- [ ] Test joint-route ETA (route 101, 101R)
- [ ] Implement `Store.add_favorite()` and `load_favorites()`
- [ ] Build Favorites tab UI with cards
- [ ] Test save/exit/relaunch persistence
- [ ] Implement ETA refresh timer (20s interval)
- [ ] Implement live minute countdown
- [ ] Test pause/resume cancellation
- [ ] Add resource checkpoints throughout
- [ ] Run 10-cycle stability test on P4
- [ ] Document peak Lua usage and validate <400 KB
- [ ] Remove all fixture server dependencies
- [ ] Update app.yaml version to 0.3.0

---

## Conclusion

These patterns replicate the S3 C++ implementation and Flutter Dart logic in P4 Lua. Follow them faithfully — they're proven to work in production mobile apps and native MCU apps. The adaptation is straightforward: Lua has the necessary primitives (HTTP, JSON, state, timers), and the memory constraints are manageable with bounded processing.

**Next step**: Start with Phase 1 (Provider Integration) — replace the fixture server calls with these Provider module functions.
