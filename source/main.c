#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>

#define WIDTH 320
#define HEIGHT 240

#define MENU_X 102
#define MENU_Y 60
#define MENU_WIDTH 110
#define MENU_HEIGHT 108

u64 TICKS_PER_SEC = 268123480;
u64 TICKS_PER_MS = 268123;

int maxDepth = 0;
u32 colorCode(int color);
int getMooreNeighborX(int x, int p);
int getMooreNeighborY(int y, int p);
int getNeumannNeighborX(int x, int p);
int getNeumannNeighborY(int y, int p);
bool randomize = false;
bool toggle = false;
bool clear = false;
int makeColor = 1;
int lastBlock = 0;
int lastNeighbor = 0;
bool erasing = false;				// if user is killing cells
bool rules[9][2][2];				// totalistic 2 dimensional cellular automota rules
									// 0-8 total neighbors
									// 2 states for what I am:				0 = dead, 1 = alive
									// 2 rule sets: red or blue
									// returns state I should become: 		0 = dead, 1 = alive
									// note: if there are more cells of other color than mine, then
									// the rules change to be that of the other color's.
int conversion[9][2];				// up to 8 neighbors for each of the 2 colors resulting in 1 of 3 states:
									//			0 = dead, 1 = red, 2 = blue

int generations[2] = {2,2};				// default star wars rules: 345/2/4

u8 * frameBuf;						// the actual hardware's graphical buffer
u8 mybuffer[4 * 240 * 320];			// the buffer to be drawn
u8 oldbuffer[4 * 240 * 320];		// the saved buffer
bool CATASTROPHIC_FAILURE = false;	// fatal error happened
u64 lastFrame = 0;					// time since last frame was rendered
u64 lastTouched = 0; 				// last touched the touch screen
int sanitizeX(int x) {
	if (x < 0) {
		if (abs(x) < 320) return 320 - abs(x);
		return 320;
	} 
	return x % 320;
}
int sanitizeY(int y) {
	if (y < 0) {
		if (abs(y) < 240) return 240 - abs(y);
		return 240;
	}
	return abs(y) % 240;
}
void makePixel2(int x, int y, u32 color) {
	if (x > 320) x = 0;
	else if(x < 0) x = 319;
	if (y > 240) y = 0;
	else if (y < 0) y = 239;
	u32 offset = ((x * 240) - y + 239) * 3;
	oldbuffer[offset] = (u8)color;
	oldbuffer[offset + 1] = (u8)(color >> 8);
	oldbuffer[offset + 2] = (u8)(color >> 16);
}
void makePixel(int x, int y, u32 color) {
	if (x > 320) x = 0;
	else if(x < 0) x = 319;
	if (y > 240) y = 0;
	else if (y < 0) y = 239;
	u32 offset = ((x * 240) - y + 239) * 3;
	mybuffer[offset] = (u8)color;
	mybuffer[offset + 1] = (u8)(color >> 8);
	mybuffer[offset + 2] = (u8)(color >> 16);
}
int getPrimaryColor(u32 color) {
	if (color & 0x00ff0000) return 1;		// is red
	if (color & 0x000000ff) return 2;		// is blue
	return 0;								// is dead
}
void writeColor2(int x, int y, u32 color) {
	makePixel2(x, sanitizeY(y + 1),color);
	makePixel2(sanitizeX(x + 1),sanitizeY(y + 1),color);
	makePixel2(sanitizeX(x + 1),y,color);
	makePixel2(x,y,color);
}
void writeColor(int x, int y, u32 color) {
	makePixel(x, sanitizeY(y + 1),color);
	makePixel(sanitizeX(x + 1),sanitizeY(y + 1),color);
	makePixel(sanitizeX(x + 1),y,color);
	makePixel(x,y,color);
}
static u32 darken(u32 color, float shade_factor) {
	u32 r = color & (u32)0x00ff0000;
	r >>= 16;
	u32 g = color & (u32)0x0000ff00;
	g >>= 8;
	u32 b = color & (u32)0x000000ff;
	return 0xff000000 | (u32)(r * (1 - shade_factor)) << 16 | (u32)(g * (1 - shade_factor)) << 8 | (u32)(b * (1 - shade_factor));
}
static float getShade(u32 color) {
	int primary = getPrimaryColor(color);
	u32 num;
	if (primary == 1) {
		num = color & (u32)0x00ff0000;
		num >>= 16;
	}
	else {
		num = color & (u32)0x000000ff;
	}
	return (float)num / 255.0f;
}
static u32 lighten(u32 color, float tint_factor) {
	u32 r = color & (u32)0x00ff0000;
	r >>= 16;
	u32 g = color & (u32)0x0000ff00;
	g >>= 8;
	u32 b = color & (u32)0x000000ff;
	return 0xff000000 | (u32)(r + (255 - r) * tint_factor) << 16 | (u32)(g + (255 - g) * tint_factor) << 8 | (u32)(b + (255 - b) * tint_factor);
}
u32 getColor(u8 buf[], int x, int y) { // Thank you WolfVak for the code!
	if (x > 320) x = 0;
	else if(x < 0) x = 319;
	if (y > 240) y = 0;
	else if (y < 0) y = 239;
	u32 offset = ((x * 240) - y + 239) * 3;
	return (u32) (buf[offset] | buf[offset + 1] << 8 | buf[offset + 2] << 16);
}
u32 newColor(u32 color, int dominantColor) {
	int primary = getPrimaryColor(color);
	if (!primary) return color;
	if (dominantColor != primary) {
		//return colorCode(dominantColor);
		if (dominantColor == 2) {
			color = ((color & 0x00ff0000) >> 16) | 0xff000000;
		}
		else {
			color = ((color & 0x000000ff) << 16) | 0xff000000;
		}
	}
	if (generations[primary % 2] <= 1) return color;
	if (getShade(darken(color,(float)(1.0f / (float)(generations[primary % 2] - 1)))) <= pow((float)(1.0f - (float)(1.0f / (float)(generations[primary % 2] - 1))), generations[primary % 2] - 1)) return 0xff000000;	// dead
	return darken(color, (float)(1.0f / (float)(generations[primary % 2] - 1)));
}
u32 changeColor(u32 color, int dominantColor) {
	int primary = getPrimaryColor(color);
	if (!primary) return color;
	if (dominantColor != primary) {
		if (primary == 1) {
			color = ((color & 0x00ff0000) >> 16) | 0xff000000;
		}
		else {
			color = ((color & 0x000000ff) << 16) | 0xff000000;
		}
	}
	return color;
}
u32 colorCode(int color) {
	if (color == 1) return 0xffff0000;
	return 0xff0000ff;
}
typedef struct scene_s {
    void (*init)(); //the initialization function of the scene
    void (*update)(); //function pointer to update function
    void (*draw)(); //function pointer to draw function
    void (*finish)(); //function pointer to clean up function
    bool initialized;
    struct scene_s * next; //next Scene in the stack
}Scene;
 
 
Scene *scenes = NULL; //the top of the stack
Scene *oldScene = NULL;
int numScenes = 0;
bool stepping = false;

