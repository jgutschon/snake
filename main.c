#include <cmsis_os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <lpc17xx.h>
#include "random.h"
#include "lfsr113.h"
#include "GLCD.h"
#include "uart.h"
#include "graphics.h"


osMutexId_t snake;
//bool to track state of game
bool stopped;

const uint16_t width = 320;
const uint16_t height = 240;
const int8_t size = 20;

//coords of apple
uint16_t appleX = 0;
uint16_t appleY = 0;

uint8_t score = 0;

enum dirs{stop, left,	up,	right, down};
enum dirs dir;

//define struct to represent single node of snake
typedef struct Node{
	int32_t x;
	int32_t y;
	struct Node *next;
} snakeNode_t;

//pointers to head and tail
snakeNode_t *head = NULL;
snakeNode_t *tail = NULL;
int16_t prevTailX;
int16_t prevTailY;


//Helper functions
void placeApple() {	
	//rechoose coords for apple if in snake
	bool goodLocation = false;
	snakeNode_t *curr = tail;
	while(!goodLocation) {
		goodLocation = true;
		appleX = (rand()%(width/size))*size;
		appleY = (rand()%(height/size))*size;
		
		while(curr != NULL) {
			if (curr->x == appleX && curr->y == appleY) {
				goodLocation = false;
				curr = tail;
				break;
			}
			else
				curr = curr->next;
		}
	}
}

//reset position of snake and reset display
void reset() {
	osMutexAcquire(snake, osWaitForever);
	GLCD_Clear(Black);
	score = 0;
	LPC_GPIO2->FIOCLR |= 0xFF;
	LPC_GPIO1->FIOCLR |= 0xFFFFFFFF;
	
	//initialize linked list and starting position/direction
	head->x = width/2 - 2*size;
	head->y = height/2;
	tail->x = head->x-size;
	tail->y = head->y;
	head->next = NULL;
	tail->next = head;
	osMutexRelease(snake);
	
	dir = right;
	placeApple();
}

//draws snake before starting
void start() {
	//initialize head and tail of snake
	head = malloc(sizeof(snakeNode_t));
	tail = malloc(sizeof(snakeNode_t));
	
	reset();
	
	GLCD_DisplayString(1, 7, 1, (unsigned char *)"SNAKE");
	GLCD_DisplayString(3, 0, 1, (unsigned char *)"Push button to start");
	
	snakeNode_t *curr = tail;
	while(curr != NULL) {
		GLCD_Bargraph(curr->x, curr->y, size, size, 1024);
		curr = curr->next;
	}
	
	//wait for button
	if ((LPC_GPIO2->FIOPIN & (0x01 << 10)) != 0)
		while((LPC_GPIO2->FIOPIN & (0x01 << 10)) != 0);
	stopped = false;
	
	osMutexAcquire(snake, osWaitForever);
	GLCD_Clear(Black);
	osMutexRelease(snake);
}

//detects if head hits the snake
bool hitSelf() {
	snakeNode_t *curr = tail;
	while(curr != head) {
		if (head->x == curr->x && head->y == curr->y)
			return true;
		curr = curr->next;
	}
	return false;
}

void turnOn(uint8_t num) {
	LPC_GPIO2->FIOCLR |= 0xFF;
	LPC_GPIO1->FIOCLR |= 0xFFFFFFFF;
	
	if (num & 0x01)
		LPC_GPIO2->FIOSET |= 0x01 << 6;
	if (num & 0x02)
		LPC_GPIO2->FIOSET |= 0x01 << 5;
	if (num & 0x04)
		LPC_GPIO2->FIOSET |= 0x01 << 4;
	if (num & 0x08)
		LPC_GPIO2->FIOSET |= 0x01 << 3;
	if (num & 0x10)
		LPC_GPIO2->FIOSET |= 0x01 << 2;
	if (num & 0x020)
		LPC_GPIO1->FIOSET |= 0x01 << 31;
	if (num & 0x040)
		LPC_GPIO1->FIOSET |= 0x01 << 29;
	if (num & 0x80)
		LPC_GPIO1->FIOSET |= 0x01 << 28;
}

