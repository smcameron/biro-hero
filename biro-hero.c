#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>

#include "png_utils.h"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define TARGET_FPS 60
#define FRAME_TARGET_TIME (1000 / TARGET_FPS)

/* Game state struct to hold core systems */
typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
	bool is_running;
} GameState;

struct image {
	char *filename;
	char *data;
	int width, height, alpha;
};

struct image background_image = { "images/notebook-image.png", NULL, 0, 0, 0 };

static int read_png_files(void)
{
	char whynot[1000];
	
	background_image.data = png_utils_read_png_image(background_image.filename,
			0, 0, 0, &background_image.width, &background_image.height,
			&background_image.alpha, whynot, sizeof(whynot));
	if (!background_image.data) {
		fprintf(stderr, "Failed to load %s: %s\n",
			background_image.filename, whynot);
		return -1;
	}
	return 0;
}

/* Initialize SDL, window, and renderer */
bool init_game(GameState *game)
{
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
		return false;
	}

	game->window = SDL_CreateWindow(
		"SDL2 2D Game Template",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		WINDOW_WIDTH,
		WINDOW_HEIGHT,
		SDL_WINDOW_SHOWN
	);

	if (!game->window) {
		SDL_Log("Failed to create window: %s", SDL_GetError());
		SDL_Quit();
		return false;
	}

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
	return true;
}

/* Handle input events (keyboard, mouse, window close) */
void process_input(GameState *game)
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
	SDL_Texture *image = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
			SDL_TEXTUREACCESS_STATIC, background_image.width, background_image.height);
	if (!image) {
		fprintf(stderr, "Could not create texture for background image\n");
		exit(1);
	}
	SDL_UpdateTexture(image, NULL, background_image.data, 4 * background_image.width);
	SDL_RenderCopy(renderer, image, NULL, NULL);
	SDL_DestroyTexture(image);
}

/* Render graphics to the screen */
void render(GameState *game)
{
	/* Set draw color to dark gray / black background and clear screen */
	SDL_SetRenderDrawColor(game->renderer, 30, 30, 30, 255);
	SDL_RenderClear(game->renderer);

	/* TODO: Draw your game objects here (e.g., SDL_RenderCopy, SDL_RenderFillRect) */
	draw_background_image(game->renderer);

	/* Present the back buffer to the screen */
	SDL_RenderPresent(game->renderer);
}

/* Free resources and shut down SDL */
void cleanup(GameState *game)
{
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
	GameState game = {0};

	if (read_png_files())
		exit(1);

	if (!init_game(&game)) {
		return 1;
	}

	Uint32 last_frame_time = SDL_GetTicks();

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

		last_frame_time = current_time;
	}

	cleanup(&game);
	return 0;
}

