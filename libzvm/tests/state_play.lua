-- Test the actual ZVMTest console path. IDs come from the compiled story;
-- the host post-check compares this numeric trace with the ZAP declarations.
local stage = 0
local session = assert(os.getenv("KONAOS_SESSION"))
local trace = assert(io.open(session:match("^(.*)/[^/]*$") .. "/state-input.tsv", "w"))
local commands = {"north", "east", "open window", "close window", "quit"}
local states = {1, 1, 1, 2, 1}
_G.libzvm_m35_expected_marker = "zvmtest: story finished"
-- MAME is started from the libzvm project directory.
dofile("tests/m35_finish.lua")
_G.woh_state_play = emu.add_machine_frame_notifier(function()
 local api = emu.plugin.konaos
 if not api or not api.keys.idle() then return end
 local rows = api.console.read()
 if not rows then return end
 local text = api.console.text(rows)
 local last, here, state, moves = "", nil, nil, nil
 for _, row in ipairs(rows) do
  row = row:match("^%s*(.-)%s*$")
  if row ~= "" then last = row end
  local h = row:match("^HERE:%s+(%d+)")
  local s = row:match("^WOH%-STATE:%s+(%d+)")
  local m = row:match("|%s*%d+%s+moves%s+(%d+)")
  if h then here = tonumber(h) end
  if s then state = tonumber(s) end
  if m then moves = tonumber(m) end
 end
 if stage < 5 and last:match("^WOH%-STATE:") and moves == stage and state == states[stage+1] then
  trace:write(string.format("%d\t%d\t%d\t%d\t%.6f\n",stage,here,state,moves,manager.machine.time:as_double()))
  trace:flush()
  api.keys.queue(commands[stage+1] .. "\r")
  stage = stage + 1
 elseif stage == 5 and text:find("Do you wish to leave the game?",1,true) and last:match("^WOH%-STATE:") then
  trace:write(string.format("confirmation\t%d\t%d\t%d\t%.6f\n",here,state,moves,manager.machine.time:as_double()))
  trace:flush()
  api.keys.queue("y\r")
  stage = 6
 end
end)
