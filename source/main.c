#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
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

bool randomize = false;

int lastBlock = 0;
int lastNeighbor = 0;
bool erasing = false;				// if user is killing cells
bool rules[9][2];					// totalistic 2 dimensional cellular automota rules
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
void writeColor(int x, int y, u32 color) {
	makePixel(x, sanitizeY(y + 1),color);
	makePixel(sanitizeX(x + 1),sanitizeY(y + 1),color);
	makePixel(sanitizeX(x + 1),y,color);
	makePixel(x,y,color);
}
u32 getColor(u8 buf[], int x, int y) { // Thank you WolfVak for the code!
	if (x > 320) x = 0;
	else if(x < 0) x = 320;
	if (y > 240) y = 0;
	else if (y < 0) y = 240;
	u32 offset = ((x * 240) - y + 239) * 3;
	return (u32) (buf[offset] | buf[offset + 1] << 8 | buf[offset + 2] << 16);
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
	for (int x = 0; x < WIDTH; x += 2) {
		for (int y = 0; y < HEIGHT; y += 2) {
			if (rand() % 2) writeColor(x,y,0xff000000);
			else writeColor(x,y,0xffff0000);
		}
	}
}
void fill_screen_init() {
    if (randomize) {
    	randomize_screen();
    	randomize = false;
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
	if (touch.px && touch.py) {
		int x = touch.px;
		int y = touch.py;
		if (x % 2) x = sanitizeX(x - 1);
		if (y % 2) y = sanitizeY(y - 1);
		if (erasing) writeColor(x,y,0xff000000);
		else writeColor(x,y,0xffff0000);
	}
	if (kDown & KEY_A) {
		if (stepping) stepping = false;
		else stepping = true;
	}
	if (kDown & KEY_B) {
		popScene();
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
	u32 kUp = hidKeysUp();
	u32 kHeld = hidKeysHeld();
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
int getNeighborX(int x, int p) {
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
int getNeighborY(int y, int p) {
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
	u32 kHeld = hidKeysHeld();
	u32 kUp = hidKeysUp();
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
					if (rules[numNeighbors][0]) rules[numNeighbors][0] = false;
					else rules[numNeighbors][0] = true;
				} else if (block == 9) {
					if (rules[numNeighbors][1]) rules[numNeighbors][1] = false;
					else rules[numNeighbors][1] = true;
				}
			}
		}
	} else {
		lastBlock = 0;
		lastNeighbor = 0;
	}
	if (kDown & KEY_R) randomize = true;
	if (kDown & KEY_B || kDown & KEY_Y) {
		popScene();		// back to previous scene
		return;
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
	for (int w = 0; w < MENU_WIDTH; w += 11) { 		// 10 blocks of 11 pixels
		for (int h = 0; h < MENU_HEIGHT; h += 12) { // 9 blocks of 12 pixels from top to bottom
			int block = w / 11;
			int numNeighbors = h / 12; 				// how many neighbors alive in this row
			for (int x = 0; x < 11; x++) {
				for (int y = 0; y < 12; y++) {
					if (block < 8) {
						if (block < numNeighbors) makePixel(MENU_X + w + x,MENU_Y + h + y,0xffff0000); 		// this block is red
						else makePixel(MENU_X + w + x,MENU_Y + h + y,0xff000000);							// this block is black
					} else {
						if (block == 8) { 																		// if I'm dead and I have this many living neighbors I should be:
							if (rules[numNeighbors][0]) makePixel(MENU_X + w + x,MENU_Y + h + y,0xffff0000); 	// alive
							else makePixel(MENU_X + w + x,MENU_Y + h + y,0xff000000);							// dead
						} else {																				// if I'm alive and I have this many living neighbors I should be:
							if (rules[numNeighbors][1]) makePixel(MENU_X + w + x,MENU_Y + h + y,0xffff0000);	// alive
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

			int livingNeighbors = 0;
			int me = getColor(oldbuffer,x,y) != 0 ? 1 : 0;
			for (int n = 0; n < 8; n++) {
				//for every neighbor of that x and y...
				if (getColor(oldbuffer,getNeighborX(x,n),getNeighborY(y,n))) livingNeighbors++;
			}

			if (rules[livingNeighbors][me]) writeColor(x,y,0xffff0000); //we live
			else writeColor(x,y,0xff000000); //we die

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
	printf("\x1b[0;0H3DS 2D Cellular Automota by Dan Dews\n\nPress START to exit.\nPress Y to change rules.\nPress B to go back.\nHold A to kill cells you touch.\nPress A to toggle iteration.\nPress R to randomize.");


	sceneInit();


	pushScene(fill_screen_init,fill_screen_update,fill_screen_draw,NULL);

	memset(rules,0,sizeof(rules[0][0]) * 8 * 2); //everything set to off;

	//set up for classical Conway Game of Life:
	for (int i = 0; i < 8; i++) {
		if (i < 2) rules[i][1] = false; 		//a live cell with less than 2 live neighbors die by underpopulation
		else if (i < 4) rules[i][1] = true; 	//a live cell with 2-3 live neighbors lives
	}
	rules[3][0] = true; 						//any dead cell with exactly three living neighbors lives by reproduction

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
