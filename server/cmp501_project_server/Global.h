#pragma once
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

const float PADDLE_SPEED = 0.75f;
const float BALL_SPEED = 0.5f;

struct Message
{
	double timestamp = 0;
	float x, y = 0; //object position
	bool ball = false;
	unsigned short port = 0;
};

inline bool compareByTimestamp(const Message& m1, const Message& m2)
{
	return m1.timestamp < m2.timestamp;
}