void pushScene(void (*initialization)(), void (*update)(), void (*draw)(), void (*finish)());
void popScene();
void applyRules();
void displayMessage();
void displayMessage_finish();
void menu_init();
void menu_update();
void menu_draw();
void menu_finish();
void pushScene(void (*initialization)(), void (*update)(), void (*draw)(), void (*finish)()) {
    Scene *newScene = malloc(sizeof(Scene)); //allocate memory for a new scene in the SceneStack
    if (newScene == NULL) {
       	printf("Error: ran out of memory.");
       	CATASTROPHIC_FAILURE = true;
        return;
    }
    newScene->initialized = false;
    newScene->init = initialization; //initialization function for the scene
    newScene->update = update; //input/update function for the scene
    newScene->draw = draw; //draw function for the scene
    newScene->finish = finish; //clean up function once the scene is popped
    if (scenes == NULL) newScene->next = NULL;
    else newScene->next = scenes; //next scene in the stack
 
    scenes = newScene; //push this new scene to the top of the stack
    numScenes++;
}
void clearScenes() {
	if (scenes == NULL) return;
	Scene *c = scenes;
	Scene *temp = NULL;
	while (c != NULL) {
		temp = c;
		c = c->next;
		free(temp);
	}
	scenes = NULL;
	numScenes = 0;
}
void popScene() {
	if (scenes == NULL) return;
    Scene *temp = scenes;
    if (scenes->finish != NULL) scenes->finish(); //if there is a finish function, execute it
    if (scenes->next != NULL) scenes = scenes->next; //set the top of the stack to be the next scene
    else scenes = NULL;
    temp->next = NULL;
    free(temp); //free the current scene
    temp = NULL;
    numScenes--;
}
void randomize_screen() {
	bool flag[2] = {0,0};
	for (int i = 0; i < 9; i++) {
		for (int j = 0; j < 2; j++) {
			if (rules[i][j][0]) flag[1] = true;
			if (rules[i][j][1]) flag[0] = true;
		}
	}
	if (flag[0] && flag[1]) { //both rule sets have on states.
		for (int x = 0; x < WIDTH; x += 2) {
			for (int y = 0; y < HEIGHT; y += 2) {
				if (rand() % 2) {
					if (rand() % 2) writeColor(x,y,0xffff0000);	// red
					else writeColor(x,y,0xff0000ff);			// blue
				}
				else writeColor(x,y,0xff000000);
			}
		}
	} else {
		u32 color = colorCode(1);
		if (flag[1]) color = colorCode(2);
		for (int x = 0; x < WIDTH; x += 2) {
			for (int y = 0; y < HEIGHT; y += 2) {
				if (rand() % 2) {
					writeColor(x,y,color);	// color we have rules for
				}
				else writeColor(x,y,0xff000000);
			}
		}
	}
}
void convertColor(int x, int y, int newColor) {
	maxDepth++;
	if (maxDepth > 800) return;
	if (!getColor(mybuffer,x,y)) return;
	writeColor(x,y,changeColor(getColor(mybuffer,x,y),newColor));	// change current cell's color
	writeColor2(x,y,changeColor(getColor(mybuffer,x,y),newColor));
	int i = rand() % 8;
	int end = i;
	for (i = (i + 1) % 8; i != end; i = (i + 1) % 8) {
		u32 neighbor = getColor(mybuffer,getMooreNeighborX(x,i),getMooreNeighborY(y,i));
		int primary = getPrimaryColor(neighbor);
		if (primary == 0 || primary == newColor) continue;				// we don't need to change this one's color.
		convertColor(getMooreNeighborX(x,i),getMooreNeighborY(y,i),newColor);					// change its neighbors too!
		//else writeColor(getMooreNeighborX(x,i),getMooreNeighborY(y,i),changeColor(neighbor,newColor));	// change only the neighbor. not a reproducing cell.
	}
}
void fill_screen_init() {
    if (randomize) {
    	randomize_screen();
    	randomize = false;
    }
    if (clear) {
    	clear = false;
    	memset(mybuffer,0,sizeof(mybuffer));
    	memset(oldbuffer,0,sizeof(oldbuffer));
    }
}
void fill_screen_update() {
	hidScanInput();
	u32 kDown = hidKeysDown();
	u32 kUp = hidKeysUp();
	u32 kHeld = hidKeysHeld();
	touchPosition touch;
	hidTouchRead(&touch);
	if (stepping && svcGetSystemTick() - lastFrame > TICKS_PER_MS * 30) {
		lastFrame = svcGetSystemTick();
		applyRules();
	}
	if (kDown & KEY_L || kHeld & KEY_L) {
		erasing = true;
	}
	if (kUp & KEY_L) erasing = false;
	if (kDown & KEY_R) randomize_screen();
	if (kDown & KEY_SELECT) {
		memset(mybuffer,0,sizeof(mybuffer));
    	memset(oldbuffer,0,sizeof(oldbuffer));
	}
	if (touch.px && touch.py) {
		int x = touch.px;
		int y = touch.py;
		if (x % 2) x = sanitizeX(x - 1);
		if (y % 2) y = sanitizeY(y - 1);
		if (erasing) writeColor(x,y,0xff000000);
		else if (toggle) {
			maxDepth = 0;
			convertColor(x,y,makeColor);
		}
		else writeColor(x,y,colorCode(makeColor));
	}
	if (kDown & KEY_A) {
		if (stepping) stepping = false;
		else stepping = true;
	}
	if (kDown & KEY_B) {
		if (toggle) toggle = false;
		else toggle = true;
		return;
	}
	if (kDown & KEY_Y) {
		pushScene(menu_init,menu_update,menu_draw,menu_finish);
		return;
	}
	if (kDown & KEY_START) {
		popScene();
		return;
	}
	if (kDown & KEY_DRIGHT || kDown & KEY_CPAD_RIGHT) {
		makeColor += 1;
		if (makeColor > 2) makeColor = 1;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_DLEFT || kDown & KEY_CPAD_LEFT) {
		makeColor -= 1;
		if (makeColor < 1) makeColor = 2;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_DUP || kDown & KEY_CPAD_UP) {
		generations[makeColor % 2]++;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_DDOWN || kDown & KEY_CPAD_DOWN) {
		generations[makeColor % 2]--;
		if (generations[makeColor % 2] < 1) generations[makeColor % 2] = 1;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
}
void fill_screen_draw() {
	frameBuf = gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,0,0);
	memcpy(frameBuf, mybuffer, 4 * 240 * 320);
	gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}
