#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>

#include "png_utils.h"
#include "snis_alloc.h"
#include "stacktrace.h"

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
	int current_image;	/* index into object_type[o->type].image[]; */
	float ticks;
	float next_animation_tick;
} go[MAX_GAME_OBJS];

static struct game_object *player;
#define MAX_OBJECT_TYPES 50
#define OBJTYPE_PLAYER 0

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
			fprintf(stderr, "Reading level background image %s\n", namelist[i]->d_name);
			snprintf(path, sizeof(path), "%s/%s", "images/level1", namelist[i]->d_name);
			free(namelist[i]);
			level[0].terrain[n].filename = strdup(path);
			level[0].terrain[n].data = NULL;
			level[0].terrain[n].width = 0;
			level[0].terrain[n].height = 0;
			level[0].terrain[n].alpha = 0;
			level[0].terrain[n].mode = 0;
			level[0].terrain[n].texture = NULL;
			x += load_png_image(renderer, &level[0].terrain[n], IMAGE_MODE_TEXTURE);
			printf("x = %d\n", x);
			printf("level[0].terrain[%d].filename = %s\n", n, level[0].terrain[n].filename);
			printf("level[0].terrain[%d].data = %p\n", n, level[0].terrain[n].data);
			printf("level[0].terrain[%d].width = %d\n", n, level[0].terrain[n].width);
			printf("level[0].terrain[%d].height = %d\n", n, level[0].terrain[n].height);
			printf("level[0].terrain[%d].alpha = %d\n", n, level[0].terrain[n].alpha);
			printf("level[0].terrain[%d].mode = %d\n", n, level[0].terrain[n].mode);
			printf("level[0].terrain[%d].texture = %p\n", n, (void *) level[0].terrain[n].texture);
			SDL_SetTextureBlendMode(level[0].terrain[n].texture, SDL_BLENDMODE_BLEND);
			n++;
			level[0].nscreens++;
		} else if (strncmp(namelist[i]->d_name, "map-code-", 9) == 0) {
			/* Is it a color coding for moveable areas and ladders and so on? */
			fprintf(stderr, "Reading level color coding image %s\n", namelist[i]->d_name);
			snprintf(path, sizeof(path), "%s/%s", "images/level1", namelist[i]->d_name);
			free(namelist[i]);
			level[0].terrain[n].filename = strdup(path);
			level[0].terrain[n].data = NULL;
			level[0].terrain[n].width = 0;
			level[0].terrain[n].height = 0;
			level[0].terrain[n].alpha = 0;
			level[0].terrain[n].mode = 0;
			level[0].terrain[n].texture = NULL;
			x += load_png_image(renderer, &level[0].terrain[n], IMAGE_MODE_RAW);
			printf("x = %d\n", x);
			printf("level[0].terrain[%d].filename = %s\n", n, level[0].terrain[n].filename);
			printf("level[0].terrain[%d].data = %p\n", n, level[0].terrain[n].data);
			printf("level[0].terrain[%d].width = %d\n", n, level[0].terrain[n].width);
			printf("level[0].terrain[%d].height = %d\n", n, level[0].terrain[n].height);
			printf("level[0].terrain[%d].alpha = %d\n", n, level[0].terrain[n].alpha);
			printf("level[0].terrain[%d].mode = %d\n", n, level[0].terrain[n].mode);
			printf("level[0].terrain[%d].texture = %p\n", n, (void *) level[0].terrain[n].texture);
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

/* Update game logic (positions, physics, AI) based on delta time */
void update(float delta_time)
{
	int do_player_animation = 0;
	/* TODO: Add game logic updates here */
#define PLAYER_VX 2
#define PLAYER_VY 2
	if (keypressed[keyleft]) {
		player->x = player->x - PLAYER_VX;
		if (player->x < 10.0)
			player->x = 10.0;
		if (player->current_image < 3)
			player->current_image = 3;
		do_player_animation = 1;
	}
	if (keypressed[keyright]) {
		player->x = player->x + PLAYER_VX;
		if (player->x > (level[0].nscreens * 1024.0f - 10.0))
			player->x = level[0].nscreens * 1024.0f - 10.0;
		if (player->current_image > 2)
			player->current_image = 0;
		do_player_animation = 1;
	}
	if (keypressed[keyup]) {
		player->y = player->y - PLAYER_VY;
		if (player->y < 10.0f)
			player->y = 10.0f;
		do_player_animation = 1;
	}
	if (keypressed[keydown]) {
		if (player->y > 750.0f)
			player->y = 750.0f;
		player->y = player->y + PLAYER_VY;
		do_player_animation = 1;
	}
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
	object_type[go[0].type].draw(game->renderer, &go[0]);

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

int main(__attribute__((unused)) int argc, __attribute__((unused)) char *argv[])
{
	if (!init_game(&game)) {
		return 1;
	}

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

