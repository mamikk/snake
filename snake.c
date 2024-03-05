// snake.c

asm ("call dosmain");

// TODO:
// PC Speaker sounds
// Test on native windows
// rand function which doesnt do syscalls
// Animations? Sprites?
// Make sure .bss is not included in the .com-file
// RLE compress levels?

#define SCREEN_WIDTH    320
#define SCREEN_HEIGHT   200

#define GRID_WIDTH      32
#define GRID_HEIGHT     20

#define BLOCK_SIZE      10

#define MODE_GAME       0
#define MODE_DEAD       1
#define MODE_EXIT       2

#define MOVE_NONE       0
#define MOVE_UP         1
#define MOVE_LEFT       2
#define MOVE_RIGHT      3
#define MOVE_DOWN       4

#define SNAKE_START     3
#define SNAKE_FOOD      2
#define SNAKE_SUPERFOOD 6

#define OBJ_EMPTY       0
#define OBJ_WALL        1
#define OBJ_SNAKE       2
#define OBJ_SNAKEHEAD   3
#define OBJ_FOOD        4
#define OBJ_SUPERFOOD   5
#define OBJ_POISON      6

#define WANTED_FOOD     3
#define WANTED_POISON   2
#define SUPERFOOD_FREQ  5

#define ROTATE_TICKS    25

#define NUMLEVELS       10

// mode 13h palette
#define COLOR_BLACK     0
#define COLOR_BLUE      1
#define COLOR_GREEN     2
#define COLOR_CYAN      3
#define COLOR_RED       4
#define COLOR_MAGENTA   5
#define COLOR_ORANGE    6
#define COLOR_GREY      7
#define COLOR_WHITE     15

#define COLOR_EMPTY     COLOR_BLACK
#define COLOR_WALL      COLOR_WHITE
#define COLOR_SNAKE     COLOR_GREY
#define COLOR_SNAKEHEAD COLOR_ORANGE
#define COLOR_FOOD      COLOR_GREEN
#define COLOR_SUPERFOOD COLOR_MAGENTA
#define COLOR_POISON    COLOR_RED

static unsigned char game_mode;
static unsigned char pause;
static unsigned char move;
static unsigned char level;
static unsigned short thistick;
static unsigned short lasttick;
static unsigned short randomseed;
static short diry;
static short dirx;
static short numfood;
static short numpoison;
static short rotatecnt;
static unsigned char alldirty;
static unsigned short dirtylen;
static unsigned short snakelen;
static unsigned short wantedlen;
static unsigned char grid[GRID_WIDTH * GRID_HEIGHT];
static unsigned char snake[GRID_WIDTH * GRID_HEIGHT * 2];
static unsigned char objs[GRID_WIDTH * GRID_HEIGHT * 2];
static unsigned char dirtycells[GRID_WIDTH * GRID_HEIGHT * 2];
static const unsigned char leveldata[NUMLEVELS * ((GRID_WIDTH * GRID_HEIGHT) / 8)] = {
#include "leveldata.h"
};
static __seg_gs unsigned char *video_memory = 0;

/*
static inline void print(const char *string)
{
    unsigned short ax = (0x09 << 8) | 0x00;
    asm volatile (
        "int $0x21"
        : "+a"(ax)
        : "d"(string)
        : "memory"
    );
}
*/

static inline void __attribute__((noreturn)) terminate(void)
{
    unsigned short ax = (0x4c << 8) | 0x00;
    asm volatile (
        "int $0x21"
        : "+a"(ax)
        : 
        :
    );
    __builtin_unreachable();
}

static inline void set_video_mode(unsigned char mode)
{
    unsigned short ax = (0x00 << 8) | mode;
    asm volatile (
        "int $0x10"
        : "+a"(ax)
        : 
        :
    );
}

static inline unsigned char check_key(void)
{
    unsigned short ax = (0x01 << 8) | 0x00;
    unsigned char zeroflag;
    asm volatile (
        "int $0x16"
        : "+a"(ax), "=@ccz"(zeroflag)
        :
        :
    );
    return ! zeroflag;
}

