#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>

#include "png_utils.h"
#include "snis_alloc.h"
#include "stacktrace.h"
#include "vec3.h"
#include "wwviaudio.h"
#include "ogg_to_pcm.h"

#define ARRAYSIZE(x) (int) (sizeof(x) / sizeof((x)[0]))

#define WINDOW_WIDTH (1024 * 1.2)
#define WINDOW_HEIGHT (768 * 1.2)
#define TARGET_FPS 60
#define FRAME_TARGET_TIME (1000 / TARGET_FPS)

#define GAME_MODE_TITLE_SCREEN 0
#define GAME_MODE_PLAY 1

/* Game state struct to hold core systems */
struct game_state {
	SDL_Window *window;
	SDL_Renderer *renderer;
	bool is_running;
	int mode;
	int current_level;
	float camera_vx; /* Camera coords are in world coord system */
	float camera_x;
	float camera_min_x;
	float camera_max_x;
	float desired_camera_x;
	int window_width, window_height;
	struct snis_object_pool *objpool;
} game = { 0 };

struct image {
	char *filename;
	char *data;
	int width, height, alpha;
	int mode;
#define IMAGE_MODE_TEXTURE 1
#define IMAGE_MODE_RAW 2
	SDL_Texture *texture;
};

#define MAX_GAME_OBJS 1000

static struct game_object {
	int type;
	int i;			/* index into go[] */
	float x, y;		/* position in world coords */
	float sx, sy;		/* position in screen coords */
	float vx, vy;		/* for "physics" */
	int is_grounded;	/* Is the object touching the floor? */
	int is_climbing;	/* Is the object climbing a ladder */
	int current_image;	/* index into object_type[o->type].image[]; */
	float ticks;
	float next_animation_tick;
} go[MAX_GAME_OBJS];

static struct game_object *player;
#define MAX_OBJECT_TYPES 50
#define OBJTYPE_PLAYER 0
#define OBJTYPE_WALLMAP 1
#define OBJTYPE_DESK 2
#define OBJTYPE_SHELLS 3
#define OBJTYPE_RADAR_CONSOLE 4
#define OBJTYPE_BED 5
#define OBJTYPE_CRATES 6
#define OBJTYPE_DIRTCLOD 7
#define OBJTYPE_SOLDIER 8
#define OBJTYPE_BARREL 9
#define OBJTYPE_TNT 10
#define OBJTYPE_AMMO 11

static struct object_type_data {
	struct image **image;
	int nimages;
	float scalex, scaley;
	void (*draw)(SDL_Renderer *renderer, struct game_object *o);
} object_type[MAX_OBJECT_TYPES] = { 0 };

#define MAX_LEVELS 3
#define MAX_SCREENS_PER_LEVEL 10

struct level {
	int level_number;
	int nscreens;
	int ncolor_codings;
	struct image terrain[MAX_SCREENS_PER_LEVEL];
	struct image collision_mask[MAX_SCREENS_PER_LEVEL];
} level[MAX_LEVELS] = { 0 };

struct image background_image = { "images/notebook-image.png", NULL, 0, 0, 0, 0, NULL, };
struct image title_screen_image = { "images/biro-hero-title-screen.png", NULL, 0, 0, 0, 0, NULL, };
struct image hero_right_1 = { "images/hero-right-1.png", NULL, 0, 0, 0, 0, NULL, };
struct image hero_right_2 = { "images/hero-right-2.png", NULL, 0, 0, 0, 0, NULL, };
struct image hero_right_3 = { "images/hero-right-3.png", NULL, 0, 0, 0, 0, NULL, };
struct image hero_left_1 = { "images/hero-left-1.png", NULL, 0, 0, 0, 0, NULL, };
struct image hero_left_2 = { "images/hero-left-2.png", NULL, 0, 0, 0, 0, NULL, };
struct image hero_left_3 = { "images/hero-left-3.png", NULL, 0, 0, 0, 0, NULL, };
struct image wallmap = { "images/map.png", NULL, 0, 0, 0, 0, NULL, };
struct image radar_console = { "images/radar-console.png", NULL, 0, 0, 0, 0, NULL, };
struct image desk = { "images/desk.png", NULL, 0, 0, 0, 0, NULL, };
struct image artillery_shells = { "images/artillery-shells.png", NULL, 0, 0, 0, 0, NULL, };
struct image bed = { "images/bed.png", NULL, 0, 0, 0, 0, NULL, };
struct image crates = { "images/crates.png", NULL, 0, 0, 0, 0, NULL, };
struct image dirtclod1 = { "images/dirtclod1.png", NULL, 0, 0, 0, 0, NULL, };
struct image dirtclod2 = { "images/dirtclod2.png", NULL, 0, 0, 0, 0, NULL, };
struct image dirtclod3 = { "images/dirtclod3.png", NULL, 0, 0, 0, 0, NULL, };
struct image dirtclod4 = { "images/dirtclod4.png", NULL, 0, 0, 0, 0, NULL, };
struct image soldier1 = { "images/soldier1.png", NULL, 0, 0, 0, 0, NULL, };
struct image soldier2 = { "images/soldier2.png", NULL, 0, 0, 0, 0, NULL, };
struct image soldier3 = { "images/soldier3.png", NULL, 0, 0, 0, 0, NULL, };
struct image soldier4 = { "images/soldier4.png", NULL, 0, 0, 0, 0, NULL, };
struct image barrel = { "images/barrel.png", NULL, 0, 0, 0, 0, NULL, };
struct image tnt = { "images/tnt.png", NULL, 0, 0, 0, 0, NULL, };
struct image ammo = { "images/ammo.png", NULL, 0, 0, 0, 0, NULL, };

