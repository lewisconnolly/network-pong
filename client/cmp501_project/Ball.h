#pragma once
#include <SDL.h>
#include <vector>
#include "Global.h"
#include "Vec2.h"

const int BALL_WIDTH = 15;
const int BALL_HEIGHT = 15;

class Ball
{
public:
	enum class CollisionType
	{
		None,
		Top,
		Middle,
		Bottom,
		Left,
		Right
	};

	struct Contact
	{
		CollisionType type;
		float penetration;
	};

	Ball(Vec2 position, Vec2 velocity);

	void CollideWithPaddle(Contact const& contact);
	void CollideWithWall(Contact const& contact);
	void AddMessage(const Message& msg);
	void AddPrediction(const Message& prediction);
	void AddPosition(const Message& position);
	Message RunPrediction(double gameTime, int mode, Ball& ball);
	Vec2 ValidatePrediction(Ball& ball, float predictedX, float predictedY, float p1X, float p1Y, float p2X, float p2Y);
	void Draw(SDL_Renderer* renderer);	
	
	Vec2 position;
	Vec2 velocity;
	SDL_Rect rect{};
	std::vector<Message> ballMessages;
	std::vector<Message> ballPredictions;
	std::vector<Message> ballPositions;
	int maxMessages = 2;
};