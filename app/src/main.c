#include "kos.h"
#include "kos/mouse.h"
#include "kos/key.h"
#include "kos/snd.h"
#include "game.h"
#include "render.h"
#include "probe.h"
#include "resources.h"
#if WOH_PROFILE
#include "kos/timer.h"
#include "kos/fs.h"
#if ZVM_PROFILE
#include "zvm_profile.h"
#endif
/* Diagnostic build only (make WOH_PROFILE=1): cumulative per-turn totals in
 * timer counts (25.6 us), written to PROF.BIN at exit. */
extern game_t game;
typedef struct {
    uint32_t t_end, vm, io_static, io_dynamic, render;
    uint32_t static_misses, dyn_misses, dyn_evictions, xreads, xwrites, instructions, byte_reads;
    uint16_t here;
} prof_row_t;
static int prof_file;
static uint32_t prof_vm, prof_render;
static uint32_t prof_load_counts;
static int woh_prof_timer;
static uint32_t woh_prof_now(void)
{
    uint32_t now = 0;
    if (woh_prof_timer > 0) (void)timer_now(woh_prof_timer,&now);
    return now;
}
static void prof_row(void)
{
    prof_row_t row, *r = &row;
#if ZVM_PROFILE
    const zvm_profile_t *vm = zvm_profile_get(&game.vm);
#endif
    if (prof_file < 1) return;
    r->t_end = woh_prof_now(); r->vm = prof_vm; r->render = prof_render;
    r->io_static = game.mapped.page_loads; r->io_dynamic = game.mapped.page_writes;
    r->static_misses = game.mapped.page_loads; r->dyn_misses = 0;
    r->dyn_evictions = game.mapped.page_writes;
    r->xreads = game.mapped.remaps; r->xwrites = game.mapped.page_writes;
#if ZVM_PROFILE
    r->instructions = vm ? vm->instructions : 0;
    r->byte_reads = vm ? vm->byte_reads : 0;
#else
    r->instructions = 0; r->byte_reads = 0;
#endif
    r->here = game.here;
    (void)write((unsigned int)prof_file,r,sizeof(*r));
}
static void prof_write(void)
{
    if (prof_file > 0) close((unsigned int)prof_file);
    prof_file = 0;
}
#define PROF_START(v) uint32_t v = woh_prof_now()
#define PROF_ADD(total,v) (total += woh_prof_now() - (v))
#else
#define PROF_START(v) ((void)0)
#define PROF_ADD(total,v) ((void)0)
#endif

/* Separate the large VM/cache object from controller BSS so the SDK can
 * place it above C000h without moving the controller's other data. */
extern game_t game;
static panel_t panel;
static menu_t menu;
static struct mouse mouse;
static char story_path[128];
woh_probe_t woh_probe;
static uint8_t waiting, finished, suppress_lf;

/* Background music: the archive's PCM stream (WOH_MUSIC_RESOURCE),
 * looped through libsnd's PCM player (kos/snd.h), which holds the sound
 * card for it.  Optional: without the resource, a sound card or the
 * real-time interrupt the game runs silent.
 *
 * It plays while the game waits for input, fed on every interrupt, and
 * pauses while the game works on a command: the RT clock's lane holds
 * would halve the speed of the story reads and scene loads of a turn,
 * which would then outlast the player's buffering.  Each pause and resume
 * glides the DAC, so a turn is a short fade. */
#if WOH_PROFILE
static uint8_t music_stage[1];   /* the profile build runs silent */
#else
static uint8_t music_stage[1024];
#endif
static kos_reader_t music_reader;
static uint8_t music_on;

static void music_start(void)
{
#if !WOH_PROFILE
    if (resources_music(&music_reader) < 0) return;
    if (snd_pcm_open(&music_reader,music_stage,sizeof(music_stage)) < 0) return;
    if (snd_pcm_play(SND_PCM_LOOP) < 0) { snd_pcm_close(); return; }
    music_on = 1;
#endif
}

static void music_stop(void)
{
    if (!music_on) return;
    snd_pcm_close();
    music_on = 0;
}

