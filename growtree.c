#define _POSIX_C_SOURCE 200809L
#include <locale.h>
#include <math.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define FPS 30
#define MAX_PARTICLES 140
#define MAX_LEAVES 220
#define PI 3.14159265358979323846

#define CP_SKY1 1
#define CP_SKY2 2
#define CP_GROUND1 3
#define CP_GROUND2 4
#define CP_TRUNK 5
#define CP_TRUNK_HI 6
#define CP_LEAF_DARK 7
#define CP_LEAF_MID 8
#define CP_LEAF_LIGHT 9
#define CP_HAND 10
#define CP_HAND_SHADE 11
#define CP_SEED 12
#define CP_GLOW 13
#define CP_TEXT 14
#define CP_SOIL 15
#define CP_STAR 16
#define CP_SLEEVE 17
#define CP_SLEEVE_SHADE 18
#define CP_ROOT 19
#define CP_HILL 20

#define GLYPH_LIGHT "█"
#define GLYPH_MED "▓"
#define GLYPH_SOFT "▒"
#define GLYPH_FAINT "░"
#define GLYPH_SEED "●"
#define GLYPH_STAR "·"
#define GLYPH_SOIL "▄"
#define GLYPH_BRANCH "┃"
#define GLYPH_BRANCH2 "│"
#define GLYPH_HILITE "▀"
#define GLYPH_LEAF "●"
#define GLYPH_SPARK "•"

static int W, H;
static float seed_x, ground_y;
static bool wind_on = true;
static bool use_unicode = true;

struct Particle {
    float angle;
    float radius;
    float lift;
    float speed;
    float size;
};

struct LeafDrift {
    float phase;
    float sway;
    float speed;
    float yoff;
};

static struct Particle particles[MAX_PARTICLES];

/* typewriter state */
static const char MSG1[] = "Every tree was once a seed someone believed in.";
static const char MSG2[] = "Put it in the ground. Let the earth do the rest.";
static float type_timer  = 0.f;
static int   type_pos1   = 0;
static int   type_pos2   = 0;
static bool  msg1_done   = false;
static bool  msg2_done   = false;
static float cursor_blink = 0.f;
static struct LeafDrift leafdrift[MAX_LEAVES];

static float clampf(float v, float a, float b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

static float lerpf(float a, float b, float t) {
    return a + (b - a) * t;
}

static float ease_in(float t) {
    t = clampf(t, 0.f, 1.f);
    return t * t * t;
}

static float ease_out(float t) {
    t = clampf(t, 0.f, 1.f);
    float u = 1.f - t;
    return 1.f - u * u * u;
}

static float ease_in_out(float t) {
    t = clampf(t, 0.f, 1.f);
    return (t < 0.5f) ? 4.f * t * t * t : 1.f - powf(-2.f * t + 2.f, 3.f) / 2.f;
}

static float smoothstepf(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

static int iroundf(float x) {
    return (int)lroundf(x);
}

static const char *shade_glyph(float v) {
    if (!use_unicode) {
        if (v > 0.80f) return "@";
        if (v > 0.60f) return "#";
        if (v > 0.40f) return "*";
        if (v > 0.22f) return "+";
        if (v > 0.10f) return ".";
        return " ";
    }
    if (v > 0.82f) return GLYPH_LIGHT;
    if (v > 0.58f) return GLYPH_MED;
    if (v > 0.30f) return GLYPH_SOFT;
    if (v > 0.12f) return GLYPH_FAINT;
    return " ";
}

static void put_cell(int y, int x, int pair, const char *g) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    attron(COLOR_PAIR(pair));
    mvaddstr(y, x, g);
    attroff(COLOR_PAIR(pair));
}

static void put_text(int y, int x, int pair, const char *s) __attribute__((unused));
static void put_text(int y, int x, int pair, const char *s) {
    if (y < 0 || y >= H || x >= W) return;
    if (x < 0) {
        int skip = -x;
        int len = (int)strlen(s);
        if (skip >= len) return;
        s += skip;
        x = 0;
    }
    attron(COLOR_PAIR(pair));
    mvaddstr(y, x, s);
    attroff(COLOR_PAIR(pair));
}

static void init_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        particles[i].angle = (float)i * 0.37f;
        particles[i].radius = 1.5f + (float)(i % 9) * 0.55f;
        particles[i].lift = 0.3f + (float)(i % 5) * 0.35f;
        particles[i].speed = 0.65f + (float)(i % 7) * 0.07f;
        particles[i].size = 1.f + (float)(i % 3) * 0.15f;
    }
    for (int i = 0; i < MAX_LEAVES; ++i) {
        leafdrift[i].phase = (float)i * 0.53f;
        leafdrift[i].sway = 0.8f + (float)(i % 6) * 0.35f;
        leafdrift[i].speed = 0.7f + (float)(i % 4) * 0.08f;
        leafdrift[i].yoff = -1.8f + (float)(i % 7) * 0.35f;
    }
}