static struct static_object_entry {
	int level;
	float x, y;
	int type;
} static_object[] = {
	{ 0, 450.0f, 700.0f, OBJTYPE_RADAR_CONSOLE, },
	{ 0, 150.0f, 590.0f, OBJTYPE_WALLMAP, },
	{ 0, 130.0f, 680.0f, OBJTYPE_DESK, },
	{ 0, 1400.0f, 450.0f, OBJTYPE_SHELLS, },
	{ 0, 1200.0f, 385.0f, OBJTYPE_BED, },
	{ 0, 1600.0f, 385.0f, OBJTYPE_CRATES, },
	{ 0, 1630.0f, 600.0f, OBJTYPE_DESK, },
	{ 0, 2250.0f, 695.0f, OBJTYPE_RADAR_CONSOLE, },
	{ 0, 1162.0f, 627.0f, OBJTYPE_BED, },
	{ 0, 1848.0f, 586.0f, OBJTYPE_BED, },
	{ 0, 2286.0f, 400.0f, OBJTYPE_CRATES, },
	{ 0, 2604.0f, 400.0f, OBJTYPE_CRATES, },
	{ 0, 2550.0f, 685.0f, OBJTYPE_DESK, },
	{ 0, 2850.0f, 670.0f, OBJTYPE_SHELLS, },
	{ 0, 3226.0f, 326.0f, OBJTYPE_BARREL, },
	{ 0, 3286.0f, 326.0f, OBJTYPE_AMMO, },
	{ 0, 3286.0f, 618.0f, OBJTYPE_TNT, },
	{ 0, 3246.0f, 618.0f, OBJTYPE_SOLDIER, },
	{ 0, 3660.0f, 424.0f, OBJTYPE_SOLDIER, },
	{ 0, 3950.0f, 632.0f, OBJTYPE_SOLDIER, },
	{ 0, 3950.0f, 612.0f, OBJTYPE_RADAR_CONSOLE, },
	{ 0, 2536.0f, 429.0f, OBJTYPE_SOLDIER, },
	{ 0, 2222.0f, 429.0f, OBJTYPE_SOLDIER, },
	{ 0, 2118.0f, 718.0f, OBJTYPE_SOLDIER, },
	{ 0, 1652.0f, 418.0f, OBJTYPE_SOLDIER, },
	{ 0, 1372.0f, 472.0f, OBJTYPE_SOLDIER, },
	{ 0, 1222.0f, 636.0f, OBJTYPE_SOLDIER, },
	{ 0, 1132.0f, 381.0f, OBJTYPE_SOLDIER, },
	{ 0, 772.0f, 472.0f, OBJTYPE_SOLDIER, },
	{ 0, 510.0f, 714.0f, OBJTYPE_SOLDIER, },
	{ 0, 228.0f, 687.0f, OBJTYPE_SOLDIER, },
};

enum keyaction {
	keyright,
	keyleft,
	keyup,
	keydown,
	keyjump,
	keycrouch,
	keyshoot,
	keygrenade,
};

static int keypressed[6] = { 0 }; /* indexed by enum keyaction */

#define NORTH 0
#define EAST 1
#define SOUTH 2
#define WEST 3
static const float xo[] = { 0, 1, 0, -1 };
static const float yo[] = { -1, 0, 1, 0 };

static int debug_rects_on = 0;

const union vec3 RED = { { 1.0f, 0.0f, 0.0f } };
const union vec3 GREEN = { { 0.0f, 1.0f, 0.0f } };

#define DIST_ARTILLERY1 0
#define DIST_ARTILLERY2 1
#define INCOMING_ARTILLERY 2
#define LIGHT_MACHINE_GUN 3
#define RIFLE_BURST_FIRE 4

/* George Marsaglia's xorshift PRNG algorithm,
 * see: https://en.wikipedia.org/wiki/Xorshift#Example_implementation
 *
 * The state word must be initialized to non-zero
 */
uint32_t xorshift(uint32_t *state)
{
	/* Algorithm "xor" from p. 4 of Marsaglia, "Xorshift RNGs" */
	uint32_t x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static void draw_rectangle(SDL_Renderer *renderer,
	float x, float y, float w, float h, int filled)
{
	SDL_Rect r = { x, y, w, h };
	if (filled)
		SDL_RenderFillRect(renderer, &r);
	else
		SDL_RenderDrawRect(renderer, &r);
}

static int load_png_image(SDL_Renderer *renderer, struct image *i, int image_mode)
{
	char whynot[1000];

	i->texture = NULL;
	fprintf(stderr, "Decoding PNG file %s\n", i->filename);
	i->data = png_utils_read_png_image(i->filename,
			0, 0, 0, &i->width, &i->height,
			&i->alpha, whynot, sizeof(whynot));
	if (!i->data) {
		fprintf(stderr, "Failed to load %s: %s\n",
			i->filename, whynot);
		return -1;
	}
	i->mode = image_mode;
	if (i->mode & IMAGE_MODE_TEXTURE) {
		i->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STATIC, i->width, i->height);
		if (!i->texture) {
			fprintf(stderr, "Could not create texture for %s\n", i->filename);
			exit(1);
		}
		SDL_UpdateTexture(i->texture, NULL, i->data, 4 * i->width);
	}
	return 0;
}

