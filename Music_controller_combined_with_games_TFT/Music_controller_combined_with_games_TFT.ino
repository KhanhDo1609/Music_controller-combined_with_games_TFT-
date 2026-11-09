#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// ================= PINS =================
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8

#define JOY_X A0
#define JOY_Y A1
#define JOY_SW 2

#define ENC_CLK 3
#define ENC_DT 4
#define ENC_SW 5

#define BACK_BTN 6
#define BUZZER 7

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);

const int SCREEN_WIDTH = 160;
const int SCREEN_HEIGHT = 128;
const int HEADER_HEIGHT = 20;
const uint16_t DARK_GREY = 0x7BEF;

// ================= STATES =================
enum Screen {
  MENU, SNAKE, SHOOTER, BREAKOUT, PLACEHOLDER, MUSIC
};

Screen screen = MENU;

byte selected = 0;
bool gameOver = false;
bool gameWon = false;

unsigned int score = 0;
unsigned long lastTick = 0;
unsigned long lastMenuMove = 0;
unsigned long lastPress = 0;

bool oldJoySW = HIGH;
bool oldEncSW = HIGH;
bool oldBackSW = HIGH;
int oldEncCLK = HIGH;
bool musicJoystickReady = false;
unsigned long lastVolumeTurn = 0;

const char* const menuItems[] = {
  "Snake",
  "Space Shooter",
  "Brick Breaker",
  "Flappy Bird",
  "Racing",
  "YouTube Music"
};

// ================= HELPERS =================
void beep(int frequency = 1200, int duration = 35) {
  tone(BUZZER, frequency, duration);
}

void drawHeader(const __FlashStringHelper* title) {
  tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, ST77XX_BLUE);

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);

  tft.setCursor(6, 6);
  tft.print(title);

  tft.setCursor(SCREEN_WIDTH - 36, 6);
  tft.print(score);
}

void startScreen() {
  score = 0;
  gameOver = false;
  gameWon = false;
  lastTick = millis();
  tft.fillScreen(ST77XX_BLACK);
}

void endGame(bool victory = false) {
  gameOver = true;
  gameWon = victory;

  beep(victory ? 1800 : 250, 250);

  tft.fillRoundRect(
    12, 39, 136, 55, 6,
    DARK_GREY
  );

  tft.setTextSize(2);
  tft.setTextColor(
    victory ? ST77XX_GREEN : ST77XX_RED
  );

  tft.setCursor(victory ? 32 : 26, 48);

  if (victory) {
    tft.print(F("YOU WIN!"));
  } else {
    tft.print(F("GAME OVER"));
  }

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(38, 72);
  tft.print(F("Press to retry"));
}

// ================= MAIN MENU =================
void drawMenu() {
  tft.fillScreen(ST77XX_BLACK);

  tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, ST77XX_BLUE);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(38, 6);
  tft.print(F("ARDUINO ARCADE"));

  for (byte i = 0; i < 6; i++) {
    int y = 26 + i * 14;

    if (i == selected) {
      tft.fillRoundRect(
        6, y - 2, SCREEN_WIDTH - 12, 12, 3,
        ST77XX_GREEN
      );
      tft.setTextColor(ST77XX_BLACK);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }

    tft.setCursor(12, y);
    tft.print(i + 1);
    tft.print(F(". "));
    tft.print(menuItems[i]);
  }

  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(8, 116);
  tft.print(F("Turn / press to select"));
}

void moveMenu(int direction) {
  int next = selected + direction;

  if (next < 0) next = 5;
  if (next > 5) next = 0;

  selected = next;
  beep(900, 20);
  drawMenu();
}

// ================= SNAKE =================
const byte CELL = 5;
const byte COLS = SCREEN_WIDTH / CELL;
const byte ROWS = 20;
const byte MAX_SNAKE = 100;

byte snakeX[MAX_SNAKE];
byte snakeY[MAX_SNAKE];

byte snakeLength = 3;
byte foodX, foodY;

int dirX = 1, dirY = 0;
int nextX = 1, nextY = 0;

