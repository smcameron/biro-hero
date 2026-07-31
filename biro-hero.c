#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#include "png_utils.h"
#include "snis_alloc.h"
#include "stacktrace.h"
#include "vec3.h"
#include "wwviaudio.h"
#include "ogg_to_pcm.h"
#include "bline.h"

/* DIFFICULTY_LEVEL Scales damage */
#define DIFFICULTY_LEVEL 5.0

#define ARRAYSIZE(x) (int) (sizeof(x) / sizeof((x)[0]))

#define WINDOW_WIDTH (1024 * 1.2)
#define WINDOW_HEIGHT (768 * 1.2)
#define TARGET_FPS 60
#define FRAME_TARGET_TIME (1000 / TARGET_FPS)

#define GAME_MODE_TITLE_SCREEN 0
#define GAME_MODE_PLAY 1
#define GAME_MODE_EDIT 2
#define MENU_HEIGHT 80
#define MENU_ITEM_WIDTH 80

static int menu_is_visible = 0;
static int dragged_object_index = -1;
static int dragging_camera = 0;
static int last_mouse_x = 0;

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
	int score;
	int lives;
	int soldier_count;
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
	uint32_t next_lookaround_time;
	int hidden;
} go[MAX_GAME_OBJS];

static struct game_object *player;
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
#define OBJTYPE_MUZZLE_FLASH 27
#define OBJTYPE_MEDICINE_BOX 28
#define NUM_OBJECT_TYPES 29

static const struct obj_type_name_entry {
	char *name;
	int type;
} obj_type_name[] = {
	{"player", OBJTYPE_PLAYER },
	{"wallmap", OBJTYPE_WALLMAP },
	{"desk", OBJTYPE_DESK },
	{"shells", OBJTYPE_SHELLS },
	{"radar_console", OBJTYPE_RADAR_CONSOLE },
	{"bed", OBJTYPE_BED },
	{"crates", OBJTYPE_CRATES },
	{"dirtclod", OBJTYPE_DIRTCLOD },
	{"soldier", OBJTYPE_SOLDIER },
	{"barrel", OBJTYPE_BARREL },
	{"tnt", OBJTYPE_TNT },
	{"ammo", OBJTYPE_AMMO },
	{"flag", OBJTYPE_FLAG },
	{"body", OBJTYPE_DEAD_SOLDIER },
	{"blood_patch", OBJTYPE_BLOOD_PATCH },
	{"blood_drop", OBJTYPE_BLOOD_DROP },
	{"dirt_speck", OBJTYPE_DIRT_SPECK },
	{"grenade", OBJTYPE_GRENADE },
	{"smoke1", OBJTYPE_SMOKE1 },
	{"smoke1", OBJTYPE_SMOKE2 },
	{"smoke1", OBJTYPE_SMOKE3 },
	{"smoke1", OBJTYPE_SMOKE4 },
	{"smoke1", OBJTYPE_SMOKE5 },
	{"smoke1", OBJTYPE_SMOKE6 },
	{"smoke1", OBJTYPE_SMOKE7 },
	{"smoke1", OBJTYPE_SMOKE8 },
	{"smoke1", OBJTYPE_SMOKE9 },
	{"muzzle_flash", OBJTYPE_MUZZLE_FLASH },
	{"medicine_box", OBJTYPE_MEDICINE_BOX },
};

static struct object_type_data {
	struct image **image;
	int nimages;
	float scalex, scaley;
	void (*draw)(SDL_Renderer *renderer, struct game_object *o);
} object_type[NUM_OBJECT_TYPES] = { 0 };

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
struct image muzzle_flash_right = { "images/muzzle-flash-right.png", NULL, 0, 0, 0, 0, NULL, };
struct image muzzle_flash_left = { "images/muzzle-flash-left.png", NULL, 0, 0, 0, 0, NULL, };
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
struct image health_bar = { "images/health-bar.png", NULL, 0, 0, 0, 0, NULL, };
struct image medicine_box = { "images/medicine-box.png", NULL, 0, 0, 0, 0, NULL, };
struct image digit[] = {
	{ "images/zero.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/one.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/two.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/three.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/four.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/five.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/six.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/seven.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/eight.png", NULL, 0, 0, 0, 0, NULL, },
	{ "images/nine.png", NULL, 0, 0, 0, 0, NULL, },
};
struct image healthlabel = { "images/healthlabel.png", NULL, 0, 0, 0, 0, NULL };
struct image scorelabel = { "images/scorelabel.png", NULL, 0, 0, 0, 0, NULL };
struct image wasted = { "images/wasted.png", NULL, 0, 0, 0, 0, NULL };