/* The game is working on a command: no RT clock while it does. */
static void music_pause(void)
{
    if (music_on) (void)snd_pcm_pause();
}

/* The game waits for input: feed the music, resuming it after a turn. */
static void music_service(void)
{
    if (!music_on) return;
    if (snd_pcm_service() == SND_PCM_PAUSED && snd_pcm_resume() < 0)
        music_stop();          /* a stream that cannot resume ends */
}

/* An empty input wait: while music plays, feed it on every interrupt (the
 * ring holds 192 ms, the staging one read) and poll the keyboard every
 * eighth; otherwise the SDK's input pacing. */
static void input_wait(void)
{
    uint8_t i;
    if (!music_on) { idle_input(); return; }
    for (i = 0; i < 8u; ++i) {
        idle();
        music_service();
    }
}

static void probe(void)
{
    woh_probe.waiting = waiting;
    woh_probe.here = game.here; woh_probe.state = game.state;
    woh_probe.view = panel.view; woh_probe.pages = panel.count;
    woh_probe.command_length = panel.command_length; woh_probe.cursor = panel.cursor;
    woh_probe.menu = menu.open; woh_probe.mouse = mouse.present;
    woh_probe.mouse_x = (uint16_t)mouse.x; woh_probe.mouse_y = (uint16_t)mouse.y;
}

static int close_menu(void)
{
    if (!menu.open) return 0;
    menu.open = 0;
    return render_menu_close(&menu);
}

/* Keyboard and menu actions both enter here, including parser confirmations. */
static int submit(const char *text)
{
    /* The turn's work starts here (the panel redraw included): pause
     * first, so the music fades rather than starves. */
    music_pause();
    if (close_menu() < 0) return -1;
    panel_begin(&panel,text);
    /* Show acceptance before input tokenization and story execution. The
     * response remains buffered until the next input boundary, but the
     * selected/typed command should not wait for a whole VM turn to appear. */
    panel_latest(&panel);
    if (render_panel(&panel,0) < 0) return -1;
    if (zvm_submit_input(&game.vm,text) != ZVM_OK) return -2;
    panel.command[0] = 0;
    panel.command_length = panel.cursor = 0;
    waiting = 0;
    ++woh_probe.commands;
    return render_prompt(&panel,0);
}

static int page(uint8_t down)
{
    if (close_menu() < 0) return -1;
    if (down ? panel_page_down(&panel) : panel_page_up(&panel)) {
        if (render_panel(&panel,1) < 0) return -1;
        return render_prompt(&panel,!finished);
    }
    return 0;
}

static int mouse_event(void)
{
    int item, result;
    char command[COMMAND_MAX+1u];
    if (mouse.handle < 1) return 0;
    result = mouse_track(&mouse);
    if (result < 0) {
        (void)render_pointer(0,0,0);
        close((unsigned int)mouse.handle); mouse.handle = 0;
        return 0; /* Keyboard remains usable if a device disappears. */
    }
    if (!mouse.present) { (void)render_pointer(0,0,0); return 0; }
    if (result > 0 && render_pointer(mouse.x,mouse.y,1) < 0) return -1;
    if (!waiting) { mouse.pressed = 0; return 0; }
    if (menu.open && menu_hover(&menu,(uint16_t)mouse.x,(uint16_t)mouse.y)) {
        if (render_menu(&menu) < 0) return -1;
    }
    if (mouse.pressed & MOUSE_LEFT) {
        if (menu.open) {
            item = menu_hit(&menu,(uint16_t)mouse.x,(uint16_t)mouse.y);
            if (item >= 0) {
                menu_command(&menu,(uint8_t)item,command);
                if (submit(command) < 0) return -1;
            } else if (close_menu() < 0) return -1;
        } else if (mouse.y >= 202 && panel_prompt_cells(&panel) != 40u) {
            if (mouse.x >= 232 && mouse.x <= 243) { if (page(0) < 0) return -1; }
            else if (mouse.x >= 244) { if (page(1) < 0) return -1; }
        } else if (!finished && resources_menu(&menu,(uint16_t)mouse.x,(uint16_t)mouse.y)) {
            (void)menu_hover(&menu,(uint16_t)mouse.x,(uint16_t)mouse.y);
            if (render_menu(&menu) < 0) return -1;
        }
    }
    mouse.pressed = 0;
    return 0;
}