void snakeCell(int x, int y, uint16_t color) {
  tft.fillRect(
    x * CELL,
    HEADER_HEIGHT + y * CELL,
    CELL - 1, CELL - 1,
    color
  );
}

void spawnFood() {
  // Search for an empty cell, starting randomly.
  int start = random((long)COLS * ROWS);

  for (int offset = 0; offset < COLS * ROWS; offset++) {
    int index = (start + offset) % (COLS * ROWS);
    byte x = index % COLS;
    byte y = index / COLS;

    bool occupied = false;

    for (byte i = 0; i < snakeLength; i++) {
      if (snakeX[i] == x && snakeY[i] == y) {
        occupied = true;
        break;
      }
    }

    if (!occupied) {
      foodX = x;
      foodY = y;
      snakeCell(foodX, foodY, ST77XX_RED);
      return;
    }
  }

  endGame(true);
}

void startSnake() {
  screen = SNAKE;
  startScreen();

  snakeLength = 3;

  for (byte i = 0; i < snakeLength; i++) {
    snakeX[i] = 8 - i;
    snakeY[i] = 10;
  }

  dirX = nextX = 1;
  dirY = nextY = 0;

  drawHeader(F("SNAKE"));

  for (byte i = 0; i < snakeLength; i++) {
    snakeCell(snakeX[i], snakeY[i], ST77XX_GREEN);
  }

  spawnFood();
}

void updateSnake(int joyX, int joyY) {
  // Direction selection
  if (abs(joyX - 512) > abs(joyY - 512)) {
    if (joyX < 300 && dirX != 1) {
      nextX = -1;
      nextY = 0;
    } else if (joyX > 700 && dirX != -1) {
      nextX = 1;
      nextY = 0;
    }
  } else {
    if (joyY < 300 && dirY != 1) {
      nextX = 0;
      nextY = -1;
    } else if (joyY > 700 && dirY != -1) {
      nextX = 0;
      nextY = 1;
    }
  }

  // Higher score = shorter movement interval
  unsigned long interval =
    max(75L, 200L - (long)score);

  if (millis() - lastTick < interval) return;
  lastTick = millis();

  dirX = nextX;
  dirY = nextY;

  int nx = snakeX[0] + dirX;
  int ny = snakeY[0] + dirY;

  if (nx < 0 || nx >= COLS ||
      ny < 0 || ny >= ROWS) {
    endGame();
    return;
  }

  bool eating = (nx == foodX && ny == foodY);
  bool growing = eating && snakeLength < MAX_SNAKE;

  int checkLength = snakeLength - (growing ? 0 : 1);

  for (int i = 0; i < checkLength; i++) {
    if (snakeX[i] == nx && snakeY[i] == ny) {
      endGame();
      return;
    }
  }

  int tailX = snakeX[snakeLength - 1];
  int tailY = snakeY[snakeLength - 1];

  // Old head becomes body
  snakeCell(snakeX[0], snakeY[0], ST77XX_GREEN);

  if (growing) {
    snakeLength++;
  } else {
    snakeCell(tailX, tailY, ST77XX_BLACK);
  }

  for (int i = snakeLength - 1; i > 0; i--) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }

  snakeX[0] = nx;
  snakeY[0] = ny;

  snakeCell(nx, ny, ST77XX_YELLOW);

  if (eating) {
    score += 10;
    beep(1800, 50);
    drawHeader(F("SNAKE"));

    if (snakeLength >= MAX_SNAKE) {
      endGame(true);
    } else {
      spawnFood();
    }
  }
}

// ================= SPACE SHOOTER =================
const int SHIP_HALF_WIDTH = 6;
const int SHIP_TOP = SCREEN_HEIGHT - 22;
const int SHIP_BOTTOM = SCREEN_HEIGHT - 9;
const int ENEMY_HALF_WIDTH = 5;
const int ENEMY_HEIGHT = 7;
const int ENEMY_START_Y = HEADER_HEIGHT + 4;
const int BULLET_HALF_WIDTH = 1;
const int BULLET_HEIGHT = 5;

