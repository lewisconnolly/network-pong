#include <SDL.h>
#include <SFML/Network.hpp>
#include <SFML/System/Time.hpp>
#include <stdio.h>
#include <iostream>
#include <list>
#include "Global.h"
#include "Vec2.h"
#include "Ball.h"
#include "Paddle.h"

struct ScoreMessage
{
	double timestamp = 0;
	int playerOneScore, playerTwoScore = 0;
};

struct Client
{
	sf::TcpSocket* tcpSocket = NULL;
	int paddle = 0;
	Vec2 lastPosition = Vec2(0,0);
	bool ready = false;
	unsigned short portBallPos = 0;
	double lastMsgTimestamp = 0;
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

	if (ball.velocity.x < 0)
	{
		// Left paddle
		contact.penetration = paddleRight - ballLeft;
	}
	else if (ball.velocity.x > 0)
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


int main(int argc, char* argv[])
{		
	// Start global timer for timestamping messages and logs
	Uint64 globalTime = SDL_GetTicks();
	
	// Initialize SDL components
	SDL_Init(SDL_INIT_VIDEO);

	// Initialize server tcp socket
	sf::TcpListener listener;	
	if (listener.listen(4445) != sf::Socket::Done) // bind the listener to a port
	{
		std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
			<< "\t| tcp socket listen error"
			<< std::endl;
	}
	
	// Create a selector
	sf::SocketSelector selector;

	// Add the listener to the selector
	selector.add(listener);

	// Initialize server udp socket
	unsigned short listenPort = 4444;
	sf::UdpSocket socket;
	socket.setBlocking(false);
	if (socket.bind(listenPort) != sf::Socket::Done)
	{
		std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
			<< "\t| udp socket bind error on port " << listenPort
			<< std::endl;
	}

	// Properties of received message
	sf::IpAddress clientIp;
	unsigned short clientPort;

	// Variables to send/receive packet data to
	sf::Packet packet;
	sf::Packet ballPacket;
	Message msg;
	Message ballMsg;
	ScoreMessage scores;
	sf::Uint8 header;
	
	// List of clients
	std::list<Client> clients;
	
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

	// Game logic
	{
		int playerOneScore = 0;
		int playerTwoScore = 0;
		int playerOnePrevScore = 0;
		int playerTwoPrevScore = 0;
		int winner = 0;
		int winningScore = 7;

		int assignedPaddle = 1;
		int playersReady = 0;
		unsigned short playerReadyMsg = 0;
		
		bool gameStarted = false;
		bool clientDisconnected = false;

		Ball::Contact contact{};

		double newestPaddleOnePosTimestamp = 0;
		double newestPaddleTwoPosTimestamp = 0;

		bool running = true;
		bool buttons[2] = {};

		// Timing variables
		float dt = 0.0f;
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

		// Prediction and interpolation variables
		Message msgBasedPrediction{};
		int numPredictions = 0;
		Message predictionBasedPrediction{};
		float averagePrediction = 0;
		float interpolatedPosition = 0;

		// Continue looping and processing events until user exits
		while (running)
		{
			startTicks = SDL_GetTicks();			

			// If both client ball position socket port numbers have been received
			// Send game started message to client and enter main game loop
			if (!gameStarted && playersReady == 2)
			{
				for (Client& c : clients)
				{										
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Sending game started message to: "
						<< (*c.tcpSocket).getRemoteAddress() << " at " << "port " << (*c.tcpSocket).getRemotePort()
						<< std::endl;
												
					packet.clear();
					packet << "game started";
					if ((*c.tcpSocket).send(packet) != sf::Socket::Done)
					{
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| tcp socket send error"
							<< std::endl;
					}
				}
				gameStarted = true;
			}

			// Wait for both clients to connect, assign paddles and send ball position socket port numbers
			if (!gameStarted)
			{
				std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
					<< "\t| Waiting for clients to connect..."
					<< std::endl;

				// Make the selector wait for data on any socket
				if (selector.wait())
				{
					// Test the listener
					if (selector.isReady(listener))
					{
						// The listener is ready: there is a pending connection
						sf::TcpSocket* clientTcpSocket = new sf::TcpSocket;
						if (listener.accept(*clientTcpSocket) == sf::Socket::Done)
						{
							// Add the new client to the clients list, assign a paddle number and send it to the client
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Accepting new client " << (*clientTcpSocket).getRemoteAddress() << " at " << "port " << (*clientTcpSocket).getRemotePort()
								<< std::endl;							

							Client newClient;
							newClient.tcpSocket = clientTcpSocket;
							newClient.paddle = assignedPaddle;
							newClient.lastPosition = Vec2(0, 0);
							newClient.ready = false;
							newClient.portBallPos = 0;
							newClient.lastMsgTimestamp = 0;
							clients.push_back(newClient);

							// Add the new client to the selector so that we will
							// be notified when it sends something
							selector.add(*clientTcpSocket);

							// Send paddle number to client							
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Sending paddle assignment " << assignedPaddle
								<< " to " << (*clientTcpSocket).getRemoteAddress() << " at " << "port " << (*clientTcpSocket).getRemotePort()
								<< std::endl;

							packet.clear();
							packet << assignedPaddle;
							if ((*clientTcpSocket).send(packet) != sf::Socket::Done)
							{
								std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
									<< "\t| tcp socket send error"
									<< std::endl;
							}
							
							// Increase paddle number for next client that connects
							assignedPaddle++;
						}
						else
						{
							// Error, we won't get a new connection, delete the socket
							delete clientTcpSocket;
						}
					}
					// Receive ball position socket port number from client after connection has been accepted,
					// or handle disconnection if client disconnects before game starts
					else
					{
						std::list<Client>::iterator it;
						for (it = clients.begin(); it != clients.end();)
						{
							if (!(*it).ready) // if client player has not yet entered to confirm ready and to send ball position socket port number
							{
								sf::TcpSocket& client = *(*it).tcpSocket;
								if (selector.isReady(client))
								{									
									packet.clear();
									if (client.receive(packet) == sf::Socket::Disconnected)
									{										
										std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
											<< "\t| Client disconnected: " << client.getRemoteAddress() << " at " << "port " << client.getRemotePort()
											<< std::endl;
										
										// Remove tcp socker for disconnected client from selector
										selector.remove(client);

										// If client previously confirmed ready, then decrease count of ready players
										if ((*it).ready)
										{
											playersReady--;
										}
										
										// Decrement paddle count to correctly assign when a new client connects
										assignedPaddle--;

										// Erase client from client list
										it = clients.erase(it);
									}
									else 
									{																														
										// Get client's ball position socket port number
										packet >> playerReadyMsg;

										std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
											<< "\t| Client " << client.getRemoteAddress() << " at " << "port " << client.getRemotePort()
											<< " sent ball position socket port number (" << playerReadyMsg << ")"
											<< std::endl;

										// Set port number and ready status for client
										(*it).portBallPos = playerReadyMsg;
										(*it).ready = true;
										playersReady++;
										
										++it;
									}
								}
								else
								{
									++it;
								}
							}
							else
							{
								++it;
							}
						}
					}
				}
			}
			else
			{								
				// Poll for escape key or SQL_QUIT event
				SDL_Event event;
				while (SDL_PollEvent(&event))
				{
					if (event.type == SDL_QUIT)
					{
						running = false;
					}
					else if (event.type == SDL_KEYDOWN)
					{
						if (event.key.keysym.sym == SDLK_ESCAPE)
						{
							running = false;
						}
					}
				}

				// For each paddle, predict its position based on previously received messages
				// then use interpolation to move to a position between the current and predicted
				for (Client& c : clients)
				{
					if (c.paddle == 1)
					{
						// Predict position of paddle one based on previous messages
						msgBasedPrediction = paddleOne.RunPrediction(static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0), false);

						// If prediction is different to previous, add to history
						numPredictions = paddleOne.paddlePredictions.size();
						if (numPredictions > 0)
						{
							if (msgBasedPrediction.y != paddleOne.paddlePredictions[numPredictions - 1].y)
							{
								paddleOne.AddPrediction(msgBasedPrediction);
							}
						}
						else
						{
							paddleOne.AddPrediction(msgBasedPrediction);
						}

						// Predict position of paddle one based on previous predictions and add to history
						predictionBasedPrediction = paddleOne.RunPrediction(static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0), true);
						paddleOne.AddPrediction(predictionBasedPrediction);

						// Get average of messages-based and predictions-based predicted positions and move a percentage towards it
						averagePrediction = (msgBasedPrediction.y + predictionBasedPrediction.y) / 2.0;

						if (logDt > logRate)
						{
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Predicted position of paddle one: "
								<< "message-based prediction y = " << msgBasedPrediction.y << "; "
								<< "prediction-based prediction y = " << predictionBasedPrediction.y << "; "
								<< std::endl;
						}

						paddleOne.position.y = averagePrediction;
					}
					else
					{
						// Predict position of paddle two based on previous messages
						msgBasedPrediction = paddleTwo.RunPrediction(static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0), false);

						// If prediction is different to previous, add to history
						numPredictions = paddleTwo.paddlePredictions.size();
						if (numPredictions > 0)
						{
							if (msgBasedPrediction.y != paddleTwo.paddlePredictions[numPredictions - 1].y)
							{
								paddleTwo.AddPrediction(msgBasedPrediction);
							}
						}
						else
						{
							paddleTwo.AddPrediction(msgBasedPrediction);
						}

						// Predict position of paddle two based on previous predictions and add to history
						predictionBasedPrediction = paddleTwo.RunPrediction(static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0), true);
						paddleTwo.AddPrediction(predictionBasedPrediction);

						// Get average of messages-based and predictions-based predicted positions and move a percentage towards it
						averagePrediction = (msgBasedPrediction.y + predictionBasedPrediction.y) / 2.0;
						
						if (logDt > logRate)
						{
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Predicted position of paddle two: "
								<< "message-based prediction y = " << msgBasedPrediction.y << "; "
								<< "prediction-based prediction y = " << predictionBasedPrediction.y << "; "
								<< std::endl;
						}
						
						paddleTwo.position.y = averagePrediction;
					}
				}

