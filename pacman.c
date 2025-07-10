#include <GL/glut.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <time.h>

// Global SDL2 Mixer music
Mix_Music* bgMusic = NULL;

//---------------------------------------------------------------
//              Game board (20x20)
//              0=path,
//              1=wall,
//              2=pellet,
//              3=power pellet
//---------------------------------------------------------------
int board[20][20] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,0,1},
    {1,2,1,1,2,1,1,1,2,1,1,2,1,1,1,2,1,1,2,1},
    {1,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,2,1},
    {1,2,2,2,2,1,1,2,1,1,1,2,1,1,2,2,2,2,2,1},
    {1,2,1,1,2,1,1,2,1,1,1,2,1,1,2,1,1,2,0,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,2,1,1,1,1,1,1,1,1,1,2,1,1,2,0,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,1,1,1,2,1,1,2,1,0,0,2,1,1,2,1,1,1,1,1},
    {1,1,1,1,2,1,1,2,1,0,0,2,1,1,2,1,1,1,1,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,2,1,1,1,1,1,1,1,1,1,2,1,1,2,0,1},
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
    {1,2,1,1,2,1,1,2,1,1,1,2,1,1,2,1,1,2,0,1},
    {1,2,2,2,2,1,1,2,1,1,1,2,1,1,2,2,2,2,2,1},
    {1,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,2,1},
    {1,2,1,1,2,1,1,1,2,1,1,2,1,1,1,2,1,1,2,1},
    {1,0,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,3,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

// Game state
struct GameState {
    float pacmanX, pacmanY; // Pac-Man position
    int score;              // Player score
    int lives;              // Pac-Man lives
    int powerPelletActive;  // 1 if power pellet is active
    int powerPelletTimer;   // Frames remaining for power pellet
    int ghostStates[4];     // 0=normal, 1=blue (edible)
    float ghostX[4], ghostY[4]; // Ghost positions
    int running;            // 1 if game is running
    int pacmanMouthOpen;    // For animation
    int menuState;          // 0=start, 1=playing, 2=paused
    int pacmanDirection;    // 0=up, 1=down, 2=left, 3=right
    time_t gameStartTime;   // Time when game starts (menuState == 1)
    time_t ghostStartTimes[4]; // Start times for each ghost
} gameState = {1.5f, 1.5f, 0, 3, 0, 0, {0,0,0,0}, {9.5f,9.5f,10.5f,10.5f}, {9.5f,9.5f,10.5f,10.5f}, 1, 0, 0, 3, 0, {0,0,0,0}};

// UI state
struct UIState {
    char scoreStr[50];
    char livesStr[50];
    int menuType; // 0=none, 1=start, 2=pause
    int startHover; // 1 if mouse is over start button
    int quitHover;  // 1 if mouse is over quit button
} uiState = {"Score: 0", "Lives: 3", 1, 0, 0}; // Start with start menu

// Synchronization primitives
pthread_mutex_t boardMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t boardRWLock;
sem_t powerPelletSem, keySem, exitPermitSem, speedBoostSem;
pthread_mutex_t powerPelletMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t uiMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t inputMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gameStateMutex = PTHREAD_MUTEX_INITIALIZER;

// Input state
#define MAX_INPUTS 10
struct InputEvent {
    int type; // 0=arrow, 1=menu
    int value; // Arrow: 0=up, 1=down, 2=left, 3=right; Menu: 0=start, 1=pause, 2=restart, 3=quit
} inputQueue[MAX_INPUTS];
int inputCount = 0;

// Initialize SDL2 Mixer for audio
void initSound() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return;
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        printf("Mix_OpenAudio failed: %s\n", Mix_GetError());
        return;
    }
    bgMusic = Mix_LoadMUS("bg-voice.mp3");
    if (!bgMusic) {
        printf("Mix_LoadMUS failed: %s\n", Mix_GetError());
    }
}

// Play background music
void playBackgroundMusic() {
    if (bgMusic && !Mix_PlayingMusic()) {
        Mix_PlayMusic(bgMusic, -1); // -1 for looping
    }
}

