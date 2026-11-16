# Bus App Quick Start — First Working Milestone

Get the bus app working with real KMB/CTB data in **one focused development session**.

## Goal

Transform the current fixture-based `bus3p` into a working app that:
1. Fetches real route stops from `data.etabus.gov.hk` / `rt.data.gov.hk`
2. Displays stop lists with names
3. Fetches and displays real ETA with provider labels
4. Works for KMB, CTB, and JOINT routes

**Not in scope**: Favorites, refresh, persistence (those come in Phase 2)

**Time estimate**: 4-6 hours of focused work

---

## Step 1: Add Provider URL Builders (30 min)

Edit `examples/bus3p/main.lua`, add after the `Catalog` module:

```lua
-- Provider module: builds API URLs and parses responses
local Providers = {}

local KMB_BASE = "https://data.etabus.gov.hk/v1/transport/kmb"
local CTB_BASE = "https://rt.data.gov.hk/v2/transport/citybus"

function Providers.route_stops_url(part)
  local op = tostring(part.operator or "KMB")
  local route = tostring(part.route or "")
  local bound = tostring(part.bound or "O")
  local service = tostring(part.service_type or "1")
  
  if op == "CTB" then
    return CTB_BASE .. "/route-stop/CTB/" .. route .. "/" .. bound
  else
    return KMB_BASE .. "/route-stop/" .. route .. "/" .. bound .. "/" .. service
  end
end

function Providers.eta_url(part, stop)
  local op = tostring(part.operator or "KMB")
  local route = tostring(part.route or "")
  local service = tostring(part.service_type or "1")
  local stop_id = tostring(stop.id or "")
  
  if op == "CTB" then
    return CTB_BASE .. "/eta/CTB/" .. stop_id .. "/" .. route
  else
    return KMB_BASE .. "/eta/" .. stop_id .. "/" .. route .. "/" .. service
  end
end
```

**Test**: Print URLs and verify format:
```lua
local test_kmb = {operator="KMB", route="1A", bound="O", service_type="1"}
print(Providers.route_stops_url(test_kmb))
-- Expected: https://data.etabus.gov.hk/v1/transport/kmb/route-stop/1A/O/1

local test_ctb = {operator="CTB", route="2", bound="O"}
print(Providers.route_stops_url(test_ctb))
-- Expected: https://rt.data.gov.hk/v2/transport/citybus/route-stop/CTB/2/O
```

---

## Step 2: Add Response Parsers (45 min)

Add these functions to the `Providers` module:

```lua
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
          name_en = nil,  -- Resolved later
          name_tc = nil,
          lat = nil,
          lon = nil
        }
      end
    end
  end
  
  return results
end

function Providers.parse_eta(part, stop, decoded)
  if type(decoded) ~= "table" or type(decoded.data) ~= "table" then
    return {}
  end
  
  local operator = tostring(part.operator or "KMB")
  local results = {}
  
  for i, item in ipairs(decoded.data) do
    if type(item) == "table" and type(item.eta) == "string" then
      -- For now, store the ISO8601 string — we'll parse it later
      results[#results + 1] = {
        operator = operator,
        eta_string = item.eta,
        remark_tc = tostring(item.rmk_tc or ""),
        source_order = i
      }
    end
  end
  
  return results
end
```

**Test**: Mock a response and verify parsing:
```lua
local test_response = {
  type = "RouteStop",
  data = {
    {stop = "001", seq = 1},
    {stop = "002", seq = 2}
  }
}
local stops = Providers.parse_route_stops(test_kmb, test_response)
assert(#stops == 2, "Expected 2 stops")
assert(stops[1].id == "001", "Expected stop 001")
```

---

## Step 3: Replace Fixture Server Calls (60 min)

Find the `open_route()` function (around line 300-400). **Replace** the fixture request:

