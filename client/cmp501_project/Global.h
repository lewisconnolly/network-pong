#pragma once
#include "Vec2.h"

const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

const float PADDLE_SPEED = 0.75f;
const float BALL_SPEED = 0.5f;

const int PADDLE_WIDTH = 15;
const int PADDLE_HEIGHT = 90;

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

/* Line intersection functions taken from https://www.geeksforgeeks.org/check-if-two-given-line-segments-intersect/ */

// Given three collinear points p, q, r, the function checks if 
// point q lies on line segment 'pr' 
inline bool onSegment(Vec2 p, Vec2 q, Vec2 r)
{
    if (q.x <= std::max(p.x, r.x) && q.x >= std::min(p.x, r.x) &&
        q.y <= std::max(p.y, r.y) && q.y >= std::min(p.y, r.y))
        return true;

    return false;
}

// To find orientation of ordered triplet (p, q, r). 
// The function returns following values 
// 0 --> p, q and r are collinear 
// 1 --> Clockwise 
// 2 --> Counterclockwise 
inline int orientation(Vec2 p, Vec2 q, Vec2 r)
{
    // See https://www.geeksforgeeks.org/orientation-3-ordered-points/ 
    // for details of below formula. 
    float val = (q.y - p.y) * (r.x - q.x) -
        (q.x - p.x) * (r.y - q.y);

    if (val == 0) return 0;  // collinear 

    return (val > 0) ? 1 : 2; // clock or counterclock wise 
}

// The main function that returns true if line segment 'p1q1' 
// and 'p2q2' intersect. 
inline bool doIntersect(Vec2 p1, Vec2 q1, Vec2 p2, Vec2 q2)
{
    // Find the four orientations needed for general and 
    // special cases 
    int o1 = orientation(p1, q1, p2);
    int o2 = orientation(p1, q1, q2);
    int o3 = orientation(p2, q2, p1);
    int o4 = orientation(p2, q2, q1);

    // General case 
    if (o1 != o2 && o3 != o4)
        return true;

    // Special Cases 
    // p1, q1 and p2 are collinear and p2 lies on segment p1q1 
    if (o1 == 0 && onSegment(p1, p2, q1)) return true;

    // p1, q1 and q2 are collinear and q2 lies on segment p1q1 
    if (o2 == 0 && onSegment(p1, q2, q1)) return true;

    // p2, q2 and p1 are collinear and p1 lies on segment p2q2 
    if (o3 == 0 && onSegment(p2, p1, q2)) return true;

    // p2, q2 and q1 are collinear and q1 lies on segment p2q2 
    if (o4 == 0 && onSegment(p2, q1, q2)) return true;

    return false; // Doesn't fall in any of the above cases 
}