// Stop background music
void stopBackgroundMusic() {
    if (Mix_PlayingMusic()) {
        Mix_HaltMusic();
    }
}

// Cleanup SDL2 Mixer resources
void cleanupSound() {
    stopBackgroundMusic();
    if (bgMusic) {
        Mix_FreeMusic(bgMusic);
        bgMusic = NULL;
    }
    Mix_CloseAudio();
    SDL_Quit();
}

// Initialize synchronization
void initSync() {
    pthread_rwlock_init(&boardRWLock, NULL);
    sem_init(&powerPelletSem, 0, 8);
    sem_init(&keySem, 0, 2);
    sem_init(&exitPermitSem, 0, 1);
    sem_init(&speedBoostSem, 0, 2);
}

// Draw a circle (modified for Pac-Man shape with direction)
void drawCircle(float x, float y, float radius, float r, float g, float b, int isPacman, int direction) {
    glColor3f(r, g, b);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    if (isPacman && gameState.menuState == 1) {
        // Draw Pac-Man with animated mouth, oriented by direction
        int baseStartAngle = gameState.pacmanMouthOpen ? 30 : 15;
        int baseEndAngle = gameState.pacmanMouthOpen ? 330 : 345;
        int rotation = 0;
        switch (direction) {
            case 0: rotation = 90; break;  // Up
            case 1: rotation = -90; break; // Down
            case 2: rotation = 180; break; // Left
            case 3: rotation = 0; break;   // Right
        }
        int startAngle = (baseStartAngle + rotation) % 360;
        int endAngle = (baseEndAngle + rotation) % 360;
        if (startAngle <= endAngle) {
            for (int i = startAngle; i <= endAngle; i++) {
                float angle = i * M_PI / 180.0;
                glVertex2f(x + radius * cos(angle), y + radius * sin(angle));
            }
        } else {
            for (int i = startAngle; i <= endAngle + 360; i++) {
                float angle = (i % 360) * M_PI / 180.0;
                glVertex2f(x + radius * cos(angle), y + radius * sin(angle));
            }
        }
    } else {
        // Draw full circle for pellets, power pellets, ghosts, or static Pac-Man
        for (int i = 0; i <= 360; i++) {
            float angle = i * M_PI / 180.0;
            glVertex2f(x + radius * cos(angle), y + radius * sin(angle));
        }
    }
    glEnd();
}

// Draw the board
void drawBoard() {
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 20; j++) {
            if (board[i][j] == 1) {
                glColor3f(0.0, 0.0, 1.0);
                glBegin(GL_QUADS);
                glVertex2f(j, 20 - i - 1);
                glVertex2f(j + 1, 20 - i - 1);
                glVertex2f(j + 1, 20 - i);
                glVertex2f(j, 20 - i);
                glEnd();
            } else if (board[i][j] == 2) {
                drawCircle(j + 0.5, 20 - i - 0.5, 0.1, 1.0, 1.0, 1.0, 0, 0);
            } else if (board[i][j] == 3) {
                drawCircle(j + 0.5, 20 - i - 0.5, 0.2, 1.0, 1.0, 0.0, 0, 0);
            }
        }
    }
}

// Helper function to render text
void renderText(float x, float y, const char* text, void* font, float r, float g, float b) {
    glColor3f(r, g, b);
    glRasterPos2f(x, y);
    for (const char* c = text; *c != '\0'; c++) {
        glutBitmapCharacter(font, *c);
    }
}

