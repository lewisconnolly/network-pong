#pragma once
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

	void Update(float dt);
	void CollideWithPaddle(Contact const& contact);
	void CollideWithWall(Contact const& contact);

	Vec2 position;
	Vec2 velocity;
};