```lua
-- OLD CODE (remove this):
-- if not request("api/routes?" .. parameters(), function(data)
--   ...
-- end, "stops_loaded") then return end

-- NEW CODE:
local function open_route(route)
  if not route or type(route.parts) ~= "table" then return end
  
  selected = route
  stop_results = {}
  stop_page = 1
  v.catalog_generation = v.catalog_generation + 1
  
  status("Loading stops...")
  
  local part = route.parts[1]  -- Use first part (KMB for JOINT)
  local url = Providers.route_stops_url(part)
  
  local id, err = http.fetch(url, {timeout_sec=15}, function(req, code, body)
    if req ~= v.request_id then return end
    v.request_id = nil
    
    if code ~= 200 then
      status("Network error " .. tostring(code))
      resource_checkpoint("stops_loaded")
      return
    end
    
    local decoded = json.decode(body)
    if type(decoded) ~= "table" then
      status("Invalid response")
      resource_checkpoint("stops_loaded")
      return
    end
    
    stop_results = Providers.parse_route_stops(part, decoded)
    
    -- Try to resolve names from catalog
    if #stop_results > 0 and Catalog.available then
      local wanted = {}
      for i, stop in ipairs(stop_results) do
        wanted[stop.id] = i
      end
      
      v.stop_catalog_job = {
        generation = v.catalog_generation,
        provider = part.operator,
        wanted = wanted,
        index = 1,
        raw = nil,
        position = 1
      }
    end
    
    render_stop_page()
    status(#stop_results > 0 and "Tap a stop for arrivals" or "No stops found")
    resource_checkpoint("stops_loaded")
  end)
  
  if not id then
    status("Request failed: " .. tostring(err))
    return
  end
  
  v.request_id = id
end
```

**Test on device**:
1. Flash firmware
2. Search for "1A"
3. Tap "1A to Chuk Yuen Estate"
4. **Expected**: Real stop list appears (might show "001", "002" without names yet)
5. **Check serial log**: Should see HTTPS request to `data.etabus.gov.hk`

---

## Step 4: Add Simple Timestamp Parsing (45 min)

Add a basic `Arrivals` module:

```lua
local Arrivals = {}

-- Simplified timestamp parser for "2026-11-16T14:30:00+08:00"
-- Returns epoch seconds (good enough for ETA display)
function Arrivals.parse_timestamp(iso8601)
  if type(iso8601) ~= "string" then return nil end
  
  local year, month, day, hour, min, sec = 
    iso8601:match("^(%d%d%d%d)-(%d%d)-(%d%d)T(%d%d):(%d%d):(%d%d)")
  
  if not year then return nil end
  
  -- Quick approximation: days since 1970-01-01
  -- (Not perfectly accurate for leap years, but good enough for ETA diff)
  local y = tonumber(year)
  local m = tonumber(month)
  local d = tonumber(day)
  local h = tonumber(hour)
  local mi = tonumber(min)
  local s = tonumber(sec)
  
  -- Simple day count (365.25 * years + 30 * months + days)
  local days = (y - 1970) * 365 + math.floor((y - 1969) / 4) + 
               (m - 1) * 30 + d
  
  local epoch = days * 86400 + h * 3600 + mi * 60 + s
  
  -- Adjust for +08:00 timezone (Hong Kong)
  epoch = epoch - (8 * 3600)
  
  return epoch
end

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

**Test**:
```lua
local test_iso = "2026-11-16T14:30:00+08:00"
local epoch = Arrivals.parse_timestamp(test_iso)
print("Epoch:", epoch)  -- Should be a large number (1731740400 range)

local now = crystal.service.time.now()
print("Minutes from now:", math.floor((epoch - now) / 60))
```

---

## Step 5: Build ETA Page (90 min)

Add an ETA page that displays arrivals. Find where you build the stop page, and add:

```lua
-- Build ETA page (one-time creation)
local function build_eta_page()
  if v.eta_page then return end
  
  local area = getVisualArea()
  local width = area.x2 - area.x1 + 1
  local height = area.y2 - area.y1 + 1
  
  v.eta_page = ui.container(v.root, {
    width = width,
    height = height,
    style = {bg_color = C.bg, pad_all = kPad}
  })
  ui.set_hidden(v.eta_page, true)
  
  -- Back button
  v.eta_back = ui.button(v.eta_page, {
    text = ICON.back .. " Back",
    width = 80,
    height = 36,
    on_click = function()
      ui.set_hidden(v.eta_page, true)
      ui.set_hidden(v.stop_page, false)
    end
  })
  ui.align(v.eta_back, "TOP_LEFT", 0, 0)
  
  -- Title
  v.eta_title = ui.label(v.eta_page, {
    text = "",
    font = "large",
    style = {text_color = C.text}
  })
  ui.align(v.eta_title, "TOP_LEFT", 90, 4)
  
  -- Arrivals list
  v.eta_list = ui.container(v.eta_page, {
    width = width - 2 * kPad,
    height = height - 80,
    style = {
      bg_color = C.card,
      radius = 8,
      pad_all = 12,
      border_width = 1,
      border_color = C.border
    }
  })
  ui.align(v.eta_list, "TOP_MID", 0, 60)
