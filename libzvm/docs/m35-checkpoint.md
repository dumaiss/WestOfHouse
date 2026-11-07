# M3.5 continuation checkpoint — 2026-11-06 20:28 Toronto

## Final update — M3.5 completed

The historical handoff below is superseded by `m35-performance.md` and
`m35-cache-asm.md`. Default builds now use the validated cache-hit ASM,
four pages per partition, direct reader batching and512-byte stack. Full
normal/discard benchmarks, profiled counter equivalence, native/target
regressions and two real-keyboard interactive runs passed. Ten commands:
359.639→52.855emulatedseconds (6.80×). No remaining implementation step is
pending in this milestone; future responsiveness work is described in the
report. Durable evidence is under docs/m35-data. Do not restart M3.5 from
the old unfinished steps below.

The requested21:45one-shot scheduler fired but its Node launcher failed;
the user explicitly resumed this conversation, and work continued here.
The saved launcher was corrected to use the absolute Node24binary. No
replacement recurring task was created.

---

This is an internal implementation handoff. **M3.5 is unfinished.** Preserve
the current files and results; do not restart the milestone. User requested
one continuation at21:45Toronto after their21:40usage reset. A local systemd
user timer `libzvm-m35-continuation-20261106.timer` dispatches `codex queue
--resume` into conversation `01a1b30b-14e6-77a1-92d7-426c931a35aa`.
Dispatch script/log: `build/m35/continue_once.py`,
`build/m35/continuation-dispatch.log`. No repeating schedule was requested.

## Scope and authoritative sources

- All modifications belong to `/home/kitamura/Documents/WestOfHouse/libzvm`.
  Do not modify ZephyrOS, KonaOS, SDK, Zephyr-80 emulator or original story.
- Work package: `/home/kitamura/.codex/attachments/e325d689-d4fd-4cd4-ae36-b54c25d88090/Pasted text.txt`.
  Read it if context is missing. User expressly requested multi-agent work,
  real baseline/after profiling, meaningful hybrid C/ASM, simulator reuse,
  actual MAME acceptance, syscall/IPC batching and final reports.
- Keep M1 modular source layout. No monolithic zvm.c rewrite, native-PC VM,
  third emulator, mandatory KRES, M4 persistence or arbitrary speed target.
- Already inspected authoritative `ZephyrOS/docs/{architecture,memory,sdk,
  abi,exe-format,kres-sdk-design,tui}.md`. architecture.md precedence wins.
- Story is original `../vendor/infocom-zork1/COMPILED/zork1.z3`:86838bytes,
  dynamic11282bytes, initialPC20693. Preserve immutable source/checksum.
- One primary bank0100-DFFF, stacktopE000.8KiBslots,32KiBplainXMEM,
  16KiBmappable,32systemwideobjects. Auxiliary allocator banks0–5.
  One aperture; no mapped data pointers survive remapping; ordinary SDK
  calls and automatic executable placer only.
- Whole libzvm is untracked from earlier milestones in WestOfHouse git.
  Do not discard it or unrelated untracked yazm-py. No AGENTS.md found.

## Completed infrastructure and evidence

### Frozen baselines — use these exact SDK layouts

- `build/m35/baseline-source.tar.gz`: original M3 before profiling.
- `build/m35/baseline.exe`, baseline.report.json: original executable.
- `build/m35/environment.sha256`: original ROM/story/MAME hashes.
- `build/m35/baseline-v2/zvmtest.exe` with full nested `zvmtest/` build
  artifacts: **corrected harness, unprofiled, unoptimized core/adapter**.
- `build/m35/baseline-profile-v2/zvmtest.exe` with full nested `zvmtest/`:
  same harness with profiling. Both snapshots immutable; live builds safe.
- `baseline-harness-source.tar.gz`, baseline-harness.exe/.map/.report.json,
  baseline-harness-profile equivalents preserved too.
- Unprofiled corrected baseline44289bytes, CODE41767,BSS8984,stack256.
  Profile47609bytes,CODE44558,BSS9042. Both placement.json null,0segments,
  0veneers. Do not confuse original M3 size42455 with corrected harness.

