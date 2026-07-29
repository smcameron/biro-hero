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
#include "bline.h"

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
	struct snis_object_pool *objpool; /* controls allocation of go[] */
	struct snis_object_pool *sparkpool;  /* controlls allocation of spark[] */
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

#define PLAYER_VX 2
#define PLAYER_VY 2
#define SOLDIER_VX 2.1
#define SOLDIER_VY 2.1
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
	int shooting;
	int throwing_grenade;
	uint32_t last_shot_time;
	uint32_t last_grenade_time;
	int hit_points;
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
#define OBJTYPE_FLAG 12
#define OBJTYPE_DEAD_SOLDIER 13
#define OBJTYPE_BLOOD_PATCH 14
#define OBJTYPE_BLOOD_DROP 15
#define OBJTYPE_DIRT_SPECK 16
#define OBJTYPE_GRENADE 17
#define OBJTYPE_SMOKE1 18
#define OBJTYPE_SMOKE2 19
#define OBJTYPE_SMOKE3 20
#define OBJTYPE_SMOKE4 21
#define OBJTYPE_SMOKE5 22
#define OBJTYPE_SMOKE6 23
#define OBJTYPE_SMOKE7 24
#define OBJTYPE_SMOKE8 25
#define OBJTYPE_SMOKE9 26

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
struct image hero_ladder_1 = { "images/hero-ladder-1.png", NULL, 0, 0, 0, 0, NULL, };
struct image hero_ladder_2 = { "images/hero-ladder-2.png", NULL, 0, 0, 0, 0, NULL, };
struct image hero_ladder_3 = { "images/hero-ladder-3.png", NULL, 0, 0, 0, 0, NULL, };
struct image russflag = { "images/flag.png", NULL, 0, 0, 0, 0, NULL, };
struct image deadsoldier1 = { "images/deadsoldier1.png", NULL, 0, 0, 0, 0, NULL, };
struct image deadsoldier2 = { "images/deadsoldier2.png", NULL, 0, 0, 0, 0, NULL, };
struct image deadsoldier3 = { "images/deadsoldier3.png", NULL, 0, 0, 0, 0, NULL, };
struct image deadsoldier4 = { "images/deadsoldier4.png", NULL, 0, 0, 0, 0, NULL, };
struct image bloodpatch1 = { "images/bloodpatch1.png", NULL, 0, 0, 0, 0, NULL, };
struct image bloodpatch2 = { "images/bloodpatch2.png", NULL, 0, 0, 0, 0, NULL, };
struct image bloodpatch3 = { "images/bloodpatch3.png", NULL, 0, 0, 0, 0, NULL, };
struct image bloodpatch4 = { "images/bloodpatch4.png", NULL, 0, 0, 0, 0, NULL, };
struct image blooddrop1 = { "images/blood-drop1.png", NULL, 0, 0, 0, 0, NULL, };
struct image blooddrop2 = { "images/blood-drop2.png", NULL, 0, 0, 0, 0, NULL, };
struct image blooddrop3 = { "images/blood-drop3.png", NULL, 0, 0, 0, 0, NULL, };
struct image dirtspeck1 = { "images/dirt-speck1.png", NULL, 0, 0, 0, 0, NULL, };
struct image dirtspeck2 = { "images/dirt-speck2.png", NULL, 0, 0, 0, 0, NULL, };
struct image dirtspeck3 = { "images/dirt-speck3.png", NULL, 0, 0, 0, 0, NULL, };
struct image grenade = { "images/grenade.png", NULL, 0, 0, 0, 0, NULL, };
struct image smoke[] = {
	{ "images/smoke1.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/smoke2.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/smoke3.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/smoke4.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/smoke5.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/smoke6.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/smoke7.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/smoke8.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/smoke9.png", NULL, 0, 0, 0, 0, NULL, },
};

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
	{ 0, 650.0f, 343.0f, OBJTYPE_SOLDIER, },
	{ 0, 1554.0f, 265.0f, OBJTYPE_SOLDIER, },
	{ 0, 2048.0f, 204.0f, OBJTYPE_SOLDIER, },
	{ 0, 2456.0f, 254.0f, OBJTYPE_SOLDIER, },
	{ 0, 3918.0f, 258.0f, OBJTYPE_SOLDIER, },
	{ 0, 2730.0f, 182.0f, OBJTYPE_SOLDIER, },
	{ 0, 2922.0f, 144.0f, OBJTYPE_SOLDIER, },
	{ 0, 494.0f, 600.0f, OBJTYPE_FLAG, },
	{ 0, 2548.0f, 608.0f, OBJTYPE_FLAG, },
	{ 0, 3952.0f, 540.0f, OBJTYPE_FLAG, },
	{ 0, 2112.0f, 718.0f, OBJTYPE_BARREL, },
};