void gameOver() {
	stopped = true;
	osMutexAcquire(snake, osWaitForever);
	GLCD_Clear(Black);
	
	uint8_t finalScore = score;
	
	//display score
	char scoreStr[] = "";
	sprintf(scoreStr, "Score: %d", finalScore);
	
	GLCD_DisplayString(1, 5, 1, (unsigned char *)"GAME OVER");
	GLCD_DisplayString(3, 2, 1, (unsigned char *)"Push to restart");
	GLCD_DisplayString(4, 5, 1, (unsigned char *)scoreStr);
	
	//wait for button to restart
	if ((LPC_GPIO2->FIOPIN & (0x01 << 10)) != 0)
		while((LPC_GPIO2->FIOPIN & (0x01 << 10)) != 0);
	stopped = true;
	
	osMutexRelease(snake);
}


//Threaded functions
//start/stop game with push button
void pushButton(void *arg) {
	while(1) {
		if((LPC_GPIO2->FIOPIN & (0x01 << 10)) == 0) {
			while((LPC_GPIO2->FIOPIN & (0x01 << 10)) == 0) {}
			stopped = !stopped;
		}
		osThreadYield();
	}
}

//poll for input from joystick to set direction
void readJoy(void *arg) {
	while(1) {
		while(!stopped) {
			//cant switch back 180 degrees to opposite direction
			if((LPC_GPIO1->FIOPIN & (0x01 << 23)) == 0 && dir != down)
				dir = up;
			else if((LPC_GPIO1->FIOPIN & (0x01 << 25)) == 0 && dir != up)
				dir = down;
			else if((LPC_GPIO1->FIOPIN & (0x01 << 26)) == 0 && dir != right)
				dir = left;
			else if((LPC_GPIO1->FIOPIN & (0x01 << 24)) == 0 && dir != left)
				dir = right;
		}
		osThreadYield();
	}
}

void moveSnek(void *arg) {
	int16_t dx = 0;
	int16_t dy = 0;
	
	while(1) {
		while(!stopped) {
			osDelay(osKernelGetTickFreq()/12);
			
			//if snake head hits wall or hits self
			if (hitSelf() || (head->x <= 0 && dir == left) || (head->x >= width-size && dir == right) || (head->y <= 0 && dir == up) || (head->y >= height-size && dir == down)) {
				gameOver();
				reset();
			}
			else {
				//save previous x,y of tail node so we dont need double linked list
				osMutexAcquire(snake, osWaitForever);
				prevTailX = tail->x;
				prevTailY = tail->y;
				//iterate through list and change coords to next coords
				snakeNode_t *curr = tail;
				while(curr != head) {
					curr->x = curr->next->x;
					curr->y = curr->next->y;
					curr = curr->next;
				}
				
				//check direction
				if (dir == up) {
					dx = 0;
					dy = -size;
				} else if (dir == down) {
					dx = 0;
					dy = size;
				} else if (dir == right) {
					dx = size;
					dy = 0;
				} else if (dir == left) {
					dx = -size;
					dy = 0;
				}
				//move head according to direction
				head->x += dx;
				head->y += dy;
				
				//increase length of snake
				if(head->x == appleX && head->y == appleY) {
					snakeNode_t *newNode = malloc(sizeof(struct Node));
					newNode->next = tail;
					newNode->x = prevTailX;
					newNode->y = prevTailY;
					tail = newNode;
					
					placeApple();
					score++;
					turnOn(score);
				}
			}
			osMutexRelease(snake);
		}
		osThreadYield();
	}
}

void display(void *arg) {
	while(1) {
		osMutexAcquire(snake, osWaitForever);
		//iterate through snake and display each node
		snakeNode_t *curr = tail;
		while(curr != NULL) {
			GLCD_Bargraph(curr->x, curr->y, size, size, 1024);
			curr = curr->next;
		}
		GLCD_Bargraph(prevTailX, prevTailY, size, size, 0);
		drawApple(appleX, appleY);
		osMutexRelease(snake);
		
		osThreadYield();
	}
}

int main(void) {
	osKernelInitialize();
	
	//mutex used to protect snake linked list data
	snake = osMutexNew(NULL);
	
	LPC_GPIO2->FIODIR |= 0x0000007C;
	LPC_GPIO1->FIODIR |= 0xB0000000;
		
	GLCD_Init();
	GLCD_SetTextColor(White);
	GLCD_SetBackColor(Black);
	
	start();
	
	printf("");
	osThreadNew(readJoy, NULL, NULL);
	osThreadNew(pushButton, NULL, NULL);
	osThreadNew(moveSnek, NULL, NULL);
	osThreadNew(display, NULL, NULL);
	osKernelStart();
	while(1){}
}
