-- End-to-end G0 UI test. Inputs use MAME's USB fields; SRAM is read only.
local session = assert(os.getenv("KONAOS_SESSION"))
local dir = assert(session:match("^(.*)/[^/]*$"))
local conf = {}
for line in io.lines(session) do local k,v=line:match("^([%w_]+)\t(.*)$"); if k then conf[k]=v end end
local trace = assert(io.open(dir.."/interface.tsv","w"))
local stage, due, release, release_at = 0, 0, nil, 0
local xpos,ypos = 0,0
local epoch,commands = 0,0
local outside_phase = 0
local function shot(name)
 for _,s in pairs(manager.machine.screens) do s:snapshot(dir.."/"..name..".png"); break end
end
local function field(suffix, name)
 for tag,p in pairs(manager.machine.ioport.ports) do
  if tag:sub(-#suffix)==suffix then
   if name then return assert(p.fields[name]) end
   for _,f in pairs(p.fields) do return f end
  end
 end
 error("Missing input "..suffix)
end
local function click()
 local f=field("MOUSE_BUTTONS","Mouse Left Button")
 f:set_value(1); release=f; release_at=manager.machine.time:as_double()+0.08
end
local function special(usage)
 local api=emu.plugin.konaos
 local f=assert(api.keys.usage[usage],"Missing key usage")
 f:set_value(1); release=f; release_at=manager.machine.time:as_double()+0.08
end
local function move(p,x,y)
 xpos=(xpos+x-p.mx)&0xffff; ypos=(ypos+y-p.my)&0xffff
 field("MOUSE_X"):set_value(xpos); field("MOUSE_Y"):set_value(ypos)
end
local function pixels(name)
 local api=emu.plugin.konaos
 local bytes={}
 for a=0,36863 do bytes[#bytes+1]=string.char(api.vram((a>>1)|((a&1)<<16))) end
 local f=assert(io.open(dir.."/"..name..".g7","wb")); f:write(table.concat(bytes)); f:close()
end
local function pointer_tables()
 local api=emu.plugin.konaos
 local function read(a,n)
  local out={}
  for i=a,a+n-1 do out[#out+1]=string.char(api.vram((i>>1)|((i&1)<<16))) end
  return table.concat(out)
 end
 local f=assert(io.open(dir.."/pointer.bin","wb"))
 f:write(read(0x1f200,9),read(0x1f000,32),read(0x1f800,64)); f:close()
end
local function next_stage(delay)
 stage=stage+1; due=manager.machine.time:as_double()+(delay or 0.3)
end
local function record(p)
 trace:write(string.format("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%u\t%.6f\n",
 stage,p.epoch,p.here,p.state,p.view,p.pages,p.commands,p.menu,p.mouse,p.bytes,p.hash,manager.machine.time:as_double()))
 trace:flush()
end
local function read_probe(api)
 local found=api.find("woh:_woh_probe"); if #found~=1 then return nil end
 local where=api.where(found[1]); if #where.phys~=1 then return nil end
 local b=where.phys[1]
 if api.sram(b,4)~="WOH0" then return nil end
 local function byte(n) return api.sram(b+n) end
 local function word(n) return byte(n)|(byte(n+1)<<8) end
 local function dword(n) return word(n)|(word(n+2)<<16) end
 return {waiting=byte(4),epoch=word(5),here=word(7),state=word(9),view=byte(11),pages=byte(12),
         length=byte(13),cursor=byte(14),menu=byte(15),mouse=byte(16),commands=word(17),
         bytes=dword(19),hash=dword(23),mx=word(27),my=word(29)}
end
local sequence={"north","east","open window","close window","xyzzy","score","quit","y"}
local expected={{81,1},{79,1},{79,2},{79,1},{79,1},{79,1},{79,1},{79,1}}
local action=1
local function tick()
 local now=manager.machine.time:as_double()
 if release and now>=release_at then release:set_value(0); release=nil end
 local api=emu.plugin.konaos
 if not api or release or now<due or not api.keys.idle() then return end
 local p=read_probe(api)
 if not p or p.waiting~=1 or p.epoch==0 then return end
 if stage==0 then
  assert(p.here==180 and p.state==1 and p.commands==0 and p.pages>1)
  -- The controller marks a decoder present after its first HID report;
  -- a stationary newly mounted mouse has not produced one yet.
  if p.mouse~=1 then move(p,129,106); due=now+0.5; return end
  record(p); shot("initial"); pixels("scene-initial"); pointer_tables(); epoch=p.epoch
  special(0x4e); next_stage()
 elseif stage==1 then
  if p.view~=1 then return end
  assert(p.commands==0); record(p); shot("page-down"); special(0x4b); next_stage()
 elseif stage==2 then
  if p.view~=0 then return end
  move(p,250,206); next_stage()
 elseif stage==3 then
  if p.mx~=250 or p.my~=206 then return end
  click(); next_stage()
 elseif stage==4 then
  if p.view~=1 then return end
  assert(p.commands==0); move(p,238,206); next_stage()
 elseif stage==5 then
  if p.mx~=238 then return end
  click(); next_stage()
 elseif stage==6 then
  if p.view~=0 then return end
  assert(p.commands==0); api.keys.queue("lk"); next_stage()
 elseif stage==7 then
  if p.length~=2 or p.view~=p.pages-1 then return end
  assert(p.commands==0); special(0x50); next_stage()
 elseif stage==8 then
  if p.cursor~=1 then return end
  api.keys.queue("oo"); next_stage()
 elseif stage==9 then
  if p.length~=4 then return end
  special(0x4d); next_stage()
 elseif stage==10 then
  if p.cursor~=4 then return end
  api.keys.queue("\r"); next_stage()
 elseif stage==11 then
  if p.epoch==epoch then return end
  assert(p.commands==1); epoch=p.epoch; record(p)
  pixels("before-cursor"); move(p,88,110); next_stage()
 elseif stage==12 then
  if p.mx~=88 or p.my~=110 then return end
  pixels("after-cursor"); click(); next_stage()
 elseif stage==13 then
  if p.menu~=1 then return end
  shot("menu"); move(p,105,69); next_stage()
 elseif stage==14 then
  if p.mx~=105 or p.my~=69 then return end
  shot("menu-open-highlight"); click(); next_stage()
 elseif stage==15 then
  if p.epoch==epoch then return end
  assert(p.commands==2 and p.menu==0); epoch=p.epoch; record(p)
  pixels("before-menu"); move(p,88,110); next_stage()
 elseif stage==16 then
  if p.mx~=88 or p.my~=110 then return end
  click(); next_stage()
 elseif stage==17 then
  if p.menu~=1 then return end
  special(0x29); next_stage()
 elseif stage==18 then
  if p.menu~=0 then return end
  assert(p.commands==2); pixels("after-menu"); click(); next_stage()
 elseif stage==19 then
  if outside_phase==0 then
   if p.menu~=1 then return end
   move(p,200,150); outside_phase=1; due=now+0.3; return
  elseif outside_phase==1 then
   if p.mx~=200 or p.my~=150 then return end
   click(); outside_phase=2; due=now+0.3; return
  elseif outside_phase==2 then
   if p.menu~=0 then return end
   assert(p.commands==2); pixels("after-outside-click"); move(p,88,110)
   outside_phase=3; due=now+0.3; return
  elseif outside_phase==3 then
   if p.mx~=88 or p.my~=110 then return end
   click(); outside_phase=4; due=now+0.3; return
  end
  if p.menu~=1 then return end
  api.keys.queue("inventory"); next_stage()
 elseif stage==20 then
  if p.menu~=0 or p.length~=9 then return end
  assert(p.commands==2); api.keys.queue("\r"); next_stage()
 elseif stage==21 then
  if p.epoch==epoch then return end
  assert(p.commands==3); epoch=p.epoch; record(p)
  api.keys.queue(string.rep("x",40)); next_stage()
 elseif stage==22 then
  if p.length~=40 then return end
  shot("long-command"); api.keys.queue(string.rep("\b",40)); next_stage()
 elseif stage==23 then
  if p.length~=0 then return end
  assert(p.commands==3); record(p); api.keys.queue(sequence[action].."\r"); next_stage()
 elseif stage==24 then
  if p.epoch==epoch then return end
  assert(p.here==expected[action][1] and p.state==expected[action][2],"Wrong room/state")
  assert(p.commands==3+action,"Extra command submitted")
  epoch=p.epoch; record(p)
  if action==3 then pixels("scene-window-open"); shot("window-open") end
  if action==4 then pixels("scene-window-closed"); shot("window-closed") end
  action=action+1
  if action<=#sequence then api.keys.queue(sequence[action].."\r")
  else api.keys.queue("\r"); next_stage() end
 end
 -- The app's normal cleanup prints measurements after releasing graphics.
 local rows=api.console.read()
 if rows then
  local output=api.console.text(rows)
  if stage>=25 and output:find("woh: finished",1,true) and output:find("/>",1,true) then
   local f=assert(io.open(conf.result,"w")); f:write("ran\nG0 full interface returned to shell\n"); f:close()
   if api.console.capture_file then api.console.capture_file:write(output.."\n"); api.console.capture_file:flush() end
   manager.machine:exit()
  end
 end
end
_G.woh_g0_interface = emu.add_machine_frame_notifier(function()
 local ok,err=pcall(tick)
 if not ok then
  local f=assert(io.open(conf.result,"w")); f:write("error\n",tostring(err),"\n"); f:close()
  print("G0 interface failed: "..tostring(err)); manager.machine:exit()
 end
end)