int shipX = SCREEN_WIDTH / 2;
int enemyX = SCREEN_WIDTH / 2;
int enemyY = ENEMY_START_Y;

int bulletX = 0;
int bulletY = -1;

unsigned long lastShot = 0;

void drawShip(int x, uint16_t color) {
  tft.fillTriangle(
    x, SHIP_TOP,
    x - SHIP_HALF_WIDTH, SHIP_BOTTOM,
    x + SHIP_HALF_WIDTH, SHIP_BOTTOM,
    color
  );
}

void drawEnemy(int x, int y, uint16_t color) {
  tft.fillRect(x - ENEMY_HALF_WIDTH, y, ENEMY_HALF_WIDTH * 2, ENEMY_HEIGHT, color);
}

void drawBullet(int x, int y, uint16_t color) {
  tft.fillRect(x - BULLET_HALF_WIDTH, y, BULLET_HALF_WIDTH * 2, BULLET_HEIGHT, color);
}

void startShooter() {
  screen = SHOOTER;
  startScreen();

  shipX = SCREEN_WIDTH / 2;
  enemyX = random(10, SCREEN_WIDTH - 10);
  enemyY = ENEMY_START_Y;

  bulletY = -1;
  lastShot = 0;

  drawHeader(F("SHOOTER"));
  drawShip(shipX, ST77XX_CYAN);
  drawEnemy(enemyX, enemyY, ST77XX_RED);
}

void updateShooter(int joyX, bool shooting) {
  if (millis() - lastTick < 40) return;
  lastTick = millis();

  // Erase only moving objects
  drawShip(shipX, ST77XX_BLACK);
  drawEnemy(enemyX, enemyY, ST77XX_BLACK);

  if (bulletY >= HEADER_HEIGHT) {
    drawBullet(bulletX, bulletY, ST77XX_BLACK);
  }

  // Move spaceship
  if (joyX < 350) shipX -= 3;
  if (joyX > 670) shipX += 3;

  shipX = constrain(shipX, SHIP_HALF_WIDTH, SCREEN_WIDTH - 1 - SHIP_HALF_WIDTH);

  // Shoot
  if (shooting && bulletY < 0 &&
      millis() - lastShot > 170) {
    bulletX = shipX;
    bulletY = SHIP_TOP - BULLET_HEIGHT;
    lastShot = millis();
    beep(1700, 15);
  }

  if (bulletY >= 0) {
    bulletY -= 5;
    if (bulletY < HEADER_HEIGHT) bulletY = -1;
  }

  // Enemy speed increases with score
  int enemySpeed = min(5, 1 + (int)score / 60);
  enemyY += enemySpeed;

  // Bullet collision
  if (bulletY >= 0 &&
      abs(bulletX - enemyX) < ENEMY_HALF_WIDTH + BULLET_HALF_WIDTH &&
      bulletY < enemyY + ENEMY_HEIGHT &&
      bulletY + BULLET_HEIGHT > enemyY) {

    score += 10;
    beep(2100, 45);

    enemyX = random(10, SCREEN_WIDTH - 10);
    enemyY = ENEMY_START_Y;
    bulletY = -1;

    drawHeader(F("SHOOTER"));
  }

  // Enemy reaches player area
  if (enemyY + ENEMY_HEIGHT >= SHIP_TOP) {
    endGame();
    return;
  }

  drawShip(shipX, ST77XX_CYAN);
  drawEnemy(enemyX, enemyY, ST77XX_RED);

  if (bulletY >= HEADER_HEIGHT) {
    drawBullet(bulletX, bulletY, ST77XX_YELLOW);
  }
}

// ================= BRICK BREAKER =================
const byte BRICK_ROWS = 4;
const byte BRICK_COLS = 8;
const int BRICK_LEFT = 2;
const int BRICK_TOP = HEADER_HEIGHT + 4;
const int BRICK_X_STEP = SCREEN_WIDTH / BRICK_COLS;
const int BRICK_Y_STEP = 8;
const int BRICK_WIDTH = BRICK_X_STEP - 3;
const int BRICK_HEIGHT = 6;
const int PADDLE_HALF_WIDTH = 14;
const int PADDLE_TOP = SCREEN_HEIGHT - 10;
const int PADDLE_HEIGHT = 4;
const int BALL_RADIUS = 2;
const int BALL_START_Y = 80;

