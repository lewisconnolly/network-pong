#include <algorithm> 
#include "Global.h"
#include "Paddle.h"

Paddle::Paddle(Vec2 position, Vec2 velocity)
	: position(position), velocity(velocity)
{	
	rect.x = static_cast<int>(position.x);
	rect.y = static_cast<int>(position.y);
	rect.w = PADDLE_WIDTH;
	rect.h = PADDLE_HEIGHT;

	playerIndicator.x = static_cast<int>(position.x + PADDLE_WIDTH / 2 - 1);
	playerIndicator.y = static_cast<int>(position.y + PADDLE_HEIGHT / 2 - 25);
	playerIndicator.w = 3;
	playerIndicator.h = 50;
}

void Paddle::Update(float dt)
{
	position += velocity * dt;
	
	if (position.y < 0)
	{
		// Restrict to top of the screen
		position.y = 0;
	}
	else if (position.y > (WINDOW_HEIGHT - PADDLE_HEIGHT))
	{
		// Restrict to bottom of the screen
		position.y = WINDOW_HEIGHT - PADDLE_HEIGHT;
	}
}

void Paddle::Draw(SDL_Renderer* renderer)
{
	rect.y = static_cast<int>(position.y);

	SDL_RenderFillRect(renderer, &rect);	
}

void Paddle::ShowPlayerIndicator(SDL_Renderer* renderer)
{
	playerIndicator.y = static_cast<int>(position.y + PADDLE_HEIGHT / 2 - 25);

	SDL_RenderFillRect(renderer, &playerIndicator);
}

void Paddle::AddMessage(const Message& msg)
{	
	int numMessages = paddleMessages.size();
	if (numMessages == maxMessages)
	{
		std::move(paddleMessages.begin() + 1, paddleMessages.end(), paddleMessages.begin());
		paddleMessages[numMessages - 1] = msg;
	}
	else
	{
		paddleMessages.push_back(msg);
	}

	// sort messages in increasing order of timestamp
	std::sort(paddleMessages.begin(), paddleMessages.end(), compareByTimestamp);
}

void Paddle::AddPrediction(const Message& prediction)
{
	int numPredictions = paddlePredictions.size();
	if (numPredictions == maxMessages)
	{
		std::move(paddlePredictions.begin() + 1, paddlePredictions.end(), paddlePredictions.begin());
		paddlePredictions[numPredictions - 1] = prediction;
	}
	else
	{
		paddlePredictions.push_back(prediction);
	}

	// sort messages in increasing order of timestamp
	std::sort(paddlePredictions.begin(), paddlePredictions.end(), compareByTimestamp);
}

Message Paddle::RunPrediction(double gameTime, bool fromPredictions) {
	
	//float predictedY = (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f);
	float predictedY = position.y;
	float speedY, displacementY;
	Message prediction;
	prediction.timestamp = gameTime;
	prediction.y = predictedY;
	prediction.x = 0;
	prediction.ball = false;
	prediction.port = 0;

	int size = paddleMessages.size();
	if (size < 2)
	{		
		return prediction;
	}
	
	const Message& msg0 = paddleMessages[size - 1];
	const Message& msg1 = paddleMessages[size - 2];
	
	if (fromPredictions)
	{
		size = paddlePredictions.size();
		if (size < 2)
		{
			return prediction;
		}
		
		const Message& msg0 = paddlePredictions[size - 1];
		const Message& msg1 = paddlePredictions[size - 2];
	}

	/* Linear model */
	speedY = (msg0.y - msg1.y) / (msg0.timestamp - msg1.timestamp);
	displacementY = speedY * (gameTime - msg0.timestamp);	
	predictedY = msg0.y + displacementY;

	if (predictedY < 0)
	{
		// Restrict to top of the screen
		predictedY = 0;
	}
	else if (predictedY > (WINDOW_HEIGHT - PADDLE_HEIGHT))
	{
		// Restrict to bottom of the screen
		predictedY = WINDOW_HEIGHT - PADDLE_HEIGHT;
	}

	prediction.y = predictedY;
	
	return prediction;
}