void displayMessage() {
	hidScanInput();
	u32 kDown = hidKeysDown();
	if (kDown & KEY_START) {
		popScene();
		return;
	}
}
void displayMessage_draw() {
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
}
void displayMessage_finish() {
}
int getLargeNeighborX(int x, int p) {
	/*if (p < 0 || p >= 24) p = 0;
	if (p >= 12) p++;
	return x + (2 * (2 - (p % 5)));*/
	switch (p) {
		case 0:
		return sanitizeX(x + 4);
		break;
		case 1:
			return sanitizeX(x + 2);
			break;
		case 2:
			return sanitizeX(x + 0);
			break;
		case 3:
			return sanitizeX(x + -2);
			break;
		case 4:
			return sanitizeX(x + -4);
			break;
		case 5:
			return sanitizeX(x + 4);
			break;
		case 6:
			return sanitizeX(x + 2);
			break;
		case 7:
			return sanitizeX(x + 0);
			break;
		case 8:
			return sanitizeX(x + -2);
			break;
		case 9:
			return sanitizeX(x + -4);
			break;
		case 10:
			return sanitizeX(x + 4);
			break;
		case 11:
			return sanitizeX(x + 2);
			break;
		case 12:
			return sanitizeX(x + -2);
			break;
		case 13:
			return sanitizeX(x + -4);
			break;
		case 14:
			return sanitizeX(x + 4);
			break;
		case 15:
			return sanitizeX(x + 2);
			break;
		case 16:
		default:
			return sanitizeX(x + 0);
			break;
		case 17:
			return sanitizeX(x + -2);
			break;
		case 18:
			return sanitizeX(x + -4);
			break;
		case 19:
			return sanitizeX(x + 4);
			break;
		case 20:
			return sanitizeX(x + 2);
			break;
		case 21:
			return sanitizeX(x + 0);
			break;
		case 22:
			return sanitizeX(x + -2);
			break;
		case 23:
			return sanitizeX(x + -4);
			break;
	}
}
int getLargeNeighborY(int y, int p) {
	/*if (p < 0 || p >= 24) p = 0;
	if (p >= 12) p++;
    return y + (2 * (2 - (p / 5)));*/
    switch (p) {
    	case 0:
			return sanitizeY(y + 4);
			break;
		case 1:
			return sanitizeY(y + 4);
			break;
		case 2:
			return sanitizeY(y + 4);
			break;
		case 3:
			return sanitizeY(y + 4);
			break;
		case 4:
			return sanitizeY(y + 4);
			break;
		case 5:
			return sanitizeY(y + 2);
			break;
		case 6:
			return sanitizeY(y + 2);
			break;
		case 7:
			return sanitizeY(y + 2);
			break;
		case 8:
			return sanitizeY(y + 2);
			break;
		case 9:
			return sanitizeY(y + 2);
			break;
		case 10:
		default:
			return sanitizeY(y + 0);
			break;
		case 11:
			return sanitizeY(y + 0);
			break;
		case 12:
			return sanitizeY(y + 0);
			break;
		case 13:
			return sanitizeY(y + 0);
			break;
		case 14:
			return sanitizeY(y + -2);
			break;
		case 15:
			return sanitizeY(y + -2);
			break;
		case 16:
			return sanitizeY(y + -2);
			break;
		case 17:
			return sanitizeY(y + -2);
			break;
		case 18:
			return sanitizeY(y + -2);
			break;
		case 19:
			return sanitizeY(y + -4);
			break;
		case 20:
			return sanitizeY(y + -4);
			break;
		case 21:
			return sanitizeY(y + -4);
			break;
		case 22:
			return sanitizeY(y + -4);
			break;
		case 23:
			return sanitizeY(y + -4);
			break;
    }
}
int getNeumannNeighborX(int x, int p) {
	switch(p) {
		case 0:
			return sanitizeX(x - 2);
		case 3:
			return sanitizeX(x + 2);
		default:
			return sanitizeX(x);
	}
}
int getNeumannNeighborY(int y, int p) {
	switch (p) {
		case 1:
			return sanitizeY(y - 2);
		case 2:
			return sanitizeY(y + 2);
		default:
			return sanitizeY(y);
	}
}
int getMooreNeighborX(int x, int p) {
	switch(p) {
		case 0:
		case 3:
		case 5:
			return sanitizeX(x - 2);
		case 1:
		case 6:
			return sanitizeX(x);
		case 4:
		case 2:
		case 7:
			return sanitizeX(x + 2);
		default:
			return sanitizeX(x);
	}
}
int getMooreNeighborY(int y, int p) {
	switch(p) {
		case 0:
		case 1:
		case 2:
			return sanitizeY(y - 2);
		case 3:
		case 4:
			return sanitizeY(y);
		case 5:
		case 6:
		case 7:
			return sanitizeY(y + 2);
		default:
			return sanitizeY(y);
	}
}
void conversion_init() {

	lastBlock = 0;			// for last touch
	lastNeighbor = 0;		// for last touch

	for (int x = 0; x < MENU_WIDTH; x++) { // clear space for menu
		for (int y = 0; y < MENU_HEIGHT; y++) {
			writeColor(MENU_X + x,MENU_Y + y, 0xff000000);
		}
	}
	lastTouched = svcGetSystemTick();
}
void conversion_update() {
	hidScanInput();
	u32 kDown = hidKeysDown();
	touchPosition touch;
	hidTouchRead(&touch);
	if (touch.px && touch.py) {
		if (touch.px >= MENU_X && touch.px <= MENU_X + MENU_WIDTH && touch.py >= MENU_Y && touch.py <= MENU_Y + MENU_HEIGHT) {	//they touched in the menu box
			int x = touch.px - MENU_X;
			int y = touch.py - MENU_Y;
			int block = x / 11;
			int numNeighbors = y / 12;
			if (svcGetSystemTick() - lastTouched > TICKS_PER_MS * 30 && !(lastBlock == block && lastNeighbor == numNeighbors)) { //touched in time
				lastTouched = svcGetSystemTick();
				lastBlock = block;
				lastNeighbor = numNeighbors;
				if (block == 8) {
					if (conversion[8 - numNeighbors][1]) conversion[8 - numNeighbors][1] = false;
					else conversion[8 - numNeighbors][1] = true;
				} else if (block == 9) {
					if (conversion[numNeighbors][0]) conversion[numNeighbors][0] = false;
					else conversion[numNeighbors][0] = true;
				}
			}
		}
	} else {
		lastBlock = 0;
		lastNeighbor = 0;
	}
	if (kDown & KEY_R) randomize = true;
	if (kDown & KEY_SELECT) clear = true;
	if (kDown & KEY_B || kDown & KEY_Y) {
		popScene();		// back to previous scene
		return;
	}
	if (kDown & KEY_DRIGHT || kDown & KEY_CPAD_RIGHT) {
		makeColor += 1;
		if (makeColor > 2) makeColor = 1;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_DLEFT || kDown & KEY_CPAD_LEFT) {
		makeColor -= 1;
		if (makeColor < 1) makeColor = 2;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_DUP || kDown & KEY_CPAD_UP) {
		generations[makeColor % 2]++;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_DDOWN || kDown & KEY_CPAD_DOWN) {
		generations[makeColor % 2]--;
		if (generations[makeColor % 2] < 1) generations[makeColor % 2] = 1;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_START) {
		clearScenes();	// they want to exit the entire program
		return;
	}
}
void conversion_draw() {
	for (int x = MENU_X - 1; x < MENU_X + MENU_WIDTH + 1; x++) {
		for (int y = MENU_Y - 1; y < MENU_Y + MENU_HEIGHT + 1; y++) {
			makePixel(x,y,0xffffffff); 				//white pixel border.
		}
	}
	for (int w = 0; w < MENU_WIDTH; w += 11) { 		// 10 blocks of 11 pixels
		for (int h = 0; h < MENU_HEIGHT; h += 12) { // 9 blocks of 12 pixels from top to bottom
			int block = w / 11;
			int numNeighbors = h / 12; 				// how many neighbors alive in this row
			for (int x = 0; x < 11; x++) {
				for (int y = 0; y < 12; y++) {
					if (block < 8) {
						if (block < numNeighbors) makePixel(MENU_X + w + x,MENU_Y + h + y,colorCode(1)); 		// this block is red
						else makePixel(MENU_X + w + x,MENU_Y + h + y,colorCode(2));							// this block is blue
					} else {
						if (block == 8) { 																		// if I'm dead and I have this many living neighbors I should be:
							if (conversion[8 - numNeighbors][1]) makePixel(MENU_X + w + x,MENU_Y + h + y,colorCode(2)); 	// change to blue
							else makePixel(MENU_X + w + x,MENU_Y + h + y,colorCode(1));							// stay red
						} else {																				// if I'm alive and I have this many living neighbors I should be:
							if (conversion[numNeighbors][0]) makePixel(MENU_X + w + x,MENU_Y + h + y,colorCode(1));	// change to red
							else makePixel(MENU_X + w + x,MENU_Y + h + y,colorCode(2));							// stay blue
						}
					}									
				}
			}
		}
	}
	frameBuf = gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,0,0);
	memcpy(frameBuf, mybuffer, 4 * 240 * 320);
	gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}