**Harness argv fix:** SDK argv contains argument tail only. argv[0] is flag,
not program name. Old M3 -a/-p had silently fallen through to fixture/probe
path. Corrected snapshots and current app use argc>0/argv[0], pathargv[1].

**MAME requires escalation outside sandbox.** Sandboxed runs hang with
`pa_write`/stream-fd errors and no window. User presses initial startupkey
when visible; user reported no window before this was diagnosed. Working
MAME calls used require_escalated. Long foreground sessions were killed143
when interrupted/new turn. Background `nohup ... &` children also failed to
survive tool completion (empty logs, no processes). Do not rely on detached
shell children. Relaunch foreground with exec_command session and poll.

MAME plugin needs exact SDK layout: mapstem.json plus
`zvmtest/place/placement.json`, parent exe filename from manifest. Renaming
only a .map breaks launch. Full baseline-v2 snapshots solve this.

### Partial real MAME data — useful, NOT completed acceptance

- `build/m35/run-baseline-profile-v2/console.txt`: valid profile normal run,
  through turn9; stopped before turn10/final/discard. Startup435040counts
  =11.137024s emulated,407VM instructions,219xreads/112128bytes,
  74xwrites/37394bytes,44readerrequests/21508bytes.
- Cumulative profile input2 openmailbox: instruction3364,1312xreads,
  446xwrites,351readerrequests. Its own duration1614762counts=41.337907s.
  Approx545xreads196xwrites176readerrequests for1518instructions in turn2.
- `build/m35/run-baseline-unprofiled/console.txt`: valid normal throughturn2.
  startup419311counts=10.734361s; look1243775=31.840640s;
  openmailbox1575176=40.324505s. No final state/hash yet.
- Timerpair47counts (~1203us). No per-instruction timer syscalls.
- `run-baseline-detached` and `run-baseline-profile-detached` have only
  empty host.log; failed detached attempts, no measurements.
- Earlier `run-baseline`, baseline-bench and pre-v2 artifacts/runs may be
  sandbox-stalled, badlayout or badargv. Exclude them from acceptance.
- Sessions95610(profile) and3119(unprofiled) ended143; no valid current
  foreground baseline runner at checkpoint. Check process/capture state.

Working invocation pattern from libzvm (escalated):

```sh
/usr/bin/time -f 'host_wall_seconds=%e' python3 ../../ZephyrOS/build/kos-sdk-0.2.0/tools/exerun.py --exe build/m35/baseline-v2/zvmtest.exe --map build/m35/baseline-v2/zvmtest/zvmtest.map --card build/m35/card-baseline-final --rundir build/m35/run-baseline-final --mame ../../Zephyr-80/Code/MODERN/Emulator/mame/mamepbitz --flash ../../ZephyrOS/build/konaos.bin --plugins ../../Zephyr-80/Code/MODERN/Emulator/mame/plugins --asset ../vendor/infocom-zork1/COMPILED/zork1.z3=ZORK1.Z3 --args '-B /fs/ZORK1.Z3' --exit --timeout 1800 --mame-args='-nothrottle -sound none -video none'
```

`-video none` unprofiled partial did advance, avoids window rendering;
report same configuration before/after. Timers represent target time,
not host walltime. Full baseline20turns may take>700emulatedseconds.

### Profiling package (completed)

Agent `/root/profiling`, GPT-6.1Sol medium, completed include/zvm_profile.h,
optional counters in core, tests/zvmtest.c harness, Makefile profile variants.
Default counters compile away. Per-VM opaque1536state still fits (target
profile1044); no public state-layout change. Calls/returns/branches,
opfamilies,fetches,read/write,text,abbrev,dict,output counters documented
in header. Conditional branches exclude unconditional jump140.

Benchmarkflags -b normal,-d discard,-B both (reloadstory).10commands:
look,openmailbox,takeleaflet,readleaflet,inventory,score,look,examinemailbox,
closemailbox,openmailbox. Timers cover load,startup,inputsubmission→next
NEED_INPUT including outputflush; exclude human input/diagnostic printing.
Narrative DJB2xor hash seed5381 + outputcount, finalroom/score/moves.
Profile host counters are adapter requests, NOT physical SD faultcounts.
SDK stdio refills conceal actual lower syscallcounts; do not fabricate.