// Draw UI
void drawUI() {
    pthread_mutex_lock(&uiMutex);
    if (uiState.menuType == 0) { // Playing: show score and lives
        renderText(0.5, 19.5, uiState.scoreStr, GLUT_BITMAP_HELVETICA_12, 1.0, 1.0, 1.0);
        renderText(15.5, 19.5, uiState.livesStr, GLUT_BITMAP_HELVETICA_12, 1.0, 1.0, 1.0);
    } else if (uiState.menuType == 1) { // Start menu
        // Gradient background
        glBegin(GL_QUADS);
        glColor3f(0.0, 0.0, 0.2); // Dark blue at top
        glVertex2f(0.0, 20.0);
        glVertex2f(20.0, 20.0);
        glColor3f(0.0, 0.0, 0.8); // Lighter blue at bottom
        glVertex2f(20.0, 0.0);
        glVertex2f(0.0, 0.0);
        glEnd();

        // Animated title (scaling effect)
        static float titleScale = 1.0f;
        static int titleScaleDirection = 1;
        titleScale += titleScaleDirection * 0.005f;
        if (titleScale > 1.2f || titleScale < 1.0f) titleScaleDirection = -titleScaleDirection;
        glPushMatrix();
        glTranslatef(7.0, 12.0, 0.0);
        glScalef(titleScale, titleScale, 1.0);
        glTranslatef(-7.0, -12.0, 0.0);
        renderText(5.0, 12.0, "Pac-Man", GLUT_BITMAP_HELVETICA_18, 1.0, 1.0, 0.0);
        glPopMatrix();

        // Start button with hover effect
        glColor3f(uiState.startHover ? 0.0 : 0.2, uiState.startHover ? 0.7 : 0.5, 0.0); // Green, brighter on hover
        glBegin(GL_QUADS);
        glVertex2f(6.0, 10.5);
        glVertex2f(14.0, 10.5);
        glVertex2f(14.0, 9.0);
        glVertex2f(6.0, 9.0);
        glEnd();
        renderText(8.0, 9.6, "Start Game", GLUT_BITMAP_HELVETICA_18, 1.0, 1.0, 1.0);

        // Quit button with hover effect
        glColor3f(uiState.quitHover ? 0.7 : 0.5, 0.0, 0.0); // Red, brighter on hover
        glBegin(GL_QUADS);
        glVertex2f(6.0, 8.0);
        glVertex2f(14.0, 8.0);
        glVertex2f(14.0, 6.5);
        glVertex2f(6.0, 6.5);
        glEnd();
        renderText(8.5, 7.1, "Quit", GLUT_BITMAP_HELVETICA_18, 1.0, 1.0, 1.0);
    } else if (uiState.menuType == 2) { // Pause menu
        renderText(6.0, 11.0, "Paused", GLUT_BITMAP_HELVETICA_18, 1.0, 1.0, 1.0);
        renderText(6.0, 10.0, "Press P to Resume", GLUT_BITMAP_HELVETICA_12, 1.0, 1.0, 1.0);
        renderText(6.0, 9.0, "Press R to Restart", GLUT_BITMAP_HELVETICA_12, 1.0, 1.0, 1.0);
        renderText(6.0, 8.0, "Press Q to Quit", GLUT_BITMAP_HELVETICA_12, 1.0, 1.0, 1.0);
    }
    pthread_mutex_unlock(&uiMutex);
}

// Display callback
void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    pthread_mutex_lock(&boardMutex);
    if (!gameState.running) {
        // Game Over screen
        // Gradient background
        glBegin(GL_QUADS);
        glColor3f(0.2, 0.0, 0.0); // Dark red at top
        glVertex2f(0.0, 20.0);
        glVertex2f(20.0, 20.0);
        glColor3f(0.8, 0.0, 0.0); // Lighter red at bottom
        glVertex2f(20.0, 0.0);
        glVertex2f(0.0, 0.0);
        glEnd();

        // Animated Game Over title
        static float titleScale = 1.0f;
        static int titleScaleDirection = 1;
        titleScale += titleScaleDirection * 0.005f;
        if (titleScale > 1.2f || titleScale < 1.0f) titleScaleDirection = -titleScaleDirection;
        glPushMatrix();
        glTranslatef(7.0, 12.0, 0.0);
        glScalef(titleScale, titleScale, 1.0);
        glTranslatef(-7.0, -12.0, 0.0);
        renderText(5.0, 12.0, "Game Over", GLUT_BITMAP_TIMES_ROMAN_24, 1.0, 1.0, 0.0);
        glPopMatrix();

        // Display final score
        char scoreText[50];
        sprintf(scoreText, "Final Score: %d", gameState.score);
        renderText(7.0, 8.0, scoreText, GLUT_BITMAP_TIMES_ROMAN_24, 1.0, 1.0, 0.0);

        // Draw static Pac-Man (closed mouth, facing right)
        drawCircle(10.0, 5.0, 0.4, 1.0, 1.0, 0.0, 0, 3);

        // Draw decorative pellets along borders
        for (float x = 0.5; x < 20.0; x += 1.0) {
            drawCircle(x, 19.5, 0.1, 1.0, 1.0, 1.0, 0, 0); // Top
            drawCircle(x, 0.5, 0.1, 1.0, 1.0, 1.0, 0, 0);  // Bottom
        }
        for (float y = 0.5; y < 20.0; y += 1.0) {
            drawCircle(0.5, y, 0.1, 1.0, 1.0, 1.0, 0, 0);  // Left
            drawCircle(19.5, y, 0.1, 1.0, 1.0, 1.0, 0, 0); // Right
        }
    } else if (gameState.menuState == 1) { // Playing
        drawBoard();
        drawCircle(gameState.pacmanX, gameState.pacmanY, 0.4, 1.0, 1.0, 0.0, 1, gameState.pacmanDirection);
        for (int i = 0; i < 4; i++) {
            float r = gameState.ghostStates[i] ? 0.0 : 1.0;
            float g = gameState.ghostStates[i] ? 0.0 : 0.0;
            float b = gameState.ghostStates[i] ? 1.0 : 0.0;
            drawCircle(gameState.ghostX[i], gameState.ghostY[i], 0.4, r, g, b, 0, 0);
        }
    }
    pthread_mutex_unlock(&boardMutex);
    drawUI();
    glutSwapBuffers();
}