end

-- Fetch and display ETA
local function fetch_eta(route, stop)
  build_eta_page()
  
  ui.set_hidden(v.stop_page, true)
  ui.set_hidden(v.eta_page, false)
  
  local title = route.route .. " → " .. (stop.name_en or stop.name_tc or stop.id)
  ui.set_text(v.eta_title, title)
  
  -- Clear existing arrivals
  if v.eta_list then
    ui.remove_children(v.eta_list)
  end
  
  local loading_label = ui.label(v.eta_list, {
    text = "Loading arrivals...",
    style = {text_color = C.muted}
  })
  
  status("Fetching ETA...")
  
  local part = route.parts and route.parts[1] or route
  local url = Providers.eta_url(part, stop)
  
  local id, err = http.fetch(url, {timeout_sec=15}, function(req, code, body)
    if req ~= v.request_id then return end
    v.request_id = nil
    
    ui.remove_children(v.eta_list)
    
    if code ~= 200 then
      ui.label(v.eta_list, {
        text = "Network error: " .. tostring(code),
        style = {text_color = C.muted}
      })
      resource_checkpoint("eta_complete")
      return
    end
    
    local decoded = json.decode(body)
    if type(decoded) ~= "table" then
      ui.label(v.eta_list, {
        text = "Invalid response",
        style = {text_color = C.muted}
      })
      resource_checkpoint("eta_complete")
      return
    end
    
    local arrivals = Providers.parse_eta(part, stop, decoded)
    
    if #arrivals == 0 then
      ui.label(v.eta_list, {
        text = "No arrivals in next 60 minutes",
        style = {text_color = C.muted}
      })
    else
      local now = crystal.service.time.now()
      
      for i, arr in ipairs(arrivals) do
        if i > 3 then break end  -- Show first 3 only
        
        local epoch = Arrivals.parse_timestamp(arr.eta_string)
        
        if epoch then
          local minutes = Arrivals.format_minutes(epoch, now)
          
          local arr_card = ui.container(v.eta_list, {
            width = "100%",
            height = 56,
            style = {
              bg_color = C.card,
              radius = 8,
              pad_all = 10,
              border_width = 1,
              border_color = C.border
            }
          })
          
          local op_label = ui.label(arr_card, {
            text = arr.operator,
            font = "small",
            style = {text_color = C.muted}
          })
          ui.align(op_label, "TOP_LEFT", 0, 0)
          
          local time_label = ui.label(arr_card, {
            text = minutes,
            font = "large",
            style = {text_color = C.accent}
          })
          ui.align(time_label, "CENTER_LEFT", 0, 0)
          
          if arr.remark_tc ~= "" then
            local remark = ui.label(arr_card, {
              text = arr.remark_tc,
              font = "small",
              style = {text_color = C.muted}
            })
            ui.align(remark, "BOTTOM_LEFT", 0, 0)
          end
        end
      end
    end
    
    status("ETA loaded")
    resource_checkpoint("eta_complete")
  end)
  
  if not id then
    ui.label(v.eta_list, {
      text = "Request failed: " .. tostring(err),
      style = {text_color = C.muted}
    })
  else
    v.request_id = id
  end
end
```

**Hook up stop selection**: In your stop row click handler:

```lua
-- In render_stop_page(), when creating stop row buttons:
on_click = function()
  local stop = stop_results[slot]
  if stop then
    fetch_eta(selected, stop)  -- selected is the current route
  end
end
```

**Test on device**:
1. Search "1A"
2. Select route
3. Wait for stops
4. **Tap first stop**
5. **Expected**: ETA page appears with "3 min", "8 min", "12 min" (or similar)

---

## Step 6: Verify Real Data Flow (30 min)

**Checklist**:

1. **KMB route works**
   - Search "1A"
   - Select "1A to Chuk Yuen Estate (KMB)"
   - See real stop list
   - Tap stop
   - See real ETA with times

2. **CTB route works**
   - Search "2"
   - Select CTB variant
   - See stops
   - Tap stop
   - See ETA

3. **Serial log shows real API calls**
   ```
   I (12345) bus_app: HTTP GET https://data.etabus.gov.hk/v1/transport/kmb/route-stop/1A/O/1
   I (12567) bus_app: HTTP 200, body: {"type":"RouteStop","data":[...]}
   ```

4. **No fixture server running**
   - Kill `phase17.bus.fixture` process
   - App still works

**If all pass**: You've successfully migrated from fixture to real APIs! 🎉

---

## Step 7: Commit and Document (15 min)

```bash
cd "/Users/szemy/Workspace/ESP32 Crystal OS"