Supervisor added `tests/m35_report.py` parser/comparison of repeated MAME
console snapshots. Review parser against complete captures; it joins
wrapped fields and last complete copies win. `--out` JSON. Partial parsed
file `build/m35/baseline-profile-partial.json` exists.

### Synthetic/ASM packages (completed locally, gameplay A/B pending)

- `/root/cpu_audit`: GPT-6.1Sol high; readonly platform/SDCC/ZIP80audit,
  then24byte independent `src/zchar.asm` helper and text.c conditional call.
  `ZVM_ASM=1`, default0 pending integrated acceptance. No ASM publicABI.
  HLword,DEslot0..2,returnA; preservesIX/SP. Documented docs/m35-asm.md.
- `tests/m35_unpack.py` passed1677C/ASM cases, register/stack checks.
  C extraction slot0/1/2 cost450/290/130T→66/107/74T. Loop2.322×.
- `/root/synthetics`: GPT-6Luna medium failed/no files; escalated to
  `/root/synthetics_sol`, GPT-6.1Sol medium, completed tests/zvmperf.c,
  tests/m35_sim.py reusing unchanged ZephyrOS Z80sim/ihx. RealSDCCVM,
  not a native-host interpreter. Ninephases, expectedstate/checksums.
- `build/m35/synthetic-{baseline,current,asm}.json`. Originalbaseline and
  currentC identical: decode307630T, arithmeticbranch1327790,
  stackbytecode1353186,callreturn1388482,locals61154,globals173826,
  memory68834,text330918,dictionary1363074.
- ASM realtextphase330918→315398T (−4.69%,1.049×). Other8unchanged.
  Maxsyntheticstack214B(callreturn) of256; text137B. Model timing estimates
  have takenbranch assumptions/missingop4Tfloor, not physicalmeasurements.
- `make test ZVM_ASM=1` passed; default/profile variants validated.
  ASM exe+9B,CODE+9,BSSunchanged,nullplacement,0segments/veneers.
- CPUagent Makefile handoff complete. No CPUagent edits pending.
- ZIP80 source referenceonly, no permissivegrant; no source copied.

## Current implementation in progress — cache/IO worker

`/root/io_audit`, GPT-6.1Sol high, now implementing authorized measured
memory/IO optimization. At20:26sourcepackage ready, testsfinishing.
**Owned files:** tests/zvmtest.c, NEW tests/story_cache.c/.h,
NEW tests/test_story_cache.c. No core or Makefile edits by worker.

- Fullyassociative resident512bytepages, DYN_CACHE_PAGES and
  STATIC_CACHE_PAGES1or4, defaults4each. Hotpage shortcut + roundrobin.
 4096Bbytes storage at4+4,1024at1+1, tinytags/valid/dirty metadata.
- XMEM remains dynamic authoritative backing11282bytes, allocated16K,
  1plainobject2slots. Static source file remains immutable/demandpaged.
- Dirtyvictim writeback before replace; failedwrite retainsvaliddirty;
  aftersuccessfulflush invalidatebeforefill, partialfailedread cannot
  publish corruptedbytes.17bitaddr,partialpage,unaligneddynamicstatic
  boundary11282mustwork.
- DirectSDK xread/xwrite. `kos_reader_from_handle` replaces FILE/fread
  CPU byteitemloop. Borrowed reader, adapter owns/closeshandle.
- Startup/checksum stage <=4096byteSDKreads/XMEMwrites reusing cachebytes
  while invalid (no additional4096buffer). Checksumoriginalsource before
  VMwrites. Sentinel probe also open/close; nostdio stream pool linked.
- Profilestats under #if only. Existingmetricnames preserved; fread0.
- **Output not changed yet.** Keep it separate until normal/discard data.
- Focusednativecache tests stubSDKservices, not nativeVM: dirtyevictions,
  randomread/writevsreference,sourceunchanged,partialpage,readonly,
  >64K,lastpage, injectedfailures.4+4profile passed, othersrunning.

### Immediate next step