bool bricks[BRICK_ROWS][BRICK_COLS];

int paddleX = SCREEN_WIDTH / 2;
int ballX = SCREEN_WIDTH / 2, ballY = BALL_START_Y;

int ballVX = 2, ballVY = -2;
int ballStep = 0;

void drawPaddle(int x, uint16_t color) {
  tft.fillRect(x - PADDLE_HALF_WIDTH, PADDLE_TOP, PADDLE_HALF_WIDTH * 2, PADDLE_HEIGHT, color);
}

void drawBall(int x, int y, uint16_t color) {
  tft.fillCircle(x, y, BALL_RADIUS, color);
}

void drawBrick(byte r, byte c, uint16_t color) {
  tft.fillRect(
    BRICK_LEFT + c * BRICK_X_STEP,
    BRICK_TOP + r * BRICK_Y_STEP,
    BRICK_WIDTH, BRICK_HEIGHT,
    color
  );
}

void startBreakout() {
  screen = BREAKOUT;
  startScreen();

  paddleX = SCREEN_WIDTH / 2;
  ballX = SCREEN_WIDTH / 2;
  ballY = BALL_START_Y;

  ballVX = 2;
  ballVY = -2;
  ballStep = 0;

  drawHeader(F("BRICKS"));

  for (byte r = 0; r < BRICK_ROWS; r++) {
    for (byte c = 0; c < BRICK_COLS; c++) {
      bricks[r][c] = true;
      drawBrick(
        r, c,
        r % 2 ? ST77XX_ORANGE : ST77XX_GREEN
      );
    }
  }

  drawPaddle(paddleX, ST77XX_CYAN);
  drawBall(ballX, ballY, ST77XX_WHITE);
}

void updateBreakout(int joyX) {
  // Increase update frequency as score increases
  unsigned long interval =
    max(14L, 35L - (long)score / 25);

  if (millis() - lastTick < interval) return;
  lastTick = millis();

  // Erase only ball and paddle
  drawBall(ballX, ballY, ST77XX_BLACK);
  drawPaddle(paddleX, ST77XX_BLACK);

  if (joyX < 350) paddleX -= 3;
  if (joyX > 670) paddleX += 3;

  paddleX = constrain(paddleX, PADDLE_HALF_WIDTH, SCREEN_WIDTH - PADDLE_HALF_WIDTH);

  // Move ball
  ballX += ballVX;
  ballY += ballVY;

  // Wall collisions
  if (ballX <= BALL_RADIUS) {
    ballX = BALL_RADIUS;
    ballVX = abs(ballVX);
  }

  if (ballX >= SCREEN_WIDTH - 1 - BALL_RADIUS) {
    ballX = SCREEN_WIDTH - 1 - BALL_RADIUS;
    ballVX = -abs(ballVX);
  }

  if (ballY <= HEADER_HEIGHT + BALL_RADIUS) {
    ballY = HEADER_HEIGHT + BALL_RADIUS;
    ballVY = abs(ballVY);
  }

  // Paddle collision
  if (ballVY > 0 &&
      ballY + BALL_RADIUS >= PADDLE_TOP &&
      ballY - BALL_RADIUS < PADDLE_TOP + PADDLE_HEIGHT &&
      abs(ballX - paddleX) <= PADDLE_HALF_WIDTH + BALL_RADIUS) {

    ballY = PADDLE_TOP - BALL_RADIUS - 1;
    ballVY = -abs(ballVY);

    // Change horizontal direction based on impact
    int offset = ballX - paddleX;

    if (offset < -5) ballVX = -abs(ballVX);
    if (offset > 5) ballVX = abs(ballVX);

    beep(1100, 25);
  }

  if (ballY - BALL_RADIUS >= SCREEN_HEIGHT) {
    endGame();
    return;
  }

  // Brick collision
  bool hitBrick = false;

  for (byte r = 0; r < BRICK_ROWS && !hitBrick; r++) {
    for (byte c = 0; c < BRICK_COLS; c++) {
      if (!bricks[r][c]) continue;

      int bx = BRICK_LEFT + c * BRICK_X_STEP;
      int by = BRICK_TOP + r * BRICK_Y_STEP;

      if (ballX + BALL_RADIUS >= bx &&
          ballX - BALL_RADIUS < bx + BRICK_WIDTH &&
          ballY + BALL_RADIUS >= by &&
          ballY - BALL_RADIUS < by + BRICK_HEIGHT) {

        bricks[r][c] = false;
        drawBrick(r, c, ST77XX_BLACK);

        ballVY = -ballVY;
        score += 10;
        hitBrick = true;

        beep(1800, 40);
        drawHeader(F("BRICKS"));
        break;
      }
    }
  }

  // All bricks destroyed
  if (score >= BRICK_ROWS * BRICK_COLS * 10) {
    endGame(true);
    return;
  }

  // Draw new positions
  drawPaddle(paddleX, ST77XX_CYAN);
  drawBall(ballX, ballY, ST77XX_WHITE);
}

