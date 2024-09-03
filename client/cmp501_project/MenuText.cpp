#pragma once
#include "MenuText.h"

MenuText::MenuText(Vec2 position, SDL_Renderer* renderer, TTF_Font* font, std::string text)
	: renderer(renderer), font(font)
{
	//surface = TTF_RenderText_Shaded(font, text.c_str(), { 0xFF, 0xFF, 0xFF, 0xFF }, { 0, 0, 0, 0});
	surface = TTF_RenderText_Solid(font, text.c_str(), { 0xFF, 0xFF, 0xFF, 0xFF });
	texture = SDL_CreateTextureFromSurface(renderer, surface);

	int width, height;
	SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);

	rect.x = static_cast<int>(position.x - width / 2);
	rect.y = static_cast<int>(position.y);
	rect.w = width;
	rect.h = height;
}

MenuText::~MenuText()
{
	SDL_FreeSurface(surface);
	SDL_DestroyTexture(texture);
}

void MenuText::SetText(Vec2 position, std::string text)
{
	SDL_FreeSurface(surface);
	SDL_DestroyTexture(texture);

	surface = TTF_RenderText_Solid(font, text.c_str(), { 0xFF, 0xFF, 0xFF, 0xFF });
	texture = SDL_CreateTextureFromSurface(renderer, surface);

	int width, height;
	SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);

	rect.x = static_cast<int>(position.x - width / 2);
	rect.y = static_cast<int>(position.y);
	rect.w = width;
	rect.h = height;
}

void MenuText::Draw()
{	
	SDL_RenderCopy(renderer, texture, nullptr, &rect);
}