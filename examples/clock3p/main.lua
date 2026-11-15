-- A package-backed clock using only the Crystal ABI v1 surface.
local ui = crystal.ui
local service = crystal.service
local state = crystal.state

local PRESETS = { 30, 60, 180, 300, 600, 1200, 1800 }
local M = {}
local v = {}
local tick

local function mmss(seconds)
  seconds = math.max(0, seconds)
  return string.format("%02d:%02d", seconds // 60, seconds % 60)
end

local function elapsed()
  local started = state.get("sw_start")
  if started then return service.time.now() - started end
  return state.get("sw_paused") or 0
end

local function refresh()
  ui.set_text(v.time, service.time.format("%H:%M:%S"))
  ui.set_text(v.date, service.time.format("%A, %d %b"))

  local timer = service.timer.status()
  ui.set_text(v.remain, mmss(timer.remaining_sec))
  ui.set_value(v.ring, timer.total_sec > 0 and
    (100 * timer.remaining_sec // timer.total_sec) or 0)
  ui.set_text(v.timer_toggle, timer.state == "running" and "Pause" or
    timer.state == "paused" and "Resume" or "Start")

  ui.set_text(v.stopwatch, mmss(elapsed()))
  ui.set_text(v.stopwatch_toggle, state.get("sw_start") and "Pause" or "Start")
end

local function start_refresh_timer()
  if not tick then tick = ui.timer(1000, refresh) end
  return tick ~= nil
end

local function build_clock(parent)
  local column = ui.col(parent, { gap = 8, style = { padding = 20 } })
  v.time = ui.label(column, { text = "--:--:--", font = "large", height = 72,
    style = { text_color = 0xF4F7FA, align = "center" } })
  v.date = ui.label(column, { text = "", height = 36,
    style = { text_color = 0xA8B7C5, align = "center" } })
  v.zone = ui.label(column, { text = service.time.timezone_name(), height = 32,
    style = { text_color = 0x8193A3, font_size = 16, align = "center" } })
end

local function build_timer(parent)
  local column = ui.col(parent, { gap = 10, style = { padding = 10 } })
  v.ring = ui.arc(column, { width = 180, height = 180, min = 0, max = 100, value = 0 })
  v.remain = ui.label(column, { text = "00:00", height = 42, font = "large",
    style = { align = "center" } })

  local presets = ui.row(column, { gap = 5 })
  for _, seconds in ipairs(PRESETS) do
    local label = seconds < 60 and (seconds .. "s") or ((seconds // 60) .. "m")
    ui.button(presets, { text = label, width = 52, height = 42, on_click = function()
      service.timer.arm(seconds)
      refresh()
    end })
  end

  local controls = ui.row(column, { gap = 8 })
  v.timer_toggle = ui.button(controls, { text = "Start", width = 130, height = 44,
    on_click = function()
      local timer = service.timer.status()
      if timer.state == "running" then service.timer.pause()
      elseif timer.state == "paused" then service.timer.resume()
      else service.timer.start() end
      refresh()
    end })
  ui.button(controls, { text = "Reset", width = 100, height = 44, on_click = function()
    service.timer.reset()
    refresh()
  end })
end

local function build_stopwatch(parent)
  local column = ui.col(parent, { gap = 8, style = { padding = 10 } })
  v.stopwatch = ui.label(column, { text = "00:00", height = 64, font = "large",
    style = { align = "center" } })
  local controls = ui.row(column, { gap = 8 })
  v.stopwatch_toggle = ui.button(controls, { text = "Start", width = 100, height = 44,
    on_click = function()
      local started = state.get("sw_start")
      if started then
        state.set("sw_paused", service.time.now() - started)
        state.set("sw_start", nil)
      else
        state.set("sw_start", service.time.now() - (state.get("sw_paused") or 0))
        state.set("sw_paused", nil)
      end
      refresh()
    end })
  ui.button(controls, { text = "Lap", width = 76, height = 44, on_click = function()
    local laps = state.get("laps") or {}
    if #laps < 50 then
      table.insert(laps, 1, elapsed())
      state.set("laps", laps)
      ui.list_insert(v.laps, 0, mmss(laps[1]))
    end
  end })
  ui.button(controls, { text = "Reset", width = 86, height = 44, on_click = function()
    state.set("sw_start", nil)
    state.set("sw_paused", nil)
    state.set("laps", {})
    ui.list_clear(v.laps)
    refresh()
  end })

  v.laps = ui.list(column, { height = 220 })
  for _, lap in ipairs(state.get("laps") or {}) do ui.list_add(v.laps, mmss(lap)) end
end

function M.onCreate()
  if not state.get("laps") then state.set("laps", {}) end
  local tabs = ui.tabview(ui.screen(), { tab_labels = { "Clock", "Timer", "Stopwatch" } })
  build_clock(ui.tab(tabs, 0))
  build_timer(ui.tab(tabs, 1))
  build_stopwatch(ui.tab(tabs, 2))
  refresh()
  -- The first launch enters through onCreate without an onResume callback.
  -- Start ticking here so every display is live from the first frame.
  return start_refresh_timer()
end

function M.onResume()
  local started = start_refresh_timer()
  refresh()
  return started
end

function M.onPause()
  -- CrystalUI pauses app-owned timers after this hook and resumes them before
  -- the next onResume hook, so keep the same timer handle across card switches.
  return true
end

function M.onDestroy()
  v = {}
  return true
end

return M
