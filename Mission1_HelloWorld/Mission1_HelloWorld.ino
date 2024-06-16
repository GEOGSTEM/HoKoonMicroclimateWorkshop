void setup(void) {
	Serial.begin(9600, SERIAL_8N1);
}

void loop(void) {
	Serial.print("Group Testing, ");
	Serial.print("Hello World ");
	Serial.print('*');
	Serial.println(123);
	delay(3000);
}

// comments:

/*
void main(void) {
	setup();
	while (true) loop();
}
*/
