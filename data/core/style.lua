local common = require "core.common"
local style = {}

---The width of dividers between Views.
---@type number
style.divider_size = common.round(1 * SCALE)

---The width of scrollbars when not expanded.
---@type number
style.scrollbar_size = common.round(4 * SCALE)

---The width of scrollbars when expanded (during hover).
---@type number
style.expanded_scrollbar_size = common.round(12 * SCALE)

---The width of the caret.
---@type number
style.caret_width = common.round(2 * SCALE)

---The maximum width of a tab.
---@type number
style.tab_width = common.round(170 * SCALE)

---The padding values between various UI elements, e.g. between buttons, icons and text.
---
---These values are general and thus widely used;
---it is not recommended to change them directly unless the UI "looks wrong".
---@type { x: number, y: number }
style.padding = {
  x = common.round(14 * SCALE),
  y = common.round(7 * SCALE),
}

---The margin values for tabs.
---@class core.style.margins.tabs
---@field top number The top tab margin. This value can be negative.

---The margin values between various UI elements.
---@class core.style.margins
---@field tab core.style.margins.tabs
style.margin = {
  tab = {
    top = common.round(-style.divider_size * SCALE)
  }
}

-- The function renderer.font.load can accept an option table as a second optional argument.
-- It shoud be like the following:
--
-- {antialiasing= "grayscale", hinting = "full"}
--
-- The possible values for each option are:
-- - for antialiasing: grayscale, subpixel
-- - for hinting: none, slight, full
--
-- The defaults values are antialiasing subpixel and hinting slight for optimal visualization
-- on ordinary LCD monitor with RGB patterns.
--
-- On High DPI monitor or non RGB monitor you may consider using antialiasing grayscale instead.
-- The antialiasing grayscale with full hinting is interesting for crisp font rendering.

---The font used for most UI elements except DocView.
---@type renderer.font
style.font = renderer.font.load(DATADIR .. "/fonts/FiraSans-Regular.ttf", 15 * SCALE)

---The font used for larger UI elements, e.g. The welcome screen.
---@type renderer.font
style.big_font = style.font:copy(46 * SCALE)

---The font used for icons throughout the UI.
---This should not be changed unless you know what you are doing.
---@type renderer.font
style.icon_font = renderer.font.load(DATADIR .. "/fonts/icons.ttf", 16 * SCALE, {antialiasing="grayscale", hinting="full"})

---The font used for larger icons throughout the UI.
---@type renderer.font
style.icon_big_font = style.icon_font:copy(23 * SCALE)

---The font used for DocView.
---@type renderer.font
style.code_font = renderer.font.load(DATADIR .. "/fonts/JetBrainsMono-Regular.ttf", 15 * SCALE)

---A table containing colors for each syntax group.
---
---This table may contain colors for other undocumented syntax groups used by plugins.
---@type { [core.syntax.syntax_group]: renderer.color }
style.syntax = {}

---A table of fonts that overrides the default font code for a syntax group.
---
---For instance, you could choose to render comments in italic by doing:
---```lua
---style.syntax_fonts["comment"] = style.code_font:copy(style.code_font:get_size(), { italic=true })
---```
---@type { [string]: renderer.font }
style.syntax_fonts = {}

---Colors for LogView entries.
---@type { [core.logview.log_level]: renderer.color }
style.log = {}

return style