#define MAXSPARKS 10000

static struct spark_object {
	float x, y, vx, vy;
	int alive;
	int type;
	int current_image;
} spark[MAXSPARKS] = { 0 }; /* blood drops and dirt specks from bullet impacts */

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

static int keypressed[8] = { 0 }; /* indexed by enum keyaction */

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
#define AR15_SHOT 5
#define GRENADE_EXPLOSION 6
#define GRENADE_BOUNCE1 7
#define GRENADE_BOUNCE2 8
#define PENUMBRA_MUSIC 9

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

static float random_angle_rads(void)
{
	static uint32_t seed = 0xBADA55;

	uint32_t deg = (xorshift(&seed) % 360);
	return (float) deg * M_PI / 180.0f;
}

static int randn(int n)
{
	static uint32_t seed = 0xa5a5a5a5;
	uint32_t x = xorshift(&seed);
	x &= 0x7fffffff; /* make sure it's positive */
	return (int) (x % n);
}

static void move_spark(struct spark_object *s, float delta_time)
{
	switch (s->type) {
	case OBJTYPE_DIRT_SPECK:
	case OBJTYPE_BLOOD_DROP:
		s->x += s->vx  /* * delta_time */ ;
		s->y += s->vy /* * delta_time */ ;
#define SPARK_GRAVITY 0.1f
		s->vy += SPARK_GRAVITY;
		if (s->alive > 0)
			s->alive--;
		break;
	case OBJTYPE_SMOKE1:
	case OBJTYPE_SMOKE2:
	case OBJTYPE_SMOKE3:
	case OBJTYPE_SMOKE4:
	case OBJTYPE_SMOKE5:
	case OBJTYPE_SMOKE6:
	case OBJTYPE_SMOKE7:
	case OBJTYPE_SMOKE8:
	case OBJTYPE_SMOKE9:
		s->x += s->vx;
		s->y += s->vy;
		/* no gravity on smoke */
		if (s->alive > 0)
			s->alive--;
		if (s->alive % 10 == 0) {
			if (s->type < OBJTYPE_SMOKE9)
				s->type++; /* make smoke smaller */
		}
		break;
	default:
		break;
	}
}

static void move_sparks(float delta_time)
{
	/* Move all the sparks */
	for (int i = 0; i <= snis_object_pool_highest_object(game.sparkpool); i++) {
		if (!snis_object_pool_is_allocated(game.sparkpool, i))
			continue;
		move_spark(&spark[i], delta_time);
	}
	/* Free dead sparks */
	for (int i = 0; i <= snis_object_pool_highest_object(game.sparkpool); i++) {
		if (!snis_object_pool_is_allocated(game.sparkpool, i))
			continue;
		if (!spark[i].alive)
			snis_object_pool_free_object(game.sparkpool, i);
	}
}

static void add_spark(int type, float x, float y, float v,
		float vxbias, float vybias, int time_to_live)
{
	int i = snis_object_pool_alloc_obj(game.sparkpool);
	if (i < 0)
		return;
	struct spark_object *s = &spark[i];
	s->x = x;
	s->y = y;
	float angle = random_angle_rads();
	float speed = (float) randn((int) (v * 100)) / 100.0f;
	s->vx = speed * cos(angle);
	s->vy = speed * sin(angle);
	s->vx += vxbias;
	s->vy += vybias;
	s->type = type;
	s->alive = time_to_live;
}

