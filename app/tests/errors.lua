local session=assert(os.getenv("KONAOS_SESSION"))
local conf={}
for line in io.lines(session) do local k,v=line:match("^([%w_]+)\t(.*)$"); if k then conf[k]=v end end
_G.woh_g0_errors=emu.add_machine_frame_notifier(function()
 local api=emu.plugin.konaos
 if not api or not api.console then return end
 local rows=api.console.read()
 if not rows then return end
 local text=api.console.text(rows)
 local last=""
 for _,row in ipairs(rows) do if row:match("%S") then last=row:match("^%s*(.-)%s*$") end end
 if text:find("woh: story path",1,true) and last=="/>" then
  local f=assert(io.open(conf.result,"w")); f:write("ran\nG0 error restored console and returned to shell\n"); f:close()
  if api.console.capture_file then api.console.capture_file:write(text.."\n"); api.console.capture_file:flush() end
  manager.machine:exit()
 end
end)