void resetGameState() {
    // Assume boardMutex and gameStateMutex are already locked by the caller
    gameState.pacmanX = 1.5f;
    gameState.pacmanY = 1.5f;
    gameState.score = 0;
    gameState.lives = 3;
    gameState.powerPelletActive = 0;
    gameState.powerPelletTimer = 0;
    for (int i = 0; i < 4; i++) {
        gameState.ghostStates[i] = 0;
        gameState.ghostX[i] = (i % 2 == 0) ? 9.5f : 10.5f;
        gameState.ghostY[i] = (i < 2) ? 9.5f : 10.5f;
    }
    gameState.pacmanDirection = 3;
    gameState.gameStartTime = time(NULL);
    for (int i = 0; i < 4; i++) {
        gameState.ghostStartTimes[i] = gameState.gameStartTime + (i * 5);
    }
    // Reset board pellets
    for (int i = 0; i < 20; i++) {
        for (int j = 0; j < 20; j++) {
            if (board[i][j] == 0 || board[i][j] == 2 || board[i][j] == 3) {
                board[i][j] = (i == 1 || i == 18 || i == 4 || i == 13) && (j == 2 || j == 17) ? 3 : 2;
            }
        }
    }
    // Protect semaphore reset
    pthread_mutex_lock(&powerPelletMutex);
    sem_destroy(&powerPelletSem);
    sem_init(&powerPelletSem, 0, 8);
    pthread_mutex_unlock(&powerPelletMutex);
}

// Reset positions and timers after a collision
void resetPositionsAfterCollision() {
    pthread_mutex_lock(&gameStateMutex);
    gameState.pacmanX = 1.5f;
    gameState.pacmanY = 1.5f;
    gameState.pacmanDirection = 3; // Reset to right
    for (int i = 0; i < 4; i++) {
        gameState.ghostStates[i] = 0;
        gameState.ghostX[i] = (i % 2 == 0) ? 9.5f : 10.5f; // Reset to initial positions
        gameState.ghostY[i] = (i < 2) ? 9.5f : 10.5f;
    }
    gameState.gameStartTime = time(NULL); // Restart the game timer
    for (int i = 0; i < 4; i++) {
        gameState.ghostStartTimes[i] = gameState.gameStartTime + (i * 5); // Reset start times with delays
    }
    pthread_mutex_unlock(&gameStateMutex);
}