#define MAX_OBJECTS 1000
static struct static_object_entry {
	int level;
	float x, y;
	int type;
} static_object[MAX_OBJECTS];
static int nobjects = 0;

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


/* one liners */
#define FIRST_ONE_LINER 10
#define LAST_ONE_LINER 18
#define TALKIN_BOUT 10
#define SPRUNG_A_LEAK 11
#define SHOUDA_STAYED_HOME 12
#define SUX_2_B_U 13
#define JUST_DESSERTS 14
#define FERTILIZER 15
#define DEATHWISH 16
#define CARGO200 17
#define LEAD_POISON 18
/* End of one liners */

#define THATS_THE_STUFF 19
#define OWWW 20
#define AK47_SHOT 21
#define BUNKER_CLEARED 22

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
	if (n == 0)
		return 0;

	static uint32_t seed = 0xa5a5a5a5;
	uint32_t x = xorshift(&seed);
	x &= 0x7fffffff; /* make sure it's positive */
	return (int) (x % n);
}

static int find_object_at(float wx, float wy)
{
	/* Iterate backwards to select the object drawn on top */
	for (int i = snis_object_pool_highest_object(game.objpool); i >= 0; i--) {
		if (!snis_object_pool_is_allocated(game.objpool, i))
			continue;
		struct game_object *o = &go[i];
		struct object_type_data *odt = &object_type[o->type];
		int img_idx = o->current_image;

		float w = odt->image[img_idx]->width * odt->scalex;
		float h = odt->image[img_idx]->height * odt->scaley;

		/* Check if world coordinates fall within the object's bounding box */
		if (wx >= o->x - w / 2.0f && wx <= o->x + w / 2.0f &&
		    wy >= o->y - h / 2.0f && wy <= o->y + h / 2.0f) {
			return i;
		}
	}
	return -1;
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
	x += load_png_image(renderer, &muzzle_flash_right, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &muzzle_flash_left, IMAGE_MODE_TEXTURE);
	for (int i = 0; i < 9; i++)
		x+= load_png_image(renderer, &smoke[i], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &health_bar, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &medicine_box, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[0], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[1], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[2], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[3], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[4], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[5], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[6], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[7], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[8], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &digit[9], IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &healthlabel, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &scorelabel, IMAGE_MODE_TEXTURE);
	x += load_png_image(renderer, &wasted, IMAGE_MODE_TEXTURE);
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
	memset(player, 0, sizeof(*player));
	player->i = i;
	player->x = 80;
	player->y = 360;
	player->type = OBJTYPE_PLAYER;
	player->ticks = 0.0;
	player->next_animation_tick = 0.0;
	player->is_grounded = 0;
	player->is_climbing = 0;
	player->vx = 0.0f;
	player->vy = 0.0f;
	player->shooting = 0;
	player->hit_points = 255;
	player->hidden = 0;
}

void save_level_items(const char *filename, struct game_state *game)
{
	FILE *f = fopen(filename, "w");
	if (!f) {
		printf("Error: Could not open %s for saving.\n", filename);
		return;
	}

	fprintf(f, "level: %d\n", 1);
	for (int i = 0; i <= snis_object_pool_highest_object(game->objpool); i++) {
		if (!snis_object_pool_is_allocated(game->objpool, i))
			continue;

		struct game_object *o = &go[i];
		/* Skip objects you don't want saved, like the player, if applicable */
		if (o->type == OBJTYPE_PLAYER)
			continue;
		fprintf(f, "	%f %f %s\n", o->x, o->y, obj_type_name[o->type].name);
	}

	fclose(f);
	printf("Level saved successfully to %s\n", filename);
}

