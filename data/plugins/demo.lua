-- mod-version: 2.0
local core = require "core"
local config = require "core.config"

-- find this file
local this_file = DATADIR .. "/plugins/demo.lua"

-- color dir
local color_dir = system.absolute_path "colors"

-- output dir
local output_dir = system.absolute_path "previews"

core.add_thread(function()
  local old_path = package.path
  package.path = package.path .. ";" .. color_dir .. PATHSEP .. "?.lua"
  config.transitions = false

  -- open this file
  core.root_view:open_doc(core.open_doc(this_file))

  for _, file in ipairs(system.list_dir(color_dir) or {}) do
    local mod = file:gsub("%.lua$", "")
    core.reload_module(mod)
    core.redraw = true

    -- yield for a while for redraw
    coroutine.yield(2 / config.fps)

    renderer.screenshot(output_dir .. PATHSEP .. mod .. ".bmp")
  end

  package.path = old_path
end)
