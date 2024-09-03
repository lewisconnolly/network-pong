#include <algorithm>
#include "Global.h"
#include "Ball.h"

Ball::Ball(Vec2 position, Vec2 velocity)
	: position(position), velocity(velocity)
{
	rect.x = static_cast<int>(position.x);
	rect.y = static_cast<int>(position.y);
	rect.w = BALL_WIDTH;
	rect.h = BALL_HEIGHT;
}

// Collision methods based only on ball position for clients (not velocity) because server
// updates ball based on velocity and sends position to client. Client ignores velocity to
// prevent ball oscillation when client and server velocity would differ due to latency

void Ball::CollideWithPaddle(Contact const& contact)
{
	position.x += contact.penetration;
	if (position.x < WINDOW_WIDTH / 2)
	{
		position.x += BALL_SPEED;
	}
	else
	{
		position.x -= BALL_SPEED;
	}

	if (contact.type == CollisionType::Top)
	{
		position.y -= 0.75f * BALL_SPEED;
	}
	else if (contact.type == CollisionType::Bottom)
	{
		position.y += 0.75f * BALL_SPEED;
	}
}

void Ball::CollideWithWall(Contact const& contact)
{
	if (contact.type == CollisionType::Top)
	{
		position.y += contact.penetration;
		position.y += BALL_SPEED;
	}
	else if (contact.type == CollisionType::Bottom)
	{
		position.y += contact.penetration;
		position.y -= BALL_SPEED;
	}
}


void Ball::AddMessage(const Message& msg)
{
	int numMessages = ballMessages.size();
	if (numMessages == maxMessages)
	{
		std::move(ballMessages.begin() + 1, ballMessages.end(), ballMessages.begin());
		ballMessages[numMessages - 1] = msg;
	}
	else
	{
		ballMessages.push_back(msg);
	}

	// sort messages in increasing order of timestamp
	std::sort(ballMessages.begin(), ballMessages.end(), compareByTimestamp);
}

void Ball::AddPrediction(const Message& prediction)
{
	int numPredictions = ballPredictions.size();
	if (numPredictions == maxMessages)
	{
		std::move(ballPredictions.begin() + 1, ballPredictions.end(), ballPredictions.begin());
		ballPredictions[numPredictions - 1] = prediction;
	}
	else
	{
		ballPredictions.push_back(prediction);
	}

	// sort messages in increasing order of timestamp
	std::sort(ballPredictions.begin(), ballPredictions.end(), compareByTimestamp);
}

void Ball::AddPosition(const Message& position)
{
	int numPositions = ballPositions.size();
	if (numPositions == maxMessages)
	{
		std::move(ballPositions.begin() + 1, ballPositions.end(), ballPositions.begin());
		ballPositions[numPositions - 1] = position;
	}
	else
	{
		ballPositions.push_back(position);
	}

	// sort messages in increasing order of timestamp
	std::sort(ballPositions.begin(), ballPositions.end(), compareByTimestamp);
}

Message Ball::RunPrediction(double gameTime, int mode, Ball& ball)
{
	float predictedX = ball.position.x;
	float predictedY = ball.position.y;
	float speedX, speedY, displacementX, displacementY;
	
	Message prediction;
	prediction.timestamp = gameTime;
	prediction.x = predictedX;
	prediction.y = predictedY;
	prediction.ball = true;
	prediction.port = 0;

	int size = ballMessages.size();
	if (size < 2)
	{		
		return prediction;
	}

	// mode:
	// 0 = predict from server messages
	// 1 = predict from previous predictions
	// 2 = predict from previous positions

	const Message& msg0 = ballMessages[size - 1];
	const Message& msg1 = ballMessages[size - 2];

	if (mode == 1)
	{
		size = ballPredictions.size();
		if (size < 2)
		{
			return prediction;
		}

		const Message& msg0 = ballPredictions[size - 1];
		const Message& msg1 = ballPredictions[size - 2];
	}

	if (mode == 2)
	{
		size = ballPositions.size();
		if (size < 2)
		{
			return prediction;
		}
		
		const Message& msg0 = ballPositions[size - 1];
		const Message& msg1 = ballPositions[size - 2];
	}

	/* Linear model */
	speedX = (msg0.x - msg1.x) / (msg0.timestamp - msg1.timestamp);
	speedY = (msg0.y - msg1.y) / (msg0.timestamp - msg1.timestamp);
	displacementX = speedX * (gameTime - msg0.timestamp);
	displacementY = speedY * (gameTime - msg0.timestamp);
	predictedX = msg0.x + displacementX;
	predictedY = msg0.y + displacementY;	

	prediction.x = predictedX;
	prediction.y = predictedY;

	return prediction;
}