				logEndTicks = SDL_GetTicks();
				logDt = (logEndTicks - logStartTicks);
				if (logDt > logRate)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Receiving paddle position message..."
						<< std::endl;
				}
				
				// Receive position of paddles from clients
				packet.clear();
				if (socket.receive(packet, clientIp, clientPort) != sf::Socket::Done)
				{
					// error...
					//std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						//<< "\t| udp socket receive error"
						//<< std::endl;
				}
				else
				{
					if (packet.getDataSize() > 0)
					{
						packet >> msg;

						if (logDt > logRate)
						{
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Received message from " << clientIp << " on port " << clientPort
								<< std::endl;

							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Timestamp=" << msg.timestamp
								<< "; Port=" << msg.port
								<< "; x=" << msg.x << "; y=" << msg.y
								<< std::endl;
						}

						// Send paddle positions between clients
						// Update clients' last positions
						// Update paddle positions with latest received message		
						for (Client& c : clients)
						{
							// Send packet containing position information of one client to the other
							if (msg.port != (*c.tcpSocket).getRemotePort())
							{
								if (logDt > logRate)
								{
									std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
										<< "\t| Sending message: Timestamp=" << msg.timestamp
										<< "; Port=" << msg.port
										<< "; x=" << msg.x << "; y=" << msg.y
										<< std::endl;
								}

								if (socket.send(packet, (*c.tcpSocket).getRemoteAddress(), (*c.tcpSocket).getRemotePort()) != sf::Socket::Done)
								{
									std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
										<< "\t| udp socket send error"
										<< std::endl;
								}
							}
							// Update last position of each paddle
							else
							{																
								c.lastPosition = Vec2(msg.x, msg.y);
								//c.lastMsgTimestamp = msg.timestamp;

								// Update paddle positions with last known (required for collision detection) and add to message list for use in prediction
								if (c.paddle == 1)
								{
									if (msg.timestamp > newestPaddleOnePosTimestamp)
									{
										newestPaddleOnePosTimestamp = msg.timestamp;
										msg.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0); // change timestamp to this server's time
										c.lastMsgTimestamp = msg.timestamp;

										paddleOne.AddMessage(msg);

										if (logDt > logRate)
										{
											std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
												<< "\t| Updating position of paddle one based on message and interpolation: y=" << interpolatedPosition
												<< std::endl;
										}

										paddleOne.position.y = c.lastPosition.y;
									}
								}
								else
								{
									if (msg.timestamp > newestPaddleTwoPosTimestamp)
									{
										newestPaddleTwoPosTimestamp = msg.timestamp;
										msg.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0); // change timestamp to this server's time
										c.lastMsgTimestamp = msg.timestamp;

										paddleTwo.AddMessage(msg);

										if (logDt > logRate)
										{
											std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
												<< "\t| Updating position of paddle two based on message and interpolation: y=" << interpolatedPosition
												<< std::endl;
										}

										paddleTwo.position.y = c.lastPosition.y;
									}
								}
							}
						}
					}
				}

				if (logDt > logRate)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Updating the ball position"
						<< std::endl;
				}

				ball.Update(dt); // Update the ball position based on frame time and velocity

				// Send ball position to clients
				sendEndTicks = SDL_GetTicks();
				sendDt = (sendEndTicks - sendStartTicks);
				if (sendDt > sendRate)
				{
					for (Client& c : clients)
					{
						ballPacket.clear();
						ballMsg.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0);
						ballMsg.port = listenPort;
						ballMsg.x = ball.position.x;
						ballMsg.y = ball.position.y;
						ballMsg.ball = true;
						ballPacket << ballMsg;

						if (logDt > logRate)
						{
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| Sending ball position: Timestamp=" << ballMsg.timestamp
								<< "; Port=" << ballMsg.port
								<< "; x=" << ballMsg.x << "; y=" << ballMsg.y
								<< "; ball=" << ballMsg.ball
								<< std::endl;
						}

						if (socket.send(ballPacket, (*c.tcpSocket).getRemoteAddress(), c.portBallPos) != sf::Socket::Done)
						{
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| udp socket send error"
								<< std::endl;
						}						
					}
					
					// Reset send rate timer
					sendStartTicks = SDL_GetTicks();
				}				

				// Collision checking
				contact = {};

				playerOnePrevScore = playerOneScore;
				playerTwoPrevScore = playerTwoScore;

				if (contact = CheckPaddleCollision(ball, paddleOne); contact.type != Ball::CollisionType::None)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Paddle one collision detected"
						<< std::endl;
					
					ball.CollideWithPaddle(contact);
				}
				else if (contact = CheckPaddleCollision(ball, paddleTwo); contact.type != Ball::CollisionType::None)
				{
					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| Paddle two collision detected"
						<< std::endl;
					
					ball.CollideWithPaddle(contact);
				}
				else if (contact = CheckWallCollision(ball);
					contact.type != Ball::CollisionType::None)
				{
					ball.CollideWithWall(contact);

					if (contact.type == Ball::CollisionType::Left)
					{
						++playerTwoScore;
						
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| Left wall collision detected"
							<< std::endl;
					}
					else if (contact.type == Ball::CollisionType::Right)
					{
						++playerOneScore;

						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| Right wall collision detected"
							<< std::endl;
					}
				}

				// If scores have changed, send to clients
				scores.timestamp = static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0);
				scores.playerOneScore = playerOneScore;
				scores.playerTwoScore = playerTwoScore;

				if (playerOnePrevScore != playerOneScore || playerTwoPrevScore != playerTwoScore)
				{										
					for (Client& c : clients)
					{
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| Sending scores to: "
							<< (*c.tcpSocket).getRemoteAddress() << " at " << "port " << (*c.tcpSocket).getRemotePort()
							<< "; PlayerOne=" << scores.playerOneScore << "; PlayerTwo=" << scores.playerTwoScore
							<< std::endl;

						packet.clear();
						header = 1; // header 1 = score update
						packet << header << scores;
						if ((*c.tcpSocket).send(packet) != sf::Socket::Done)
						{
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| tcp socket send error"
								<< std::endl;
						}
					}

					if (playerOneScore >= winningScore || playerTwoScore >= winningScore) // If either player has reached 11 points, determine winner
					{
						if (std::abs(playerOneScore - playerTwoScore) >= 2) // Player needs to win by at least two points
						{
							if (playerOneScore > playerTwoScore)
							{
								winner = 1;
							}
							else
							{
								winner = 2;
							}
						}
					}
				}

				if (!winner)
				{
					// Client disconnection handling

					for (Client& c : clients)
					{
						if (selector.wait(sf::microseconds(1)))
						{
							if (selector.isReady(*c.tcpSocket))
							{
								packet.clear();
								if ((*c.tcpSocket).receive(packet) == sf::Socket::Disconnected) // client has disconnected
								{
									std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
										<< "\t| Client disconnected: "
										<< (*c.tcpSocket).getRemoteAddress() << " at " << "port " << (*c.tcpSocket).getRemotePort()
										<< std::endl;

									// Set client to non-ready and remove its tcp socket from selector
									c.ready = false;
									selector.remove(*c.tcpSocket);
									clientDisconnected = true;
								}
							}
						}
						// If client disconnect status has not been received, check if it hasn't sent a message in 5 seconds or more
						else if (c.lastMsgTimestamp + 5.0 <= static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0))
						{
							packet.clear();
							if (c.lastMsgTimestamp != 0)
							{
								std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
									<< "\t| Previously active client has last timestamp that is at least 5 seconds old: "
									<< (*c.tcpSocket).getRemoteAddress() << " at " << "port " << (*c.tcpSocket).getRemotePort()
									<< std::endl;

								// Set client to not ready, remove its tcp socket from selector, and disconnect it
								c.ready = false;
								selector.remove(*c.tcpSocket);
								clientDisconnected = true;
								(*c.tcpSocket).disconnect();

							}
						}
					}

					// If a client has disconnected, notify other client and reset the game
					if (clientDisconnected)
					{
						for (Client& c : clients)
						{
							// If current client is still connected
							if (c.ready)
							{
								// Search client list for other client
								for (Client& c1 : clients)
								{
									// Other client is one whose port is different
									if ((*c.tcpSocket).getRemotePort() != (*c1.tcpSocket).getRemotePort())
									{
										// If other client is disconnected, send message to connected client
										if (!c1.ready)
										{
											std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
												<< "\t| Sending opponent disconnected message to: "
												<< (*c.tcpSocket).getRemoteAddress() << " at " << "port " << (*c.tcpSocket).getRemotePort()
												<< std::endl;

											packet.clear();
											header = 0;
											packet << header << "opponent disconnected";
											if ((*c.tcpSocket).send(packet) != sf::Socket::Done)
											{
												std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
													<< "\t| tcp socket send error"
													<< std::endl;
											}
										}
									}
								}

								std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
									<< "\t| Disconnecting still connected client: "
									<< (*c.tcpSocket).getRemoteAddress() << " at " << "port " << (*c.tcpSocket).getRemotePort()
									<< std::endl;

								// Remove from selector and disconnect socket of still connected client
								selector.remove(*c.tcpSocket);
								(*c.tcpSocket).disconnect();
							}
						}

						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| Resetting the game due to client disconnection"
							<< std::endl;

						// Reset game	
						clients.clear();
						playerOneScore = 0;
						playerTwoScore = 0;
						playersReady = 0;
						assignedPaddle = 1;
						paddleOne.paddleMessages.clear();
						paddleTwo.paddlePredictions.clear();
						paddleOne.paddleMessages.clear();
						paddleTwo.paddlePredictions.clear();
						paddleOne.position = Vec2(50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f));
						paddleTwo.position = Vec2(WINDOW_WIDTH - 50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f));
						ball.position = Vec2((WINDOW_WIDTH / 2.0f) - (BALL_WIDTH / 2.0f), (WINDOW_HEIGHT / 2.0f) - (BALL_WIDTH / 2.0f));
						ball.velocity = Vec2(BALL_SPEED, 0.0f);
						clientDisconnected = false;
						gameStarted = false;
						sendStartTicks = SDL_GetTicks();
						logStartTicks = SDL_GetTicks();
					}
				}
				else
				{					
					// Send number of winning paddle to clients
					for (Client& c : clients)
					{
						std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
							<< "\t| Sending winner to: "
							<< (*c.tcpSocket).getRemoteAddress() << " at " << "port " << (*c.tcpSocket).getRemotePort()
							<< "; Winner is player " << winner
							<< std::endl;

						packet.clear();
						header = 2; // header 2 = winner message
						packet << header << winner;
						if ((*c.tcpSocket).send(packet) != sf::Socket::Done)
						{
							std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
								<< "\t| tcp socket send error"
								<< std::endl;
						}
					}

					std::cout << static_cast<double>((SDL_GetTicks() - globalTime) / 1000.0)
						<< "\t| We have a winner. Resetting the game"
						<< std::endl;

					// Reset game	
					for (Client& c : clients)
					{
						selector.remove(*c.tcpSocket);
						(*c.tcpSocket).disconnect();
					}
					clients.clear();
					playerOneScore = 0;
					playerTwoScore = 0;
					playersReady = 0;
					assignedPaddle = 1;
					winner = 0;
					paddleOne.paddleMessages.clear();
					paddleTwo.paddlePredictions.clear();
					paddleOne.paddleMessages.clear();
					paddleTwo.paddlePredictions.clear();
					paddleOne.position = Vec2(50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f));
					paddleTwo.position = Vec2(WINDOW_WIDTH - 50.0f, (WINDOW_HEIGHT / 2.0f) - (PADDLE_HEIGHT / 2.0f));
					ball.position = Vec2((WINDOW_WIDTH / 2.0f) - (BALL_WIDTH / 2.0f), (WINDOW_HEIGHT / 2.0f) - (BALL_WIDTH / 2.0f));
					ball.velocity = Vec2(BALL_SPEED, 0.0f);
					gameStarted = false;
					sendStartTicks = SDL_GetTicks();
					logStartTicks = SDL_GetTicks();
				}

				// Reset the log printing timer
				if (logDt > logRate)
				{
					logStartTicks = SDL_GetTicks();
				}

				endTicks = SDL_GetTicks();
				dt = (endTicks - startTicks); //dt as fraction of a second				
			}
		}
	}

	// Cleanup
	SDL_Quit();

	return 0;
}