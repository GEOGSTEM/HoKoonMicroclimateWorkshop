#ifndef INCLUDE_CONFIG_DEVICE_H
#define INCLUDE_CONFIG_DEVICE_H

/* Hareware */
#define ENABLE_SDCARD
#define ENABLE_LOG_FILE
#define ENABLE_SHT40
#define ENABLE_BME280
#define ENABLE_BATTERY_GAUGE BATTERY_GAUGE_LC709203F
#define ENABLE_CLOCK CLOCK_DS3231

/* LoRa */
#define SECRET_KEY "HoKoon GeogSTEM"
#define ROUTER_TOPOLOGY {}
#define LORA_BAND 923000000 /* Hz */

/* Internet */
#define WIFI_SSID "hokoon-geo"
#define WIFI_PASS "GEo101121"
#define HTTP_UPLOAD_LENGTH 512
/* Ho Koon internal */
#define HTTP_UPLOAD_FORMAT \
	"http://192.168.1.210:8080/REST/upload" \
	"?site=HoKoon&device=%1$u&serial=%2$u&time=%3$s" \
	"&battery_voltage=%4$.2F&battery_percentage=%5$.2F" \
	"&sht_temperature=%6$.1F&sht_humidity=%7$.1F" \
	"&bme_temperature=%8$.1F&bme_pressure=%9$.1F&bme_humidity=%10$.1F"
#define HTTP_AUTHORIZATION_TYPE "BASIC"
#define HTTP_AUTHORIZATION_CODE "THISISTOKEN"
#define HTTP_RESPONE_SIZE 256
#define NTP_SERVER "stdtime.gov.hk"
#define NTP_INTERVAL 12345678UL /* milliseconds */

/* Timimg */
#define START_DELAY 10000UL /* milliseconds */
#define IDLE_INTERVAL 123UL /* milliseconds */
#define ACK_TIMEOUT 1000UL /* milliseconds */
#define RESEND_TIMES 3
#define SEND_INTERVAL 6000UL /* milliseconds */ /* MUST: > ACK_TIMEOUT * (RESEND_TIMES + 1) */
#define MEASURE_INTERVAL 60000UL /* milliseconds */ /* MUST: > SEND_INTERVAL */
#define SEND_IDLE_INTERVAL (SEND_INTERVAL * 2)
#define SYNCHONIZE_INTERVAL 12345678UL /* milliseconds */
#define SYNCHONIZE_TIMEOUT 1000UL /* milliseconds */ /* MUST: > SYNCHONIZE_INTERVAL */
#define CLEANLOG_INTERVAL 21600000UL /* milliseconds */
#define SLEEP_MARGIN 1000UL /* milliseconds */
//	#define REBOOT_TIMEOUT 1800000UL /* milliseconds */
#define DASHBOARD_INTERVAL 5000UL /* milliseconds */
#define DASHBOARD_TIMEZONE 8 /* hours */

/* Display */
#define ENABLE_LED
#define ENABLE_COM_OUTPUT
#define ENABLE_OLED_OUTPUT
#define OLED_ROTATION 3

/* Power saving */
#define ENABLE_SLEEP
#define CPU_FREQUENCY 20 /* MHz */ /* >= 20MHz for LoRa, and >= 80MHz for WiFi */

#endif // INCLUDE_CONFIG_DEVICE_H