Vec2 Ball::ValidatePrediction(Ball& ball, float predictedX, float predictedY, float p1X, float p1Y, float p2X, float p2Y)
{
	Vec2 validPrediction = Vec2(predictedX, predictedY);
	bool paddleOneCheck1 = false;
	bool paddleOneCheck2 = false;
	bool paddleTwoCheck1 = false;
	bool paddleTwoCheck2 = false;

	Vec2 paddleOneTopLeftCorner = Vec2(p1X, p1Y);
	Vec2 paddleOneBottomLeftCorner = Vec2(p1X, p1Y + PADDLE_HEIGHT);

	Vec2 paddleTwoTopRightCorner = Vec2(p2X + PADDLE_WIDTH, p2Y);
	Vec2 paddleTwoBottomRightCorner = Vec2(p2X + PADDLE_WIDTH, p2Y + PADDLE_HEIGHT);

	// Left paddle lines to check
	Vec2 ballStartTopLeftCorner = ball.position; // Line 1 start
	Vec2 ballEndTopRightCorner = Vec2(predictedX + BALL_WIDTH, predictedY); // Line 1 end
	Vec2 ballStartBottomLeftCorner = Vec2(ball.position.x, ball.position.y + PADDLE_HEIGHT); // Line 2 start
	Vec2 ballEndBottomRightCorner = Vec2(predictedX + BALL_WIDTH, predictedY + PADDLE_HEIGHT); // Line 2 end

	// Right paddle
	Vec2 ballStartTopRightCorner = Vec2(ball.position.x + BALL_WIDTH, ball.position.y); // Line 1 start
	Vec2 ballEndTopLeftCorner = Vec2(predictedX, predictedY); // Line 1 end
	Vec2 ballStartBottomRightCorner = Vec2(ball.position.x + BALL_WIDTH, ball.position.y + PADDLE_HEIGHT); // Line 2 start
	Vec2 ballEndBottomLeftCorner = Vec2(predictedX, predictedY + PADDLE_HEIGHT); // Line 2 end

	// Check if lines from current position to prediction position have intersected the back side of the paddle
	// If true then ball has fully phased through paddle and skipped collision check, therefore predicted position needs to be moved back
	paddleOneCheck1 = doIntersect(ballStartTopLeftCorner, ballEndTopRightCorner, paddleOneTopLeftCorner, paddleOneBottomLeftCorner);
	paddleOneCheck2 = doIntersect(ballStartBottomLeftCorner, ballEndBottomRightCorner, paddleOneTopLeftCorner, paddleOneBottomLeftCorner);

	if (paddleOneCheck1 || paddleOneCheck2)
	{	
		validPrediction.x = p1X + PADDLE_WIDTH - 1.0; // -1.0 to trigger a collision in the same frame
	}

	paddleTwoCheck1 = doIntersect(ballStartTopRightCorner, ballEndTopLeftCorner, paddleTwoTopRightCorner, paddleTwoBottomRightCorner);
	paddleTwoCheck2 = doIntersect(ballStartBottomRightCorner, ballEndBottomLeftCorner, paddleTwoTopRightCorner, paddleTwoBottomRightCorner);

	if (paddleTwoCheck1 || paddleTwoCheck2)
	{
		validPrediction.x = p2X - BALL_WIDTH + 1.0;	// +1.0 to trigger a collision in the same frame	
	}

	return validPrediction;
}

void Ball::Draw(SDL_Renderer* renderer)
{
	rect.x = static_cast<int>(position.x);
	rect.y = static_cast<int>(position.y);

	SDL_RenderFillRect(renderer, &rect);
}