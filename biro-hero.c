#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <limits.h>

#include "png_utils.h"

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
	float camera_x;
	float camera_vx;
	float camera_min_x;
	float camera_max_x;
	int window_width, window_height;
} game = { 0 };

struct image {
	char *filename;
	char *data;
	int width, height, alpha;
	int mode;
#define IMAGE_MODE_TEXTURE 1
#define IMAGE_MODE_SURFACE 2
	SDL_Texture *texture;
	SDL_Surface *surface;
};

#define MAX_GAME_OBJS 1000

static struct game_object {
	int type;
	float x, y;	   /* position */
	int current_image; /* index into object_type[o->type].image[]; */
} go[MAX_GAME_OBJS];

static struct game_object *player = &go[0];
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
	struct image terrain[MAX_SCREENS_PER_LEVEL];
	struct image collision_mask[MAX_SCREENS_PER_LEVEL];
} level[MAX_LEVELS] = { 0 };

struct image background_image = { "images/notebook-image.png", NULL, 0, 0, 0, 0, NULL, NULL };
struct image title_screen_image = { "images/biro-hero-title-screen.png", NULL, 0, 0, 0, 0, NULL, NULL };
struct image hero_right_1 = { "images/hero-right-1.png", NULL, 0, 0, 0, 0, NULL, NULL };
struct image hero_right_2 = { "images/hero-right-2.png", NULL, 0, 0, 0, 0, NULL, NULL };
struct image hero_right_3 = { "images/hero-right-3.png", NULL, 0, 0, 0, 0, NULL, NULL };
struct image hero_left_1 = { "images/hero-left-1.png", NULL, 0, 0, 0, 0, NULL, NULL };
struct image hero_left_2 = { "images/hero-left-2.png", NULL, 0, 0, 0, 0, NULL, NULL };
struct image hero_left_3 = { "images/hero-left-3.png", NULL, 0, 0, 0, 0, NULL, NULL };

static int load_png_image(SDL_Renderer *renderer, struct image *i, int image_mode)
{
	char whynot[1000];

	i->surface = NULL;
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
	if (i->mode & IMAGE_MODE_SURFACE) {
		/* TODO: surface stuff */
	}
	return 0;
}

static void image_cleanup(struct image *i)
{
	if (i->texture) {
		SDL_DestroyTexture(i->texture);
		i->texture = NULL;
	}
	if (i->surface) {
		/* TODO: surface stuff */
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
		level[0].terrain[n].surface = NULL;
		x += load_png_image(renderer, &level[0].terrain[n], IMAGE_MODE_TEXTURE);
		printf("x = %d\n", x);
		printf("level[0].terrain[%d].filename = %s\n", n, level[0].terrain[n].filename);
		printf("level[0].terrain[%d].data = %p\n", n, level[0].terrain[n].data);
		printf("level[0].terrain[%d].width = %d\n", n, level[0].terrain[n].width);
		printf("level[0].terrain[%d].height = %d\n", n, level[0].terrain[n].height);
		printf("level[0].terrain[%d].alpha = %d\n", n, level[0].terrain[n].alpha);
		printf("level[0].terrain[%d].mode = %d\n", n, level[0].terrain[n].mode);
		printf("level[0].terrain[%d].texture = %p\n", n, (void *) level[0].terrain[n].texture);
		printf("level[0].terrain[%d].surface = %p\n", n, (void *) level[0].terrain[n].surface);
		SDL_SetTextureBlendMode(level[0].terrain[n].texture, SDL_BLENDMODE_BLEND);
		n++;
		level[0].nscreens = n;
	}
	game.camera_min_x = 512.0f;
	game.camera_max_x = 1024.0f * level[0].nscreens - 512.0f;
	free(namelist);
	return x;
}

static void player_init(void)
{
	player->x = 50;
	player->y = 0;
	player->type = OBJTYPE_PLAYER;
}