static inline unsigned char get_key(void)
{
    unsigned short ax = (0x00 << 8) | 0x00;
    asm volatile (
        "int $0x16"
        : "+a"(ax)
        : 
        :
    );
    return (ax >> 8) & 0xff;
}

static inline unsigned short get_ticks(void)
{
    unsigned short ax = (0x00 << 8) | 0x00, cx, dx;
    asm volatile (
        "int $0x1a"
        : "+a"(ax), "=cx"(cx), "=dx"(dx)
        :
        :
    );
    return dx;
}

static inline short get_random(int n)
{
    randomseed = (randomseed << 5) + randomseed + get_ticks(); /* djb hash */
    return randomseed % n;
}

static inline void set_gs(unsigned short gs)
{
    unsigned short ax = gs;
    asm volatile (
        "mov %%ax, %%gs"
        : "+a"(ax)
        :
        :
    );
}

static void init()
{
    level = 0;
    randomseed = 5381; /* djb hash */
}

static void grid_set(short y, short x, unsigned char type)
{
    grid[(y * GRID_WIDTH) + x] = type;
    dirtycells[(dirtylen * 2) + 0] = y;
    dirtycells[(dirtylen * 2) + 1] = x;
    dirtylen++;
}

static void add_object(unsigned char type)
{
    short ry, rx;
    short yloop, xloop;
    short objy, objx;

    if (type == OBJ_SNAKEHEAD) {
        ry = GRID_HEIGHT / 2;
        rx = GRID_WIDTH / 2;
    } else {
        ry = get_random(GRID_HEIGHT);
        rx = get_random(GRID_WIDTH);
    }

    for (yloop = 0; yloop < GRID_HEIGHT; yloop++) {
        for (xloop = 0; xloop < GRID_WIDTH; xloop++) {
            objy = (ry + yloop) % GRID_HEIGHT;
            objx = (rx + xloop) % GRID_WIDTH;

            if (grid[(objy * GRID_WIDTH) + objx] == OBJ_EMPTY) {
                grid_set(objy, objx, type);

                if (type == OBJ_FOOD || type == OBJ_SUPERFOOD) {
                    objs[((numfood + numpoison) * 2) + 0] = objy;
                    objs[((numfood + numpoison) * 2) + 1] = objx;
                    numfood++;
                } else if (type == OBJ_POISON) {
                    objs[((numfood + numpoison) * 2) + 0] = objy;
                    objs[((numfood + numpoison) * 2) + 1] = objx;
                    numpoison++;
                } else if (type == OBJ_SNAKEHEAD) {
                    snake[0] = objy;
                    snake[1] = objx;
                    snakelen = 1;
                }

                return;
            }
        }
    }
}

static void remove_obj(short objy, short objx)
{
    short i, j;
    unsigned char type = grid[(objy * GRID_WIDTH) + objx];

    for (i = 0; i < (numfood + numpoison); i++) {
        if (objs[(i * 2) + 0] == objy && objs[(i * 2) + 1] == objx) {
            for (j = i; j < ((numfood + numpoison) - 1); j++) {
                objs[(j * 2) + 0] = objs[(j * 2) + 2];
                objs[(j * 2) + 1] = objs[(j * 2) + 3];
            }
            break;
        }
    }

    if (type == OBJ_FOOD || type == OBJ_SUPERFOOD) {
        numfood--;
    } else if (type == OBJ_POISON) {
        numpoison--;
    }

    grid_set(objy, objx, OBJ_EMPTY);
}

static void rotate_objects(void)
{
    short objy, objx;

    if ((numfood + numpoison) <= 0) return;

    objy = objs[0];
    objx = objs[1];

    remove_obj(objy, objx);
}