static void add_sparks(int count, int type, float x, float y, float v,
		float vxbias, float vybias, int time_to_live)
{
	for (int i = 0; i < count; i++)
		add_spark(type, x, y, v, vxbias, vybias, time_to_live);
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
	x += load_png_image(renderer, &hero_ladder_1, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &hero_ladder_2, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &hero_ladder_3, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &russflag, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &deadsoldier1, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &deadsoldier2, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &deadsoldier3, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &deadsoldier4, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &bloodpatch1, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &bloodpatch2, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &bloodpatch3, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &bloodpatch4, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &blooddrop1, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &blooddrop2, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &blooddrop3, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &dirtspeck1, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &dirtspeck2, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &dirtspeck3, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &grenade, IMAGE_MODE_TEXTURE);
	for (int i = 0; i < 9; i++)
		x+= load_png_image(renderer, &smoke[i], IMAGE_MODE_TEXTURE);
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
	player->shooting = 0;
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
		if (o->type == OBJTYPE_SOLDIER)
			o->hit_points = 1 + randn(3);
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

	/* Define the clipping region: 100px from left and right edges */
	SDL_Rect clip_rect = { 100, 0, game.window_width - 200, game.window_height };
	SDL_RenderSetClipRect(renderer, &clip_rect); /* Apply the clipping rectangle */
	SDL_SetTextureBlendMode(im[i]->texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, im[i]->texture, NULL, &destrect);
	SDL_RenderSetClipRect(renderer, NULL); /* Remove the clipping rectangle */
}

static void draw_spark(SDL_Renderer *renderer, struct spark_object *s)
{
	struct object_type_data *odt = &object_type[s->type];
	struct image **im = odt->image;
	int i = s->current_image;
	float windowx_scale = game.window_width / WINDOW_WIDTH;
	float windowy_scale = game.window_height / WINDOW_HEIGHT;
	float scx = odt->scalex * windowx_scale;
	float scy = odt->scaley * windowy_scale;
	float w = scx * im[i]->width;
	float h = scy * im[i]->height;

	SDL_Rect destrect = {
		world_to_screenx(s->x - 0.5 * w),
		world_to_screeny(s->y - 0.5 * h),
		w, h };

	/* Define the clipping region: 100px from left and right edges */
	SDL_Rect clip_rect = { 100, 0, game.window_width - 200, game.window_height };
	SDL_RenderSetClipRect(renderer, &clip_rect); /* Apply the clipping rectangle */
	SDL_SetTextureBlendMode(im[i]->texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, im[i]->texture, NULL, &destrect);
	SDL_RenderSetClipRect(renderer, NULL); /* Remove the clipping rectangle */
}

