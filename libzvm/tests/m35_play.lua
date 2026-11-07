-- Exercise the real console_readline host path through the existing SDK
-- plugin's USB keyboard queue. Commands are not zvm_submit_input shortcuts.
local stage = 0
local session = assert(os.getenv("KONAOS_SESSION"))
local trace = assert(io.open(session:match("^(.*)/[^/]*$") .. "/input-state.log", "w"))
local next_trace = 0
_G.libzvm_m35_expected_marker = "zvmtest: story finished"
-- The startup script is launched from the libzvm project directory.
dofile("tests/m35_finish.lua")
_G.libzvm_m35_play_handle = emu.add_machine_frame_notifier(function()
 local now = manager.machine.time:as_double()
 if now >= next_trace then
  local cpu = manager.machine.devices[":maincpu"]
  trace:write(string.format("t=%.3f stage=%d PC=%04x SP=%04x HL=%04x DE=%04x\n",now,stage,
  cpu.state["PC"].value,cpu.state["SP"].value,cpu.state["HL"].value,cpu.state["DE"].value))
  trace:flush();next_trace=now+1
 end
 local api = emu.plugin.konaos
 if not api or not api.keys.idle() then return end
 local rows = api.console.read()
 if not rows then return end
 local text = api.console.text(rows)
 local last = ""
 for _, row in ipairs(rows) do
  row = row:match("^%s*(.-)%s*$")
  if row ~= "" then last = row end
 end
 -- The current text host prints V3 status after the story's prompt. A
 -- completed status row identifies the waiting console_readline call.
 if stage == 0 and last:find("moves 0",1,true) then
  api.keys.queue("look\r");stage=1
 elseif stage == 1 and last:find("moves 1",1,true) then
  api.keys.queue("open mailbox\r");stage=2
 elseif stage == 2 and text:find("reveals a leaflet",1,true) and last:find("moves 2",1,true) then
  api.keys.queue("quit\r");stage=3
 elseif stage == 3 and text:find("Do you wish to leave the game?",1,true) then
  api.keys.queue("y\r");stage=4
 end
end)
