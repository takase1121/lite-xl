-- mod-version: 2.0
local core = require "core"
local config = require "core.config"

-- find this file
local this_file = DATADIR .. "/plugins/demo.lua"
-- color dir
local color_dir = system.absolute_path "colors"
-- output dir
local output_dir = system.absolute_path "previews"
-- quit after demo
local quit_after_demo = true
-- window size
local window_w, window_h = 1600, 900

core.add_thread(function()
  local old_path = package.path
  package.path = package.path .. ";" .. color_dir .. PATHSEP .. "?.lua"
  config.transitions = false
  config.message_timeout = 0

  -- open this file
  core.root_view:open_doc(core.open_doc(this_file))

  -- clear statusview
  core.status_view.message_timeout = 0

  system.set_window_size(window_w, window_h, 0, 0)

  core.status_view.message_timeout = 0

  coroutine.yield(2 / config.fps)

  for _, file in ipairs(system.list_dir(color_dir) or {}) do
    local mod = file:gsub("%.lua$", "")
    core.reload_module(mod)
    core.redraw = true

    -- yield for a while for redraw
    coroutine.yield(2 / config.fps)

    renderer.screenshot(output_dir .. PATHSEP .. mod .. ".bmp")
  end

  package.path = old_path

  if quit_after_demo then
    core.quit(true)
  end
end)