static void image_cleanup(struct image *i)
{
	if (i->texture) {
		SDL_DestroyTexture(i->texture);
		i->texture = NULL;
		if (i->data) {
			free(i->data);
			i->data = NULL;
		}
	}
}

static int read_png_files(SDL_Renderer *renderer)
{
	int x = 0;

	x += load_png_image(renderer, &background_image, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &title_screen_image, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &hero_right_1, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &hero_right_2, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &hero_right_3, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &hero_left_1, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &hero_left_2, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &hero_left_3, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &wallmap, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &desk, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &radar_console, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &artillery_shells, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &bed, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &crates, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &dirtclod1, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &dirtclod2, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &dirtclod3, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &dirtclod4, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &soldier1, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &soldier2, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &soldier3, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &soldier4, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &barrel, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &ammo, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &tnt, IMAGE_MODE_TEXTURE);
	return x;
}

static int read_levels(SDL_Renderer *renderer)
{
	struct dirent **namelist;
	char path[PATH_MAX];

	int rc = scandir("images/level1", &namelist, NULL, alphasort);
	if (rc < 0) {
		fprintf(stderr, "Failed to scan directory images/level1\n");
		exit(1);
	}
	int x = 0;
	int n = 0;
	for (int i = 0; i < rc; i++) {
		if (strcmp(namelist[i]->d_name, ".") == 0 ||
			strcmp(namelist[i]->d_name, "..") == 0)
			continue;
		/* Is it a level background image? */
		if (strncmp(namelist[i]->d_name, "level-", 6) == 0) {
			int ns = level[0].nscreens;
			fprintf(stderr, "Reading level background image %s\n", namelist[i]->d_name);
			snprintf(path, sizeof(path), "%s/%s", "images/level1", namelist[i]->d_name);
			free(namelist[i]);
			level[0].terrain[ns].filename = strdup(path);
			level[0].terrain[ns].data = NULL;
			level[0].terrain[ns].width = 0;
			level[0].terrain[ns].height = 0;
			level[0].terrain[ns].alpha = 0;
			level[0].terrain[ns].mode = 0;
			level[0].terrain[ns].texture = NULL;
			x += load_png_image(renderer, &level[0].terrain[ns], IMAGE_MODE_TEXTURE);
			printf("x = %d\n", x);
			printf("level[0].terrain[%d].filename = %s\n", n, level[0].terrain[ns].filename);
			printf("level[0].terrain[%d].data = %p\n", n, level[0].terrain[ns].data);
			printf("level[0].terrain[%d].width = %d\n", n, level[0].terrain[ns].width);
			printf("level[0].terrain[%d].height = %d\n", n, level[0].terrain[ns].height);
			printf("level[0].terrain[%d].alpha = %d\n", n, level[0].terrain[ns].alpha);
			printf("level[0].terrain[%d].mode = %d\n", n, level[0].terrain[ns].mode);
			printf("level[0].terrain[%d].texture = %p\n", n, (void *) level[0].terrain[ns].texture);
			SDL_SetTextureBlendMode(level[0].terrain[ns].texture, SDL_BLENDMODE_BLEND);
			n++;
			level[0].nscreens++;
		} else if (strncmp(namelist[i]->d_name, "map-code-", 9) == 0) {
			/* Is it a color coding for moveable areas and ladders and so on? */
			int nc = level[0].ncolor_codings;
			fprintf(stderr, "Reading level color coding image %s\n", namelist[i]->d_name);
			snprintf(path, sizeof(path), "%s/%s", "images/level1", namelist[i]->d_name);
			free(namelist[i]);
			level[0].collision_mask[nc].filename = strdup(path);
			level[0].collision_mask[nc].data = NULL;
			level[0].collision_mask[nc].width = 0;
			level[0].collision_mask[nc].height = 0;
			level[0].collision_mask[nc].alpha = 0;
			level[0].collision_mask[nc].mode = 0;
			level[0].collision_mask[nc].texture = NULL;
			x += load_png_image(renderer, &level[0].collision_mask[nc], IMAGE_MODE_RAW);
			printf("x = %d\n", x);
			printf("level[0].collision_mask[%d].filename = %s\n",
				n, level[0].collision_mask[nc].filename);
			printf("level[0].collision_mask[%d].data = %p\n",
				n, level[0].collision_mask[nc].data);
			printf("level[0].collision_mask[%d].width = %d\n",
				n, level[0].collision_mask[nc].width);
			printf("level[0].collision_mask[%d].height = %d\n",
				n, level[0].collision_mask[nc].height);
			printf("level[0].collision_mask[%d].alpha = %d\n",
				n, level[0].collision_mask[nc].alpha);
			printf("level[0].collision_mask[%d].mode = %d\n",
				n, level[0].collision_mask[nc].mode);
			printf("level[0].collision_mask[%d].texture = %p\n",
				n, (void *) level[0].collision_mask[nc].texture);
			n++;
			level[0].ncolor_codings++;
		}
	}
	game.camera_min_x = 512.0f;
	game.camera_max_x = 1024.0f * level[0].nscreens - 512.0f;
	free(namelist);
	return x;
}

static void player_init(void)
{
	int i = snis_object_pool_alloc_obj(game.objpool);
	if (i < 0) {
		fprintf(stderr, "Failed to allocated player object\n");
		exit(1);
	}
	player = &go[i];
	player->i = i;
	player->x = 50;
	player->y = 0;
	player->type = OBJTYPE_PLAYER;
	player->ticks = 0.0;
	player->next_animation_tick = 0.0;
	player->is_grounded = 0;
	player->is_climbing = 0;
	player->vx = 0.0f;
	player->vy = 0.0f;
}

