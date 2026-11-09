-- G0 integration through the real USB keyboard and SDK bank-aware debugger.
-- No target memory is written. The probe is read-only application telemetry.
local session = assert(os.getenv("KONAOS_SESSION"))
local directory = assert(session:match("^(.*)/[^/]*$"))
local trace = assert(io.open(directory .. "/play.tsv", "w"))
local stage, previous = 1, 0
local commands = {"north", "east", "open window", "close window", "xyzzy",
                  "inventory", "quit", "y", ""}
local function snapshot(name)
 for _, screen in pairs(manager.machine.screens) do
  screen:snapshot(directory .. "/" .. name .. ".png")
  break
 end
end
_G.woh_g0_test = emu.add_machine_frame_notifier(function()
 local api = emu.plugin.konaos
 if not api or not api.keys.idle() then return end
 local found = api.find("woh:_woh_probe")
 if #found ~= 1 then return end
 local where = api.where(found[1])
 if #where.phys ~= 1 then return end
 local base = where.phys[1]
 if api.sram(base,4) ~= "WOH0" or api.sram(base+4) ~= 1 then return end
 local function word(offset) return api.sram(base+offset) | (api.sram(base+offset+1)<<8) end
 local epoch = word(5)
 if epoch == previous or epoch == 0 then return end
 previous = epoch
 trace:write(string.format("%d\t%d\t%d\t%d\t%d\t%d\t%.6f\n",epoch,word(7),word(9),
             api.sram(base+11),api.sram(base+12),word(17),manager.machine.time:as_double()))
 trace:flush()
 if stage == 1 then snapshot("initial") end
 if stage == 4 then snapshot("window-open") end
 if stage == 5 then snapshot("window-closed") end
 if stage <= #commands then api.keys.queue(commands[stage] .. "\r"); stage = stage+1 end
end)
local conf = {}
for line in io.lines(session) do
 local k,v = line:match("^([%w_]+)\t(.*)$")
 if k then conf[k] = v end
end
_G.woh_g0_finish = emu.add_machine_frame_notifier(function()
 local api = emu.plugin.konaos
 if not api or not api.console then return end
 local rows = api.console.read()
 if not rows then return end
 local output = api.console.text(rows)
 if output:find("woh: finished",1,true) and output:find("/>",1,true) then
  if api.console.capture_file then
   api.console.capture_file:write(string.format("--- %.3f s\n%s\n",manager.machine.time:as_double(),output))
   api.console.capture_file:flush()
  end
  local f = assert(io.open(conf.result,"w")); f:write("ran\nG0 keyboard session returned to shell\n"); f:close()
  manager.machine:exit()
 end
end)
