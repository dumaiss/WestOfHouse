-- Extend the existing SDK session runner for long, scrolling test programs.
-- Its original command row scrolls off the screen, preventing last_prompt()
-- from recognizing return. Require both the app's completion marker and the
-- actual shell prompt. Do not alter the OS, SDK or installed MAME plugin.
local session = os.getenv("KONAOS_SESSION")
assert(session, "KONAOS_SESSION is required")
local conf = {}
for line in io.lines(session) do
 local key, value = line:match("^([%w_]+)\t(.*)$")
 if key then conf[key] = value end
end
-- Retain the notifier subscription after this startup chunk returns.
_G.libzvm_m35_finish_handle = emu.add_machine_frame_notifier(function()
 local api = emu.plugin.konaos
 if not api or not api.console then return end
 local rows = api.console.read()
 if not rows then return end
 local complete, prompt = false, false
 for _, row in ipairs(rows) do
  if row:find(_G.libzvm_m35_expected_marker or "zvmtest: scripted Zork sequence completed", 1, true) then complete = true end
  if row:match("^%s*/>%s*$") then prompt = true end
 end
 if complete and prompt then
  -- Preserve the final screen even if the SDK's six-frame capture interval
  -- has not fired yet. Use its existing writer and serialization.
  if api.console.capture_file then
   api.console.capture_file:write(string.format("--- %.3f s\n%s\n", manager.machine.time:as_double(), api.console.text(rows)))
   api.console.capture_file:flush()
  end
  local f = assert(io.open(conf.result, "w"))
  f:write("ran\n", conf.command, " completed and returned to shell; scrolling-session extension\n")
  f:close()
  print("m35_finish: completed and returned to shell at " .. tostring(manager.machine.time:as_double()) .. " emulated s")
  manager.machine:exit()
 end
end)