static void new_game(void)
{
    short y, x;

    game_mode = MODE_GAME;
    pause = 0;
    move = MOVE_NONE;
    thistick = 0;
    lasttick = 0;
    diry = 0;
    dirx = 0;
    numfood = 0;
    numpoison = 0;
    rotatecnt = 0;
    alldirty = 1;
    dirtylen = 0;
    snakelen = 0;
    wantedlen = SNAKE_START;
    //__builtin_memset(grid, '\0', sizeof(grid));
    for (y = 0; y < GRID_HEIGHT; y++) {
        for (x = 0; x < GRID_WIDTH; x++) {
            grid[(y * GRID_WIDTH) + x] = leveldata[(level * ((GRID_WIDTH * GRID_HEIGHT) / 8)) + (((y * GRID_WIDTH) + x) / 8)] & (1 << (7 - (x%8))) ? OBJ_WALL : OBJ_EMPTY;
        }
    }
    //__builtin_memset(snake, '\0', sizeof(snake));
    //__builtin_memset(dirtycells, '\0', sizeof(dirtycells));

    add_object(OBJ_SNAKEHEAD);
}

static void handle_input(void)
{
    unsigned char scancode;

    if (! check_key())
        return;

    scancode = get_key();
    switch (scancode) {
        case 0x01: // ESC
        case 0x10: // Q
            game_mode = MODE_EXIT;
            break;

        case 0x48: // Up
        case 0x11: // W
            move = MOVE_UP;
            break;
        case 0x4B: // Left
        case 0x1E: // A
            move = MOVE_LEFT;
            break;
        case 0x4D: // Right
        case 0x20: // D
            move = MOVE_RIGHT;
            break;
        case 0x50: // Down
        case 0x1F: // S
            move = MOVE_DOWN;
            break;

        case 0x19: // P (Pause)
            pause = ! pause;
            break;

        case 0x02: // 1 (Level 1)
        case 0x03: // 2 (Level 2)
        case 0x04: // 3 (Level 3)
        case 0x05: // 4 (Level 4)
        case 0x06: // 5 (Level 5)
        case 0x07: // 6 (Level 6)
        case 0x08: // 7 (Level 7)
        case 0x09: // 8 (Level 8)
        case 0x0A: // 9 (Level 9)
        case 0x0B: // 10 (Level 10)
            level = scancode - 0x02;
            /* fall through */

        case 0x13: // R (Restart)
            new_game();
            break;
    }
}

static void update_dir(void)
{
    switch (move) {
        case MOVE_UP:
            if (diry == 0) {
                diry = -1;
                dirx = 0;
            }
            break;
        case MOVE_LEFT:
            if (dirx == 0) {
                diry = 0;
                dirx = -1;
            }
            break;
        case MOVE_RIGHT:
            if (dirx == 0) {
                diry = 0;
                dirx = 1;
            }
            break;
        case MOVE_DOWN:
            if (diry == 0) {
                diry = 1;
                dirx = 0;
            }
            break;
    }

    move = MOVE_NONE;
}

static void move_snake(void)
{
    short heady = snake[((snakelen - 1) * 2) + 0];
    short headx = snake[((snakelen - 1) * 2) + 1];
    unsigned char type;

    heady = heady + diry;
    if (heady < 0) heady = GRID_HEIGHT - 1;
    else if (heady >= GRID_HEIGHT) heady = 0;

    headx = headx + dirx;
    if (headx < 0) headx = GRID_WIDTH - 1;
    else if (headx >= GRID_WIDTH) headx = 0;

    type = grid[(heady * GRID_WIDTH) + headx];

    switch (type) {
        case OBJ_WALL:
        case OBJ_SNAKE:
        case OBJ_SNAKEHEAD:
        case OBJ_POISON:
            game_mode = MODE_DEAD;
            break;
        case OBJ_FOOD:
        case OBJ_SUPERFOOD:
            /* eat food */
            remove_obj(heady, headx);
            wantedlen += (type == OBJ_SUPERFOOD ? SNAKE_SUPERFOOD : SNAKE_FOOD);
            /* fall through */

        case OBJ_EMPTY:
        default:
            /* move snake */
            if (snakelen < wantedlen) {
                snakelen++;
            } else {
                short taily = snake[0];
                short tailx = snake[1];
                short i;

                grid_set(taily, tailx, OBJ_EMPTY);

                if (snakelen >= 2) {
                    //__builtin_memmove(snake, snake + 2, snakelen - 1);
                    for (i = 0; i < (snakelen - 1); i++) {
                        snake[(i * 2) + 0] = snake[(i * 2) + 2];
                        snake[(i * 2) + 1] = snake[(i * 2) + 3];
                    }
                }
            }

            snake[((snakelen - 1) * 2) + 0] = heady;
            snake[((snakelen - 1) * 2) + 1] = headx;
            grid_set(heady, headx, OBJ_SNAKEHEAD);

            if (snakelen >= 2) {
                short bodyy = snake[((snakelen - 2) * 2) + 0];
                short bodyx = snake[((snakelen - 2) * 2) + 1];

                grid_set(bodyy, bodyx, OBJ_SNAKE);
            }

            break;
    }
}

