#pragma once
#include <vector>
#include "Global.h"
#include "Vec2.h"

const int PADDLE_WIDTH = 15;
const int PADDLE_HEIGHT = 90;

class Paddle
{
public:
	Paddle(Vec2 position, Vec2 velocity);

	void Update(float dt);
	void AddMessage(const Message& msg);
	void AddPrediction(const Message& prediction);
	Message RunPrediction(double gameTime, bool fromPredictions);

	Vec2 position;
	Vec2 velocity;
	std::vector<Message> paddleMessages;
	std::vector<Message> paddlePredictions;
	int maxMessages = 2;
};