void conversion_finish() {
	memcpy(mybuffer,oldbuffer,sizeof(mybuffer)); 	// go back to game's screen
}
void menu_init() {
	memcpy(oldbuffer,mybuffer,sizeof(oldbuffer)); // keep the game of life paused by backing it up

	lastBlock = 0;			// for last touch
	lastNeighbor = 0;		// for last touch

	for (int x = 0; x < MENU_WIDTH; x++) { // clear space for menu
		for (int y = 0; y < MENU_HEIGHT; y++) {
			writeColor(MENU_X + x,MENU_Y + y, 0xff000000);
		}
	}
	lastTouched = svcGetSystemTick();
}
void menu_update() {
	hidScanInput();
	u32 kDown = hidKeysDown();
	touchPosition touch;
	hidTouchRead(&touch);
	if (touch.px && touch.py) {
		if (touch.px >= MENU_X && touch.px <= MENU_X + MENU_WIDTH && touch.py >= MENU_Y && touch.py <= MENU_Y + MENU_HEIGHT) {	//they touched in the menu box
			int x = touch.px - MENU_X;
			int y = touch.py - MENU_Y;
			int block = x / 11;
			int numNeighbors = y / 12;
			if (svcGetSystemTick() - lastTouched > TICKS_PER_MS * 30 && !(lastBlock == block && lastNeighbor == numNeighbors)) { //touched in time
				lastTouched = svcGetSystemTick();
				lastBlock = block;
				lastNeighbor = numNeighbors;
				if (block == 8) {
					if (rules[numNeighbors][0][makeColor % 2]) rules[numNeighbors][0][makeColor % 2] = false;
					else rules[numNeighbors][0][makeColor % 2] = true;
				} else if (block == 9) {
					if (rules[numNeighbors][1][makeColor % 2]) rules[numNeighbors][1][makeColor % 2] = false;
					else rules[numNeighbors][1][makeColor % 2] = true;
				}
			}
		}
	} else {
		lastBlock = 0;
		lastNeighbor = 0;
	}
	if (kDown & KEY_R) randomize = true;
	if (kDown & KEY_SELECT) clear = true;
	if (kDown & KEY_B || kDown & KEY_Y) {
		popScene();		// back to previous scene
		return;
	}
	if (kDown & KEY_L) {
		popScene();	// remove this scene
		pushScene(conversion_init,conversion_update,conversion_draw,conversion_finish);
		return;
	}
	if (kDown & KEY_DRIGHT || kDown & KEY_CPAD_RIGHT) {
		makeColor += 1;
		if (makeColor > 2) makeColor = 1;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_DLEFT || kDown & KEY_CPAD_LEFT) {
		makeColor -= 1;
		if (makeColor < 1) makeColor = 2;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_DUP || kDown & KEY_CPAD_UP) {
		generations[makeColor % 2]++;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_DDOWN || kDown & KEY_CPAD_DOWN) {
		generations[makeColor % 2]--;
		if (generations[makeColor % 2] < 1) generations[makeColor % 2] = 1;
		printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);
	}
	if (kDown & KEY_START) {
		clearScenes();	// they want to exit the entire program
		return;
	}
}
void menu_draw() {
	for (int x = MENU_X - 1; x < MENU_X + MENU_WIDTH + 1; x++) {
		for (int y = MENU_Y - 1; y < MENU_Y + MENU_HEIGHT + 1; y++) {
			makePixel(x,y,0xffffffff); 				//white pixel border.
		}
	}
	u32 color = colorCode(makeColor);
	for (int w = 0; w < MENU_WIDTH; w += 11) { 		// 10 blocks of 11 pixels
		for (int h = 0; h < MENU_HEIGHT; h += 12) { // 9 blocks of 12 pixels from top to bottom
			int block = w / 11;
			int numNeighbors = h / 12; 				// how many neighbors alive in this row
			for (int x = 0; x < 11; x++) {
				for (int y = 0; y < 12; y++) {
					if (block < 8) {
						if (block < numNeighbors) makePixel(MENU_X + w + x,MENU_Y + h + y,color); 		// this block is red
						else makePixel(MENU_X + w + x,MENU_Y + h + y,0xff000000);							// this block is black
					} else {
						if (block == 8) { 																		// if I'm dead and I have this many living neighbors I should be:
							if (rules[numNeighbors][0][makeColor % 2]) makePixel(MENU_X + w + x,MENU_Y + h + y,color); 	// alive
							else makePixel(MENU_X + w + x,MENU_Y + h + y,0xff000000);							// dead
						} else {																				// if I'm alive and I have this many living neighbors I should be:
							if (rules[numNeighbors][1][makeColor % 2]) makePixel(MENU_X + w + x,MENU_Y + h + y,color);	// alive
							else makePixel(MENU_X + w + x,MENU_Y + h + y,0xff000000);							// dead
						}
					}									
				}
			}
		}
	}
	frameBuf = gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,0,0);
	memcpy(frameBuf, mybuffer, 4 * 240 * 320);
	gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}