static int read_level_items(char *filename)
{
	int line = 0;
	char buffer[1024];
	char *c;
	int current_level = -1;

	FILE *f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "Cannot open %s: %s\n", filename, strerror(errno));
		return -1;
	}

	nobjects = 0;
	do {
		c = fgets(buffer, sizeof(buffer), f);
		if (!c) {
			if (feof(f))
				break;
			fprintf(stderr, "Error reading file %s: %s\n", filename, strerror(errno));
			return -1;
		}
		line++;
		if (buffer[0] == '#')
			continue;
		if (strncmp(buffer, "\n", sizeof(buffer)) == 0)
			continue;
		int iv = 0;
		int rc = sscanf(buffer, " level: %d", &iv);
		if (rc == 1) {
			if (iv < 1 || iv > 100) {
				fprintf(stderr, "%s:%d: level %d out of range 1 - 100\n",
						filename, line, iv);
				return -1;
			}
			current_level = iv - 1;
			continue;
		}

		float x, y;
		char object_type[1000];
		rc = sscanf(buffer, " %f %f %100s", &x, &y, object_type);
		if (rc == 3) {
			if (x < 0 || x >= 4096 || y < 0 || y >= 768) {
				fprintf(stderr, "%s:%d Coordinates out of range: %g,%g\n",
					filename, line, x, y);
				return -1;
			}
			int found = 0;
			for (int i = 0; i < ARRAYSIZE(obj_type_name); i++) {
				if (strncasecmp(obj_type_name[i].name,
					object_type, sizeof(object_type)) == 0) {
					static_object[nobjects].level = current_level;
					static_object[nobjects].type = obj_type_name[i].type;
					static_object[nobjects].x = x;
					static_object[nobjects].y = y;
					nobjects++;
					found = 1;
					printf("Added %d %g, %g, %s (%d)\n", current_level, x, y,
						obj_type_name[i].name, obj_type_name[i].type);
					break;
				}
			}
			if (!found) {
				fprintf(stderr, "%s:%d bad object type '%s'\n",
					filename, line, object_type);
				return -1;
			}
		} else {
			fprintf(stderr, "%s:%d: Bad line, expected 'level:', or x, y object-type\n",
					filename, line);
			return -1;
		}
	} while (1);
	printf("nobjects = %d\n", nobjects);
	return 0;
}