static void init_colors_custom(void) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();
    if (can_change_color() && COLORS >= 256) {
        init_color(101,  70, 130, 210);
        init_color(102, 120, 220, 330);
        init_color(103, 220, 340, 180);
        init_color(104, 340, 240, 140);
        init_color(105, 420, 270, 120);
        init_color(106, 560, 380, 200);
        init_color(107, 110, 350, 110);
        init_color(108, 240, 470, 180);
        init_color(109, 430, 650, 250);
        init_color(110, 830, 650, 520);
        init_color(111, 640, 450, 340);
        init_color(112, 930, 820, 300);
        init_color(113, 980, 910, 560);
        init_color(114, 820, 820, 760);
        init_color(115, 260, 170, 110);
        init_color(116, 900, 900, 900);
        init_color(117, 240, 390, 520);
        init_color(118, 140, 220, 340);
        init_color(119, 360, 260, 170);
        init_color(120, 180, 280, 160);
        init_pair(CP_SKY1, 101, -1);
        init_pair(CP_SKY2, 102, -1);
        init_pair(CP_GROUND1, 103, -1);
        init_pair(CP_GROUND2, 104, -1);
        init_pair(CP_TRUNK, 105, -1);
        init_pair(CP_TRUNK_HI, 106, -1);
        init_pair(CP_LEAF_DARK, 107, -1);
        init_pair(CP_LEAF_MID, 108, -1);
        init_pair(CP_LEAF_LIGHT, 109, -1);
        init_pair(CP_HAND, 110, -1);
        init_pair(CP_HAND_SHADE, 111, -1);
        init_pair(CP_SEED, 112, -1);
        init_pair(CP_GLOW, 113, -1);
        init_pair(CP_TEXT, 114, -1);
        init_pair(CP_SOIL, 115, -1);
        init_pair(CP_STAR, 116, -1);
        init_pair(CP_SLEEVE, 117, -1);
        init_pair(CP_SLEEVE_SHADE, 118, -1);
        init_pair(CP_ROOT, 119, -1);
        init_pair(CP_HILL, 120, -1);
    } else {
        init_pair(CP_SKY1, COLOR_BLUE, -1);
        init_pair(CP_SKY2, COLOR_CYAN, -1);
        init_pair(CP_GROUND1, COLOR_GREEN, -1);
        init_pair(CP_GROUND2, COLOR_YELLOW, -1);
        init_pair(CP_TRUNK, COLOR_YELLOW, -1);
        init_pair(CP_TRUNK_HI, COLOR_WHITE, -1);
        init_pair(CP_LEAF_DARK, COLOR_GREEN, -1);
        init_pair(CP_LEAF_MID, COLOR_GREEN, -1);
        init_pair(CP_LEAF_LIGHT, COLOR_CYAN, -1);
        init_pair(CP_HAND, COLOR_YELLOW, -1);
        init_pair(CP_HAND_SHADE, COLOR_RED, -1);
        init_pair(CP_SEED, COLOR_YELLOW, -1);
        init_pair(CP_GLOW, COLOR_WHITE, -1);
        init_pair(CP_TEXT, COLOR_WHITE, -1);
        init_pair(CP_SOIL, COLOR_RED, -1);
        init_pair(CP_STAR, COLOR_WHITE, -1);
        init_pair(CP_SLEEVE, COLOR_BLUE, -1);
        init_pair(CP_SLEEVE_SHADE, COLOR_BLUE, -1);
        init_pair(CP_ROOT, COLOR_YELLOW, -1);
        init_pair(CP_HILL, COLOR_GREEN, -1);
    }
}

static void draw_sky(float t) {
    for (int y = 0; y < H; ++y) {
        float v = 1.f - (float)y / (float)H;
        int pair = (v > 0.55f) ? CP_SKY2 : CP_SKY1;
        for (int x = 0; x < W; ++x) {
            float glow = 0.0f;
            float gx = W * 0.72f + sinf(t * 0.15f) * 2.0f;
            float gy = H * 0.18f;
            float dx = (x - gx) / (W * 0.18f);
            float dy = (y - gy) / (H * 0.20f);
            float d = sqrtf(dx * dx + dy * dy);
            glow = clampf(1.0f - d, 0.f, 1.f) * 0.85f;
            float star_band = (y < H * 0.48f) ? 1.f : 0.f;
            float noise = 0.5f + 0.5f * sinf(x * 0.21f + y * 0.13f + t * 0.05f);
            float val = 0.04f + glow * 0.75f + star_band * noise * 0.08f;
            put_cell(y, x, pair, shade_glyph(val));
        }
    }

    for (int i = 0; i < W / 3; ++i) {
        int sx = (i * 11 + 7) % W;
        int sy = (i * 7 + 3) % (H > 6 ? H / 2 : 1);
        if (((i * 13) % 9) < 3) put_cell(sy, sx, CP_STAR, GLYPH_STAR);
    }
}

