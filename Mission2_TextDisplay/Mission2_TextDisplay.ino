#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 SSD1306(128, 64);

void setup(void) {
	pinMode(LED_BUILTIN, OUTPUT);

	SSD1306.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	SSD1306.setRotation(2);
	SSD1306.setTextSize(1);
	SSD1306.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
	SSD1306.invertDisplay(false);
	SSD1306.clearDisplay();
	SSD1306.display();
}

bool light = false;

unsigned int y = 0;

void loop(void) {
	if (light)
		digitalWrite(LED_BUILTIN, HIGH);
	else
		digitalWrite(LED_BUILTIN, LOW);

	SSD1306.clearDisplay();
	SSD1306.setCursor(0, y);
	SSD1306.print("Hello, world!");
	SSD1306.display();

	y = y + 1;
	if (y > 64) {
		y = 0;
		light = !light;
	}
	delay(100);
}
