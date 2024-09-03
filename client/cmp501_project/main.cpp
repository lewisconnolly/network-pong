#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_mixer.h>
#include <SFML/Network.hpp>
#include <SFML/System/Time.hpp>
#include <stdio.h>
#include <iostream>
#include <math.h>
#include "Global.h"
#include "Vec2.h"
#include "Ball.h"
#include "Paddle.h"
#include "PlayerScore.h"
#include "MenuText.h"

struct ScoreMessage
{
	double timestamp = 0;
	int playerOneScore, playerTwoScore = 0;
};

sf::Packet& operator <<(sf::Packet& packet, const Message& message)
{
	return packet << message.timestamp << message.x << message.y << message.ball << message.port;
}

sf::Packet& operator >>(sf::Packet& packet, Message& message)
{
	return packet >> message.timestamp >> message.x >> message.y >> message.ball >> message.port;
}

sf::Packet& operator <<(sf::Packet& packet, const ScoreMessage& scoreMessage)
{
	return packet << scoreMessage.timestamp << scoreMessage.playerOneScore << scoreMessage.playerTwoScore;
}

sf::Packet& operator >>(sf::Packet& packet, ScoreMessage& scoreMessage)
{
	return packet >> scoreMessage.timestamp >> scoreMessage.playerOneScore >> scoreMessage.playerTwoScore;
}

struct Ball::Contact CheckPaddleCollision(Ball const& ball, Paddle const& paddle)
{
	float ballLeft = ball.position.x;
	float ballRight = ball.position.x + BALL_WIDTH;
	float ballTop = ball.position.y;
	float ballBottom = ball.position.y + BALL_HEIGHT;	

	float paddleLeft = paddle.position.x;
	float paddleRight = paddle.position.x + PADDLE_WIDTH;
	float paddleTop = paddle.position.y;
	float paddleBottom = paddle.position.y + PADDLE_HEIGHT;

	Ball::Contact contact{};
	
	if (ballLeft >= paddleRight)
	{
		return contact;
	}

	if (ballRight <= paddleLeft)
	{
		return contact;
	}

	if (ballTop >= paddleBottom)
	{
		return contact;
	}

	if (ballBottom <= paddleTop)
	{
		return contact;
	}

	float paddleRangeUpper = paddleBottom - (2.0f * PADDLE_HEIGHT / 3.0f);
	float paddleRangeMiddle = paddleBottom - (PADDLE_HEIGHT / 3.0f);

	if (ball.position.x < WINDOW_WIDTH/2)
	{
		// Left paddle
		contact.penetration = paddleRight - ballLeft;
	}
	else
	{
		// Right paddle
		contact.penetration = paddleLeft - ballRight;
	}

	if ((ballBottom > paddleTop)
		&& (ballBottom < paddleRangeUpper))
	{
		contact.type = Ball::CollisionType::Top;
	}
	else if ((ballBottom > paddleRangeUpper)
		&& (ballBottom < paddleRangeMiddle))
	{
		contact.type = Ball::CollisionType::Middle;
	}
	else
	{
		contact.type = Ball::CollisionType::Bottom;
	}

	return contact;
}

struct Ball::Contact CheckWallCollision(Ball const& ball)
{
	float ballLeft = ball.position.x;
	float ballRight = ball.position.x + BALL_WIDTH;
	float ballTop = ball.position.y;
	float ballBottom = ball.position.y + BALL_HEIGHT;	

	Ball::Contact contact{};

	if (ballLeft < 0.0f)
	{
		contact.type = Ball::CollisionType::Left;
	}
	else if (ballRight > WINDOW_WIDTH)
	{
		contact.type = Ball::CollisionType::Right;
	}
	else if (ballTop < 0.0f)
	{
		contact.type = Ball::CollisionType::Top;
		contact.penetration = -ballTop;
	}
	else if (ballBottom > WINDOW_HEIGHT)
	{
		contact.type = Ball::CollisionType::Bottom;
		contact.penetration = WINDOW_HEIGHT - ballBottom;
	}

	return contact;
}

float lerp(float begin, float end, float t)
{
	return begin + t * (end - begin);
}