static void set_up_level(int l)
{
	for (int i = 0; i < ARRAYSIZE(static_object); i++) {
		if (static_object[i].level != l)
			continue;
		printf("Allocating object (total %d)\n", ARRAYSIZE(static_object));
		int n = snis_object_pool_alloc_obj(game.objpool);
		if (n < 0) {
			fprintf(stderr, "Out of objects at %s:%d\n", __FILE__, __LINE__);
			abort();
		}
		struct game_object *o = &go[n];
		o->i = n;
		o->type = static_object[i].type;
		o->x = static_object[i].x;
		o->y = static_object[i].y;
		o->vx = 0.0;
		o->vy = 0.0;
		o->ticks = 0.0;
		o->is_grounded = 1;
		o->is_climbing = 0;
		o->next_animation_tick = 0.0;
	}
}

/* Functions to convert from world coords to screen coords */
static float world_to_screenx(float x)
{
	float sx = x - game.camera_x + 512.0f;
	sx = 100.0f + ((game.window_width - 200.0f) * sx) / 1024.0f;
	return sx;
}

static float world_to_screeny(float y)
{
	float sy = ((game.window_height - 200.0f) * y) / 768.0f;
	sy = sy + 100.0;
	return sy;
}

/* Functions to convert from screen coords to world coords */
static float screen_to_worldx(float sx)
{
	float x = sx - 100.0f;
	x = (x * 1024.0f) / (game.window_width - 200.0f);
	x = x + game.camera_x - 512.0f;
	return x;
}

static float screen_to_worldy(float sy)
{
	float y = sy - 100.0f;
	y = (y * 768.0f) / (game.window_height - 200.0f);
	return y;
}

static void draw_object(SDL_Renderer *renderer, struct game_object *o)
{
	struct object_type_data *odt = &object_type[o->type];
	struct image **im = odt->image;
	int i = o->current_image;
	float windowx_scale = game.window_width / WINDOW_WIDTH;
	float windowy_scale = game.window_height / WINDOW_HEIGHT;
	float scx = odt->scalex * windowx_scale;
	float scy = odt->scaley * windowy_scale;
	float w = scx * im[i]->width;
	float h = scy * im[i]->height;

	SDL_Rect destrect = {
		world_to_screenx(o->x - 0.5 * w),
		world_to_screeny(o->y - 0.5 * h),
		w, h };

	SDL_SetTextureBlendMode(im[i]->texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, im[i]->texture, NULL, &destrect);
}

static void set_up_object_type_data(void)
{
	/* Setup OBJTYPE_PLAYER data */
	int n = OBJTYPE_PLAYER;
	object_type[n].image = malloc(6 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &hero_right_1;
	object_type[n].image[1] = &hero_right_2;
	object_type[n].image[2] = &hero_right_3;
	object_type[n].image[3] = &hero_left_1;
	object_type[n].image[4] = &hero_left_2;
	object_type[n].image[5] = &hero_left_3;
	object_type[n].nimages = 6;
	object_type[n].scalex = 0.25;
	object_type[n].scaley = 0.25;
	object_type[n].draw = draw_object;

	n = OBJTYPE_WALLMAP;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &wallmap;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.18;
	object_type[n].scaley = 0.18;
	object_type[n].draw = draw_object;

	n = OBJTYPE_DESK;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &desk;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.15;
	object_type[n].scaley = 0.15;
	object_type[n].draw = draw_object;

	n = OBJTYPE_RADAR_CONSOLE;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &radar_console;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.20;
	object_type[n].scaley = 0.20;
	object_type[n].draw = draw_object;

	n = OBJTYPE_SHELLS;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &artillery_shells;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.15;
	object_type[n].scaley = 0.15;
	object_type[n].draw = draw_object;

	n = OBJTYPE_BED;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &bed;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.25;
	object_type[n].scaley = 0.25;
	object_type[n].draw = draw_object;

	n = OBJTYPE_CRATES;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &crates;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.65;
	object_type[n].scaley = 0.65;
	object_type[n].draw = draw_object;

	n = OBJTYPE_DIRTCLOD;
	object_type[n].image = malloc(4 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &dirtclod1;
	object_type[n].image[1] = &dirtclod2;
	object_type[n].image[2] = &dirtclod3;
	object_type[n].image[3] = &dirtclod4;
	object_type[n].nimages = 4;
	object_type[n].scalex = 0.3;
	object_type[n].scaley = 0.3;
	object_type[n].draw = draw_object;

	n = OBJTYPE_SOLDIER;
	object_type[n].image = malloc(4 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &soldier1;
	object_type[n].image[1] = &soldier2;
	object_type[n].image[2] = &soldier3;
	object_type[n].image[3] = &soldier4;
	object_type[n].nimages = 4;
	object_type[n].scalex = 0.18;
	object_type[n].scaley = 0.18;
	object_type[n].draw = draw_object;

	n = OBJTYPE_BARREL;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &barrel;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.18;
	object_type[n].scaley = 0.18;
	object_type[n].draw = draw_object;

	n = OBJTYPE_TNT;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &tnt;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.18;
	object_type[n].scaley = 0.18;
	object_type[n].draw = draw_object;

	n = OBJTYPE_AMMO;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &ammo;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.13;
	object_type[n].scaley = 0.13;
	object_type[n].draw = draw_object;
}

/* Initialize SDL, window, and renderer */
bool init_game(struct game_state *game)
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
		return false;
	}

	game->window = SDL_CreateWindow(
		"BIRO - HERO",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		WINDOW_WIDTH, WINDOW_HEIGHT,
		SDL_WINDOW_SHOWN);

	if (!game->window) {
		SDL_Log("Failed to create window: %s", SDL_GetError());
		SDL_Quit();
		return false;
	}
	SDL_SetWindowResizable(game->window, SDL_TRUE);

	game->renderer = SDL_CreateRenderer(
		game->window,
		-1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
	);

	if (!game->renderer) {
		SDL_Log("Failed to create renderer: %s", SDL_GetError());
		SDL_DestroyWindow(game->window);
		SDL_Quit();
		return false;
	}

	game->is_running = true;
	game->camera_x = 1024.0 / 2.0;
	snis_object_pool_setup(&game->objpool, MAX_GAME_OBJS);
	player_init();
	set_up_level(0);

	return true;
}

static void process_keydown(__attribute__((unused)) SDL_Event event)
{
	if (game.mode == GAME_MODE_TITLE_SCREEN) {
		game.mode = GAME_MODE_PLAY;
		return;
	}
	switch (event.key.keysym.sym) {
	case SDLK_RIGHT:
	case SDLK_d:
		keypressed[keyright] = 1;
		break;
	case SDLK_LEFT:
	case SDLK_a:
		keypressed[keyleft] = 1;
		break;
	case SDLK_UP:
	case SDLK_w:
		keypressed[keyup] = 1;
		break;
	case SDLK_DOWN:
	case SDLK_s:
		keypressed[keydown] = 1;
		break;
	case SDLK_SPACE:
		keypressed[keyjump] = 1;
		break;
	case SDLK_r:
		debug_rects_on = !debug_rects_on;
		break;
	case SDLK_p:
		printf("Player xy = (%g,%g)\n", player->x, player->y);
		break;
	default:
		break;
	}
}

static void process_keyup(__attribute__((unused)) SDL_Event event)
{
	switch (event.key.keysym.sym) {
	case SDLK_RIGHT:
	case SDLK_d:
		keypressed[keyright] = 0;
		break;
	case SDLK_LEFT:
	case SDLK_a:
		keypressed[keyleft] = 0;
		break;
	case SDLK_UP:
	case SDLK_w:
		keypressed[keyup] = 0;
		break;
	case SDLK_DOWN:
	case SDLK_s:
		keypressed[keydown] = 0;
		break;
	case SDLK_SPACE:
		keypressed[keyjump] = 0;
		break;
	default:
		break;
	}
}

/* Handle input events (keyboard, mouse, window close) */
void process_input(struct game_state *game)
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_QUIT:
			game->is_running = false;
			break;
		case SDL_KEYDOWN:
			if (event.key.keysym.sym == SDLK_ESCAPE) {
				game->is_running = false;
			}
			process_keydown(event);
			break;
		case SDL_KEYUP:
			process_keyup(event);
			break;
		default:
		break;
		}
	}
}

