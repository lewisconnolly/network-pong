#pragma once
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include "Vec2.h"

class MenuText
{
public:
	MenuText(Vec2 position, SDL_Renderer* renderer, TTF_Font* font, std::string text);

	~MenuText();

	void Draw();
	void SetText(Vec2 position, std::string text);

	SDL_Renderer* renderer;
	TTF_Font* font;
	SDL_Surface* surface{};
	SDL_Texture* texture{};
	SDL_Rect rect{};
};