static void update_game(void)
{
    if (game_mode != MODE_GAME) return;

    update_dir();

    /* move_snake */
    if (diry != 0 || dirx != 0) {
        move_snake();
    }

    if (game_mode != MODE_GAME) return;

    rotatecnt++;
    if (rotatecnt >= ROTATE_TICKS) {
        rotate_objects();
        rotatecnt = 0;
    }

    /* add food */
    while (numfood < WANTED_FOOD) {
        add_object(get_random(SUPERFOOD_FREQ) == 0 ? OBJ_SUPERFOOD : OBJ_FOOD);
    }

    /* add poison */
    while (numpoison < WANTED_POISON) {
        add_object(OBJ_POISON);
    }
}

static void draw_line(short y, short x, short yloop, short xstart, short xend, unsigned char color)
{
    short xloop;

    for (xloop = 0; xloop < BLOCK_SIZE; xloop++) {
        if (xloop >= xstart && xloop < xend) {
            video_memory[(((y * BLOCK_SIZE) + yloop) * SCREEN_WIDTH) + (x * BLOCK_SIZE) + xloop] = color;
        } else {
            video_memory[(((y * BLOCK_SIZE) + yloop) * SCREEN_WIDTH) + (x * BLOCK_SIZE) + xloop] = COLOR_EMPTY;
        }
    }
}

static void draw_block(short y, short x, unsigned char color)
{
    short yloop;

    for (yloop = 0; yloop < BLOCK_SIZE; yloop++) {
        if (yloop < (BLOCK_SIZE - 1)) {
            draw_line(y, x, yloop, 0, BLOCK_SIZE - 1, color);
        } else {
            draw_line(y, x, yloop, 0, 0, color);
        }
    }
}

static void draw_circle(short y, short x, unsigned char color)
{
    short yloop, xstart, xend;

    for (yloop = 0; yloop < BLOCK_SIZE; yloop++) {
        xstart = 0;
        xend = BLOCK_SIZE - 1;
        switch (yloop) {
            case 0:
            case 8:
                xstart = 3;
                xend = BLOCK_SIZE - 4;
                break;
            case 1:
            case 7:
                xstart = 2;
                xend = BLOCK_SIZE - 3;
                break;
            case 2:
            case 6:
                xstart = 1;
                xend = BLOCK_SIZE - 2;
                break;
            case 9:
                xstart = 0;
                xend = 0;
                break;
        }

        draw_line(y, x, yloop, xstart, xend, color);
    }
}

