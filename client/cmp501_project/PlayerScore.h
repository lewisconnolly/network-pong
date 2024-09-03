#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include "Vec2.h"

class PlayerScore
{
public:
	PlayerScore(Vec2 position, SDL_Renderer* renderer, TTF_Font* font);

	~PlayerScore();
	
	void SetScore(int score);
	void Draw();

	SDL_Renderer* renderer;
	TTF_Font* font;
	SDL_Surface* surface{};
	SDL_Texture* texture{};
	SDL_Rect rect{};
};