static void draw_hills(void) {
    for (int x = 0; x < W; ++x) {
        float xf = (float)x / (float)(W - 1);
        int y1 = iroundf(H * 0.58f + sinf(xf * 7.0f) * 1.5f + cosf(xf * 12.0f) * 1.0f);
        int y2 = iroundf(H * 0.68f + sinf(xf * 5.1f + 0.7f) * 1.2f);
        for (int y = y1; y < y2; ++y) put_cell(y, x, CP_HILL, shade_glyph(0.22f));
    }
}

static void draw_ground(void) {
    for (int x = 0; x < W; ++x) {
        float xf = (float)x / (float)(W - 1);
        int gy = iroundf(ground_y + sinf(xf * 6.5f) * 0.9f + cosf(xf * 11.0f) * 0.6f);
        for (int y = gy; y < H; ++y) {
            float depth = (float)(y - gy) / (float)(H - gy + 1);
            int pair = (depth < 0.35f) ? CP_GROUND1 : CP_GROUND2;
            float grain = 0.30f + 0.28f * sinf(x * 0.18f + y * 0.31f);
            put_cell(y, x, pair, shade_glyph(clampf(0.25f + grain * 0.4f + depth * 0.2f, 0.f, 1.f)));
        }
        if (gy >= 0 && gy < H) put_cell(gy, x, CP_GROUND1, GLYPH_SOIL);
    }
}

static void draw_seed(float x, float y, float vis, float pulse) {
    if (vis <= 0.01f) return;
    int ix = iroundf(x);
    int iy = iroundf(y);
    if (vis > 0.15f) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                float d = sqrtf((float)(dx * dx + dy * dy));
                float a = clampf(1.0f - d / (2.6f + pulse), 0.f, 1.f) * vis;
                if (a > 0.18f) put_cell(iy + dy, ix + dx, CP_GLOW, shade_glyph(a * 0.7f));
            }
        }
    }
    attron(COLOR_PAIR(CP_SEED) | A_BOLD);
    mvaddstr(iy, ix - 1, "(o)");
    attroff(COLOR_PAIR(CP_SEED) | A_BOLD);
    attron(COLOR_PAIR(CP_GLOW));
    mvaddstr(iy + 1, ix - 3, "~ seed ~");
    attroff(COLOR_PAIR(CP_GLOW));
}

static void draw_soil_open(float x, float open) {
    if (open <= 0.01f) return;
    int cx = iroundf(x);
    int w = 2 + iroundf(open * 4.0f);
    int y = iroundf(ground_y);
    for (int dx = -w; dx <= w; ++dx) {
        put_cell(y, cx + dx, CP_SOIL, GLYPH_SOIL);
        if (fabsf((float)dx) < open * 2.5f) put_cell(y + 1, cx + dx, CP_SOIL, shade_glyph(0.5f));
    }
}

/* ── Large ASCII hand frames ────────────────────────────────────────────────
   Each frame: 11 rows, drawn relative to (ix, iy) = palm centre.
   open 0.0 = closed fist  0.3 = fingers parting  0.7 = flat palm  1.0 = release tilt
   Row offsets: -8 .. +2 from iy (fingers above, sleeve below)
*/

/* colours for each character type in the art */
static void hand_char(int row, int col, char c, int ix, int iy) {
    int y = iy + row;
    int x = ix + col;
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    switch (c) {
        case '#': case '@':
            attron(COLOR_PAIR(CP_HAND));
            mvaddch(y, x, '#');
            attroff(COLOR_PAIR(CP_HAND));
            break;
        case 's': /* sleeve */
            attron(COLOR_PAIR(CP_SLEEVE));
            mvaddch(y, x, '=');
            attroff(COLOR_PAIR(CP_SLEEVE));
            break;
        case 'S': /* sleeve shade */
            attron(COLOR_PAIR(CP_SLEEVE_SHADE));
            mvaddch(y, x, '=');
            attroff(COLOR_PAIR(CP_SLEEVE_SHADE));
            break;
        case 'h': /* hand highlight */
            attron(COLOR_PAIR(CP_HAND) | A_BOLD);
            mvaddch(y, x, '/');
            attroff(COLOR_PAIR(CP_HAND) | A_BOLD);
            break;
        case 'H': /* hand shade */
            attron(COLOR_PAIR(CP_HAND_SHADE));
            mvaddch(y, x, '#');
            attroff(COLOR_PAIR(CP_HAND_SHADE));
            break;
        case 'o': /* seed */
            attron(COLOR_PAIR(CP_SEED) | A_BOLD);
            mvaddstr(y, x, "o");
            attroff(COLOR_PAIR(CP_SEED) | A_BOLD);
            break;
        case '*': /* glow */
            attron(COLOR_PAIR(CP_GLOW));
            mvaddch(y, x, '*');
            attroff(COLOR_PAIR(CP_GLOW));
            break;
        case '|': case '/': case '\\': case '_': case '(':  case ')':
            attron(COLOR_PAIR(CP_HAND));
            mvaddch(y, x, c);
            attroff(COLOR_PAIR(CP_HAND));
            break;
        default:
            break;  /* space = skip */
    }
}

