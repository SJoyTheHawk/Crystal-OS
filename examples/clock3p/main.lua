-- Third-party Clock. Same three tabs as the bundled app, written against the
-- Crystal host ABI v1 instead of C++/LVGL.
--
-- Two things worth noticing, because they are the whole point of the ABI:
--   * the countdown is owned by the OS (svc.timer), not by this app, so it keeps
--     running after the app is destroyed on an app switch;
--   * the stopwatch stores an absolute start instant, never an elapsed counter,
--     so it stays correct across destruction and across an SNTP correction.

local ui  = require("crystal.ui")
local svc = require("crystal.service")
local st  = require("crystal.state")   -- backed by data/state.json for script apps

local PRESETS = { 30, 60, 180, 300, 600, 1200, 1800 }

local M = {}
local v = {}          -- widget handles, rebuilt every onCreate
local tick            -- 1s ui.timer handle

-- helpers -------------------------------------------------------------------

local function mmss(sec)
  if sec < 0 then sec = 0 end
  return string.format("%02d:%02d", sec // 60, sec % 60)
end

-- tabs ----------------------------------------------------------------------

local function build_clock(parent)
  v.time = ui.label(parent, { text = "--:--:--", font = "large", align = "center" })
  v.date = ui.label(parent, { text = "",         font = "medium", align = "center" })
  v.tz   = ui.label(parent, { text = svc.time.timezone_name(),
                              font = "small", align = "center", opacity = 60 })
end

local function build_timer(parent)
  v.ring = ui.arc(parent, { size = 220, align = "center", value = 0 })
  v.remain = ui.label(v.ring, { text = "00:00", font = "large", align = "center" })

  local chips = ui.row(parent, { wrap = true, gap = 8 })
  for _, sec in ipairs(PRESETS) do
    local label = sec < 60 and (sec .. "s") or ((sec // 60) .. "m")
    ui.button(chips, { text = label, min_height = 44, on_click = function()
      svc.timer.arm(sec)      -- hands the countdown to the OS service
      M.refresh()
    end })
  end

  local row = ui.row(parent, { gap = 8 })
  v.toggle = ui.button(row, { text = "Start", min_height = 44, on_click = function()
    local t = svc.timer.status()
    if t.state == "running" then svc.timer.pause()
    elseif t.state == "paused" then svc.timer.resume()
    elseif t.state == "ready" then svc.timer.start() end
    M.refresh()
  end })
  ui.button(row, { text = "Reset", min_height = 44, on_click = function()
    svc.timer.reset(); M.refresh()
  end })
end

local function build_stopwatch(parent)
  v.sw = ui.label(parent, { text = "00:00", font = "large", align = "center" })
  local row = ui.row(parent, { gap = 8 })

  v.sw_toggle = ui.button(row, { text = "Start", min_height = 44, on_click = function()
    if st.get("sw_start") then
      -- pausing: collapse the absolute start into a frozen elapsed value
      st.set("sw_paused", os.time() - st.get("sw_start"))
      st.set("sw_start", nil)
    else
      st.set("sw_start", os.time() - (st.get("sw_paused") or 0))
      st.set("sw_paused", nil)
    end
    M.refresh()
  end })

  ui.button(row, { text = "Lap", min_height = 44, on_click = function()
    local laps = st.get("laps") or {}
    if #laps < 50 then                        -- 480px screen, not a running watch
      table.insert(laps, 1, M.elapsed())
      st.set("laps", laps)
      ui.list_insert(v.laps, 1, mmss(laps[1]))
    end
  end })

  ui.button(row, { text = "Reset", min_height = 44, on_click = function()
    st.set("sw_start", nil); st.set("sw_paused", nil); st.set("laps", {})
    ui.list_clear(v.laps); M.refresh()
  end })

  v.laps = ui.list(parent, { flex = 1 })
  for _, lap in ipairs(st.get("laps") or {}) do ui.list_add(v.laps, mmss(lap)) end
end

-- shared refresh ------------------------------------------------------------

function M.elapsed()
  local start = st.get("sw_start")
  if start then return os.time() - start end
  return st.get("sw_paused") or 0
end

function M.refresh()
  ui.set_text(v.time, svc.time.format("%H:%M:%S"))
  ui.set_text(v.date, svc.time.format("%A, %d %b"))

  local t = svc.timer.status()          -- {state, remaining, total}
  ui.set_text(v.remain, mmss(t.remaining))
  ui.set_value(v.ring, t.total > 0 and (100 * t.remaining // t.total) or 0)
  ui.set_text(v.toggle, t.state == "running" and "Pause"
                     or t.state == "paused"  and "Resume" or "Start")

  ui.set_text(v.sw, mmss(M.elapsed()))
  ui.set_text(v.sw_toggle, st.get("sw_start") and "Pause" or "Start")
end

-- lifecycle -----------------------------------------------------------------
-- Mirrors CrystalApp's hooks one-to-one. onCreate runs on every launch, on a
-- fresh widget tree (max_running_num = 1), and must finish inside 80ms.

function M.onInstall()
  st.set("laps", st.get("laps") or {})
end

function M.onCreate()
  local tabs = ui.tabview(ui.screen(), { tabs = { "Clock", "Timer", "Stopwatch" } })
  build_clock(ui.tab(tabs, 1))
  build_timer(ui.tab(tabs, 2))
  build_stopwatch(ui.tab(tabs, 3))
  M.refresh()
end

function M.onResume()
  tick = ui.timer(1000, M.refresh)     -- 1s timer, never a busy loop
  M.refresh()                          -- catch up immediately after a rebuild
end

function M.onStop()
  -- Fired when quick settings or the keyboard fully covers us. Stop redrawing:
  -- this panel is bandwidth-bound and an animation needs every frame.
  if tick then ui.timer_delete(tick); tick = nil end
end

function M.onPause()
  if tick then ui.timer_delete(tick); tick = nil end
  st.flush()                           -- the only guaranteed write point
end

function M.onDestroy()
  v = {}                               -- handles die with the tree
end

return M