int main(int argc, char* argv[])
{
	// Start global timer for timestamping messages and logs
	Uint64 globalTime = SDL_GetTicks();

	// Initialize random seed
	srand(time(NULL));
	
	// Initialize SDL components
	SDL_Init(SDL_INIT_VIDEO);
	TTF_Init();
	Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);

	// Create unique ID of this client for window title
	char letters[27] = "abcdefghijklmnopqrstuvwxyz";
	std::string clientId = "client_";
	for (int i = 0; i < 5; i++)
	{
		(i % 2 == 0)
			? clientId += std::to_string(rand() % 10)
			: clientId += letters[rand() % 26];
	}	
	
	// Create window and 2D rendering context
	std::string windowTitle = std::string("Pong") + " | " + clientId;
	SDL_Window* window = SDL_CreateWindow(windowTitle.c_str(), 10, 40, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);

	// Initialize fonts
	TTF_Font* scoreFont = TTF_OpenFont("DejaVuSansMono.ttf", 40);
	TTF_Font* controlsFont = TTF_OpenFont("DejaVuSansMono.ttf", 12);
	TTF_Font* mainMenuFont = TTF_OpenFont("DejaVuSansMono.ttf", 26);

	// Initialize sound effects
	int volume = Mix_Volume(-1, -1);
	bool muted = false;
	Mix_Chunk* wallHitSound = Mix_LoadWAV("wall_hit.wav");
	Mix_Chunk* paddleHitSound = Mix_LoadWAV("paddle_hit.wav");
	Mix_Chunk* winSound = Mix_LoadWAV("win.wav");
	Mix_Chunk* loseSound = Mix_LoadWAV("lose.wav");

	// Server connection
	const sf::IpAddress serverIp = "127.0.0.1";
	const unsigned short serverPort = 4444;
	const unsigned short serverTcpPort = 4445;
	
	// Initialize client TCP socket for sending and receiving player score data
	sf::TcpSocket tcpSocket;
	sf::Socket::Status status = tcpSocket.connect(serverIp, serverTcpPort);
	if (status != sf::Socket::Done)
	{
		std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
			<< "\t| tcp socket connect error to server at " << serverIp << ":" << serverTcpPort
			<< std::endl;
		return 0;
	}

	// Create a selector
	sf::SocketSelector selector;

	// Initialize client UDP socket for sending and receiving position data
	sf::UdpSocket udpSocket;
	unsigned short port = tcpSocket.getLocalPort(); // use same port as tcp socket
	udpSocket.setBlocking(false); // make socket non-blocking
	if (udpSocket.bind(port) != sf::Socket::Done)
	{
		std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
			<< "\t| udp socket bind error on port " << port
			<< std::endl;
	}

	// Initialize client UDP socket for receiving ball position data
	sf::UdpSocket udpSocketBallPos;
	udpSocketBallPos.setBlocking(false);
	if (udpSocketBallPos.bind(sf::Socket::AnyPort) != sf::Socket::Done) // use OS-allocated port
	{
		std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
			<< "\t| ball position udp socket bind error"
			<< std::endl;
	}

	// Properties of received messages
	sf::IpAddress receiveIp;
	unsigned short receivePort;

	// Declare variables used to create network messages
	sf::Packet packet;
	Message msg;
	ScoreMessage scores;
	sf::Uint8 header;	

	// Define buttons
	enum Buttons
	{
		PaddleUp = 0,
		PaddleDown
	};
	// Create the ball
	Ball ball(
		Vec2((WINDOW_WIDTH / 2.0f) - (BALL_WIDTH / 2.0f), (WINDOW_HEIGHT / 2.0f) - (BALL_WIDTH / 2.0f)),
		Vec2(BALL_SPEED, 0.0f)
	);

	// Create the paddles
	Paddle paddleOne(
		Vec2(50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f)),
		Vec2(0.0f, 0.0f)
	);

	Paddle paddleTwo(
		Vec2(WINDOW_WIDTH - 50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f)),
		Vec2(0.0f, 0.0f)
	);

	// Create the text fields
	PlayerScore playerOneScoreText(Vec2(WINDOW_WIDTH / 4, 20), renderer, scoreFont);
	PlayerScore playerTwoScoreText(Vec2(3 * WINDOW_WIDTH / 4, 20), renderer, scoreFont);
	MenuText mainMenuText(Vec2(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2 - 15), renderer, mainMenuFont, "[Enter] to confirm ready");
	MenuText controlsText1(Vec2(WINDOW_WIDTH / 2 - 100, 20), renderer, controlsFont, "[w] = Up   [s] = Down");
	MenuText controlsText2(Vec2(WINDOW_WIDTH / 2 + 110, 20), renderer, controlsFont, "[m] = Mute   [Esc] = Quit");
		

	// Game logic
	{		
		int playerOneScore = 0;
		int playerTwoScore = 0;
		int winner = 0;
		std::string winnerText = "";

		int assignedPaddle = 0;
		Paddle* playerOnePaddle = &paddleOne;
		Paddle* playerTwoPaddle = &paddleTwo;
		
		bool gameStarted = false;
		bool playerReady = false;
		std::string oppDisconnected = "";
	
		Ball::Contact contact{};
		
		double newestPaddlePosTimestamp = 0;
		double newestBallPosTimestamp = 0;

		bool running = true;
		bool buttons[2] = {};		

		// Timing variables
		float dt = 0.004f;
		Uint64 startTicks = 0;
		Uint64 endTicks = 0;
		float sendDt = 0.0f;
		float sendRate = 100.0f;
		Uint64 sendStartTicks = SDL_GetTicks();
		Uint64 sendEndTicks = 0;
		float logDt = 0.0f;
		float logRate = 1500.0f;
		Uint64 logStartTicks = SDL_GetTicks();
		Uint64 logEndTicks = 0;
		float collisionDt = 0.0f;
		float collisionRate = 175.0f;
		Uint64 collisionStartTicks = SDL_GetTicks();
		Uint64 collisionEndTicks = 0;
		Uint64 winScreenStartTicks = 0;
		Uint64 winScreenEndTicks = 0;

		// Prediction and interpolation variables
		bool enablePandI = true;
		int numPredictions = 0;
		Message msgBasedPrediction{};
		Message predictionBasedPrediction{};
		Message validPredictionMsg{};
		Message ballPosition{};
		Message positionBasedPrediction{};
		Vec2 validPrediction{};
		Vec2 validPrediction2{};
		float averagePredictionX = 0;
		float averagePredictionY = 0;
		float interpolatedPositionX = 0;
		float interpolatedPositionY = 0;
		float interpolationPcntg = 0.005;	
		
		// Continue looping and processing events until user exits
		while (running)
		{			
			startTicks = SDL_GetTicks();			

			// Get paddle number from server and wait for confirmation of other player ready before starting game
			// socket is in blocking mode so game can't start until certain messages sent/received
			if(assignedPaddle == 0)
			{				
				// Server closes connections to all clients when any client disconnects, so reconnection to server and udp rebind required
				if (oppDisconnected == "opponent disconnected")
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Opponent disconnected. Reconnecting tcp socket to " << serverIp << " at port " << serverTcpPort
						<< std::endl;
					
					sf::Socket::Status status = tcpSocket.connect(serverIp, serverTcpPort);
					if (status != sf::Socket::Done)
					{
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| tcp socket connect error to " << serverIp << ":" << serverTcpPort
							<< std::endl;
					}

					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Bind paddle position udp socket to new tcp port (" << tcpSocket.getLocalPort() << ")"
						<< std::endl;

					port = tcpSocket.getLocalPort();
					if (udpSocket.bind(port) != sf::Socket::Done)
					{
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| udp socket bind error on port " << port
							<< std::endl;
					}

					oppDisconnected = "";
					mainMenuText.SetText(Vec2(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2 - 15), "Opponent disconnected! [Enter] to play again");
				}
				// Server disconnects clients after game ends normally, so reconnect and udp rebind required
				else if (winner) 
				{
					sf::Socket::Status status = tcpSocket.connect(serverIp, serverTcpPort);
					if (status != sf::Socket::Done)
					{
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| tcp socket connect error to " << serverIp << ":" << serverTcpPort
							<< std::endl;
					}

					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Bind paddle position udp socket to new tcp port (" << tcpSocket.getLocalPort() << ")"
						<< std::endl;

					port = tcpSocket.getLocalPort();
					if (udpSocket.bind(port) != sf::Socket::Done)
					{
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| udp socket bind error on port " << port
							<< std::endl;
					}

					winner = 0;
					mainMenuText.SetText(Vec2(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2 - 15), "[Enter] to confirm ready");
				}
				
				SDL_SetRenderDrawColor(renderer, 0x0, 0x0, 0x0, 0xFF);
				SDL_RenderClear(renderer);

				// Display start text
				mainMenuText.Draw();

				// Present the backbuffer
				SDL_RenderPresent(renderer);

				// Wait for player to quit or confirm ready
				while (!playerReady)
				{
					SDL_Event event;
					while (SDL_PollEvent(&event))
					{
						if (event.type == SDL_QUIT)
						{
							tcpSocket.disconnect();
							running = false;
						}
						else if (event.type == SDL_KEYDOWN)
						{
							if (event.key.keysym.sym == SDLK_ESCAPE)
							{
								tcpSocket.disconnect();
								playerReady = true;
								running = false;
							}
							else if (event.key.keysym.sym == SDLK_RETURN)
							{
								playerReady = true;
							}
						}
					}
				}

				// Display waiting message
				mainMenuText.SetText(Vec2(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2 - 5), "Waiting for second player...");

				SDL_SetRenderDrawColor(renderer, 0x0, 0x0, 0x0, 0xFF);
				SDL_RenderClear(renderer);

				mainMenuText.Draw();

				// Present the backbuffer
				SDL_RenderPresent(renderer);
				
				// Wait to receive packet with paddle number from server
				std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					<< "\t| Waiting for server to assign paddle..."
					<< std::endl;
				
				packet.clear();
				if (tcpSocket.receive(packet) != sf::Socket::Done)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| tcp socket receive error"
						<< std::endl;
				}

				// Set this client's paddle number and assign each paddle to player pointers
				if (packet.getDataSize() > 0)
				{
					packet >> assignedPaddle;

					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Assigned paddle = " << assignedPaddle
						<< std::endl;

					if (assignedPaddle == 1)
					{
						playerOnePaddle = &paddleOne;
						playerTwoPaddle = &paddleTwo;
					}
					else
					{
						playerOnePaddle = &paddleTwo;
						playerTwoPaddle = &paddleOne;
					}
				}

				// Send udp ball position socket's port number to server using existing tcp connection
				std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					<< "\t| Sending udp ball position socket port number (" << udpSocketBallPos.getLocalPort() << ") to server"
					<< std::endl;

				packet.clear();
				packet << udpSocketBallPos.getLocalPort();
				if (tcpSocket.send(packet) != sf::Socket::Done)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| tcp socket send error"
						<< std::endl;
				}

				// Wait for server message to confirm other player ready (tcp socket is in blocking mode)
				std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					<< "\t| Waiting for server confirm game start..."
					<< std::endl;
				packet.clear();
				if (tcpSocket.receive(packet) != sf::Socket::Done)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| tcp socket receive error"
						<< std::endl;
				}
				
				// Add tcp socket to selector to stop it blocking if there is no data to send/receive when game is running
				selector.add(tcpSocket);
				
				std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					<< "\t| Starting game..."
					<< std::endl;
			}

			// Poll for pending key press or SQL_QUIT event
			SDL_Event event;
			while (SDL_PollEvent(&event))
			{
				if (event.type == SDL_QUIT)
				{
					tcpSocket.disconnect();
					running = false;
				}
				else if (event.type == SDL_KEYDOWN)
				{
					if (event.key.keysym.sym == SDLK_ESCAPE)
					{
						tcpSocket.disconnect();
						running = false;						
					}
					else if (event.key.keysym.sym == SDLK_w)
					{
						buttons[Buttons::PaddleUp] = true;
					}
					else if (event.key.keysym.sym == SDLK_s)
					{
						buttons[Buttons::PaddleDown] = true;
					}
					else if (event.key.keysym.sym == SDLK_p)
					{
						if (enablePandI)
						{
							enablePandI = false;
						}
						else
						{
							enablePandI = true;
						}
					}
					else if (event.key.keysym.sym == SDLK_m)
					{
						if (muted)
						{
							Mix_Volume(-1, volume);
							muted = false;
						}
						else
						{
							Mix_Volume(-1, 0);
							muted = true;
						}
					}
				}
				else if (event.type == SDL_KEYUP)
				{
					if (event.key.keysym.sym == SDLK_w)
					{
						buttons[Buttons::PaddleUp] = false;
					}
					else if (event.key.keysym.sym == SDLK_s)
					{
						buttons[Buttons::PaddleDown] = false;
					}
				}
			}

			// Change paddle velocity based on key pressed
			if (buttons[Buttons::PaddleUp])
			{
				playerOnePaddle->velocity.y = -PADDLE_SPEED;
			}
			else if (buttons[Buttons::PaddleDown])
			{
				playerOnePaddle->velocity.y = PADDLE_SPEED;
			}
			else
			{
				playerOnePaddle->velocity.y = 0.0f;
			}

			// Update player one position and send to server
			playerOnePaddle->Update(dt);
			
			sendEndTicks = SDL_GetTicks();
			sendDt = (sendEndTicks - sendStartTicks);
			if(sendDt > sendRate) // If sendRate milliseconds passed since last sent paddle position, send again
			{
				// Send paddle position to server	
				packet.clear();
				msg.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0);
				msg.port = port;
				msg.x = playerOnePaddle->position.x;
				msg.y = playerOnePaddle->position.y;				
				msg.ball = false;
				packet << msg;
				
				logEndTicks = SDL_GetTicks();
				logDt = (logEndTicks - logStartTicks);
				if (logDt > logRate) // If logRate milliseconds passed since log timer last reset, print to console
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Sending paddle position message: Timestamp=" << msg.timestamp
						<< "; Port=" << msg.port
						<< "; x=" << msg.x << "; y=" << msg.y
						<< "; ball=" << msg.ball
						<< std::endl;
				}

				if (udpSocket.send(packet, serverIp, serverPort) != sf::Socket::Done)
				{
					//std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						//<< "\t| udp socket send error"
						//<< std::endl;
				}
				
				// Reset send rate timer
				sendStartTicks = SDL_GetTicks();
			}

			if (enablePandI)
			{
				// Predict position of player two paddle based on prevous messages
				msgBasedPrediction = playerTwoPaddle->RunPrediction(static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0), false);

				// If message based prediction is different to the previous one, add to predictions
				numPredictions = playerTwoPaddle->paddlePredictions.size();
				if (numPredictions > 0)
				{
					if (msgBasedPrediction.y != playerTwoPaddle->paddlePredictions[numPredictions - 1].y)
					{
						playerTwoPaddle->AddPrediction(msgBasedPrediction);
					}
				}
				else
				{
					playerTwoPaddle->AddPrediction(msgBasedPrediction);
				}

				// Predict position based on previous predictions				
				predictionBasedPrediction = playerTwoPaddle->RunPrediction(static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0), true);
				playerTwoPaddle->AddPrediction(predictionBasedPrediction);

				// Get average of messages-based and predictions-based predicted positions
				averagePredictionY = (msgBasedPrediction.y + predictionBasedPrediction.y) / 2.0;

				// Move percentage towards average of predictions
				interpolatedPositionY = lerp(playerTwoPaddle->position.y, averagePredictionY, interpolationPcntg);
				
				playerTwoPaddle->position.y = interpolatedPositionY;

				if (logDt > logRate)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Predicted position of player two paddle: "
						<< "message-based prediction y = " << msgBasedPrediction.y << "; "
						<< "prediction-based prediction y = " << predictionBasedPrediction.y << "; "
						<< "interpolated position y = " << interpolatedPositionY
						<< std::endl;
				}
			}
			
			// Receive position of paddle two from server
			packet.clear();
			msg.timestamp = 0;
			msg.port = 0;
			msg.x = playerTwoPaddle->position.x;
			msg.y = playerTwoPaddle->position.y;
			msg.ball = false;			
			
			if (logDt > logRate)
			{
				std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					<< "\t| Receiving paddle position message from server..."
					<< std::endl;
			}

			// Receive new paddle position message
			if (udpSocket.receive(packet, receiveIp, receivePort) != sf::Socket::Done)
			{
				//std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					//<< "\t| udp socket receive error"
					//<< std::endl;
			}

			// Update player two paddle position based on new messages
			if (packet.getDataSize() > 0)
			{				
				packet >> msg;	
				msg.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0); // change timestamp to this client's time

				if (!msg.ball)
				{
					if (logDt > logRate)
					{
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| Received paddle position message from " << receiveIp << " on port " << receivePort
							<< std::endl;

						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| Timestamp=" << msg.timestamp
							<< "; Port=" << msg.port
							<< "; x=" << msg.x << "; y=" << msg.y
							<< "; ball=" << msg.ball
							<< std::endl;
					}

					if (msg.timestamp > newestPaddlePosTimestamp)
					{
						newestPaddlePosTimestamp = msg.timestamp;
						// Add message to history of player two position messages
						playerTwoPaddle->AddMessage(msg);

						// Move percentage towards new position received from server
						if (enablePandI)
						{
							interpolatedPositionY = lerp(playerTwoPaddle->position.y, msg.y, interpolationPcntg);

							if (logDt > logRate)
							{
								std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
									<< "\t| Moving player two paddle to interpolated position y=" << interpolatedPositionY
									<< std::endl;
							}

							playerTwoPaddle->position.y = interpolatedPositionY;
						}
						else // Move straight to received position for player two paddle
						{
							if (logDt > logRate)
							{
								std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
									<< "\t| Moving player two paddle to y=" << msg.y
									<< std::endl;
							}

							playerTwoPaddle->position.y = msg.y;
						}
					}
				}
			}

			if (enablePandI)
			{
				// Predict position of ball based on prevous messages
				msgBasedPrediction = ball.RunPrediction(static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0), 0, ball);

				// Check for ball phasing through paddle and correct
				validPrediction =
					ball.ValidatePrediction(
						ball,
						msgBasedPrediction.x,
						msgBasedPrediction.y,
						paddleOne.position.x,
						paddleOne.position.y,
						paddleTwo.position.x,
						paddleTwo.position.y
					);

				// Store validated prediction as message
				validPredictionMsg.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0);
				validPredictionMsg.x = validPrediction.x;
				validPredictionMsg.y = validPrediction.y;
				validPredictionMsg.ball = true;
				validPredictionMsg.port = 0;
				
				// If validated messages-based prediction is different to the previous one, add to predictions
				numPredictions = ball.ballPredictions.size();
				if (numPredictions > 0)
				{
					if (Vec2(validPrediction.x, validPrediction.y) != Vec2(ball.ballPredictions[numPredictions - 1].x, ball.ballPredictions[numPredictions - 1].y))
					{
						ball.AddPrediction(validPredictionMsg);
					}
				}
				else
				{
					ball.AddPrediction(validPredictionMsg);

				}

				// Predict position based on previous predictions
				predictionBasedPrediction = ball.RunPrediction(static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0), 1, ball);

				// Validate predictions-based prediction (check for paddle phase)
				validPrediction2 =
					ball.ValidatePrediction(
						ball,
						predictionBasedPrediction.x,
						predictionBasedPrediction.y,
						paddleOne.position.x,
						paddleOne.position.y,
						paddleTwo.position.x,
						paddleTwo.position.y
					);
				
				/*validPredictionMsg.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0);
				validPredictionMsg.x = validPrediction2.x;
				validPredictionMsg.y = validPrediction2.y;
				validPredictionMsg.ball = true;
				validPredictionMsg.port = 0;*/

				// If validated predictions-based prediction is different to previous prediction, add to predictions history
				/*numPredictions = ball.ballPredictions.size();
				if (numPredictions > 0)
				{
					if (Vec2(validPrediction2.x, validPrediction2.y) != Vec2(ball.ballPredictions[numPredictions - 1].x, ball.ballPredictions[numPredictions - 1].y))
					{						
						ball.AddPrediction(validPredictionMsg);
					}
				}
				else
				{
					ball.AddPrediction(validPredictionMsg);
				}*/

				// Get average of messages-based prediction and predictions-based prediction
				averagePredictionX = (validPrediction.x + validPrediction2.x) / 2.0;
				averagePredictionY = (validPrediction.y + validPrediction2.y) / 2.0;

				// Move percentage towards average of predictions
				interpolatedPositionX = lerp(ball.position.x, averagePredictionX, interpolationPcntg);
				interpolatedPositionY = lerp(ball.position.y, averagePredictionY, interpolationPcntg);

				if (logDt > logRate)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Predicted position of ball: "
						<< "message-based prediction = (" << validPrediction.x << "," << validPrediction.y << ")" << "; "
						<< "prediction-based prediction = (" << validPrediction2.x << "," << validPrediction2.y << ")" << "; "
						<< "interpolated position = (" << interpolatedPositionX << "," << interpolatedPositionY << ")"
						<< std::endl;
				}

				ball.position.x = interpolatedPositionX;
				ball.position.y = interpolatedPositionY;
			}
		
			// Receive position of ball from server
			packet.clear();
			msg.timestamp = 0;
			msg.port = 0;
			msg.x = 0;
			msg.y = 0;
			msg.ball = false;

			if (logDt > logRate)
			{
				std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					<< "\t| Receiving ball position message..."
					<< std::endl;
			}

			if (udpSocketBallPos.receive(packet, receiveIp, receivePort) != sf::Socket::Done)
			{
				//std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					//<< "\t| udp ball position socket receive error"
					//<< std::endl;
			}

			// Update ball position
			if (packet.getDataSize() > 0)
			{
				packet >> msg;
				msg.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0); // change timestamp to this client's time

				if (msg.ball)
				{					
					if (logDt > logRate)
					{
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| Received ball position message " << receiveIp << " on port " << receivePort
							<< std::endl;

						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| Timestamp=" << msg.timestamp
							<< "; Port=" << msg.port
							<< "; x=" << msg.x << "; y=" << msg.y
							<< "; ball=" << msg.ball
							<< std::endl;
					}			

					if (msg.timestamp > newestBallPosTimestamp)
					{
						newestBallPosTimestamp = msg.timestamp;
						// Add message to history of ball position messages
						ball.AddMessage(msg);

						// Move percentage towards new position received from server
						if (enablePandI)
						{
							// If ball has moved to center of the screen after a player has scored, don't interpolate or predict
							if (std::fabs(msg.x - ball.position.x) >= (WINDOW_WIDTH / 2 - BALL_WIDTH * 2))
							{
								std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
									<< "\t| Ball position reset after goal"
									<< std::endl;

								ball.position.x = msg.x;
								ball.position.y = msg.y;
								ball.ballMessages.clear();
								ball.ballPredictions.clear();
							}
							else
							{
								// Interpolate new x and y positions and move ball there
								interpolatedPositionX = lerp(ball.position.x, msg.x, interpolationPcntg);
								interpolatedPositionY = lerp(ball.position.y, msg.y, interpolationPcntg);
								;
								if (logDt > logRate)
								{
									std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
										<< "\t| Moving ball towards latest received position via interpolation: "
										<< "(" << interpolatedPositionX << "," << interpolatedPositionY << ")"
										<< std::endl;
								}

								ball.position.x = interpolatedPositionX;
								ball.position.y = interpolatedPositionY;
							}
						}
						else
						{
							if (logDt > logRate)
							{
								std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
									<< "\t| Moving ball to latest received position: "
									<< "(" << interpolatedPositionX << "," << interpolatedPositionY << ")"
									<< std::endl;
							}

							// Move ball directly to received position if prediction and interpolation toggled off
							ball.position.x = msg.x;
							ball.position.y = msg.y;
						}
					}
				}
			}									

			// Collision checking
			contact = {};

			collisionEndTicks = SDL_GetTicks();
			collisionDt = (collisionEndTicks - collisionStartTicks);
			if (collisionDt > collisionRate) // To prevent multiple collision sounds being played in quick succession, limit number of collisions per unit of time
			{												
				if (contact = CheckPaddleCollision(ball, paddleOne); contact.type != Ball::CollisionType::None)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Paddle one collision detected"
						<< std::endl;
					
					ball.CollideWithPaddle(contact);
					Mix_PlayChannel(-1, paddleHitSound, 0);

					collisionStartTicks = SDL_GetTicks();
				}
				else if (contact = CheckPaddleCollision(ball, paddleTwo); contact.type != Ball::CollisionType::None)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Paddle two collision detected"
						<< std::endl;
					
					ball.CollideWithPaddle(contact);
					Mix_PlayChannel(-1, paddleHitSound, 0);

					collisionStartTicks = SDL_GetTicks();
				}
				else if (contact = CheckWallCollision(ball); contact.type != Ball::CollisionType::None)
				{
					ball.CollideWithWall(contact);
					Mix_PlayChannel(-1, wallHitSound, 0);

					collisionStartTicks = SDL_GetTicks();
				}				
			}

			/*	Update ball position
			* 
			*	Required because ball may have changed direction as the result of a collision and
			*	should continue moving in that direction (client ignores ball velocity property to
			*	prevent case where ball oscillates due to delay between client and server collisions)
			*/
			
			// Add current position to history of positions
			ballPosition.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0);
			ballPosition.port = 0;
			ballPosition.x = ball.position.x;
			ballPosition.y = ball.position.y;
			ballPosition.ball = true;

			if (logDt > logRate)
			{
				std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					<< "\t| Adding current ball position to history: (" << ballPosition.x << "," << ballPosition.y << ")"
					<< std::endl;
			}

			ball.AddPosition(ballPosition);

			// Predict new position based on previous positions and interpolate between current position and prediction
			positionBasedPrediction = ball.RunPrediction(static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0), 2, ball);
			interpolatedPositionX = lerp(ball.position.x, positionBasedPrediction.x, interpolationPcntg);
			interpolatedPositionY = lerp(ball.position.y, positionBasedPrediction.y, interpolationPcntg);
			
			// Validate prediction in case of paddle phase
			validPrediction =
				ball.ValidatePrediction(
					ball,
					interpolatedPositionX,
					interpolatedPositionY,
					paddleOne.position.x,
					paddleOne.position.y,
					paddleTwo.position.x,
					paddleTwo.position.y
				);

			// Add validated prediction to history of predictions
			validPredictionMsg.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0);
			validPredictionMsg.x = validPrediction.x;
			validPredictionMsg.y = validPrediction.y;
			validPredictionMsg.ball = true;
			validPredictionMsg.port = 0;

			ball.AddPrediction(validPredictionMsg);

			if (logDt > logRate)
			{
				std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					<< "\t| Moving ball percentage towards history-based predicted position: (" << interpolatedPositionX << "," << interpolatedPositionY << ")"
					<< std::endl;
			}

			// Set ball position to new valid prediction (that was based on history of positions)
			ball.position = validPrediction;

			// Add new position to history of positions
			ballPosition.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0);
			ballPosition.port = 0;
			ballPosition.x = ball.position.x;
			ballPosition.y = ball.position.y;
			ballPosition.ball = true;

			ball.AddPosition(ballPosition);

			// Receive player scores or opponent disconnected message from server and update
			if (selector.wait(sf::microseconds(1)))
			{
				if (selector.isReady(tcpSocket))
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Receiving score message from server"
						<< std::endl;

					packet.clear();
					if (tcpSocket.receive(packet) != sf::Socket::Done)
					{
						//std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							//<< "\t| tcp socket receive error"
							//<< std::endl;
					}

					if (packet.getDataSize() > 0)
					{
						// Check packet header to process message from server
						// 0 = opponent disconnect message
						// 1 = score update
						packet >> header;
						
						switch (header)
						{
						case 0:
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Received opponent disconnected message"
								<< std::endl;

							packet >> oppDisconnected;
							if (oppDisconnected == "opponent disconnected")
							{
								std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
									<< "\t| Resetting the game"
									<< std::endl;

								// Reset game
								gameStarted = false;
								playerReady = false;
								assignedPaddle = 0;
								winner = 0;
								ball.ballMessages.clear();
								ball.ballPredictions.clear();
								ball.ballPositions.clear();
								playerTwoPaddle->paddleMessages.clear();
								playerTwoPaddle->paddlePredictions.clear();
								paddleOne.position = Vec2(50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f));
								paddleTwo.position = Vec2(WINDOW_WIDTH - 50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f));
								ball.position = Vec2((WINDOW_WIDTH / 2.0f) - (BALL_WIDTH / 2.0f), (WINDOW_HEIGHT / 2.0f) - (BALL_WIDTH / 2.0f));
								playerOneScore = 0;
								playerTwoScore = 0;
								selector.remove(tcpSocket);
								sendStartTicks = SDL_GetTicks();
								logStartTicks = SDL_GetTicks();
								collisionStartTicks = SDL_GetTicks();
							}
							
							break;
						case 1:
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Received score update message"
								<< std::endl;

							scores.timestamp = 0;
							scores.playerOneScore = playerOneScore;
							scores.playerTwoScore = playerTwoScore;

							packet >> scores;
							playerOneScore = scores.playerOneScore;
							playerTwoScore = scores.playerTwoScore;
							
							break;						
						case 2:														
							packet >> winner;
							
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Received winner message. Paddle " << winner << " won"
								<< std::endl;
							
							if (assignedPaddle == winner)
							{
								winnerText = "You win! ";
								Mix_PlayChannel(-1, winSound, 0);
							}
							else
							{
								winnerText = "You lose! ";
								Mix_PlayChannel(-1, loseSound, 0);
							}
							winnerText += "Returning to start screen...";

							mainMenuText.SetText(Vec2(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2 - 15), winnerText);
						
							SDL_SetRenderDrawColor(renderer, 0x0, 0x0, 0x0, 0xFF);
							SDL_RenderClear(renderer);

							// Display start text
							mainMenuText.Draw();

							// Present the backbuffer
							SDL_RenderPresent(renderer);

							// Wait for 5 seconds						
							winScreenEndTicks = SDL_GetTicks();								
							winScreenStartTicks = SDL_GetTicks();
							while ((winScreenEndTicks - winScreenStartTicks) <= 5000)
							{
								winScreenEndTicks = SDL_GetTicks();
							}
							
							// Reset game
							gameStarted = false;
							playerReady = false;
							assignedPaddle = 0;
							ball.ballMessages.clear();
							ball.ballPredictions.clear();
							ball.ballPositions.clear();
							playerTwoPaddle->paddleMessages.clear();
							playerTwoPaddle->paddlePredictions.clear();
							paddleOne.position = Vec2(50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f));
							paddleTwo.position = Vec2(WINDOW_WIDTH - 50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f));
							ball.position = Vec2((WINDOW_WIDTH / 2.0f) - (BALL_WIDTH / 2.0f), (WINDOW_HEIGHT / 2.0f) - (BALL_WIDTH / 2.0f));
							playerOneScore = 0;
							playerTwoScore = 0;
							selector.remove(tcpSocket);
							sendStartTicks = SDL_GetTicks();
							logStartTicks = SDL_GetTicks();
							collisionStartTicks = SDL_GetTicks();

							break;
						}
					}
				}
			}
			
			playerOneScoreText.SetScore(playerOneScore);
			playerTwoScoreText.SetScore(playerTwoScore);

			// Clear the window to black
			SDL_SetRenderDrawColor(renderer, 0x0, 0x0, 0x0, 0xFF);
			SDL_RenderClear(renderer);

			// Set the draw color to be white
			SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);

			// Draw the net
			for (int y = 0; y < WINDOW_HEIGHT; ++y)
			{
				if (y % 5)
				{
					SDL_RenderDrawPoint(renderer, WINDOW_WIDTH / 2, y);
				}
			}	
			
			// Draw the ball
			ball.Draw(renderer);

			// Draw the paddles
			paddleOne.Draw(renderer);
			paddleTwo.Draw(renderer);

			// Display the scores
			playerOneScoreText.Draw();
			playerTwoScoreText.Draw();

			// Display controls
			controlsText1.Draw();
			controlsText2.Draw();

			// Set the draw color to be black
			SDL_SetRenderDrawColor(renderer, 0x0, 0x0, 0x0, 0xFF);

			// Display player one paddle indicator
			playerOnePaddle->ShowPlayerIndicator(renderer);
			
			// Present the backbuffer
			SDL_RenderPresent(renderer);
			
			// Reset the log printing timer
			if (logDt > logRate)
			{
				logStartTicks = SDL_GetTicks();
			}			
			
			// Lerp t = 1 - f^dt
			// f is the remaining distance to travel each second
			// dt in above formula is delta time as a fraction of a second
			interpolationPcntg = 1.0 - pow(0.000001, dt / 1000.0);

			// Calculate frame time
			endTicks = SDL_GetTicks();
			dt = (endTicks - startTicks);
		}
	}

	// Cleanup
	Mix_FreeChunk(wallHitSound);
	Mix_FreeChunk(paddleHitSound);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	TTF_CloseFont(scoreFont);
	Mix_Quit();
	TTF_Quit();
	SDL_Quit();

	return 0;
}