Worker final handoff received before scheduling turn ended: source package
complete. PASS native4+4profile,1+1profile,4+4unprofiled,mixed1+4profile;
50,000randomaccesses and failurecases. PASSASan/UBSan4+4,LeakSanitizer
disabled dueptraceenvironment (noheapallocations). PASSisolatedSDCCWerror
cache1/4profileon/off and appprofileon/off, repeatedafterfinalheaderchange.
ExactSDCCstructsize4+4:4136Bunprofiled/4177Bprofile;1+1:1046/1087B.
Worker hasfinished; no agentedits pending. Buildintegration and linked
footprint/MAME remain supervisorwork.

1. Getworker finalmessage/teststatus; reviewsourcefailurepaths.
2. **Makefile currently NOT integrated with new story_cache.c.** Add
   thatsource to exe_program, cacheconfigdefines, isolatedbuilddirs to
   avoidstale1vs4flags. Addfocusedcachetests to testtarget. Preserve
   existingprofile/ASMmatrix and noOSwrites.
3. RunM1–M3regressions, cachetestvariants,targetSDKpackvalidate. Review
   map for unsupportedmultiplyhelpers, initializedwritablestatics,
   callbackresidency,auto placement. CompareBSS/remove stdiostreampool.
4. Freeze optimized cache1 andcache4 SDKartifactdirectories; runMAME
   unprofiled+profile pairs samecommands/ROM/story/environment. Complete
   baseline finalhash/state forcomparison. Cache1 isolatesdirecthandle
   path; cache4 addsresidency. Measureactualbenefit, notjustmicro.

## Ranked diagnosis / next decisions

Before authorizing cachepackage, measured407startupinstructions caused
219XMEMreads74writes44readerrequests and11.1seconds. Turn2had hugechurn.
Rank1 dynamic/static singlepage thrashing; Rank2stdio fread(dst,1,n)
itemloopCPU+repeatedseek/read; Rank3perbytecachedlogicaladdr/functioncall
CPU; Rank4packedZchar shifts; consolecontribution pendingdiscarddata.

AvoidspeculativefetchcursormajorABIuntilcachebenefitmeasured. Dispatcher
already directswitch/ordinarycalls; no tableforcingallhandlersresident.
CPUaudit evaluateddecode,dispatch,stack,vars,frames,dict/memory APIs;
complexASMarchitecture risks largerthan currentisolatedhelperbenefit.

Aftercache:
- CompareASM0/1 actualgameplay; retainhelperonlyifintegratedbenefitjustifies
  its9Bnetcodegrowth. No claiming2.3×game frommicrobenchmark.
- Measure normalvsdiscard. SDKputcharalready128byte newlineflush. If
  material, addhostonly256/512outputbuffer acrosslines andSDKconsole_write
  descriptors, flushbeforestatus/NEED_INPUT/finished/fault; propagateerrors,
  rawstoryhash beforepresentation. MayfixLFstaircaserendering viaCRLF in
  adapteronly. NoVMconsoledependency. Thispackage notyetstarted.
- ActualIPCbatching should reducecalls through biggerstartupreads and
  retaineddirtypages; readerrequestcount is not SDfaultcount. SDKdirect
  handleread_at does seek+read. Reportcounts honestly.

## Remaining acceptance/reporting

Needfinal docs/m35-performance.md (or equivalent) containing before/after
table all10turns,load/startup,normal/discard,profileoverhead,separateIO/cache/
ASM/outputeffects; checksums/finalVMstate; focused+M1–M3tests; fullMAME
script and interactiveplayability; exactenvironmenthashes/reproduction.
Resourceledger:primaryCODE,BSS,stack256,VMstack128words/frames16,cachebytes,
XMEMrequested11282/allocated16384,objects/slots,farsegments/veneers and
primaryfreeheadroom. diagvalidbankmask is **not** freecapacity; SDK hasno
exactsystemwidefreequery, identifylimitsandcompetitionhonestly.
Finalrecommendationresponsiveenough?,remainingbottlenecks,M4safe? No
inventedcompletion ifMAMEunfinished. Multiagentreport modelsabove,
ownership/dependencies, Lunaescalation/rejectedapproaches. Supervisorowns
finalreview. User wants continueuntilcomplete orusagelimit, nofreshstart.