static void draw_snakehead(short y, short x, unsigned char color)
{
    short yloop, xstart, xend;

    for (yloop = 0; yloop < BLOCK_SIZE; yloop++) {
        if (yloop < (BLOCK_SIZE - 1)) {
            xstart = 0;
            xend = BLOCK_SIZE - 1;
        } else {
            xstart = 0;
            xend = 0;
        }

        if (diry == -1) { // Up
            switch (yloop) {
                case 0:
                    xstart = 4;
                    xend = BLOCK_SIZE - 5;
                    break;
                case 1:
                case 2:
                    xstart = 3;
                    xend = BLOCK_SIZE - 4;
                    break;
                case 3:
                case 4:
                    xstart = 2;
                    xend = BLOCK_SIZE - 3;
                    break;
                case 5:
                case 6:
                    xstart = 1;
                    xend = BLOCK_SIZE - 2;
                    break;
            }
        } else if (dirx == -1) { // Left
            switch (yloop) {
                case 0:
                case 8:
                    xstart = 7;
                    break;
                case 1:
                case 7:
                    xstart = 5;
                    break;
                case 2:
                case 6:
                    xstart = 3;
                    break;
                case 3:
                case 5:
                    xstart = 1;
                    break;
                case 4:
                    xstart = 0;
                    break;
            }
        } else if (dirx == 1) { // Right
            switch (yloop) {
                case 0:
                case 8:
                    xend = BLOCK_SIZE - 8;
                    break;
                case 1:
                case 7:
                    xend = BLOCK_SIZE - 6;
                    break;
                case 2:
                case 6:
                    xend = BLOCK_SIZE - 4;
                    break;
                case 3:
                case 5:
                    xend = BLOCK_SIZE - 2;
                    break;
                case 4:
                    xend = BLOCK_SIZE - 1;
                    break;
            }
        } else if (diry == 1) { // Down
            switch (yloop) {
                case 8:
                    xstart = 4;
                    xend = BLOCK_SIZE - 5;
                    break;
                case 7:
                case 6:
                    xstart = 3;
                    xend = BLOCK_SIZE - 4;
                    break;
                case 5:
                case 4:
                    xstart = 2;
                    xend = BLOCK_SIZE - 3;
                    break;
                case 3:
                case 2:
                    xstart = 1;
                    xend = BLOCK_SIZE - 2;
                    break;
            }
        }

        draw_line(y, x, yloop, xstart, xend, color);
    }
}

static void draw_obj(short y, short x)
{
    unsigned char type, color;

    type = grid[(y * GRID_WIDTH) + x];
    color = COLOR_EMPTY;

    switch (type) {
        case OBJ_WALL:
            color = COLOR_WALL;
            break;
        case OBJ_SNAKE:
            color = COLOR_SNAKE;
            break;
        case OBJ_SNAKEHEAD:
            color = COLOR_SNAKEHEAD;
            break;
        case OBJ_FOOD:
            color = COLOR_FOOD;
            break;
        case OBJ_SUPERFOOD:
            color = COLOR_SUPERFOOD;
            break;
        case OBJ_POISON:
            color = COLOR_POISON;
            break;
    }

    switch (type) {
        /* clear all */
        default:
            draw_block(y, x, color);
            break;

        /* box with 1px border */
        case OBJ_WALL:
        case OBJ_SNAKE:
            draw_block(y, x, color);
            break;

        case OBJ_SNAKEHEAD:
            draw_snakehead(y, x, color);
            break;

        /* circle */
        case OBJ_FOOD:
        case OBJ_SUPERFOOD:
        case OBJ_POISON:
            draw_circle(y, x, color);
            break;
    }

}

static void draw_game(void)
{
    short y, x;
    short i;

    if (alldirty) {
        for (y = 0; y < GRID_HEIGHT; y++) {
            for (x = 0; x < GRID_WIDTH; x++) {
                draw_obj(y, x);
            }
        }
    } else {
        for (i = 0; i < dirtylen; i++) {
            y = dirtycells[(i * 2) + 0];
            x = dirtycells[(i * 2) + 1];

            draw_obj(y, x);
        }
    }

    alldirty = 0;
    dirtylen = 0;
}

void __attribute__((noreturn)) dosmain(void)
{
    set_video_mode(0x13); /* mode 13h (320x200) */
    set_gs(0xa000); /* video memory */
    init();
    new_game();
    while (game_mode != MODE_EXIT) {
        handle_input();
        if (game_mode == MODE_EXIT) break;
        thistick = get_ticks() >> 1;
        if (thistick != lasttick) {
            lasttick = thistick;
            if (! pause) {
                update_game();
            }
        }
        draw_game();
    }
    set_video_mode(0x03); /* textmode */
    terminate();
}