/* 1 exits, negative values report errors. Keys come from the SDK's key
 * decoder (kos/key.h); all visible application output goes through libgfx. */
static int keyboard(key_code_t key)
{
    uint8_t old_view;
    if (key == '\n' && suppress_lf) { suppress_lf = 0; return 0; }
    suppress_lf = key == '\r';
    if (key == 17u) return 1; /* Ctrl-Q: application exit, never a story turn. */
    if (key == KEY_ESC) return close_menu();
    if (key == KEY_PGUP) return page(0);
    if (key == KEY_PGDN) return page(1);
    if (finished) return (key == '\r' || key == '\n') ? 1 : 0;
    if (close_menu() < 0) return -1;
    if (key == '\r' || key == '\n') return submit(panel.command);
    if (key == 8u || key == 127u) (void)panel_backspace(&panel);
    else if (key == KEY_DELETE) (void)panel_delete(&panel);
    else if (key == KEY_LEFT) { if (panel.cursor) --panel.cursor; }
    else if (key == KEY_RIGHT) { if (panel.cursor < panel.command_length) ++panel.cursor; }
    else if (key == KEY_HOME) panel.cursor = 0;
    else if (key == KEY_END) panel.cursor = panel.command_length;
    else if (key >= 32u && key < 127u) {
        old_view = panel.view;
        panel_latest(&panel);
        (void)panel_insert(&panel,(uint8_t)key);
        if (old_view != panel.view && render_panel(&panel,1) < 0) return -1;
    }
    return render_prompt(&panel,1);
}

/* Normal launch opens WOH.KPR in the inherited CWD. An optional archive
 * pathname follows the SDK convention: woh /games/kona/WOH.KPR. */
static int archive_location(int argc, char **argv)
{
    unsigned int length;
    const char *directory;
    if (!argc) {
        memcpy(story_path,"WOH.KPR",8u);
        return 0;
    }
    directory = argv[0];
    length = strlen(directory);
    if (!length || length >= sizeof(story_path)) return -1;
    memcpy(story_path,directory,length+1u);
    return 0;
}