static void sample_collision_mask(float wx, float wy, union vec3 *color)
{
	int img = (int) wx / 1024.0f;

	if (img < 0 || img >= level[0].ncolor_codings) {
		fprintf(stderr, "sample_color_coded_map(): Bad image number %d\n", img);
		return;
	}

	wx = fmodf(wx, 1024.0f);

	/* Clamp coords in bounds */
	if (wx < 0.0f)
		wx = 0.0f;
	if (wx > 1023.0)
		wx = 1023.0;
	if (wy < 0.0f)
		wy = 0.0f;
	if (wy > 767.0f)
		wy = 767.0f;

	int col = (int) wx;
	int row = (int) wy;
	uint8_t *pixel = (unsigned char *) &level[0].collision_mask[img].data[4 * (row * 1024 + col)];

	color->r = (float) pixel[0] / 255.0;
	color->g = (float) pixel[1] / 255.0;
	color->b = (float) pixel[2] / 255.0;
}

static void sample_mask_around_object(struct game_object *o, union vec3 *colors)
{
	for (int i = 0; i < 4; i++) {
		int wx = (int) (o->x + xo[i] * 10);
		int wy = (int) (o->y + yo[i] * 10);
		sample_collision_mask(wx, wy, &colors[i]);
	}
}

/* Helper function to check if a specific pixel is passable (green or red),
 * x and y are world coords.
 */
static bool is_passable(float x, float y) {
	union vec3 color;
	sample_collision_mask(x, y, &color);

	float d_red = vec3_dot(&RED, &color);
	float d_green = vec3_dot(&GREEN, &color);

	return (d_red > 0.8f || d_green > 0.8f);
}

/* Helper function to check if a specific pixel is a ladder (red),
 * x and y are world coords.
 */
static bool is_ladder(float x, float y) {
	union vec3 color;
	sample_collision_mask(x, y, &color);

	/* Check strictly for the red channel */
	return vec3_dot(&RED, &color) > 0.8f;
}

#define MAX_STEP_HEIGHT 20 /* Maximum pixels the player can step up at once */