void menu_finish() {
	memcpy(mybuffer,oldbuffer,sizeof(mybuffer)); 	// go back to game's screen
}
void applyRules() {
	memcpy(oldbuffer,mybuffer,sizeof(oldbuffer));
	for (int x = 0; x < WIDTH; x += 2) {
		for (int y = 0; y < HEIGHT; y += 2) {
			//for every x and y on the bottom screen...
			u32 color = getColor(oldbuffer,x,y);
			int dominantColor = getPrimaryColor(color);
			int primary = dominantColor;
			if (dominantColor == 0) dominantColor = 1;
			int livingColor[2][3];
			memset(livingColor,0,sizeof(livingColor));
			int livingNeighbors = 0;
			int me = getShade(getColor(oldbuffer,x,y)) != 0 ? 1 : 0;
			for (int n = 0; n < 8; n++) {
				//for every neighbor of that x and y...
				u32 neighborColor = getColor(oldbuffer,getMooreNeighborX(x,n),getMooreNeighborY(y,n));
				int primaryNeighbor = getPrimaryColor(neighborColor);
				if (primaryNeighbor) {
					livingNeighbors++;
					livingColor[primaryNeighbor % 2][1]++;		// von Neuman Neighborhood, manhatten distance = 2
					if (getMooreNeighborX(0,n) == 0 || getMooreNeighborY(0,n) == 0) livingColor[primaryNeighbor % 2][2]++;	// von Neumann Neighborhood, manhattan distance = 1
					if (getShade(neighborColor) >= 1.0f) {
						livingColor[primaryNeighbor % 2][0]++;	// living cell in von Neumann Neighborhood, manhatten distance = 2
					}
				}
			}

			if (me == 0) {
				if (livingColor[0][1] == livingColor[1][1]) {			// we are dead, so if we have equal numbered colors in neighbors, pick a ruleset at random
					//memset(livingColor,0,sizeof(livingColor));	// they are cancelling each other out.
					if (rand() % 2) dominantColor = 2;	// change color!
				} else if (livingColor[(dominantColor + 1) % 2][0] > livingColor[dominantColor % 2][0]) dominantColor = 2;	// the other color won!
			} else if (conversion[livingColor[(dominantColor + 1) % 2][1]][dominantColor % 2]) {	// if the conversion rules say to swap color
				if (dominantColor == 1) dominantColor = 2;									// then swap colors
				else dominantColor = 1;
				writeColor(x,y,changeColor(color,dominantColor));
				writeColor2(x,y,changeColor(color,dominantColor));	
				for (int i = 0; i < 4; i++) {
					u32 neighbor = getColor(oldbuffer,getNeumannNeighborX(x,i),getNeumannNeighborY(y,i));
					if (getPrimaryColor(neighbor) != dominantColor) {
						writeColor(getNeumannNeighborX(x,i),getNeumannNeighborY(y,i),changeColor(neighbor,dominantColor));
						writeColor2(getNeumannNeighborX(x,i),getNeumannNeighborY(y,i),changeColor(neighbor,dominantColor));						
					}
				}
				continue;
				/*maxDepth = 0;
				if (generations[dominantColor % 2] > 2) {
					writeColor(x,y,newColor(color,primary));
					writeColor2(x,y,newColor(color,primary));
					convertColor(x,y,dominantColor);
				} else {
					convertColor(x,y,dominantColor);
				}
				continue;*/
			}

			if (rules[livingColor[dominantColor % 2][0]][me][dominantColor % 2]) {
				if (me == 0) {
					if (livingColor[dominantColor % 2][0] > livingColor[(dominantColor + 1) % 2][0]) writeColor(x,y,colorCode(dominantColor));
				}
				else {
					if (getShade(color) == 1.0) writeColor(x,y,changeColor(color,dominantColor)); //we live
					else writeColor(x,y,newColor(color,dominantColor));	// we age
				}
			}
			else {
				if (generations[dominantColor % 2] > 1) writeColor(x,y,newColor(color,dominantColor)); //we age
				else writeColor(x,y,0xff000000);		// we die
			}
		}
	}
}
void sceneInit() {
	memset(mybuffer,0,sizeof(mybuffer));
    int x,y;
    for (x=0; x < WIDTH; x++) {
        for (y=0; y < HEIGHT; y++) {
            writeColor(x,y,0xff000000);
        }
    }
    frameBuf = gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,0,0);
    memcpy(oldbuffer, mybuffer, sizeof(oldbuffer));
}
//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------
	// Initialize graphics
	gfxInitDefault();
	gfxSetDoubleBuffering(GFX_BOTTOM,0);
	gfxSetDoubleBuffering(GFX_TOP,1);
	consoleInit(GFX_TOP, NULL);
	printf("\x1b[0;0H3DS 2D Cellular Automota by Dan Dews\n\nPress START to exit.\nPress Y to change rules.\nPress B to go back.\nHold L to kill cells you touch.\nPress A to toggle iteration.\nPress LEFT/RIGHT to change color.\nPress R to randomize.");
	printf("\x1b[%d;0HGenerations: [%d,%d] Color: %d",9,generations[1],generations[0],makeColor);

	sceneInit();


	pushScene(fill_screen_init,fill_screen_update,fill_screen_draw,NULL);

	//set up for classical Conway Game of Life:
	memset(rules,0,sizeof(rules));	// clear
	memset(conversion,0,sizeof(conversion));
	for (int i = 0; i < 2; i++) {
		rules[2][0][i] = true;				// star wars rule 345/2/4
		rules[3][1][i] = true;
		rules[4][1][i] = true;
		rules[5][1][i] = true;
	}

	while (aptMainLoop()) {
		if (scenes == NULL) break;
		if (oldScene != scenes || !scenes->initialized) {
			scenes->initialized = false;
			oldScene = scenes;
			scenes->initialized = true;
			if (scenes->init != NULL) scenes->init();
		}
		if (oldScene != scenes) continue;
		if (scenes->update != NULL) scenes->update();
		if (scenes == NULL) break;
		if (oldScene != scenes) continue;
		if (scenes->draw != NULL) scenes->draw();
		if (CATASTROPHIC_FAILURE) break;
	}
	clearScenes();
	gfxExit();
	return 0;
}