// ================= YOUTUBE MUSIC USB REMOTE =================
const byte ART_SIZE = 40;
const byte ART_X = 6;
const byte ART_Y = 30;
char musicPacket[4 + 2 + 4 * ART_SIZE + 1];
byte musicPacketLength = 0;

void musicText(byte x, byte y, const __FlashStringHelper* text) {
  tft.setCursor(x, y);
  tft.print(text);
}

void clearMusicArt() {
  tft.fillRect(ART_X, ART_Y, ART_SIZE, ART_SIZE, DARK_GREY);
  tft.setTextColor(ST77XX_WHITE);
  musicText(ART_X + 11, ART_Y + 17, F("ART"));
}

void startMusic() {
  screen = MUSIC;
  musicJoystickReady = false;
  lastVolumeTurn = millis();
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, SCREEN_WIDTH, HEADER_HEIGHT, ST77XX_BLUE);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  musicText(6, 6, F("YOUTUBE MUSIC"));
  clearMusicArt();
  musicText(52, 32, F("Waiting for"));
  musicText(52, 42, F("Mac helper"));
  musicText(133, 25, F("VOL"));
  tft.drawRect(141, 37, 12, 48, ST77XX_WHITE);
  musicText(6, 114, F("K0 Back | USB remote"));
  Serial.println(F("YTM:OPEN"));
}

int8_t musicHex(char digit) {
  if (digit >= '0' && digit <= '9') return digit - '0';
  if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
  return -1;
}

void drawMusicArtRow(const char* data) {
  const int row = (data[0] - '0') * 10 + (data[1] - '0');
  if (data[0] < '0' || data[0] > '3' ||
      data[1] < '0' || data[1] > '9' || row >= ART_SIZE) return;
  uint16_t pixels[ART_SIZE];
  data += 2;
  for (byte x = 0; x < ART_SIZE; x++) {
    uint16_t color = 0;
    for (byte digit = 0; digit < 4; digit++) {
      const int8_t value = musicHex(*data++);
      if (value < 0) return;
      color = (color << 4) | value;
    }
    pixels[x] = color;
  }
  tft.drawRGBBitmap(ART_X, ART_Y + row, pixels, ART_SIZE, 1);
}