void move_horizontal(struct game_object *o, float dx)
{
	/* Determine the leading edge based on sprite width */
	struct object_type_data *odt = &object_type[o->type];
	float half_width = (odt->image[0]->width * odt->scalex) / 2.0f;
	float half_height = (odt->image[0]->height * odt->scaley) / 2.0f;

	float target_x = o->x + dx;
	float leading_x = dx > 0 ? target_x + half_width : target_x - half_width;
	float foot_y = o->y + half_height;

	/* If the target space at foot level is blocked... */
	if (!is_passable(leading_x, foot_y)) {
		bool stepped_up = false;

		/* Look upward pixel by pixel to find a valid step */
		for (int step = 1; step <= MAX_STEP_HEIGHT; step++) {
			if (is_passable(leading_x, foot_y - step)) {
				/* We found an opening! Adjust Y to walk up the slope. */
				o->y -= step;
				stepped_up = true;
				break;
			}
		}

		/* If a step wasn't found, it's a wall. Stop horizontal movement. */
		if (!stepped_up) {
			o->vx = 0;
			return;
		}
	}

	/* If passable or successfully stepped up, apply the X movement */
	o->x = target_x;
}

#define GRAVITY 15.0f
#define MAX_FALL_SPEED 10.0f
#define SNAP_TO_GROUND_DIST 3 /* Pixels to look down to stay attached to a slope */

void apply_gravity_and_vertical_movement(struct game_object *o, float delta_time)
{
	struct object_type_data *odt = &object_type[o->type];
	float half_height = (odt->image[0]->height * odt->scaley) / 2.0f;

	/* If climbing, bypass gravity and apply direct vertical movement */
	if (o->is_climbing) {
		float target_y = o->y + o->vy;
		float foot_y = target_y + half_height;

		/* Ensure we aren't climbing down into solid ground */
		if (is_passable(o->x, foot_y)) {
			o->y = target_y;
		} else {
			/* Hit the ground while climbing down */
			o->is_climbing = 0;
			o->is_grounded = 1;
		}
		return;
	}

	/* Apply Gravity */
	o->vy += GRAVITY * delta_time;
	if (o->vy > MAX_FALL_SPEED) o->vy = MAX_FALL_SPEED;

	float target_y = o->y + o->vy;
	float foot_y = target_y + half_height;

	if (o->vy > 0 && !is_passable(o->x, foot_y)) {
		/* 1. Check for floor collision (falling) */
		/* Hit the ground. Align exactly with the floor pixel. */
		while (!is_passable(o->x, foot_y)) {
			foot_y -= 1.0f;
		}
		o->y = foot_y - half_height;
		o->vy = 0;
		o->is_grounded = true;
	} else if (o->is_grounded && o->vy >= 0) {
		/* 2. Slope adhesion (sticking to downward slopes) */
		bool found_ground = false;
		for (int i = 1; i <= SNAP_TO_GROUND_DIST; i++) {
			if (!is_passable(o->x, foot_y + i)) {
				/* Ground is just a few pixels below, snap down to it */
				o->y += (i - 1);
				found_ground = true;
				break;
			}
		}
		if (!found_ground) {
			o->is_grounded = false; /* Walked off a ledge */
		}
	} else {
		/* 3. Free falling or moving up */
		o->y = target_y;
		o->is_grounded = false;
	}
}

/* Update game logic (positions, physics, AI) based on delta time */
void update(float delta_time)
{
	int do_player_animation = 0;

#define PLAYER_VX 2
#define PLAYER_VY 2

	/* Input sets velocity, not direct position */
	player->vx = 0;
	if (keypressed[keyleft]) {
		player->vx = -PLAYER_VX;
		do_player_animation = 1;
	}
	if (keypressed[keyright]) {
		player->vx =  PLAYER_VX;
		do_player_animation = 1;
	}
	if (keypressed[keyjump] && player->is_grounded) {
		player->vy = -5.0f; /* Jump velocity */
		player->is_grounded = false;
		do_player_animation = 1;
	}

	/* Check if the player's center is over a ladder */
	bool on_ladder = is_ladder(player->x, player->y);

	/* Auto-grab the ladder if overlapping it and not moving upward (jumping) */
	if (on_ladder && player->vy >= 0.0f)
		player->is_climbing = 1;

	/* Exit climbing state if they walk off the ladder */
	if (!on_ladder) {
		player->is_climbing = 0;
	}

	if (player->is_climbing) {
		player->vy = 0.0f; /* Stop falling/floating */

		if (keypressed[keyup]) {
			player->vy = -PLAYER_VY;
			do_player_animation = 1;
		}
		if (keypressed[keydown]) {
			player->vy = PLAYER_VY;
			do_player_animation = 1;
		}

		/* Allow jumping off the ladder */
		if (keypressed[keyjump]) {
			player->is_climbing = 0;
			player->vy = -5.0f;
			player->is_grounded = false;
			do_player_animation = 1;
		}
	} else {
		/* Standard jump logic when not climbing */
		if (keypressed[keyjump] && player->is_grounded) {
			player->vy = -5.0f; /* Jump velocity */
			player->is_grounded = false;
			do_player_animation = 1;
		}
	}

	/* Apply movement */
	if (player->vx != 0.0f) {

		/* Turn player around if he is facing the wrong direction */
		if (player->vx < 0.0f && player->current_image >= 0 && player->current_image <= 2)
			player->current_image = 3;
		else if (player->vx > 0.0f && player->current_image >= 3 && player->current_image <= 5)
			player->current_image = 0;

		move_horizontal(player, player->vx);
	}
	apply_gravity_and_vertical_movement(player, delta_time);

	if (game.camera_x > game.camera_max_x)
			game.camera_x = game.camera_max_x;
	if (game.camera_x < game.camera_min_x)
			game.camera_x = game.camera_min_x;

	player->ticks += delta_time;
	if (do_player_animation) {
		if (player->ticks > player->next_animation_tick) {
			player->next_animation_tick = player->ticks + 0.1;
			player->current_image++;
			if (player->current_image == 3)
				player->current_image = 0;
			if (player->current_image == 6)
				player->current_image = 3;
		}
	}
}