static void draw_row(int row, const char *line, int ix, int iy) {
    for (int i = 0; line[i]; ++i)
        hand_char(row, i, line[i], ix, iy);
}

static void draw_hand(float x, float y, float open) {
    int ix = iroundf(x);
    int iy = iroundf(y);

    /* ── SLEEVE (always visible) ─────────────────────────── */
    /* sleeve body */
    draw_row(+1, "SSSSSSSSSSSSSSSSSSSSSSSS",                 ix - 22, iy);
    draw_row( 0, "ssssssssssssssssssssssss",                 ix - 22, iy);
    draw_row(-1, "  sssssssssssssssssssss",                  ix - 22, iy);
    /* wrist connect */
    draw_row(-1, "(############################)",           ix - 2, iy);
    draw_row( 0, "(############################)",           ix - 2, iy);

    if (open < 0.15f) {
        /* ── FRAME 0 : closed fist ─────────────────────── */
        draw_row(-6, "   ___  ___  ___  ___    ",  ix, iy);
        draw_row(-5, "  |   ||   ||   ||   |   ",  ix, iy);
        draw_row(-4, "  |   ||   ||   ||   |   ",  ix, iy);
        draw_row(-3, "  |___|____|___|___|      ",  ix, iy);
        draw_row(-2, " /##########################\\", ix - 1, iy);
        draw_row(-1, "|############################|", ix - 1, iy);

    } else if (open < 0.45f) {
        /* ── FRAME 1 : fingers parting ─────────────────── */
        float t = (open - 0.15f) / 0.30f; /* 0..1 */
        int spread = (int)(t * 2);
        draw_row(-7, "  |   |   |   |   |  ",         ix, iy);
        draw_row(-6, "  |   |   |   |   |  ",         ix, iy);
        draw_row(-5, "  |   |   |   |   |  ",         ix, iy);
        draw_row(-4, "  |___|___|___|___|  ",          ix + spread, iy);
        draw_row(-3, " /#######################\\",   ix - 1, iy);
        draw_row(-2, "|#########################|",    ix - 1, iy);
        (void)spread;

    } else if (open < 0.80f) {
        /* ── FRAME 2 : palm open flat, seed glowing ────── */
        draw_row(-8, "  |    |    |    |    |  ",  ix, iy);
        draw_row(-7, "  |    |    |    |    |  ",  ix, iy);
        draw_row(-6, "  |    |    |    |    |  ",  ix, iy);
        draw_row(-5, "  |    |    |    |    |  ",  ix, iy);
        draw_row(-4, "   \\____\\____\\____\\____/ ",  ix, iy);
        draw_row(-3, "  /##########################\\", ix - 1, iy);
        draw_row(-2, " |   *     o     *   |",      ix + 3, iy);
        draw_row(-2, "|############################--|", ix - 1, iy);
        /* seed glow ring on palm */
        attron(COLOR_PAIR(CP_GLOW));
        mvaddstr(iy - 2, ix + 9, "* o *");
        attroff(COLOR_PAIR(CP_GLOW));
        attron(COLOR_PAIR(CP_SEED) | A_BOLD);
        mvaddstr(iy - 2, ix + 11, "o");
        attroff(COLOR_PAIR(CP_SEED) | A_BOLD);

    } else {
        /* ── FRAME 3 : tilting to release ──────────────── */
        draw_row(-8, "   |    |    |    |  ",   ix + 2, iy);
        draw_row(-7, "   |    |    |    |  ",   ix + 2, iy);
        draw_row(-6, "   |    |    |    |  ",   ix + 2, iy);
        draw_row(-5, "    \\____\\____\\____/ ",   ix + 2, iy);
        draw_row(-4, "   /######################\\", ix, iy);
        draw_row(-3, "  |######################/ ",  ix, iy);
        draw_row(-2, "  |######################/  ", ix, iy);
        /* seed about to drop */
        attron(COLOR_PAIR(CP_SEED) | A_BOLD);
        mvaddstr(iy - 2, ix + 16, "o");
        attroff(COLOR_PAIR(CP_SEED) | A_BOLD);
    }
}

