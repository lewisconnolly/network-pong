#pragma once
#include <SDL.h>
#include <vector>
#include "Global.h"
#include "Vec2.h"

class Paddle
{
public:	
	Paddle(Vec2 position, Vec2 velocity);

	void Update(float dt);
	void Draw(SDL_Renderer* renderer);
	void ShowPlayerIndicator(SDL_Renderer* renderer);
	void AddMessage(const Message& msg);
	void AddPrediction(const Message& prediction);
	Message RunPrediction(double gameTime, bool fromPredictions);

	Vec2 position;
	Vec2 velocity;
	SDL_Rect rect{};
	SDL_Rect playerIndicator{};
	std::vector<Message> paddleMessages;
	std::vector<Message> paddlePredictions;
	int maxMessages = 2;
};