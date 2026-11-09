#include "kos.h"
#include "kos/mouse.h"
#include "tui.h"
#include "game.h"
#include "render.h"
#include "probe.h"

/* Separate the large VM/cache object from controller BSS so the SDK can
 * place it above C000h without moving the controller's other data. */
extern game_t game;
static panel_t panel;
static menu_t menu;
static struct mouse mouse;
static char story_path[128];
woh_probe_t woh_probe;
static uint8_t waiting, finished, tui_opened, suppress_lf;

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
        } else if (!finished && fixture_hit((uint16_t)mouse.x,(uint16_t)mouse.y)) {
            fixture_menu(&menu);
            (void)menu_hover(&menu,(uint16_t)mouse.x,(uint16_t)mouse.y);
            if (render_menu(&menu) < 0) return -1;
        }
    }
    mouse.pressed = 0;
    return 0;
}

/* 1 exits, negative values report errors. TUI is used only for its existing
 * console key decoder; all visible application output goes through libgfx. */
static int keyboard(tui_key_t key)
{
    uint8_t old_view;
    if (key == '\n' && suppress_lf) { suppress_lf = 0; return 0; }
    suppress_lf = key == '\r';
    if (key == 17u) return 1; /* Ctrl-Q: application exit, never a story turn. */
    if (key == TUI_KEY_ESC) return close_menu();
    if (key == TUI_KEY_PGUP) return page(0);
    if (key == TUI_KEY_PGDN) return page(1);
    if (finished) return (key == '\r' || key == '\n') ? 1 : 0;
    if (close_menu() < 0) return -1;
    if (key == '\r' || key == '\n') return submit(panel.command);
    if (key == 8u || key == 127u) (void)panel_backspace(&panel);
    else if (key == TUI_KEY_DELETE) (void)panel_delete(&panel);
    else if (key == TUI_KEY_LEFT) { if (panel.cursor) --panel.cursor; }
    else if (key == TUI_KEY_RIGHT) { if (panel.cursor < panel.command_length) ++panel.cursor; }
    else if (key == TUI_KEY_HOME) panel.cursor = 0;
    else if (key == TUI_KEY_END) panel.cursor = panel.command_length;
    else if (key >= 32u && key < 127u) {
        old_view = panel.view;
        panel_latest(&panel);
        (void)panel_insert(&panel,(uint8_t)key);
        if (old_view != panel.view && render_panel(&panel,1) < 0) return -1;
    }
    return render_prompt(&panel,1);
}

/* Normal launch reads the story from the inherited CWD. An explicit directory
 * remains available for relocated stories and test fixtures. */
static int story_location(int argc, char **argv)
{
    unsigned int length;
    const char *directory;
    if (!argc) {
        memcpy(story_path,"zork1.z3",9u);
        return 0;
    }
    directory = argv[0];
    length = strlen(directory);
    if (!length || length > sizeof(story_path) - 11u) return -1;
    memcpy(story_path,directory,length);
    if (story_path[length-1u] != '/') story_path[length++] = '/';
    memcpy(story_path+length,"zork1.z3",9u);
    return 0;
}

int main(int argc, char **argv)
{
    int result = 0, handle;
    const char *error = 0;
    zvm_status_t status;
    tui_key_t key;
    uint8_t vm_error;
    uint32_t pc;
    if (story_location(argc,argv) < 0) {
        printf("woh: invalid story directory\r\n");
        return 1;
    }
    if (tui_init() < 0) { error = "keyboard initialization failed"; goto done; }
    tui_opened = 1;
    if (render_open() < 0) { error = "graphics initialization failed"; goto done; }
    panel_init(&panel);
    panel_begin(&panel,0);
    if (render_scene(0,0) < 0 || render_panel(&panel,0) < 0 || render_prompt(&panel,0) < 0) {
        error = "initial screen failed"; goto done;
    }
    result = game_open(&game,story_path,&panel);
    if (result < 0) { error = game_load_error(result); goto done; }
    handle = mouse_open();
    if (handle > 0) mouse_init(&mouse,handle,256,212);
    woh_probe.output_hash = 5381ul;
    woh_probe.magic[0] = 'W'; woh_probe.magic[1] = 'O';
    woh_probe.magic[2] = 'H'; woh_probe.magic[3] = '0';
    for (;;) {
        if (!waiting) {
            status = game_step(&game);
            if (status == ZVM_NEED_INPUT || status == ZVM_FINISHED) {
                game_output_finish(&panel);
                finished = status == ZVM_FINISHED;
                if (finished) {
                    const char *s = "\n[Story finished. Enter exits.]";
                    while (*s) panel_byte(&panel,(uint8_t)*s++);
                }
                if (game_observe(&game) < 0) { error = "global inspection failed"; break; }
                panel_finish(&panel);
                if (render_scene(game.here,game.state) < 0 || render_panel(&panel,0) < 0 ||
                    render_prompt(&panel,!finished) < 0) { error = "screen update failed"; break; }
                waiting = 1;
                ++woh_probe.epoch;
            } else if (status != ZVM_YIELDED && status != ZVM_READY) {
                error = "unexpected VM status or story read failure"; result = (int)status; break;
            }
        }
        /* Empty input waits use SDK pacing. Each bounded VM slice returns
         * here too, so pointer movement does not wait for the whole turn. */
        if (mouse_event() < 0) { error = "mouse rendering failed"; break; }
        probe();
        if (waiting) {
            result = tui_key_pending();
            if (result < 0) { error = "keyboard read failed"; break; }
            key = tui_pollkey();
            if (key == TUI_KEY_NONE) idle_input();
            else {
                idle_input_ready();
                result = keyboard(key);
                if (result > 0) { result = 0; break; }
                if (result < 0) { error = "command or UI update failed"; break; }
            }
        }
    }
done:
    vm_error = (uint8_t)zvm_error(&game.vm);
    pc = zvm_program_counter(&game.vm);
    game_close(&game);
    if (mouse.handle > 0) close((unsigned int)mouse.handle);
    render_close();
    if (tui_opened) tui_shutdown();
    render_performance();
    if (error) {
        printf("\rwoh: %s (code %d, VM %u, PC %lu)\r\n",error,result,
               (unsigned)vm_error,(unsigned long)pc);
        printf("\rwoh: story path %s\r\n",story_path);
        return 1;
    }
    printf("\rwoh: finished\r\n");
    return 0;
}