// Helper function to check if a position is valid
int isValidPosition(float x, float y) {
    // Pac-Man's and ghosts' radius is 0.4, so check all grid cells that the bounding box overlaps
    float radius = 0.4f;
    int minX = (int)floor(x - radius);
    int maxX = (int)floor(x + radius);
    int minY = (int)floor(y - radius);
    int maxY = (int)floor(y + radius);

    // Check all overlapping grid cells
    for (int gridX = minX; gridX <= maxX; gridX++) {
        for (int gridY = minY; gridY <= maxY; gridY++) {
            int boardY = 19 - gridY; // Convert to board coordinates
            if (gridX < 0 || gridX >= 20 || boardY < 0 || boardY >= 20 || board[boardY][gridX] == 1) {
                return 0; // Invalid if any cell is a wall or out of bounds
            }
        }
    }
    return 1; // Valid if no walls are found
}

// Helper function to check if a position is occupied by another ghost
int isPositionOccupied(float x, float y, int currentGhostId) {
    for (int i = 0; i < 4; i++) {
        if (i == currentGhostId) continue; // Skip the current ghost
        float dx = x - gameState.ghostX[i];
        float dy = y - gameState.ghostY[i];
        if (sqrt(dx * dx + dy * dy) < 0.8f) { // Check if too close (0.8f is diameter of two ghosts)
            return 1; // Position is occupied
        }
    }
    return 0; // Position is free
}

// Game engine thread
void* gameEngine(void* arg) {
    float speed = 0.1f;
    while (gameState.running) {
        // Process input events
        pthread_mutex_lock(&inputMutex);
        for (int i = 0; i < inputCount; i++) {
            if (inputQueue[i].type == 0) { // Arrow key
                // Update Pac-Man's direction based on arrow key input
                pthread_mutex_lock(&boardMutex);
                gameState.pacmanDirection = inputQueue[i].value; // 0=up, 1=down, 2=left, 3=right
                pthread_mutex_unlock(&boardMutex);
            } else if (inputQueue[i].type == 1) { // Menu input
                pthread_mutex_lock(&boardMutex);
                pthread_mutex_lock(&gameStateMutex);
                if (inputQueue[i].value == 0) { // Start
                    gameState.menuState = 1;
                    gameState.gameStartTime = time(NULL);
                    for (int j = 0; j < 4; j++) {
                        gameState.ghostStartTimes[j] = gameState.gameStartTime + (j * 5);
                        printf("Ghost %d start time set to %ld\n", j, gameState.ghostStartTimes[j]);
                    }
                    playBackgroundMusic();
                    resetGameState();
                } else if (inputQueue[i].value == 1) { // Pause
                    if (gameState.menuState == 1)
                        gameState.menuState = 2;
                    else if (gameState.menuState == 2)
                        gameState.menuState = 1;
                } else if (inputQueue[i].value == 2) { // Restart
                    resetGameState();
                    gameState.menuState = 1;
                    playBackgroundMusic();
                } else if (inputQueue[i].value == 3) { // Quit
                    gameState.running = 0;
                    stopBackgroundMusic();
                    pthread_mutex_unlock(&gameStateMutex);
                    pthread_mutex_unlock(&boardMutex);
                    exit(0);
                }
                pthread_mutex_unlock(&gameStateMutex);
                pthread_mutex_unlock(&boardMutex);
            }
        }
        inputCount = 0; // Clear input queue after processing
        pthread_mutex_unlock(&inputMutex);

        if (gameState.menuState == 1) { // Playing
            // Automatic movement based on current direction
            float newX = gameState.pacmanX;
            float newY = gameState.pacmanY;
            pthread_mutex_lock(&boardMutex);
            switch (gameState.pacmanDirection) {
                case 0: newY += speed; break; // Up
                case 1: newY -= speed; break; // Down
                case 2: newX -= speed; break; // Left
                case 3: newX += speed; break; // Right
            }

            // Validate and update Pac-Man position
            if (isValidPosition(newX, newY)) {
                gameState.pacmanX = newX;
                gameState.pacmanY = newY;

                // Check for pellets at Pac-Man's center
                int gridX = (int)floor(newX);
                int gridY = 19 - (int)floor(newY);
                if (gridX >= 0 && gridX < 20 && gridY >= 0 && gridY < 20) {
                    if (board[gridY][gridX] == 2) {
                        board[gridY][gridX] = 0;
                        gameState.score += 10;
                    } else if (board[gridY][gridX] == 3) {
                        pthread_mutex_lock(&powerPelletMutex);
                        int value;
                        sem_getvalue(&powerPelletSem, &value);
                        if (value > 0) {
                            sem_wait(&powerPelletSem);
                            board[gridY][gridX] = 0;
                            gameState.powerPelletActive = 1;
                            gameState.powerPelletTimer = 600;
                            for (int i = 0; i < 4; i++)
                                gameState.ghostStates[i] = 1;
                        }
                        pthread_mutex_unlock(&powerPelletMutex);
                    }
                }
            }
            // Note: If position is invalid, Pac-Man stops until a new direction is set by arrow keys

            if (gameState.powerPelletActive && gameState.powerPelletTimer > 0) {
                gameState.powerPelletTimer--;
                if (gameState.powerPelletTimer == 0) {
                    gameState.powerPelletActive = 0;
                    for (int i = 0; i < 4; i++)
                        gameState.ghostStates[i] = 0;
                    pthread_mutex_lock(&powerPelletMutex);
                    sem_post(&powerPelletSem);
                    pthread_mutex_unlock(&powerPelletMutex);
                }
            }
            // Check collisions with ghosts
            for (int i = 0; i < 4; i++) {
                float dx = gameState.pacmanX - gameState.ghostX[i];
                float dy = gameState.pacmanY - gameState.ghostY[i];
                if (sqrt(dx*dx + dy*dy) < 0.8) {
                    if (gameState.ghostStates[i]) {
                        gameState.score += 200;
                        gameState.ghostX[i] = 9.5f;
                        gameState.ghostY[i] = 9.5f;
                        gameState.ghostStates[i] = 0;
                    } else {
                        gameState.lives--;
                        if (gameState.lives == 0) {
                            gameState.running = 0;
                        } else {
                            resetPositionsAfterCollision();
                        }
                    }
                }
            }
            pthread_mutex_unlock(&boardMutex);
        }

        glutPostRedisplay();
        usleep(16667); // ~60 FPS
    }
    return NULL;
}