static void set_up_level(int l)
{
	for (int i = 0; i < nobjects; i++) {
		if (static_object[i].level != l)
			continue;
		printf("Allocating object (total %d)\n", nobjects);
		int n = snis_object_pool_alloc_obj(game.objpool);
		if (n < 0) {
			fprintf(stderr, "Out of objects at %s:%d\n", __FILE__, __LINE__);
			abort();
		}
		struct game_object *o = &go[n];
		memset(o, 0, sizeof(*o));
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
		o->next_lookaround_time = 0;
		o->hidden = 0;
		if (o->type == OBJTYPE_SOLDIER) {
			o->hit_points = 1 + randn(3);
			o->next_lookaround_time = SDL_GetTicks() + 5000 + o->i * 23;
		}
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

	if (o->hidden)
		return;

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

	n = OBJTYPE_MUZZLE_FLASH;
	object_type[n].image = malloc(2 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &muzzle_flash_right;
	object_type[n].image[1] = &muzzle_flash_left;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.3;
	object_type[n].scaley = 0.3;
	object_type[n].draw = draw_object;

	for (n = 0 + OBJTYPE_SMOKE1; n < 9 + OBJTYPE_SMOKE1; n++) {
		object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
		object_type[n].image[0] = &smoke[n - OBJTYPE_SMOKE1];
		object_type[n].nimages = 1;
		object_type[n].scalex = 0.5;
		object_type[n].scaley = 0.5;
		object_type[n].draw = draw_object;
	}

	n = OBJTYPE_MEDICINE_BOX;
	object_type[n].image = malloc(1 * sizeof(*object_type[0].image));
	object_type[n].image[0] = &medicine_box;
	object_type[n].nimages = 1;
	object_type[n].scalex = 0.3;
	object_type[n].scaley = 0.3;
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
	snis_object_pool_setup(&game->sparkpool, MAXSPARKS);
	player_init();
	if (read_level_items("level-items.txt"))
		exit(1);
	set_up_level(0);
	game->score = 0;
	game->lives = 3;

	return true;
}

static void reset_game(int lives)
{
	snis_object_pool_free_all_objects(game.objpool);
	snis_object_pool_free_all_objects(game.sparkpool);
	player_init();
	set_up_level(0);
	game.lives = lives;
	if (lives == 3) {
		game.score = 0;
		game.mode = GAME_MODE_TITLE_SCREEN; 
	}
	game.is_running = true;
	game.camera_x = 1024.0 / 2.0;
	memset(keypressed, 0, sizeof(keypressed));
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
	case SDLK_COMMA:
		keypressed[keyshoot] = 1;
		break;
	case SDLK_x:
	case SDLK_PERIOD:
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
	case SDLK_COMMA:
		keypressed[keyshoot] = 0;
		break;
	case SDLK_x:
	case SDLK_PERIOD:
		keypressed[keygrenade] = 0;
		break;
	default:
		break;
	}
}

/* Handle input events (keyboard, mouse, window close) */
void process_input(struct game_state *game)
{
	static int wasted_time = 0;

	if (player->hit_points == 0 && wasted_time == 0) {
		wasted_time = 100;
		player->hidden = 1;
	}
	if (wasted_time > 0) {
		printf("wasted time = %d\n", wasted_time);
		wasted_time--;
		if (wasted_time == 0) {
			player->hit_points = 255;
			reset_game(game->lives); 
			if (game->lives == 0) {
				reset_game(3);
				game->mode = GAME_MODE_TITLE_SCREEN;
			}
		}

		/* Eat events during this time. */
		SDL_Event event;
		while (SDL_PollEvent(&event)) { (void) 1; }

		return;
	}

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
			/* Ctrl-E to toggle Edit Mode */
			if (event.key.keysym.sym == SDLK_e && (SDL_GetModState() & KMOD_CTRL)) {
				if (game->mode != GAME_MODE_EDIT) {
					reset_game(3);
					game->mode = GAME_MODE_EDIT;
				} else {
					reset_game(3); /* Exit edit mode to the title screen */
				}
				break;
			}
			process_keydown(event);
			break;
		case SDL_KEYUP:
			process_keyup(event);
			break;
		case SDL_MOUSEBUTTONUP:
			if (event.button.button == SDL_BUTTON_LEFT) {
				dragged_object_index = -1;
			} else if (event.button.button == SDL_BUTTON_RIGHT) {
				dragging_camera = 0;
			}
			break;
		case SDL_MOUSEMOTION:
			if (game->mode == GAME_MODE_EDIT) {
				/* Reveal menu if mouse is at the top of the screen */
				menu_is_visible =
					(event.motion.y < MENU_HEIGHT * ((1 + NUM_OBJECT_TYPES) / 10));

				if (dragged_object_index >= 0) {
					go[dragged_object_index].x = screen_to_worldx(event.motion.x);
					go[dragged_object_index].y = screen_to_worldy(event.motion.y);
				}
				if (dragging_camera) {
					int dx = event.motion.x - last_mouse_x;
					float world_dx = (dx * 1024.0f) / (game->window_width - 200.0f);
					game->camera_x -= world_dx;
					game->desired_camera_x = game->camera_x;
					last_mouse_x = event.motion.x;
				}
			}
			break;

		case SDL_MOUSEBUTTONDOWN:
			if (game->mode != GAME_MODE_EDIT)
				break;
			if (event.button.button == SDL_BUTTON_RIGHT) {
				dragging_camera = 1;
				last_mouse_x = event.button.x;
				break;
			}
			if (event.button.button == SDL_BUTTON_LEFT) {
				/* Intercept click if menu is visible and clicked */
				if (menu_is_visible && event.button.y <
						MENU_HEIGHT * (NUM_OBJECT_TYPES + 1) / 10) {
					int clicked_row = event.button.y / MENU_HEIGHT;
					int clicked_index = clicked_row * 10 +
							event.button.x / MENU_ITEM_WIDTH;

					if (clicked_index < NUM_OBJECT_TYPES) {
						/* Clicked an object: Spawn it and attach it to the mouse */
						int new_obj = snis_object_pool_alloc_obj(game->objpool);
						if (new_obj >= 0) {
							go[new_obj].type = clicked_index;
							go[new_obj].current_image = 0;
							go[new_obj].x = screen_to_worldx(event.button.x);
							go[new_obj].y = screen_to_worldy(event.button.y);
							dragged_object_index = new_obj; /* Immediately start dragging */
						}
					} else if (clicked_index == NUM_OBJECT_TYPES) {
						/* Clicked the slot just after the last object type: Save */
						save_level_items("custom_level.txt", game);
					}
				} else {
					/* Normal object selection in the world */
					float wx = screen_to_worldx(event.button.x);
					float wy = screen_to_worldy(event.button.y);
					dragged_object_index = find_object_at(wx, wy);
				}
			}
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

static void pithy_one_liner(void) /* Hopefully pithy anyway.  Maybe just stupid. */
{
	static uint32_t last_one = 0;

	printf("pithy one liner\n");
	uint32_t now = SDL_GetTicks();
	if (now < last_one + 5000) { /* No more than 1 one liner per 5 seconds */
		printf("POL bailing\n");
		return;
	}
	last_one = now;
	int n = randn(LAST_ONE_LINER - FIRST_ONE_LINER) + FIRST_ONE_LINER;
	printf("POL Playing sound %d\n", n);
	wwviaudio_add_sound(n);
}

static void damage_player(int x, __attribute__((unused)) int y, __attribute__((unused)) void *context)
{
	/* Roll for damage */
	int damage = 8 * DIFFICULTY_LEVEL + randn(10 * DIFFICULTY_LEVEL);
	/* harder to actually kill in last moments */
	if (player->hit_points < 10 * DIFFICULTY_LEVEL)
		damage -= 8 * DIFFICULTY_LEVEL;
	printf("Damage = %d\n", damage);
	if (damage > 0) {
		if (player->hit_points > 0 && player->hit_points - damage <= 0) {
			game.lives--;
		}
		player->hit_points -= damage;
		if (player->hit_points < 0)
			player->hit_points = 0;
		if (randn(100) < 33)
			wwviaudio_add_sound(OWWW);
		int xbias = 0;
		if (x < player->x)
			xbias = -2.0f;
		else
			xbias = 2.0f;
		add_sparks(damage, OBJTYPE_BLOOD_DROP,
			player->x, player->y, 2.0f, xbias, 0.0f, 20);
	}
}

static int shoot_at_player_sampler(int x, int y, void *context)
{
	/* Soldier is looking around to see if the player is shootable */
	struct game_object *o = context;

	uint32_t now = SDL_GetTicks();
	if (now - o->last_shot_time < 200) /* throttle shots to 5 / sec */
		return -1;
	if (o->type != OBJTYPE_SOLDIER)
		return -1;
	if (!is_passable(x, y))
		return -1;
	float dist2 = (x - player->x) * (x - player->x) +
			(y - player->y) * (y - player->y);
	if (dist2 < 20*20) { /* We can see the player */
		o->last_shot_time = now;
		wwviaudio_add_sound(AK47_SHOT);
		int n = snis_object_pool_alloc_obj(game.objpool);
		if (n >= 0) {
			struct game_object *mf = &go[n];
			mf->type = OBJTYPE_MUZZLE_FLASH;
			if (o->vx > 0) { /* facing right? */
				mf->current_image = 0;
				mf->x = o->x + 25;
				mf->y = o->y - 5;
			} else {
				mf->current_image = 1;
				mf->x = o->x - 30;
				mf->y = o->y - 5;
			}
			mf->vx = 0;
			mf->vy = 0;
			mf->next_animation_tick = 0;
			mf->is_climbing = 0;
			mf->is_grounded = 0;
			mf->throwing_grenade = 0;
			mf->hit_points = 4;
		}
		o->next_lookaround_time += 250; /* seen recently, look again sooner. */

		/* Roll for hit */
		int hitchance = randn(1000);
		printf("hitchance = %d\n", hitchance);
		if (hitchance < 333)
			damage_player(x, y, context);
		return -1;
	}
	return 0;
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

	if (o->next_lookaround_time < SDL_GetTicks()) {
		o->next_lookaround_time += 500;

		float dist2 = (o->x - player->x) * (o->x - player->x) +
				(o->y - player->y) * (o->y - player->y);

		if (!player->hidden) {
			/* If the player is within 512 units of the soldier, and nearby in y,
			 * check for a viable shot
			 */
			if (dist2 < 512 * 512 && fabsf(player->y - o->y) < 30.0) {
				/* Check if soldier if facing in player's direction */
				if ((player->x < o->x && o->vx < 0) ||
					(player->x > o->x && o->vx > 0))
					bline(o->x, o->y, player->x, player->y,
							shoot_at_player_sampler, o);
			}
		}
	}
}
static int bullet_shot_sampler(int x, int y, void *context);

#define GRENADE_FUSE_TIME_SECS 4.0
#define GRENADE_FRAGMENT_COUNT 60
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

static void move_medicine_box(struct game_object *o)
{
	if (player->hidden)
		return;

	if (player->hit_points >= 200)
		return;

	float dist2 = (o->x - player->x) * (o->x - player->x) +
			(o->y - player->y) * (o->y - player->y);
	if (dist2 < 25 * 25) {
		player->hit_points += 150;
		if (player->hit_points > 255)
			player->hit_points = 255;
		snis_object_pool_free_object(game.objpool, o - &go[0]);
		wwviaudio_add_sound(THATS_THE_STUFF);
	}
}

static void move_objects(float delta_time)
{
	game.soldier_count = 0;
	for (int i = 0; i <= snis_object_pool_highest_object(game.objpool); i++) {
		if (!snis_object_pool_is_allocated(game.objpool, i))
			continue;
		struct game_object *o = &go[i];
		switch (o->type) {
		case OBJTYPE_SOLDIER:
			game.soldier_count++;
			move_soldier(o, delta_time);
			break;
		case OBJTYPE_GRENADE:
			move_grenade(o, delta_time);
			break;
		case OBJTYPE_MUZZLE_FLASH:
			if (o->hit_points > 0)
				o->hit_points--;
			if (o->hit_points == 0)
				snis_object_pool_free_object(game.objpool, o - &go[0]);
			break;
		case OBJTYPE_MEDICINE_BOX:
			move_medicine_box(o);
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
	if (randn(100) < 33)
		pithy_one_liner();
	snis_object_pool_free_object(game.objpool, o - &go[0]);
	game.score += 200;
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
			if (o->type != OBJTYPE_SOLDIER && o->type != OBJTYPE_PLAYER)
				continue;
			if (o->type == OBJTYPE_PLAYER && shooter && shooter == player)
				continue; /* Player can't shoot himself */
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
			if (o->type == OBJTYPE_SOLDIER) {
				if (o->hit_points > 0)
					o->hit_points--;
			} else {
				if (o->type == OBJTYPE_PLAYER)
					damage_player(x, y, context);
			}
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

	if (game.mode == GAME_MODE_EDIT) {
		/* Enforce camera boundaries but do nothing else */
		if (game.camera_x > game.camera_max_x)
			game.camera_x = game.camera_max_x;
		if (game.camera_x < game.camera_min_x)
			game.camera_x = game.camera_min_x;
		return;
	}

	if (game.mode != GAME_MODE_PLAY)
		return;

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
		int i = snis_object_pool_alloc_obj(game.objpool);
		if (i < 0)
			goto out;
		struct game_object *o = &go[i];
		o->type = OBJTYPE_MUZZLE_FLASH;
		if (player->current_image >= 0 && player->current_image <= 2) { /* facing right? */
			o->current_image = 0;
			o->x = player->x + 25;
			o->y = player->y - 5;
		} else {
			o->current_image = 1;
			o->x = player->x - 30;
			o->y = player->y - 5;
		}
		o->vx = 0;
		o->vy = 0;
		o->next_animation_tick = 0;
		o->is_climbing = 0;
		o->is_grounded = 0;
		o->throwing_grenade = 0;
		o->hit_points = 4;
	}
out:
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
	static uint32_t last_time_cleared = 0;
	if (game.soldier_count == 0 &&
		(SDL_GetTicks() > last_time_cleared + 20000.0 || last_time_cleared == 0 )) {
			wwviaudio_add_sound(BUNKER_CLEARED);
			last_time_cleared = SDL_GetTicks();
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

static void draw_number_at(SDL_Renderer *renderer, int x, int y, int number)
{
	char buffer[100];

	snprintf(buffer, sizeof(buffer), "%d", number);

	for (int i = 0; buffer[i]; i++) {
		if (buffer[i] < '0' || buffer[i] > '9')
			continue;
		int n = buffer[i] - '0';
		SDL_Rect destrect = { x, y, digit[n].width, digit[i].height };
		SDL_SetTextureBlendMode(digit[n].texture, SDL_BLENDMODE_MOD);
		SDL_RenderCopy(renderer, digit[n].texture, NULL, &destrect);
		x += digit[n].width;
	}
}

static void draw_health_bar(SDL_Renderer *renderer)
{
	int x1 = (int) (game.window_width * 0.2);
	int x2 = (int) (game.window_width * 0.8);
	int y1 = (int) (game.window_height * 0.9);
	int y2 = (int) (game.window_height * 0.95);

	float health = (float) player->hit_points / 255.0f;

	SDL_Rect destrect = { x1, y1, (int) (health * (x2 - x1)), y2 - y1 };
	SDL_Rect srcrect = { 0, 0, (int) (health * health_bar.width), health_bar.height };
	SDL_SetTextureBlendMode(health_bar.texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, health_bar.texture, &srcrect, &destrect);
	draw_number_at(renderer, x1 + health * (x2 - x1) + 20, y1, (int) (100.0f * health));

	x1 = (int) (game.window_width * 0.05);
	SDL_Rect hldest = { x1, y1, 0.3 * healthlabel.width, 0.3 * healthlabel.height };
	SDL_SetTextureBlendMode(healthlabel.texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, healthlabel.texture, NULL, &hldest);
	
}

static void draw_score(SDL_Renderer *renderer)
{
	int x1 = (int) game.window_width * 0.05f;
	int y1 = (int) game.window_height * 0.18f;

	SDL_Rect destrect = { x1, y1, 0.3 * scorelabel.width, 0.3 * scorelabel.height };
	SDL_SetTextureBlendMode(scorelabel.texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, scorelabel.texture, NULL, &destrect);
	draw_number_at(renderer, x1 + 0.3 * scorelabel.width + 20.0f, y1, game.score);
}

static void draw_lives(SDL_Renderer *renderer)
{
	for (int i = 0; i < game.lives; i++) {
		int x1 = (int) game.window_width * 0.8f;
		int y1 = (int) game.window_height * 0.2f;

		SDL_Rect destrect = { x1 + i * 0.3 * 1.1 * hero_right_2.width, y1,
					0.3 * hero_right_2.width, 0.3 * hero_right_2.height };
		SDL_SetTextureBlendMode(hero_right_2.texture, SDL_BLENDMODE_MOD);
		SDL_RenderCopy(renderer, hero_right_2.texture, NULL, &destrect);
	}
}

static void draw_wasted(SDL_Renderer *renderer)
{
	int x1 = (int) game.window_width * 0.2f;
	int y1 = (int) game.window_height * 0.2f;
	int x2 = (int) game.window_width * 0.8f;
	int y2 = (int) game.window_height * 0.8f;

	SDL_Rect destrect = { x1, y1, x2 - x1, y2 - y1 };
	SDL_SetTextureBlendMode(wasted.texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, wasted.texture, NULL, &destrect);
}

void draw_edit_menu(SDL_Renderer *renderer)
{
	if (!menu_is_visible) return;

	/* Draw a semi-transparent background for the menu */
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
	SDL_Rect menu_bg = {0, 0, game.window_width, MENU_HEIGHT};
	SDL_RenderFillRect(renderer, &menu_bg);

	/* Draw available objects as icons */
	for (int i = 0; i < NUM_OBJECT_TYPES; i++) {
		struct object_type_data *odt = &object_type[i];

		int row = i / 10;
		/* Define the destination rectangle for the icon */
		SDL_Rect dest;
		dest.w = 70; /* Scale icons down to fit nicely */
		dest.h = 70;
		dest.x = ((i % 10) * MENU_ITEM_WIDTH) + (MENU_ITEM_WIDTH / 2) - (dest.w / 2);
		dest.y = row * 80 + (MENU_HEIGHT / 2) - (dest.h / 2);

		/* Render the first frame of the object's animation */
		if (odt->image[0] && odt->image[0]->texture) {
			SDL_SetTextureBlendMode(odt->image[0]->texture, SDL_BLENDMODE_MOD);
			SDL_RenderCopy(renderer, odt->image[0]->texture, NULL, &dest);
		}

		/* Draw a subtle separator line */
		SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
		SDL_RenderDrawLine(renderer, (i + 1) * MENU_ITEM_WIDTH, 0, (i + 1) * MENU_ITEM_WIDTH, MENU_HEIGHT);
	}

	/* Draw a mock "Save" Button (A simple red square as an icon) */
	int row = NUM_OBJECT_TYPES / 10;
	int save_slot_x = (NUM_OBJECT_TYPES % 10) * MENU_ITEM_WIDTH;
	SDL_Rect save_btn = { save_slot_x + 5, row * 80, 70, 70 };
	SDL_SetRenderDrawColor(renderer, 200, 0, 0, 255); /* Red for save */
	SDL_RenderFillRect(renderer, &save_btn);
}

/* Render graphics to the screen */
void render(struct game_state *game)
{
	/* Set draw color to dark gray / black background and clear screen */
	SDL_SetRenderDrawColor(game->renderer, 30, 30, 30, 255);
	SDL_RenderClear(game->renderer);

	/* TODO: Draw your game objects here (e.g., SDL_RenderCopy, SDL_RenderFillRect) */
	draw_background_image(game->renderer);
	if (game->mode != GAME_MODE_PLAY && game->mode != GAME_MODE_EDIT)
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

	draw_health_bar(game->renderer);
	draw_score(game->renderer);

	draw_lives(game->renderer);
	if (player->hit_points == 0)
		draw_wasted(game->renderer);

	union vec3 colors[4];
	sample_mask_around_object(&go[0], colors);
	draw_debug_rectangles(player, colors);
	debug_sampling();
	if (game->mode == GAME_MODE_EDIT)
		draw_edit_menu(game->renderer);

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
	wwviaudio_read_ogg_clip(TALKIN_BOUT, "sounds/what-im-talkin-about.ogg");
	wwviaudio_read_ogg_clip(SPRUNG_A_LEAK, "sounds/sprung-a-leak.ogg");
	wwviaudio_read_ogg_clip(SHOUDA_STAYED_HOME, "sounds/shouda-stayed-home.ogg");
	wwviaudio_read_ogg_clip(SUX_2_B_U, "sounds/sux-to-be-u.ogg");
	wwviaudio_read_ogg_clip(JUST_DESSERTS, "sounds/just-desserts.ogg");
	wwviaudio_read_ogg_clip(FERTILIZER, "sounds/fertilizer.ogg");
	wwviaudio_read_ogg_clip(DEATHWISH, "sounds/deathwish-granted.ogg");
	wwviaudio_read_ogg_clip(CARGO200, "sounds/cargo-200.ogg");
	wwviaudio_read_ogg_clip(LEAD_POISON, "sounds/acute-lead-poisonin.ogg");
	wwviaudio_read_ogg_clip(THATS_THE_STUFF, "sounds/thats-the-stuff.ogg");
	wwviaudio_read_ogg_clip(OWWW, "sounds/owww.ogg");
	wwviaudio_read_ogg_clip(AK47_SHOT, "sounds/ak47.ogg");
	wwviaudio_read_ogg_clip(BUNKER_CLEARED, "sounds/bunker-cleared.ogg");
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
		if (game.mode != GAME_MODE_EDIT)
			move_camera();
		last_frame_time = current_time;
	}

	cleanup(&game);
	return 0;
}