git add examples/bus3p/main.lua
git add docs/BUS_APP_TRANSFORMATION_GUIDE.md
git add docs/BUS_APP_LUA_CODE_PATTERNS.md
git add docs/BUS_APP_QUICK_START.md

git commit -m "Phase 1: Bus app provider integration

- Add Providers module with KMB/CTB URL builders
- Implement response parsers for route stops and ETA
- Replace fixture server with real API calls
- Add basic ISO8601 timestamp parsing
- Build ETA display page
- Test KMB route 1A and CTB route 2

This establishes real data flow without fixture dependency.
Next: joint routes, favorites, and refresh.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Next Steps (Phase 2)

Once Phase 1 works:

1. **Joint routes** (2-3 hours)
   - Add `Arrivals.match_joint_stop()` with haversine
   - Implement sequential KMB→CTB ETA fetching
   - Test route 101

2. **Favorites** (2 hours)
   - Add `Store.add_favorite()` / `remove_favorite()`
   - Build Favorites tab UI
   - Test persistence

3. **Refresh** (1 hour)
   - Add 20s timer to ETA page
   - Update minutes live

**Total Phase 1+2 estimate**: 8-12 hours → **complete bus app with feature parity**

---

## Troubleshooting

### "No matching routes" after typing

**Cause**: Catalog not loaded or search prefix doesn't match

**Fix**: Check `Catalog.available` is true, verify `catalog/runtime.txt` exists

### "Network error 0" or timeout

**Cause**: WiFi not connected or HTTPS cert issue

**Fix**: 
1. Verify WiFi connected (check serial log for IP address)
2. Check `component_config → mbedtls → TLS Session Tickets`
3. Test with `curl` on Mac to verify API is accessible

### "Invalid response" on ETA fetch

**Cause**: JSON parsing failed

**Fix**:
1. Print raw `body` before `json.decode()`
2. Check body isn't truncated (should see closing `}`)
3. Verify `crystal.json` is available

### Lua arena overflow during ETA fetch

**Cause**: Peak memory exceeds 512 KB

**Fix**:
1. Check `resource_checkpoint("eta_complete")` for peak usage
2. If >400 KB, increase arena to 640 KB in `app.yaml`
3. Verify catalog job is not running during ETA fetch

### Stop names not appearing

**Cause**: Catalog metadata missing or job not running

**Fix**:
1. Check `v.stop_catalog_job` is created in `open_route()`
2. Verify `catalog_stop_step()` is called from timer
3. Add fallback: fetch from `Providers.stop_details_url()` if name is nil

---

## Success Criteria

**Phase 1 is complete when**:

- [ ] Route search works (unchanged from current implementation)
- [ ] Selecting a route fetches real stops from provider API
- [ ] Stop list displays (with IDs at minimum, names if catalog works)
- [ ] Tapping a stop fetches real ETA from provider API
- [ ] ETA page shows 1-3 arrival times with operator labels
- [ ] No fixture server is running
- [ ] Serial log shows HTTPS requests to `data.etabus.gov.hk` and `rt.data.gov.hk`
- [ ] KMB route (1A) works end-to-end
- [ ] CTB route (2) works end-to-end
- [ ] Peak Lua usage stays under 400 KB

**When all checked**: Phase 1 done. Move to Phase 2 (joint routes + favorites).

---

## Final Notes

This quick-start focuses on **one milestone: real data flow**. It intentionally skips:
- Joint route ETA merging (Phase 2)
- Favorites persistence (Phase 2)
- Auto-refresh (Phase 2)
- Stop matching algorithms (Phase 2)
- Precise timestamp parsing (acceptable approximation for now)

**Why**: Get something working first. Then iterate.

**The S3 app took this exact path**: basic flow first, then joint routes, then favorites. Don't try to build everything at once.

**Estimated time**: 4-6 hours of focused development. That's one good session. You can have a working bus app by tonight.

Let's do this. 🚀