static void draw_stem(float x, float base_y, float h, float sway) {
    int steps = iroundf(h);
    for (int i = 0; i < steps; ++i) {
        float k = (float)i / (float)(steps + 1);
        int px = iroundf(x + sinf(k * 2.8f + sway) * 1.2f);
        int py = iroundf(base_y - i);
        put_cell(py, px, CP_LEAF_MID, GLYPH_BRANCH2);
        if (i % 3 == 0) put_cell(py, px + 1, CP_LEAF_LIGHT, GLYPH_HILITE);
    }
}

static void draw_leaf_blob(float cx, float cy, float rx, float ry, int pair, float phase) {
    int minx = iroundf(cx - rx - 1);
    int maxx = iroundf(cx + rx + 1);
    int miny = iroundf(cy - ry - 1);
    int maxy = iroundf(cy + ry + 1);
    for (int y = miny; y <= maxy; ++y) {
        for (int x = minx; x <= maxx; ++x) {
            float dx = (x - cx) / (rx <= 0.1f ? 1.f : rx);
            float dy = (y - cy) / (ry <= 0.1f ? 1.f : ry);
            float d = dx * dx + dy * dy;
            float warp = 0.10f * sinf((x + y) * 0.7f + phase);
            if (d - warp <= 1.0f) {
                float shade = clampf(1.0f - d * 0.7f, 0.f, 1.f);
                put_cell(y, x, pair, shade_glyph(0.35f + shade * 0.65f));
            }
        }
    }
}

static void draw_leaf_cluster(float x, float y, float scale, float sway, float t) {
    draw_leaf_blob(x - 1.5f + sway * 0.2f, y + 0.0f, 2.4f * scale, 1.4f * scale, CP_LEAF_DARK, t);
    draw_leaf_blob(x + 1.8f + sway * 0.35f, y - 0.5f, 2.2f * scale, 1.3f * scale, CP_LEAF_MID, t + 0.7f);
    draw_leaf_blob(x + 0.0f + sway * 0.25f, y - 1.1f, 2.0f * scale, 1.2f * scale, CP_LEAF_LIGHT, t + 1.4f);
}

static void draw_branch(float x1, float y1, float len, float angle, float thick, int depth, float sway, float t) {
    if (depth <= 0 || len < 1.5f) return;
    float x2 = x1 + cosf(angle + sway) * len;
    float y2 = y1 - sinf(angle + sway) * len;

    int steps = iroundf(len * 1.6f);
    for (int i = 0; i <= steps; ++i) {
        float k = (float)i / (float)(steps == 0 ? 1 : steps);
        float bx = lerpf(x1, x2, k) + sinf(k * PI) * 0.4f * sinf(angle);
        float by = lerpf(y1, y2, k);
        int width = (int)fmaxf(1.f, thick * (1.f - k * 0.65f));
        for (int w = -width/2; w <= width/2; ++w) {
            put_cell(iroundf(by), iroundf(bx) + w, CP_TRUNK, width > 1 ? GLYPH_BRANCH : GLYPH_BRANCH2);
        }
        if (i % 3 == 0) put_cell(iroundf(by) - 1, iroundf(bx), CP_TRUNK_HI, GLYPH_HILITE);
    }

    if (depth == 1) {
        draw_leaf_cluster(x2, y2, fmaxf(0.8f, thick * 0.5f), sway * 8.f, t);
        return;
    }

    draw_branch(x2, y2, len * 0.72f, angle + 0.72f, thick * 0.72f, depth - 1, sway * 1.15f, t + 0.4f);
    draw_branch(x2, y2, len * 0.68f, angle - 0.66f, thick * 0.68f, depth - 1, sway * 1.10f, t + 0.9f);
    if (depth >= 3) draw_branch(x2, y2, len * 0.52f, angle + 0.10f, thick * 0.54f, depth - 2, sway * 0.8f, t + 1.3f);
}