static void set_up_object_type_data(void)
{
	/* Setup OBJTYPE_PLAYER data */
	int n = OBJTYPE_PLAYER;
	object_type[n].image = malloc(10 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &hero_right_1;
	object_type[n].image[1] = &hero_right_2;
	object_type[n].image[2] = &hero_right_3;
	object_type[n].image[3] = &hero_left_1;
	object_type[n].image[4] = &hero_left_2;
	object_type[n].image[5] = &hero_left_3;
	object_type[n].image[6] = &hero_ladder_1;
	object_type[n].image[7] = &hero_ladder_2;
	object_type[n].image[8] = &hero_ladder_3;
	object_type[n].image[9] = &hero_ladder_2; /* hero_ladder_2 is deliberately repeated here. */
	object_type[n].nimages = 10;
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

	n = OBJTYPE_FLAG;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &russflag;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.35;
	object_type[n].scaley = 0.35;
	object_type[n].draw = draw_object;

	n = OBJTYPE_DEAD_SOLDIER;
	object_type[n].image = malloc(4 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &deadsoldier1;
	object_type[n].image[1] = &deadsoldier2;
	object_type[n].image[2] = &deadsoldier3;
	object_type[n].image[3] = &deadsoldier4;
	object_type[n].nimages = 4;
	object_type[n].scalex = 0.18;
	object_type[n].scaley = 0.18;
	object_type[n].draw = draw_object;

	n = OBJTYPE_BLOOD_PATCH;
	object_type[n].image = malloc(4 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &bloodpatch1;
	object_type[n].image[1] = &bloodpatch2;
	object_type[n].image[2] = &bloodpatch3;
	object_type[n].image[3] = &bloodpatch4;
	object_type[n].nimages = 4;
	object_type[n].scalex = 0.18;
	object_type[n].scaley = 0.18;
	object_type[n].draw = draw_object;

	n = OBJTYPE_BLOOD_DROP;
	object_type[n].image = malloc(3 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &blooddrop1;
	object_type[n].image[1] = &blooddrop2;
	object_type[n].image[2] = &blooddrop3;
	object_type[n].nimages = 3;
	object_type[n].scalex = 1.0;
	object_type[n].scaley = 1.0;
	object_type[n].draw = draw_object;

	n = OBJTYPE_DIRT_SPECK;
	object_type[n].image = malloc(3 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &dirtspeck1;
	object_type[n].image[1] = &dirtspeck2;
	object_type[n].image[2] = &dirtspeck3;
	object_type[n].nimages = 3;
	object_type[n].scalex = 1.0;
	object_type[n].scaley = 1.0;
	object_type[n].draw = draw_object;

	n = OBJTYPE_GRENADE;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &grenade;
	object_type[n].nimages = 1;
	object_type[n].scalex = 1.0;
	object_type[n].scaley = 1.0;
	object_type[n].draw = draw_object;

	for (n = 0 + OBJTYPE_SMOKE1; n < 9 + OBJTYPE_SMOKE1; n++) {
		object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
		object_type[n].image[0] = &smoke[n - OBJTYPE_SMOKE1];
		object_type[n].nimages = 1;
		object_type[n].scalex = 0.5;
		object_type[n].scaley = 0.5;
		object_type[n].draw = draw_object;
	}
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
	snis_object_pool_setup(&game->sparkpool, MAXSPARKS);
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
	case SDLK_z:
		keypressed[keyshoot] = 1;
		break;
	case SDLK_x:
		keypressed[keygrenade] = 1;
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
	case SDLK_z:
		keypressed[keyshoot] = 0;
		break;
	case SDLK_x:
		keypressed[keygrenade] = 0;
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

static void move_soldier(struct game_object *o, float delta_time)
{
	/* 1. First frame initialization */
	if (o->vx == 0.0f && o->vy == 0.0f && !o->is_climbing) {
		static uint32_t init_seed = 0xDEADBEEF;
		o->vx = ((xorshift(&init_seed) % 2) == 0) ? SOLDIER_VX : -SOLDIER_VX;
	}

	bool on_ladder = is_ladder(o->x, o->y);

	/* 2. Ladder AI Decision Making */
	if (on_ladder && !o->is_climbing) {
		o->is_climbing = 1;
		o->vy = 0.0f;

		if (o->vx == 0.0f) {
			/* Reached top/bottom. Pick a horizontal direction and walk away. */
			static uint32_t walk_seed = 0x12345678;
			o->vx = ((xorshift(&walk_seed) % 2) == 0) ? SOLDIER_VX : -SOLDIER_VX;
		} else {
			/* Encountered a ladder while walking. Make a choice. */
			static uint32_t choice_seed = 0x87654321;
			int choice = xorshift(&choice_seed) % 3;

			if (choice == 0) {
				/* Climb */
				o->vx = 0.0f;
				o->vy = ((xorshift(&choice_seed) % 2) == 0) ? SOLDIER_VY : -SOLDIER_VY;
			} else if (choice == 1) {
				/* Pass by (do nothing, vx remains, vy=0) */
			} else {
				/* Turn around */
				o->vx = -o->vx;
			}
		}
	} else if (!on_ladder && o->is_climbing) {
		/* Walked or climbed completely off the ladder */
		o->is_climbing = 0;

		if (o->vx == 0.0f) {
			/* Climbed off the top or bottom while going vertically. Resume walking. */
			static uint32_t end_ladder_seed = 0xABCDEF01;
			o->vx = ((xorshift(&end_ladder_seed) % 2) == 0) ? SOLDIER_VX : -SOLDIER_VX;
			o->vy = 0.0f;
		}
	}

	/* 3. Execute Movement */
	if (o->vx != 0.0f) {
		float intended_vx = o->vx;
		move_horizontal(o, o->vx);

		/* Did we hit a wall? */
		if (o->vx == 0.0f) {
			if (o->is_climbing) {
				/* Blocked horizontally while inside a ladder shaft!
				 * Force a vertical climb so they don't jitter forever. */
				static uint32_t shaft_seed = 0x99999999;
				o->vy = ((xorshift(&shaft_seed) % 2) == 0) ? SOLDIER_VY : -SOLDIER_VY;
			} else {
				/* Normal wall hit, turn around */
				o->vx = -intended_vx;
			}
		}
	}

	apply_gravity_and_vertical_movement(o, delta_time);

	/* 4. Animation */
	o->ticks += delta_time;
	if (o->ticks > o->next_animation_tick) {
		o->next_animation_tick = o->ticks + 0.15f;
		if (o->vx > 0.0f) {
			o->current_image = (o->current_image == 0) ? 1 : 0;
		} else if (o->vx < 0.0f) {
			o->current_image = (o->current_image == 2) ? 3 : 2;
		} else {
			o->current_image = (o->current_image == 0) ? 1 : 0;
		}
	}
}
static int bullet_shot_sampler(int x, int y, void *context);

#define GRENADE_FUSE_TIME_SECS 4.0
#define GRENADE_FRAGMENT_COUNT 30
#define GRENADE_LAUNCH_SPEED 6.0f
static void move_grenade(struct game_object *o, float delta_time)
{
	struct object_type_data *odt = &object_type[o->type];
	float half_width = (odt->image[0]->width * odt->scalex) / 2.0f;
	float half_height = (odt->image[0]->height * odt->scaley) / 2.0f;

	o->vy += SPARK_GRAVITY;

	/* Horizontal movement and collision check */
	float target_x = o->x + o->vx;
	float check_x = o->vx > 0 ? target_x + half_width : target_x - half_width;

	if (is_passable(check_x, o->y)) {
		o->x = target_x;
	} else {
		/* Hit a wall: bounce horizontally with dampening and slight randomness */
		o->vx = -o->vx * 0.7f;

		/* Add minor random vertical kick */
		static uint32_t bounce_seed = 0x13579BDF;
		float random_fudge = ((float)(xorshift(&bounce_seed) % 20) - 10.0f) / 100.0f;
		o->vy += random_fudge;
		if (fabsf(o->vx) > 1.0f) /* prevent rattling */
			wwviaudio_add_sound(GRENADE_BOUNCE1 + ((int) (random_fudge * 100) & 0x01));
	}

	/* Vertical movement and collision check */
	float target_y = o->y + o->vy;
	float check_y = o->vy > 0 ? target_y + half_height : target_y - half_height;

	if (is_passable(o->x, check_y)) {
		o->y = target_y;
	} else {
		/* Hit floor or ceiling: bounce vertically with dampening */
		if (o->vy > 0) /* Ground bounce: also add a bit of friction/damping to vx */
			o->vx *= 0.8f;
		o->vy = -o->vy * 0.5f; /* Less elastic bounce for the floor */

		/* Add random horizontal spin/deflection on impact */
		static uint32_t floor_seed = 0x2468ACE0;
		float random_fudge = ((float)(xorshift(&floor_seed) % 40) - 20.0f) / 100.0f;
		o->vx += random_fudge;
		if (fabsf(o->vy) > 1.0f) /* prevent rattling */
			wwviaudio_add_sound(GRENADE_BOUNCE1 + ((int) (random_fudge * 100) & 0x01));
	}

	/* Fuse expiration check */
	uint32_t now = SDL_GetTicks();
	if (now > o->next_animation_tick) {
		/* Explode the grenade */
		for (int i = 0; i < GRENADE_FRAGMENT_COUNT; i++) {
			float angle = random_angle_rads();
			float targetx = o->x + cos(angle) * 1000.0f;
			float targety = o->y + sin(angle) * 1000.0f;
			bline(o->x, o->y, targetx, targety, bullet_shot_sampler, o);
		}
		snis_object_pool_free_object(game.objpool, o - &go[0]);
		add_sparks(10, OBJTYPE_SMOKE1, o->x, o->y, 2.0f, 0.0, -2.0f, 99);
		wwviaudio_add_sound(GRENADE_EXPLOSION);
	}
}

static void move_objects(float delta_time)
{
	for (int i = 0; i <= snis_object_pool_highest_object(game.objpool); i++) {
		if (!snis_object_pool_is_allocated(game.objpool, i))
			continue;
		struct game_object *o = &go[i];
		switch (o->type) {
		case OBJTYPE_SOLDIER:
			move_soldier(o, delta_time);
			break;
		case OBJTYPE_GRENADE:
			move_grenade(o, delta_time);
			break;
		default:
			break;
		}
	}
}

static void reap_dead_soldier(struct game_object *o)
{
	int n = snis_object_pool_alloc_obj(game.objpool);
	if (n < 0)
		goto get_rid_of_soldier;
	struct game_object *blood = &go[n];
	struct object_type_data *odt = &object_type[o->type];
	float half_height = (odt->image[0]->height * odt->scaley) / 2.0f;
	blood->x = o->x;
	blood->y = o->y + half_height + 8;
	blood->type = OBJTYPE_BLOOD_PATCH;
	blood->current_image = randn(object_type[blood->type].nimages);
	blood->vx = 0.0f;
	blood->vy = 0.0f;
	blood->is_grounded = o->is_grounded;
	blood->is_climbing = 0;

	n = snis_object_pool_alloc_obj(game.objpool);
	if (n < 0)
		goto get_rid_of_soldier;

	struct game_object *body = &go[n];
	body->x = o->x;
	body->y = o->y + half_height;
	body->type = OBJTYPE_DEAD_SOLDIER;
	body->current_image = randn(object_type[body->type].nimages);
	printf("body nimages = %d\n", object_type[body->type].nimages);
	printf("current body = %d\n", body->current_image);
	body->vx = 0.0f;
	body->vy = 0.0f;
	body->is_grounded = o->is_grounded;
	body->is_climbing = 0;

get_rid_of_soldier:
	snis_object_pool_free_object(game.objpool, o - &go[0]);
}

static void reap_dead_soldiers(void)
{
	for (int i = 0; i <= snis_object_pool_highest_object(game.objpool); i++) {
		if (!snis_object_pool_is_allocated(game.objpool, i))
			continue;
		struct game_object *o = &go[i];
		switch (o->type) {
		case OBJTYPE_SOLDIER:
			if (o->hit_points == 0)
				reap_dead_soldier(o);
			break;
		default:
			break;
		}
	}
}

static void player_shoot(struct game_object *o)
{
	if (player->is_climbing) /* can't shoot from ladder */
		return;
	uint32_t now = SDL_GetTicks();
	if (now - player->last_shot_time < 100) /* throttle shots to 10 / sec */
		return;
	player->last_shot_time = now;
	wwviaudio_add_sound(AR15_SHOT);
	player->shooting = 1;
}

static void player_throw_grenade(struct game_object *o)
{
	if (player->is_climbing) /* can't shoot from ladder */
		return;
	uint32_t now = SDL_GetTicks();
	if (now - player->last_grenade_time < 1500) /* throttle grenades to 2 per 3 secs */
		return;
	player->last_grenade_time = now;
	/* TODO: Add grenade throwing sound */
	player->throwing_grenade = 1;
}

static int bullet_shot_sampler(int x, int y, void *context)
{
	struct game_object *shooter = context;
	if (x < 0)
		return -1;
	if (x >= 4096)
		return -1;
	if (y < 0)
		return -1;
	if (y >= 768)
		return -1;

	if (is_passable(x, y))  {
		/* Check for collisions with soldiers */
		for (int i = 0; i <= snis_object_pool_highest_object(game.objpool); i++) {
			if (!snis_object_pool_is_allocated(game.objpool, i))
				continue;
			struct game_object *o = &go[i];
			if (o->type != OBJTYPE_SOLDIER)
				continue;
			float dist2 = (o->x - x) * (o->x -x) + (o->y - y) * (o->y - y);
			if (dist2 >= 25.0f)
				continue;
			fprintf(stderr, "Hit soldier %d\n", i);
			float vxb = 0.0f;
			if (shooter && snis_object_pool_is_allocated(game.objpool, shooter - &go[0])) {
				if (shooter->type == OBJTYPE_PLAYER) {
					if (shooter->current_image < 3)
						vxb = -2.0f;
					else
						vxb = 2.0f;
				}
			}
			add_sparks(20, OBJTYPE_BLOOD_DROP, x, y, 2.0f, vxb, 0.0f, 20);
			if (o->hit_points > 0)
				o->hit_points--;
			return -1;
		}
		return 0;
	} else {
		add_sparks(20, OBJTYPE_DIRT_SPECK, x, y, 2.0f, 0.0f, -2.0f, 20);
		return -1;
	}
}

/* Update game logic (positions, physics, AI) based on delta time */
void update(float delta_time)
{
	int do_player_animation = 0;


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
	if (keypressed[keyshoot]) {
		player_shoot(player);
	}
	if (keypressed[keygrenade]) {
		player_throw_grenade(player);
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
			if (player->is_climbing) {
				/* Ladder animation sequence: 6,7,8,9, repeating (7 and 9 are the same image) */
				if (player->current_image < 6 || player->current_image > 9) {
					player->current_image = 6;
				} else {
					player->current_image++;
					if (player->current_image > 9)
						player->current_image = 6;
				}
			} else {
				player->current_image++;
				if (player->current_image > 6) { /* we were climbing, but no more */
					if (player->vx >= 0)
						player->current_image = 0;
					else
						player->current_image = 3;
				} else {
					if (player->current_image == 3)
						player->current_image = 0;
					if (player->current_image == 6)
						player->current_image = 3;
				}
			}
		}
	}
	if (player->shooting) {
		player->shooting = 0;
		/* TODO: add muzzle flash and do some ray casting */
		int targetx, targety;
		static uint32_t seed = 0xBADBABE;
		if (player->current_image >= 0 && player->current_image <= 2) /* facing right? */
			targetx = player->x + 1000;
		else
			targetx = player->x - 1000;
		targety = player->y + (xorshift(&seed) & 0x03f) - 63;
		bline(player->x, player->y, targetx, targety, bullet_shot_sampler, player);
	}
	if (player->throwing_grenade) {
		float angle;
		int i = snis_object_pool_alloc_obj(game.objpool);
		if (i < 0)
			goto done;
		struct game_object *grenade = &go[i];
		if (player->current_image >= 0 && player->current_image <= 2) /* facing right? */
			angle = 45.0f * M_PI / 180.0f;
		else
			angle = (180.0f - 45.0f) * M_PI / 180.0f;
		grenade->type = OBJTYPE_GRENADE;
		grenade->x = player->x;
		grenade->y = player->y;
		grenade->vx = GRENADE_LAUNCH_SPEED * cos(angle);
		grenade->vy = GRENADE_LAUNCH_SPEED * -sin(angle);
		grenade->next_animation_tick = SDL_GetTicks() + GRENADE_FUSE_TIME_SECS * 1000.0f;
		grenade->is_climbing = 0;
		grenade->is_grounded = 0;
		grenade->current_image = 0;
		player->throwing_grenade = 0;
		player->last_grenade_time = SDL_GetTicks();
	}
done:
	move_objects(delta_time);
	move_sparks(delta_time);
	reap_dead_soldiers();
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

	/* Draw objects */
	for (int i = 0; i <= snis_object_pool_highest_object(game->objpool); i++) {
		if (!snis_object_pool_is_allocated(game->objpool, i))
			continue;
		struct game_object *o = &go[i];
		if (object_type[o->type].draw)
			object_type[o->type].draw(game->renderer, o);
	}

	/* Draw sparks */
	for (int i = 0; i <= snis_object_pool_highest_object(game->sparkpool); i++) {
		if (!snis_object_pool_is_allocated(game->sparkpool, i))
			continue;
		draw_spark(game->renderer, &spark[i]);
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
	wwviaudio_read_ogg_clip(AR15_SHOT, "sounds/ar15-shot.ogg");
	wwviaudio_read_ogg_clip(GRENADE_EXPLOSION, "sounds/grenade-explosion.ogg");
	wwviaudio_read_ogg_clip(GRENADE_BOUNCE1, "sounds/grenade-bounce-1.ogg");
	wwviaudio_read_ogg_clip(GRENADE_BOUNCE2, "sounds/grenade-bounce-2.ogg");
	wwviaudio_read_ogg_clip(PENUMBRA_MUSIC, "sounds/Penumbra.ogg");
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
	wwviaudio_play_music(PENUMBRA_MUSIC);
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