int main(int argc, char **argv)
{
    int result = 0, handle;
    const char *error = 0;
    zvm_status_t status;
    key_code_t key;
    uint8_t vm_error;
    uint32_t pc;
    kos_reader_t reader;
    if (archive_location(argc,argv) < 0) {
        printf("woh: invalid resource archive path\r\n");
        return 1;
    }
    if (resources_open(story_path) < 0) { error = "cannot open resource archive"; goto done; }
    if (resources_story(&reader) < 0) { error = "cannot open archived story"; goto done; }
    if (render_open() < 0) { error = "graphics initialization failed"; goto done; }
    woh_probe.magic[0] = 'W'; woh_probe.magic[1] = 'O';
    woh_probe.magic[2] = 'H'; woh_probe.magic[3] = '0';
    if (render_title() < 0) { error = "title resource display failed"; goto done; }
    music_start();
    woh_probe.title = 1;
    for (;;) {
        if (key_pending() < 0) break;
        key = key_poll();
        if (key != KEY_NONE) break;
        music_service();
        input_wait();
    }
    idle_input_ready();
    music_pause();             /* the game screen and story load next */
    woh_probe.title = 0;
    if (render_game_screen() < 0) { error = "game screen initialization failed"; goto done; }
    panel_init(&panel);
    panel_begin(&panel,0);
    if (render_scene(0,0) < 0 || render_panel(&panel,0) < 0 || render_prompt(&panel,0) < 0) {
        error = "initial screen failed"; goto done;
    }
#if WOH_PROFILE
    woh_prof_timer = timer_open();
    {
        uint32_t t0 = woh_prof_now(), t1;
        result = game_open_reader(&game,&reader,&panel);
        t1 = woh_prof_now();
        prof_load_counts = t1 - t0;
    }
#else
    result = game_open_reader(&game,&reader,&panel);
#endif
    if (result < 0) { error = game_load_error(result); goto done; }
#if WOH_PROFILE
    prof_file = open("PROF.BIN",KFS_O_WRITE|KFS_O_CREATE|KFS_O_TRUNC);
#endif
    handle = mouse_open();
    if (handle > 0) mouse_init(&mouse,handle,256,212);
    woh_probe.output_hash = 5381ul;
    woh_probe.magic[0] = 'W'; woh_probe.magic[1] = 'O';
    woh_probe.magic[2] = 'H'; woh_probe.magic[3] = '0';
    for (;;) {
        if (!waiting) {
            music_pause();
            {
                PROF_START(t0);
                status = game_step(&game);
                PROF_ADD(prof_vm,t0);
            }
            if (status == ZVM_NEED_INPUT || status == ZVM_FINISHED) {
                game_output_finish(&panel);
                finished = status == ZVM_FINISHED;
                if (finished) {
                    const char *s = "\n[Story finished. Enter exits.]";
                    while (*s) panel_byte(&panel,(uint8_t)*s++);
                }
                if (game_observe(&game) < 0) { error = "global inspection failed"; break; }
                panel_finish(&panel);
                {
                    PROF_START(t1);
                    if (render_scene(game.here,game.state) < 0 || render_panel(&panel,0) < 0 ||
                        render_prompt(&panel,!finished) < 0) { error = "screen update failed"; break; }
                    PROF_ADD(prof_render,t1);
                }
#if WOH_PROFILE
                prof_row();
#endif
                waiting = 1;
                ++woh_probe.epoch;
            } else if (status != ZVM_YIELDED && status != ZVM_READY) {
                error = "unexpected VM status or story read failure"; result = (int)status; break;
            }
        }
        /* Empty input waits use SDK pacing. Each bounded VM slice returns
         * here too, so pointer movement and the music's feed do not wait
         * for the whole turn; the music is fed only while the game waits. */
        if (waiting) music_service();
        if (mouse_event() < 0) { error = "mouse rendering failed"; break; }
        probe();
        if (waiting) {
            result = key_pending();
            if (result < 0) { error = "keyboard read failed"; break; }
            key = key_poll();
            if (key == KEY_NONE) input_wait();
            else {
                idle_input_ready();
                result = keyboard(key);
                if (result > 0) { result = 0; break; }
                if (result < 0) { error = "command or UI update failed"; break; }
            }
        }
    }
done:
    music_stop();
#if WOH_PROFILE
    prof_write();
    printf("\rwoh: preload_bytes=%lu mapped_objects=%u load_counts=%lu map_remaps=%lu\r\n",
           (unsigned long)game.mapped.bytes, (unsigned)game.mapped.count,
           (unsigned long)prof_load_counts, (unsigned long)game.mapped.remaps);
    if (woh_prof_timer > 0) close((unsigned int)woh_prof_timer);
#endif
    vm_error = (uint8_t)zvm_error(&game.vm);
    pc = zvm_program_counter(&game.vm);
    game_close(&game);
    resources_close();
    if (mouse.handle > 0) close((unsigned int)mouse.handle);
    render_close();
    render_performance();
    if (error) {
        printf("\rwoh: %s (code %d, VM %u, PC %lu)\r\n",error,result,
               (unsigned)vm_error,(unsigned long)pc);
        printf("\rwoh: resource archive %s\r\n",story_path);
        printf("\rPress any key to return to shell.\r\n");
        /* Do not let the shell clear the diagnostic before it can be read. */
        for (;;) {
            if (key_pending() < 0) break;
            if (key_poll() != KEY_NONE) break;
            input_wait();
        }
        return 1;
    }
    printf("\rwoh: finished\r\n");
    return 0;
}