// Pac-Man thread (animation)
void* pacmanThread(void* arg) {
    while (gameState.running) {
        pthread_mutex_lock(&boardMutex);
        if (gameState.menuState == 1)
            gameState.pacmanMouthOpen = !gameState.pacmanMouthOpen;
        pthread_mutex_unlock(&boardMutex);
        usleep(200000);
    }
    return NULL;
}

// Ghost thread
void* ghostThread(void* arg) {
    int id = *(int*)arg;
    float speed = 0.05f; // Slow speed for smooth, continuous movement
    int direction = -1; // 0=up, 1=down, 2=left, 3=right
    int directionChangeCounter = 0; // Counter to change direction every 60 frames

    while (gameState.running) {
        // Wait until the game is in playing mode
        int currentMenuState;
        pthread_mutex_lock(&gameStateMutex);
        currentMenuState = gameState.menuState;
        pthread_mutex_unlock(&gameStateMutex);

        if (currentMenuState != 1) {
            usleep(16667); // Wait until the game starts
            continue;
        }

        // Check the start time
        time_t currentTime = time(NULL);
        time_t ghostStartTime;
        pthread_mutex_lock(&gameStateMutex);
        ghostStartTime = gameState.ghostStartTimes[id];
        pthread_mutex_unlock(&gameStateMutex);

        if (currentTime < ghostStartTime) {
            usleep(16667); // Wait until it's time to start
            continue;
        }

        pthread_mutex_lock(&boardMutex);
        float ghostX = gameState.ghostX[id];
        float ghostY = gameState.ghostY[id];
        pthread_mutex_unlock(&boardMutex);

        // Change direction every 60 frames (~1 second at 60 FPS)
        if (directionChangeCounter <= 0) {
            direction = rand() % 4; // Random direction: 0=up, 1=down, 2=left, 3=right
            directionChangeCounter = 60; // Reset counter
        }
        directionChangeCounter--;

        // Calculate new position based on direction
        float newX = ghostX;
        float newY = ghostY;
        switch (direction) {
            case 0: newY += speed; break; // Up
            case 1: newY -= speed; break; // Down
            case 2: newX -= speed; break; // Left
            case 3: newX += speed; break; // Right
        }

        // Validate new position
        if (isValidPosition(newX, newY)) {
            pthread_mutex_lock(&boardMutex);
            if (!isPositionOccupied(newX, newY, id)) {
                gameState.ghostX[id] = newX;
                gameState.ghostY[id] = newY;
            } else {
                directionChangeCounter = 0;
            }
            pthread_mutex_unlock(&boardMutex);
        } else {
            directionChangeCounter = 0;
        }

        usleep(16667); // ~60 FPS for smooth movement
    }
    return NULL;
}