static void draw_background_image(SDL_Renderer *renderer)
{
	struct image *i;

	i = &background_image;
	switch (game.mode) {
	case GAME_MODE_PLAY:
		break;
	case GAME_MODE_TITLE_SCREEN:
		i = &title_screen_image;
		break;
	default:
		i = &background_image;
		break;
	}
	if (i->mode & IMAGE_MODE_TEXTURE) {
		SDL_RenderCopy(renderer, i->texture, NULL, NULL);
	} else {
		fprintf(stderr, "draw_background_image(): non-mapped texture: %s.\n", i->filename);
		exit(1);
	}
}

static void checkrect(SDL_Rect *r)
{
	if (r->x < 0 || r->y < 0 || r->w < 0 || r->h < 0) {
		printf("Bad rect: { %d, %d, %d, %d }\n", r->x, r->y, r->w, r->h);
		stacktrace("bad rect");
		abort();
	}
}

static void draw_level(SDL_Renderer *renderer)
{
	int width, height;

	/* printf("--- Begin draw_level() ---\n"); */
	/* Get the window dimensions and stash in the game state */
	SDL_GetWindowSize(game.window, &width, &height);
	game.window_width = width;
	game.window_height = height;

	/* Figure out which images we need to draw */
	int img1 = (int) (game.camera_x - 512.0f) / 1024.0f;
	int img2 = img1 + 1;
	/* printf("img1 = %d, img2 = %d\n", img1, img2); */

	/* Figure out which part of first image to draw (in world coords) */
	float left_edge_x = fmodf((game.camera_x - 512.0f), 1024.0f);
	float right_edge_x = left_edge_x + 1024.0f;
	if (right_edge_x > 1024.0f)
		right_edge_x = 1024.0f;

	/* printf("left_edge_x = %f\n", left_edge_x); */
	/* printf("right_edge_x = %f\n", right_edge_x); */
	SDL_Rect src1rect = { left_edge_x, 0.0, right_edge_x - left_edge_x, 768.0f };
	/* printf("src1rect = %d, %d, %d, %d\n", src1rect.x, src1rect.y, src1rect.w, src1rect.h); */
	checkrect(&src1rect);

	/* Figure out which part of the screen to draw the 1st image to */
	float left_sx = world_to_screenx(left_edge_x + img1 * 1024.0f);
	float right_sx = world_to_screenx(right_edge_x + img1 * 1024.0f);
	float top_sy = world_to_screeny(0.0f);
	float bottom_sy = world_to_screeny(768.0f);

	/* printf("left_sx = %f, right_sx = %f, top_sy = %f, bottom_sy = %f\n",
			left_sx, right_sx, top_sy, bottom_sy); */

	SDL_Rect dest1rect = { left_sx, top_sy, right_sx - left_sx, bottom_sy - top_sy };
	/* printf("dest1rect = %d, %d, %d, %d\n", dest1rect.x, dest1rect.y, dest1rect.w, dest1rect.h); */
	checkrect(&dest1rect);

	SDL_SetTextureBlendMode(level[0].terrain[img1].texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, level[0].terrain[img1].texture, &src1rect, &dest1rect);

	if (img2 >= level[0].nscreens) { /* Need to draw 2nd image? */
		/* printf("Skipping image 2\n"); */
		return;
	}

	/* Figure out which part of 2nd image to draw */
	float left2_x = 0.0f;
	float right2_x = fmodf(screen_to_worldx(game.window_width - 100.0f), 1024.0f);
	float top2_y = 0.0f;
	float bottom2_y = 768.0f;
	/* printf("left2_x = %f, right2_x = %f, top2_y = %f, bottom2_y = %f\n",
			left2_x, right2_x, top2_y, bottom2_y); */
	SDL_Rect src2rect = { left2_x, top2_y, right2_x - left2_x, bottom2_y - top2_y };
	/* printf("src2rect = { %d, %d, %d, %d }\n", src2rect.x, src2rect.y, src2rect.w, src2rect.h); */
	checkrect(&src2rect);

	/* Figure out which part of screen to draw the 2nd image to */
	float left2_sx = right_sx;
	float right2_sx = (game.window_width - 100.0f);
	/* printf("left2_sx = %f, right2_sx = %f\n", left2_sx, right2_sx); */
	SDL_Rect dest2rect = { left2_sx, top_sy, right2_sx - left2_sx, bottom_sy - top_sy };
	/* printf("dest2rect = { %d, %d, %d, %d }\n", dest2rect.x, dest2rect.y, dest2rect.w, dest2rect.h); */
	checkrect(&dest2rect);

	/* printf("Drawing image 2\n"); */
	SDL_SetTextureBlendMode(level[0].terrain[img2].texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, level[0].terrain[img2].texture, &src2rect, &dest2rect);
	/* printf("--- End draw_level() ---\n"); */
}

static void draw_debug_rectangles(struct game_object *o, union vec3 *v)
{
	if (!debug_rects_on)
		return;


	for (int i = 0; i < 4; i++) {
		float x = world_to_screenx(o->x) + xo[i] * 20.0f;
		float y = world_to_screeny(o->y) + yo[i] * 20.0f;

		uint8_t r, g, b;

		r = (uint8_t) v[i].r * 255;
		g = (uint8_t) v[i].g * 255;
		b = (uint8_t) v[i].b * 255;

		SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
		draw_rectangle(game.renderer, x, y, 10.0f, 10.0f, 1);
	}
}