static void draw_tree(float growth, float crown, float t) {
    if (growth <= 0.01f) return;
    float sway = wind_on ? (sinf(t * 1.2f) * 0.12f + sinf(t * 2.1f) * 0.05f) : 0.f;
    float trunk_h = lerpf(3.f, H * 0.34f, ease_out(growth));
    float trunk_w = lerpf(1.f, 3.8f, ease_out(growth));
    float top_x = seed_x + sway * 3.5f;
    float top_y = ground_y - trunk_h;

    int steps = iroundf(trunk_h * 1.25f);
    for (int i = 0; i <= steps; ++i) {
        float k = (float)i / (float)(steps == 0 ? 1 : steps);
        float bx = lerpf(seed_x, top_x, k) + sinf(k * 2.6f) * 0.55f;
        float by = lerpf(ground_y, top_y, k);
        int width = (int)fmaxf(1.f, trunk_w * (1.f - k * 0.58f));
        for (int w = -width; w <= width; ++w) {
            put_cell(iroundf(by), iroundf(bx) + w, CP_TRUNK, width > 1 ? GLYPH_BRANCH : GLYPH_BRANCH2);
        }
        if (i % 3 == 0) put_cell(iroundf(by) - 1, iroundf(bx), CP_TRUNK_HI, GLYPH_HILITE);
    }

    float root_spread = 2.f + growth * 5.f;
    for (int r = 1; r <= (int)root_spread; ++r) {
        put_cell(iroundf(ground_y) + 1, iroundf(seed_x) - r, CP_ROOT, GLYPH_BRANCH2);
        put_cell(iroundf(ground_y) + 1, iroundf(seed_x) + r, CP_ROOT, GLYPH_BRANCH2);
    }

    if (crown > 0.02f) {
        float len = lerpf(3.f, H * 0.12f, ease_out(crown));
        draw_branch(top_x, top_y + 1.f, len, PI / 2.f - 0.95f, trunk_w * 0.95f, 4, sway, t);
        draw_branch(top_x, top_y + 2.f, len * 0.95f, PI / 2.f + 0.90f, trunk_w * 0.90f, 4, sway, t + 0.6f);
        draw_branch(top_x, top_y + 1.f, len * 0.82f, PI / 2.f, trunk_w * 0.78f, 3, sway * 0.8f, t + 1.1f);

        float canopy = ease_out(crown);
        draw_leaf_blob(top_x - 7 + sway * 2.0f, top_y + 2, 7.0f * canopy, 3.2f * canopy, CP_LEAF_DARK, t);
        draw_leaf_blob(top_x + 7 + sway * 2.4f, top_y + 1, 6.5f * canopy, 3.0f * canopy, CP_LEAF_MID, t + 0.5f);
        draw_leaf_blob(top_x + 0 + sway * 2.8f, top_y - 3, 8.0f * canopy, 3.5f * canopy, CP_LEAF_LIGHT, t + 1.0f);
        draw_leaf_blob(top_x - 3 + sway * 1.8f, top_y - 5, 5.2f * canopy, 2.5f * canopy, CP_LEAF_MID, t + 1.7f);
        draw_leaf_blob(top_x + 4 + sway * 2.2f, top_y - 5, 4.8f * canopy, 2.2f * canopy, CP_LEAF_DARK, t + 2.3f);

        for (int i = 0; i < 16; ++i) {
            float lx = top_x + sinf(t * leafdrift[i].speed + leafdrift[i].phase) * (6.5f + leafdrift[i].sway);
            float ly = top_y - 2.0f + cosf(t * 0.8f + leafdrift[i].phase) * 1.5f + leafdrift[i].yoff;
            put_cell(iroundf(ly), iroundf(lx), CP_LEAF_LIGHT, GLYPH_LEAF);
        }
    }
}

static void draw_sprout(float p, float t) {
    if (p <= 0.01f) return;
    float sway = wind_on ? sinf(t * 1.8f) * 0.25f : 0.f;
    float stem_h = lerpf(1.f, H * 0.12f, ease_out(p));
    draw_stem(seed_x, ground_y, stem_h, sway);

    if (p > 0.24f) {
        float leafp = smoothstepf(0.24f, 1.f, p);
        draw_leaf_cluster(seed_x - 2.f, ground_y - stem_h * 0.55f, leafp, sway, t);
        draw_leaf_cluster(seed_x + 2.f, ground_y - stem_h * 0.78f, leafp * 0.9f, sway, t + 0.9f);
    }

    for (int i = 0; i < MAX_PARTICLES / 5; ++i) {
        float a = t * particles[i].speed + particles[i].angle;
        float px = seed_x + cosf(a) * (particles[i].radius + p * 1.2f);
        float py = ground_y - 1.0f - sinf(a * 1.3f) * particles[i].lift;
        put_cell(iroundf(py), iroundf(px), CP_GLOW, GLYPH_SPARK);
    }
}