// User Interface thread
void* uiThread(void* arg) {
    while (gameState.running) {
        pthread_mutex_lock(&boardMutex);
        int score = gameState.score;
        int lives = gameState.lives;
        int menuState = gameState.menuState;
        pthread_mutex_unlock(&boardMutex);

        // Update UI state
        pthread_mutex_lock(&uiMutex);
        sprintf(uiState.scoreStr, "Score: %d", score);
        sprintf(uiState.livesStr, "Lives: %d", lives);
        uiState.menuType = (menuState == 0) ? 1 : (menuState == 2) ? 2 : 0;
        pthread_mutex_unlock(&uiMutex);

        usleep(16667); // ~60 FPS
    }
    return NULL;
}

// Keyboard callbacks
void specialKeys(int key, int x, int y) {
    pthread_mutex_lock(&boardMutex);
    if (gameState.menuState != 1) { // Ignore arrow keys in menus
        pthread_mutex_unlock(&boardMutex);
        return;
    }
    pthread_mutex_unlock(&boardMutex);
    pthread_mutex_lock(&inputMutex);
    if (inputCount < MAX_INPUTS) {
        inputQueue[inputCount].type = 0; // Arrow key
        switch (key) {
            case GLUT_KEY_UP:    inputQueue[inputCount].value = 0; break;
            case GLUT_KEY_DOWN:  inputQueue[inputCount].value = 1; break;
            case GLUT_KEY_LEFT:  inputQueue[inputCount].value = 2; break;
            case GLUT_KEY_RIGHT: inputQueue[inputCount].value = 3; break;
        }
        inputCount++;
    }
    pthread_mutex_unlock(&inputMutex);
}

void keyboard(unsigned char key, int x, int y) {
    pthread_mutex_lock(&gameStateMutex);
    int currentMenuState = gameState.menuState;
    pthread_mutex_unlock(&gameStateMutex);

    pthread_mutex_lock(&inputMutex);
    if (inputCount < MAX_INPUTS) {
        if (currentMenuState == 0 && key == '1') { // Start game on '1' key press in start menu
            inputQueue[inputCount].type = 1;
            inputQueue[inputCount].value = 0; // Start event
            inputCount++;
        } else if (currentMenuState != 0) { // Handle other keys only when not in start menu
            inputQueue[inputCount].type = 1;
            switch (key) {
                case 'p': case 'P': inputQueue[inputCount].value = 1; break; // Pause
                case 'r': case 'R': inputQueue[inputCount].value = 2; break; // Restart
                case 'q': case 'Q': inputQueue[inputCount].value = 3; break; // Quit
                default: pthread_mutex_unlock(&inputMutex); return;
            }
            inputCount++;
        }
    }
    pthread_mutex_unlock(&inputMutex);
}

// Mouse callback
void mouse(int button, int state, int x, int y) {
    pthread_mutex_lock(&boardMutex);
    if (gameState.menuState != 0 || button != GLUT_LEFT_BUTTON || state != GLUT_DOWN) {
        pthread_mutex_unlock(&boardMutex);
        return;
    }
    pthread_mutex_unlock(&boardMutex);

    // Map window coordinates to OpenGL coordinates
    int windowHeight = glutGet(GLUT_WINDOW_HEIGHT);
    float oglX = (float)x / glutGet(GLUT_WINDOW_WIDTH) * 20.0f;
    float oglY = (float)(windowHeight - y) / windowHeight * 20.0f;

    pthread_mutex_lock(&inputMutex);
    if (inputCount < MAX_INPUTS) {
        inputQueue[inputCount].type = 1; // Menu input
        if (oglX >= 6.0f && oglX <= 14.0f && oglY >= 9.0f && oglY <= 10.5f) {
            inputQueue[inputCount].value = 0; // Start button
        } else if (oglX >= 6.0f && oglX <= 14.0f && oglY >= 6.5f && oglY <= 8.0f) {
            inputQueue[inputCount].value = 3; // Quit button
        } else {
            pthread_mutex_unlock(&inputMutex);
            return;
        }
        inputCount++;
    }
    pthread_mutex_unlock(&inputMutex);
}