static void debug_sampling(void)
{
#if 0
	for (int x = 0; x < 4 * 1024; x += 10) {
		for (int y = 0; y < 768; y+= 10) {
			union vec3 color;
			sample_collision_mask(x, y, &color);
			int sx = world_to_screenx(x);
			int sy = world_to_screeny(y);

			uint8_t r = (uint8_t) (color.r * 255);
			uint8_t g = (uint8_t) (color.g * 255);
			uint8_t b = (uint8_t) (color.b * 255);

			SDL_SetRenderDrawColor(game.renderer, r, g, b, 255);
			draw_rectangle(game.renderer, sx, sy, 5, 5, 1);
		}
	}
#endif
}

/* Render graphics to the screen */
void render(struct game_state *game)
{
	/* Set draw color to dark gray / black background and clear screen */
	SDL_SetRenderDrawColor(game->renderer, 30, 30, 30, 255);
	SDL_RenderClear(game->renderer);

	/* TODO: Draw your game objects here (e.g., SDL_RenderCopy, SDL_RenderFillRect) */
	draw_background_image(game->renderer);
	if (game->mode != GAME_MODE_PLAY)
		goto done;
	draw_level(game->renderer);

	for (int i = 0; i <= snis_object_pool_highest_object(game->objpool); i++) {
		if (!snis_object_pool_is_allocated(game->objpool, i))
			continue;
		struct game_object *o = &go[i];
		if (object_type[o->type].draw)
			object_type[o->type].draw(game->renderer, o);
	}
	union vec3 colors[4];
	sample_mask_around_object(&go[0], colors);
	draw_debug_rectangles(player, colors);
	debug_sampling();

done:
	/* Present the back buffer to the screen */
	SDL_RenderPresent(game->renderer);
}

/* Free resources and shut down SDL */
void cleanup(struct game_state *game)
{
	image_cleanup(&background_image);
	image_cleanup(&title_screen_image);
	if (game->renderer) {
		SDL_DestroyRenderer(game->renderer);
	}
	if (game->window) {
		SDL_DestroyWindow(game->window);
	}
	snis_object_pool_free_object(game->objpool, player->i);
	SDL_Quit();
	wwviaudio_stop_portaudio();
}

static void move_camera(void)
{
	if (player->current_image >= 0 && player->current_image <= 2) /* player facing right? */
		game.desired_camera_x = player->x + 128.0f; /* move camera to right */
	if (player->current_image >= 3 && player->current_image <= 5) /* player facing left? */
		game.desired_camera_x = player->x - 128.0f; /* move camera to left */

	if (game.camera_x == game.desired_camera_x)
		return;

	float dx = game.desired_camera_x - game.camera_x;
	if (fabs(dx) < 2.1) {
		game.camera_x = game.desired_camera_x;
	}

	game.camera_x += dx / 25.0; /* ease camera towards desired location */

	/* limit camera motion near edges of play area */
	if (game.camera_x >= game.camera_max_x) {
		game.camera_x = game.camera_max_x;
	}
	if (game.camera_x <= game.camera_min_x) {
		game.camera_x = game.camera_min_x;
	}
}

static void setup_audio_system(void)
{
	if (wwviaudio_initialize_portaudio(30, 100) != 0) {
		fprintf(stderr, "Audio system initialization failed.\n");
		exit(1);
	}
	wwviaudio_read_ogg_clip(DIST_ARTILLERY1, "sounds/distant-artillery1.ogg");
	wwviaudio_read_ogg_clip(DIST_ARTILLERY2, "sounds/distant-artillery2.ogg");
	wwviaudio_read_ogg_clip(INCOMING_ARTILLERY, "sounds/incoming-artillery.ogg");
	wwviaudio_read_ogg_clip(LIGHT_MACHINE_GUN, "sounds/light-machine-gun.ogg");
	wwviaudio_read_ogg_clip(RIFLE_BURST_FIRE, "sounds/rifle-burst-fire.ogg");
}

static void maybe_play_ambient_sounds(Uint32 now)
{
	static uint32_t randomseed = 0xa5a5a5a5;
	static Uint32 next_time = 1500;

	if (now > next_time) {
		int s = (int) xorshift(&randomseed) % 5;
		next_time = now + xorshift(&randomseed) % 4500 + 500;
		wwviaudio_add_sound(s);
	}
}

int main(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[])
{
	if (!init_game(&game)) {
		return 1;
	}

	setup_audio_system();
	if (read_png_files(game.renderer))
		exit(1);
	if (read_levels(game.renderer))
		exit(1);
	set_up_object_type_data();
	go[0].x = 100;
	go[0].y = 100;
	go[0].type = OBJTYPE_PLAYER;
	go[0].current_image = 0;

	Uint32 last_frame_time = SDL_GetTicks();

	game.camera_vx = 0.0;
	while (game.is_running) {
		/* Calculate delta time */
		Uint32 current_time = SDL_GetTicks();
		float delta_time = (current_time - last_frame_time) / 1000.0f;

		process_input(&game);
		update(delta_time);
		maybe_play_ambient_sounds(current_time);
		render(&game);

		/* Simple frame rate capping (if VSync isn't doing the job) */
		Uint32 time_to_wait = FRAME_TARGET_TIME - (SDL_GetTicks() - current_time);
		if (time_to_wait > 0 && time_to_wait <= FRAME_TARGET_TIME) {
			SDL_Delay(time_to_wait);
		}
		move_camera();
		last_frame_time = current_time;
	}

	cleanup(&game);
	return 0;
}