static void draw_object(SDL_Renderer *renderer, struct game_object *o)
{
	struct object_type_data *odt = &object_type[o->type];
	struct image **im = odt->image;
	int i = o->current_image;
	float sx = odt->scalex;
	float sy = odt->scaley;
	float w = sx * im[i]->width;
	float h = sy * im[i]->height;

	float wsx = game.window_width / 1024.0;
	float wsy = game.window_height / 768.0;

	SDL_Rect destrect = {
		o->x - 0.5 * w,
		o->y - 0.5 * h,
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
	player_init();
	return true;
}

static void process_keydown(__attribute__((unused)) SDL_Event event)
{
	if (game.mode == GAME_MODE_TITLE_SCREEN) {
		game.mode = GAME_MODE_PLAY;
		return;
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
		default:
		break;
		}
	}
}

/* Update game logic (positions, physics, AI) based on delta time */
void update(__attribute__((unused)) float delta_time)
{
	/* TODO: Add game logic updates here */
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

static void draw_level(SDL_Renderer *renderer)
{
	int use_2nd_image = 0;
	int img1, img2;
	int width, height;

	SDL_GetWindowSize(game.window, &width, &height);
	game.window_width = width;
	game.window_height = height;

	/* figure out which images we need to draw */
	img1 = (int) (game.camera_x - 512.0f) / 1024.0f;
	float src1_startx = game.camera_x - (img1 * 1024.0f) - 512.0f;
	float src1_stopx = 1024.0;
	float src2_startx;
	float src2_stopx;

	float dest1_startx;
	float dest1_width;
	float dest2_startx;
	float dest2_width;

	dest1_startx = 100;
	dest1_width = ((1024.0f - src1_startx) / 1024.0) * 824.0f;
	dest2_startx = dest1_width + dest1_startx;
	dest2_width = ((1024.0f - dest2_startx) / 1024.0) * 824.0f;
	dest2_width = 824.0f - dest1_width;

	// fprintf(stderr, "ax1 = %f, ax2 = %f\n", src1_startx, src1_stopx);

	img2 = img1 + 1;
	if (img2 >= level[game.current_level].nscreens)
		use_2nd_image = 1;
	else
		use_2nd_image = 1;
	src2_startx = 0.0;
	src2_stopx = 1024.0 * ((824.0f - dest1_width) / 824.0f);
	// fprintf(stderr, "bx1 = %f, bx2 = %f\n", src2_startx, src2_stopx);

	float xscale = (float) width / 1024.0;
	float yscale = (float) height / 768.0;

	SDL_Rect srcrect = { src1_startx, 0, src1_stopx - src1_startx, 768 };
	SDL_Rect destrect = { xscale * dest1_startx, yscale * 100,
				xscale * dest1_width, yscale * 568 };
	SDL_SetTextureBlendMode(level[0].terrain[img1].texture, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(renderer, level[0].terrain[img1].texture, &srcrect, &destrect);

	if (use_2nd_image) {
		SDL_Rect srcrect = { src2_startx, 0, src2_stopx - src2_startx, 768 };
		SDL_Rect destrect = { xscale * dest2_startx, yscale * 100,
					xscale * dest2_width, yscale * 568 };
		SDL_SetTextureBlendMode(level[0].terrain[img2].texture, SDL_BLENDMODE_MOD);
		SDL_RenderCopy(renderer, level[0].terrain[img2].texture, &srcrect, &destrect);
	}
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
	SDL_Quit();
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

	game.camera_vx = 1.5;
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

		game.camera_x += game.camera_vx;
		if (game.camera_x >= game.camera_max_x) {
			game.camera_vx = -1.5f;
			game.camera_x = game.camera_max_x;
		}
		if (game.camera_x <= game.camera_min_x) {
			game.camera_vx = 1.5f;
			game.camera_x = game.camera_min_x;
		}
		last_frame_time = current_time;
	}

	cleanup(&game);
	return 0;
}