void drawMusicTitle(const char* data) {
  tft.fillRect(52, 31, 78, 30, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(52, 32);
  byte row = 0;
  byte column = 0;
  while (*data && row < 3) {
    const char letter = *data++;
    if (letter == '|') {
      row++;
      column = 0;
      tft.setCursor(52, 32 + 10 * row);
    } else if (column < 13) {
      tft.write(letter);
      column++;
    }
  }
}

void drawMusicState(const char* data) {
  const int volume = (data[0] - '0') * 100 + (data[1] - '0') * 10 + data[2] - '0';
  const bool muted = data[6] == '1';
  tft.fillRect(6, 76, 125, 30, ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  musicText(6, 77, data[3] == 'P' ? F("Playing") :
                   data[3] == 'A' ? F("Paused") : F("No track"));
  tft.setTextColor(ST77XX_WHITE);
  musicText(6, 87, F("Shuffle: "));
  tft.print(data[4] == '1' ? F("On") : data[4] == '0' ? F("Off") : F("--"));
  musicText(6, 97, F("Repeat: "));
  tft.print(data[5] == 'A' ? F("All") : data[5] == '1' ? F("One") :
            data[5] == '0' ? F("Off") : F("--"));

  tft.fillRect(142, 38, 10, 46, ST77XX_BLACK);
  tft.fillRect(130, 89, 30, 9, ST77XX_BLACK);
  tft.setCursor(130, 89);
  if (volume >= 0 && volume <= 100) {
    const byte height = (volume * 46 + 50) / 100;
    if (height) tft.fillRect(142, 84 - height, 10, height,
                             muted ? DARK_GREY : ST77XX_GREEN);
    if (muted) tft.print(F("MUTE"));
    else {
      tft.print(volume);
      tft.print('%');
    }
  } else {
    tft.print(F("--"));
  }
}

void receiveMusicPacket() {
  // The Mac waits for an ACK before writing another packet.
  while (Serial.available()) {
    const char value = Serial.read();
    if (value == '@') {
      musicPacketLength = 0;
      musicPacket[musicPacketLength++] = value;
    } else if (value == '\n') {
      if (musicPacketLength >= 4 && screen == MUSIC) {
        musicPacket[musicPacketLength] = '\0';
        const char* data = musicPacket + 4;
        const byte length = musicPacketLength - 4;
        switch (musicPacket[3]) {
          case 'C': clearMusicArt(); break;
          case 'T': drawMusicTitle(data); break;
          case 'N':
            tft.fillRect(52, 62, 78, 8, ST77XX_BLACK);
            tft.setTextColor(ST77XX_YELLOW);
            tft.setCursor(52, 62);
            for (byte i = 0; i < length && i < 13; i++) tft.write(data[i]);
            break;
          case 'S':
            if (length == 7) drawMusicState(data);
            break;
          case 'R':
            if (length == 2 + 4 * ART_SIZE) drawMusicArtRow(data);
            break;
        }
        Serial.print(F("YTM:ACK"));
        Serial.write(musicPacket + 1, 2);
        Serial.println();
      }
      musicPacketLength = 0;
    } else if (musicPacketLength && musicPacketLength < sizeof(musicPacket) - 1) {
      musicPacket[musicPacketLength++] = value;
    } else {
      musicPacketLength = 0;
    }
  }
}

void updateMusic(int joyX, int joyY, bool joyPressed, bool encPressed,
                 int encCLK, unsigned long now) {
  const int horizontal = joyX - 512;
  const int vertical = joyY - 512;

  // A held direction sends once. Returning to center re-arms the stick.
  if (abs(horizontal) < 110 && abs(vertical) < 110) {
    musicJoystickReady = true;
  } else if (musicJoystickReady &&
             (abs(horizontal) > 210 || abs(vertical) > 210)) {
    musicJoystickReady = false;
    if (abs(horizontal) > abs(vertical)) {
      Serial.println(horizontal < 0 ? F("YTM:PREVIOUS") : F("YTM:NEXT"));
    } else {
      Serial.println(vertical < 0 ? F("YTM:SHUFFLE") : F("YTM:REPEAT"));
    }
  }

  if (oldEncCLK == HIGH && encCLK == LOW && now - lastVolumeTurn > 35) {
    Serial.println(digitalRead(ENC_DT) == HIGH ? F("YTM:LOUDER") : F("YTM:QUIETER"));
    lastVolumeTurn = now;
  }

  if ((joyPressed || encPressed) && now - lastPress > 180) {
    Serial.println(joyPressed ? F("YTM:PLAY_PAUSE") : F("YTM:MUTE"));
    lastPress = now;
  }
}

// ================= PLACEHOLDERS =================
void startPlaceholder() {
  screen = PLACEHOLDER;
  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);

  tft.setCursor(20, 34);
  tft.print(menuItems[selected]);

  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 55);
  tft.print(F("COMING SOON"));

  tft.setCursor(20, 100);
  tft.print(F("Press BACK to return"));
}