// Passive motion callback for hover effects
void passiveMotion(int x, int y) {
    pthread_mutex_lock(&boardMutex);
    if (gameState.menuState != 0) {
        pthread_mutex_unlock(&boardMutex);
        return;
    }
    pthread_mutex_unlock(&boardMutex);

    // Map window coordinates to OpenGL coordinates
    int windowHeight = glutGet(GLUT_WINDOW_HEIGHT);
    float oglX = (float)x / glutGet(GLUT_WINDOW_WIDTH) * 20.0f;
    float oglY = (float)(windowHeight - y) / windowHeight * 20.0f;

    pthread_mutex_lock(&uiMutex);
    uiState.startHover = (oglX >= 6.0f && oglX <= 14.0f && oglY >= 9.0f && oglY <= 10.5f) ? 1 : 0;
    uiState.quitHover = (oglX >= 6.0f && oglX <= 14.0f && oglY >= 6.5f && oglY <= 8.0f) ? 1 : 0;
    pthread_mutex_unlock(&uiMutex);

    glutPostRedisplay();
}

// Initialize OpenGL
void init() {
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, 20, 0, 20);
    glMatrixMode(GL_MODELVIEW);
}

// Main function
int main(int argc, char** argv) {
    srand(time(NULL));
    initSync();
    initSound();

    // Initialize ghost start times to a future time to avoid race condition
    time_t currentTime = time(NULL);
    pthread_mutex_lock(&gameStateMutex);
    gameState.gameStartTime = currentTime;
    for (int i = 0; i < 4; i++) {
        gameState.ghostStartTimes[i] = gameState.gameStartTime + (i * 5);
    }
    pthread_mutex_unlock(&gameStateMutex);

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(600, 600);
    glutCreateWindow("Multi-threaded Pac-Man");
    init();
    glutDisplayFunc(display);
    glutSpecialFunc(specialKeys);
    glutKeyboardFunc(keyboard);
    glutMouseFunc(mouse);
    glutPassiveMotionFunc(passiveMotion);

    pthread_t gameEngineT, pacmanT, uiT, ghostT[4];
    int ghostIds[4] = {0, 1, 2, 3};
    if (pthread_create(&gameEngineT, NULL, gameEngine, NULL) != 0 ||
        pthread_create(&pacmanT, NULL, pacmanThread, NULL) != 0 ||
        pthread_create(&uiT, NULL, uiThread, NULL) != 0) {
        printf("Thread creation failed\n");
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (pthread_create(&ghostT[i], NULL, ghostThread, &ghostIds[i]) != 0) {
            printf("Ghost thread %d creation failed\n", i);
            return 1;
        }
    }

    glutMainLoop();

    // Cleanup
    pthread_join(gameEngineT, NULL);
    pthread_join(pacmanT, NULL);
    pthread_join(uiT, NULL);
    for (int i = 0; i < 4; i++)
        pthread_join(ghostT[i], NULL);
    cleanupSound();
    pthread_mutex_destroy(&boardMutex);
    pthread_mutex_destroy(&inputMutex);
    pthread_mutex_destroy(&powerPelletMutex);
    pthread_mutex_destroy(&uiMutex);
    pthread_mutex_destroy(&gameStateMutex);
    pthread_rwlock_destroy(&boardRWLock);
    sem_destroy(&powerPelletSem);
    sem_destroy(&keySem);
    sem_destroy(&exitPermitSem);
    sem_destroy(&speedBoostSem);

    return 0;
}