static void draw_ui(float elapsed, const char *phase, float dt) {
    /* ── top bar ── */
    attron(COLOR_PAIR(CP_TEXT) | A_BOLD);
    mvaddstr(1, 2, "seed-tree.c  |  terminal sowing animation");
    attroff(COLOR_PAIR(CP_TEXT) | A_BOLD);
    attron(COLOR_PAIR(CP_TEXT));
    mvprintw(2, 2, "> sow.seed --style organic --target tree");
    mvprintw(3, 2, "phase: %-18s  time: %4.1fs  wind:%s  replay:r  quit:q",
             phase, elapsed, wind_on ? "on " : "off");
    attroff(COLOR_PAIR(CP_TEXT));

    /* ── typewriter message — appears when crown blooms (elapsed > 10s) ── */
    float msg_start = 10.5f;
    if (elapsed < msg_start) return;

    /* advance typewriter */
    cursor_blink += dt * 3.2f;
    if (cursor_blink > 2.f * 3.14159f) cursor_blink -= 2.f * 3.14159f;

    float speed = 0.058f;   /* seconds per character */
    type_timer += dt;
    if (!msg1_done) {
        int target = (int)(type_timer / speed);
        int maxlen = (int)(sizeof(MSG1) - 1);
        type_pos1 = target < maxlen ? target : maxlen;
        if (type_pos1 >= maxlen) { msg1_done = true; type_timer = 0.f; }
    } else if (!msg2_done) {
        int target = (int)(type_timer / (speed * 1.25f));
        int maxlen = (int)(sizeof(MSG2) - 1);
        type_pos2 = target < maxlen ? target : maxlen;
        if (type_pos2 >= maxlen) msg2_done = true;
    }

    /* ── box dimensions ── */
    int msg_len  = (int)(sizeof(MSG1) - 1);
    int box_w    = msg_len + 4;
    int box_x    = (W - box_w) / 2;
    int box_y    = H - 7;
    if (box_y < 5) box_y = 5;

    /* ── decorative border ── */
    attron(COLOR_PAIR(CP_LEAF_LIGHT) | A_BOLD);
    /* top border */
    mvaddch(box_y - 1, box_x, '+');
    for (int i = 1; i < box_w - 1; ++i) mvaddch(box_y - 1, box_x + i, '-');
    mvaddch(box_y - 1, box_x + box_w - 1, '+');
    /* side borders */
    for (int r = 0; r < 3; ++r) {
        mvaddch(box_y + r, box_x, '|');
        mvaddch(box_y + r, box_x + box_w - 1, '|');
    }
    /* bottom border */
    mvaddch(box_y + 3, box_x, '+');
    for (int i = 1; i < box_w - 1; ++i) mvaddch(box_y + 3, box_x + i, '-');
    mvaddch(box_y + 3, box_x + box_w - 1, '+');

    /* leaf corners */
    mvaddch(box_y - 1, box_x,              '*');
    mvaddch(box_y - 1, box_x + box_w - 1, '*');
    mvaddch(box_y + 3, box_x,              '*');
    mvaddch(box_y + 3, box_x + box_w - 1, '*');
    attroff(COLOR_PAIR(CP_LEAF_LIGHT) | A_BOLD);

    /* fill interior */
    attron(COLOR_PAIR(CP_TEXT));
    for (int r = 0; r < 3; ++r)
        for (int c = 1; c < box_w - 1; ++c)
            mvaddch(box_y + r, box_x + c, ' ');
    attroff(COLOR_PAIR(CP_TEXT));

    /* ── message line 1 ── */
    char buf1[64] = {0};
    if (type_pos1 > 0) {
        int n = type_pos1 < (int)(sizeof(MSG1)-1) ? type_pos1 : (int)(sizeof(MSG1)-1);
        strncpy(buf1, MSG1, n);
        buf1[n] = '\0';
    }
    attron(COLOR_PAIR(CP_GLOW) | A_BOLD);
    mvprintw(box_y, box_x + 2, "\"%s", buf1);
    /* blinking cursor on line 1 if still typing */
    if (!msg1_done && sinf(cursor_blink) > 0.f)
        mvaddch(box_y, box_x + 2 + 1 + type_pos1, '_');
    if (msg1_done) mvaddch(box_y, box_x + 2 + 1 + (int)(sizeof(MSG1)-1), '"');
    attroff(COLOR_PAIR(CP_GLOW) | A_BOLD);

    /* ── message line 2 : author ── */
    if (msg1_done) {
        char buf2[64] = {0};
        int n2 = type_pos2 < (int)(sizeof(MSG2)-1) ? type_pos2 : (int)(sizeof(MSG2)-1);
        strncpy(buf2, MSG2, n2);
        buf2[n2] = '\0';
        attron(COLOR_PAIR(CP_SEED) | A_BOLD);
        mvprintw(box_y + 1, box_x + 2, "%s", buf2);
        /* blinking cursor on line 2 */
        if (!msg2_done && sinf(cursor_blink) > 0.f)
            mvaddch(box_y + 1, box_x + 2 + type_pos2, '_');
        attroff(COLOR_PAIR(CP_SEED) | A_BOLD);
    }

    /* ── small leaf decoration on box border ── */
    attron(COLOR_PAIR(CP_LEAF_MID));
    mvaddstr(box_y - 1, box_x + box_w / 2 - 3, " ~*~ ");
    attroff(COLOR_PAIR(CP_LEAF_MID));
}