// ================= START SELECTED =================
void startSelected() {
  beep(1500, 60);

  switch (selected) {
    case 0:
      startSnake();
      break;
    case 1:
      startShooter();
      break;
    case 2:
      startBreakout();
      break;
    case 5:
      startMusic();
      break;
    default:
      startPlaceholder();
      break;
  }
}

void restartGame() {
  switch (screen) {
    case SNAKE:
      startSnake();
      break;
    case SHOOTER:
      startShooter();
      break;
    case BREAKOUT:
      startBreakout();
      break;
    default:
      break;
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  pinMode(JOY_SW, INPUT_PULLUP);
  // The TFT module pulls its encoder and button signals up to its supply.
  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT, INPUT);
  pinMode(ENC_SW, INPUT);
  pinMode(BACK_BTN, INPUT);
  pinMode(BUZZER, OUTPUT);

  oldEncCLK = digitalRead(ENC_CLK);

  randomSeed(analogRead(A2));

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.setTextWrap(false);

  screen = MENU;
  drawMenu();
}

// ================= LOOP =================
void loop() {
  receiveMusicPacket();
  unsigned long now = millis();

  // This joystick orientation reports higher values when pushed left.
  int joyX = 1023 - analogRead(JOY_X);
  int joyY = analogRead(JOY_Y);

  bool joySW = digitalRead(JOY_SW);
  bool encSW = digitalRead(ENC_SW);
  bool backSW = digitalRead(BACK_BTN);

  int encCLK = digitalRead(ENC_CLK);

  bool joyPressed =
    joySW == LOW && oldJoySW == HIGH;

  bool encPressed =
    encSW == LOW && oldEncSW == HIGH;

  bool backPressed =
    backSW == LOW && oldBackSW == HIGH;

  oldJoySW = joySW;
  oldEncSW = encSW;
  oldBackSW = backSW;

  // K0 returns to the menu from any screen.
  if (backPressed && screen != MENU) {
    if (screen == MUSIC) Serial.println(F("YTM:CLOSE"));
    screen = MENU;
    beep(1000, 40);
    drawMenu();
    lastPress = now;
    oldEncCLK = encCLK;
    return;
  }

  if (screen == MUSIC) {
    updateMusic(joyX, joyY, joyPressed, encPressed, encCLK, now);
  } else if (screen == MENU) {
    // Joystick menu movement
    if (now - lastMenuMove > 220) {
      if (joyY < 300) {
        moveMenu(-1);
        lastMenuMove = now;
      } else if (joyY > 700) {
        moveMenu(1);
        lastMenuMove = now;
      }
    }

    // Rotary encoder menu movement
    if (oldEncCLK == HIGH &&
        encCLK == LOW &&
        now - lastMenuMove > 70) {

      if (digitalRead(ENC_DT) == HIGH) {
        moveMenu(1);
      } else {
        moveMenu(-1);
      }

      lastMenuMove = now;
    }

    // Select
    if ((joyPressed || encPressed) &&
        now - lastPress > 180) {
      startSelected();
      lastPress = now;
    }
  } else {
    if (gameOver) {
      if ((joyPressed || encPressed) &&
          now - lastPress > 180) {
        restartGame();
        lastPress = now;
      }
    } else {
      switch (screen) {
        case SNAKE:
          updateSnake(joyX, joyY);
          break;

        case SHOOTER:
          updateShooter(joyX, joySW == LOW);
          break;

        case BREAKOUT:
          updateBreakout(joyX);
          break;

        default:
          break;
      }
    }
  }

  oldEncCLK = encCLK;
  delay(3);
}