int main(void) {
    setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    timeout(0);

    init_colors_custom();
    init_particles();

    const float T1 = 2.6f;
    const float T2 = 1.5f;
    const float T3 = 1.1f;
    const float T4 = 0.8f;
    const float T5 = 0.8f;
    const float T6 = 2.1f;
    const float T7 = 2.8f;
    const float T8 = 3.8f;
    const float total = T1 + T2 + T3 + T4 + T5 + T6 + T7 + T8;

    struct timespec ts0;
    clock_gettime(CLOCK_MONOTONIC, &ts0);

    while (1) {
        getmaxyx(stdscr, H, W);
        if (H < 28 || W < 90) {
            erase();
            attron(COLOR_PAIR(CP_TEXT));
            mvprintw(H / 2, (W > 34 ? (W - 34) / 2 : 0), "Resize terminal to at least 90x28");
            attroff(COLOR_PAIR(CP_TEXT));
            refresh();
            int ch = getch();
            if (ch == 'q') break;
            struct timespec req = {0, 1000000000L / FPS};
            nanosleep(&req, NULL);
            continue;
        }

        seed_x = W * 0.50f;
        ground_y = H * 0.77f;

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        float elapsed = (float)(ts.tv_sec - ts0.tv_sec) + (float)(ts.tv_nsec - ts0.tv_nsec) / 1000000000.0f;
        float t = elapsed;
        float dt = 1.0f / FPS;

        float hand_enter = clampf(elapsed / T1, 0.f, 1.f);
        float lower = clampf((elapsed - T1) / T2, 0.f, 1.f);
        float release = clampf((elapsed - T1 - T2) / T3, 0.f, 1.f);
        float sink = clampf((elapsed - T1 - T2 - T3) / T4, 0.f, 1.f);
        float pausep = clampf((elapsed - T1 - T2 - T3 - T4) / T5, 0.f, 1.f);
        float sprout = clampf((elapsed - T1 - T2 - T3 - T4 - T5) / T6, 0.f, 1.f);
        float sapling = clampf((elapsed - T1 - T2 - T3 - T4 - T5 - T6) / T7, 0.f, 1.f);
        float crown = clampf((elapsed - T1 - T2 - T3 - T4 - T5 - T6 - T7) / T8, 0.f, 1.f);

        const char *phase = "hand enters";
        if (elapsed >= T1) phase = "seed lowers";
        if (elapsed >= T1 + T2) phase = "seed released";
        if (elapsed >= T1 + T2 + T3) phase = "seed settles";
        if (elapsed >= T1 + T2 + T3 + T4) phase = "stillness";
        if (elapsed >= T1 + T2 + T3 + T4 + T5) phase = "sprout grows";
        if (elapsed >= T1 + T2 + T3 + T4 + T5 + T6) phase = "branches form";
        if (elapsed >= total) phase = "tree sways";

        erase();
        draw_sky(t);
        draw_hills();
        draw_ground();

        float hand_x = lerpf(-18.f, seed_x - 9.f, ease_in_out(hand_enter));
        float hand_y = ground_y - 7.f + lerpf(-1.5f, 4.5f, ease_in_out(lower));
        float hand_open = (elapsed < T1 + T2) ? 0.05f : ease_in_out(release);
        if (elapsed < T1 + T2 + T3 + T4 * 0.7f) draw_hand(hand_x, hand_y, hand_open);

        float seed_vis = 0.f;
        float seed_y = hand_y - 2.f;
        if (elapsed < T1 + T2) {
            seed_vis = 1.f;
            seed_y = hand_y - 3.f;
        } else if (elapsed < T1 + T2 + T3) {
            seed_vis = 1.f;
            seed_y = lerpf(hand_y - 3.f, ground_y - 1.f, ease_in(release));
        } else if (elapsed < T1 + T2 + T3 + T4) {
            seed_vis = 1.f - sink * 0.9f;
            seed_y = lerpf(ground_y - 1.f, ground_y + 1.f, ease_in_out(sink));
        }

        float soil_open = 0.f;
        if (elapsed >= T1 + T2) soil_open = (elapsed < T1 + T2 + T3 + T4) ? smoothstepf(T1 + T2, T1 + T2 + T3 + T4, elapsed) : 0.f;
        draw_soil_open(seed_x, soil_open);
        draw_seed(seed_x, seed_y, seed_vis, 0.5f + 0.5f * sinf(t * 5.0f));

        if (pausep >= 0.f) draw_sprout(sprout, t);
        draw_tree(sapling, crown, t);
        draw_ui(elapsed, phase, dt);

        refresh();

        int ch = getch();
        if (ch == 'q' || ch == 'Q') break;
        if (ch == 'r' || ch == 'R') {
            clock_gettime(CLOCK_MONOTONIC, &ts0);
            type_timer = 0.f; type_pos1 = 0; type_pos2 = 0;
            msg1_done = false; msg2_done = false; cursor_blink = 0.f;
        }
        if (ch == 'w' || ch == 'W') wind_on = !wind_on;
        if (ch == 'a' || ch == 'A') use_unicode = !use_unicode;

        struct timespec req = {0, 1000000000L / FPS};
        nanosleep(&req, NULL);
    }

    endwin();
    return 0;
}
