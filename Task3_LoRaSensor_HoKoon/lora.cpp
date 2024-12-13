#include <cstring>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>

#include <LoRa.h>
#include <RNG.h>
#include <AES.h>
#include <GCM.h>

#include "id.h"
#include "display.h"
#include "device.h"
#include "inet.h"
#include "daemon.h"
#include "lora.h"

/* ************************************************************************** */

/* Protocol Constants */
#define PACKET_TIME    0
#define PACKET_ASKTIME 1
#define PACKET_ACK     2
#define PACKET_SEND    3

typedef uint8_t PacketType;

/* Cipher parameters */
#define CIPHER_IV_LENGTH 12
#define CIPHER_TAG_SIZE 4

typedef GCM<AES128> AuthCipher;

static Device const router_topology[][2] = ROUTER_TOPOLOGY;
static PROGMEM char const secret_key[16] = SECRET_KEY;

template<typename TO, typename FROM>
static inline TO *pointer_offset(FROM *const from, size_t const offset) {
	return reinterpret_cast<TO *>(reinterpret_cast<char *>(from) + offset);
}

template<typename TO, typename FROM>
static inline TO const *pointer_offset(FROM const *const from, size_t const offset) {
	return reinterpret_cast<TO const *>(reinterpret_cast<char const *>(from) + offset);
}

namespace LORA {
	static Device last_receiver = 0;

	Millisecond last_time = 0;

	bool initialize(void) {
		SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
		LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

		if (!enable_gateway) {
			size_t const N = sizeof router_topology / sizeof *router_topology;
			size_t i = 0;
			for (size_t i = 0;; ++i) {
				if (i >= N) {
					last_receiver = 0;
					break;
				}
				if (router_topology[i][1] == my_device_id) {
					last_receiver = router_topology[i][0];
					break;
				}
				++i;
			}
		}

		if (!LoRa.begin(LORA_BAND)) {
			OLED_LOCK(oled_lock);
			Display::println("LoRa uninitialized");
			return false;
		}

		LoRa.enableCrc();
		last_time = millis();
		OLED_LOCK(oled_lock);
		Display::println("LoRa initialized");

		#if defined(ENABLE_COM_OUTPUT)
			char buffer[16];
			COM::print("SECRET_KEY =");
			for (unsigned int i = 0; i < sizeof secret_key; ++i) {
				sprintf(buffer, " %02X", secret_key[i]);
				COM::print(buffer);
			}
			COM::println('.');
		#endif

		return true;
	}

	void sleep(void) {
		LoRa.sleep();
	}

	void wake(void) {
		LoRa.idle();
	}

	namespace Send {
		static bool packet(
			char const *const message,
			PacketType const packet_type,
			Device const device,
			void const *const payload,
			size_t const size)
		{
			{
				DEBUG_LOCK(debug_lock);
				Debug::print("DEBUG: LORA::Send::packet ");
				Debug::dump(message, payload, size);
				Debug::flush();
			}

			AuthCipher cipher;
			if (!cipher.setKey(reinterpret_cast<uint8_t const *>(secret_key), sizeof secret_key)) {
				COM::print("LoRa ");
				COM::print(message);
				COM::println(": unable to set key");
				OLED_LOCK(oled_lock);
				OLED::println("Unable to set key");
				return false;
			}

			uint8_t nonce[CIPHER_IV_LENGTH];
			RNG.rand(nonce, sizeof nonce);
			if (!cipher.setIV(nonce, sizeof nonce)) {
				COM::print("LoRa ");
				COM::print(message);
				COM::println(": unable to set nonce");
				OLED_LOCK(oled_lock);
				OLED::println("Unable to set nonce");
				return false;
			}

			std::vector<uint8_t> ciphertext(size);
			cipher.encrypt(ciphertext.data(), reinterpret_cast<uint8_t const *>(payload), size);
			uint8_t tag[CIPHER_TAG_SIZE];
			cipher.computeTag(tag, sizeof tag);

			DEVICE_LOCK(device_lock);
			LoRa.beginPacket();
			LoRa.write(packet_type);
			LoRa.write(device);
			LoRa.write(nonce, sizeof nonce);
			LoRa.write(ciphertext.data(), ciphertext.size());
			LoRa.write(tag, sizeof tag);
			LoRa.endPacket();
			return true;
		}

		void TIME(struct FullTime const *const fulltime) {
			packet("TIME", PACKET_TIME, my_device_id, fulltime, sizeof *fulltime);
		}

		void ASKTIME(void) {
			packet("ASKTIME", PACKET_ASKTIME, last_receiver, &my_device_id, sizeof my_device_id);
		}

		void SEND(Device const receiver, SerialNumber const serial, Data const *const data) {
			{
				DEBUG_LOCK(debug_lock);
				Debug::print("DEBUG: LORA::Send::SEND ");
				#if !defined(NDEBUG) && defined(ENABLE_COM_OUTPUT)
					data->writeln(&Serial);
				#endif
			}
			char content[static_cast<size_t>(2 * sizeof my_device_id + sizeof serial + sizeof *data)];
			std::memcpy(content, &my_device_id, sizeof my_device_id);
			std::memcpy(content + sizeof my_device_id, &my_device_id, sizeof my_device_id);
			std::memcpy(content + 2 * sizeof my_device_id, &serial, sizeof serial);
			std::memcpy(content + 2 * sizeof my_device_id + sizeof serial, data, sizeof *data);
			packet("SEND", PACKET_SEND, receiver, content, sizeof content);
		}
	}

	namespace Receive {
		static void TIME(Device const device, std::vector<uint8_t> const &content) {
			if (!enable_gateway) {
				if (content.size() != sizeof (struct FullTime)) return;
				struct FullTime const *const time = reinterpret_cast<struct FullTime const *>(content.data());

				if (device != Device(0)) { /* always accept TIME packet from gateway */
					size_t i = 0;
					for (;;) {
						if (i >= sizeof router_topology / sizeof *router_topology) return;
						if (router_topology[i][0] == my_device_id && router_topology[i][1] == device) break;
						++i;
					}
				}

				RTC::set(time);
				DAEMON::AskTime::synchronized();
				Send::packet("TIME+", PACKET_TIME, my_device_id, time, sizeof *time);
			}
		}

		static void ASKTIME(Device const device, std::vector<uint8_t> const &content) {
			if (enable_gateway) {
				if (content.size() != sizeof (Device)) {
					COM::print("WARN: LoRa ASKTIME: incorrect packet size: ");
					COM::println(content.size());
					return;
				}
				if (device != my_device_id) return;
				Device const sender = *reinterpret_cast<Device const *>(content.data());
				if (!(sender > 0 && sender < number_of_device)) {
					COM::print("WARN: LoRa ASKTIME: incorrect device: ");
					COM::println(device);
					return;
				}
				{
					DEBUG_LOCK(debug_lock);
					Debug::print("DEBUG: LORA::Receive::ASKTIME ");
					Debug::println(sender);
				}
				DAEMON::Time::run();
			}
		}

		static void SEND(Device const receiver, std::vector<uint8_t> const &content) {
			size_t const minimal_content_size =
				sizeof (Device)         /* terminal */
				+ sizeof (Device)       /* router list length >= 1 */
				+ sizeof (SerialNumber) /* serial code */
				+ sizeof (struct Data); /* data */
			if (enable_gateway) {
				if (!(content.size() >= minimal_content_size)) {
					COM::print("WARN: LoRa SEND: incorrect packet size: ");
					COM::println(content.size());
					return;
				}

				Device const device = *reinterpret_cast<Device const *>(content.data());
				if (!(device > 0 && device < number_of_device)) {
					COM::print("WARN: LoRa SEND: incorrect device: ");
					COM::println(device);
					return;
				}

				size_t routers_length = sizeof (Device);
				for (;;) {
					if (routers_length >= content.size()) {
						COM::println("WARN: LoRa SEND: incorrect router list");
						return;
					}
					Device const router =
						*reinterpret_cast<Device const *>(
							content.data()
							+ routers_length
						);
					if (router == device) break;
					routers_length += sizeof router;
				}
				size_t const excat_content_size =
					minimal_content_size
					+ routers_length * sizeof (Device)
					- sizeof (Device);
				if (content.size() != excat_content_size) {
					COM::print("WARN: LoRa SEND: incorrect packet size or router list: ");
					COM::print(content.size());
					COM::print(" / ");
					COM::println(routers_length);
					return;
				}

				SerialNumber const serial =
					*reinterpret_cast<SerialNumber const *>(
						content.data()
						+ sizeof (Device)
						+ routers_length
					);
				size_t const overhead_size =
					sizeof (Device) * (1 + routers_length)
					+ sizeof (SerialNumber);
				struct Data const data =
					*reinterpret_cast<struct Data const *>(
						content.data()
						+ overhead_size
					);
				{
					OLED_LOCK(oled_lock);
					OLED::home();
					OLED::print("Receive ");
					OLED::print(device);
					OLED::print(" #");
					OLED::println(serial);
					data.println();
					OLED::display();
				}

				class WIFI::upload__result const upload_result = WIFI::upload(device, serial, &data);
				{
					OLED_LOCK(oled_lock);
					OLED::display();
				}
				if (!upload_result.upload_success) return;

				Device const router = *reinterpret_cast<Device const *>(content.data() + sizeof device);
				if (upload_result.update_configuration) {
					std::vector<uint8_t> ack(overhead_size + sizeof upload_result.configuration);
					std::memcpy(ack.data(), content.data(), overhead_size);
					std::memcpy(ack.data() + overhead_size, &upload_result.configuration, sizeof upload_result.configuration);
					Send::packet("ACK", PACKET_ACK, router, ack.data(), ack.size());
				}
				else
					Send::packet("ACK", PACKET_ACK, router, content.data(), overhead_size);
			}
			else {
				size_t const minimal_content_size =
					sizeof (Device)         /* terminal */
					+ sizeof (Device)       /* router list length >= 1 */
					+ sizeof (SerialNumber) /* serial code */
					+ sizeof (struct Data); /* data */

				if (!(content.size() >= minimal_content_size)) {
					COM::print("WARN: LoRa SEND: incorrect packet size: ");
					COM::println(content.size());
					return;
				}

				Device const receiver = *reinterpret_cast<Device const *>(content.data());
				if (receiver != my_device_id) return;

				std::vector<uint8_t> bounce(content.size() + sizeof (Device));
				std::memcpy(bounce.data() + sizeof (Device), content.data(), content.size());
				std::memcpy(bounce.data(), content.data(), sizeof (Device));
				std::memcpy(bounce.data() + sizeof (Device), &receiver, sizeof receiver);
				Send::packet("SEND+", PACKET_SEND, last_receiver, bounce.data(), bounce.size());
			}
		}

		static void ACK(Device const receiver, std::vector<uint8_t> const &content) {
			if (!enable_gateway) {
				size_t const minimal_content_size =
					sizeof (Device)          /* terminal */
					+ sizeof (Device)        /* router list length >= 1 */
					+ sizeof (SerialNumber); /* serial code */
				size_t const content_size = content.size();
				if (!(content_size >= minimal_content_size)) {
					COM::print("WARN: LoRa ACK: incorrect packet size: ");
					COM::println(content_size);
					return;
				}

				if (my_device_id != receiver) return;

				Device const terminal = *reinterpret_cast<Device const *>(content.data());
				Device const router0 = *reinterpret_cast<Device const *>(content.data() + sizeof (Device));
				if (my_device_id == terminal) {
					if (my_device_id != router0) {
						COM::print("WARN: LoRa ACK: dirty router list");
						return;
					}

					SerialNumber const serial =
						*reinterpret_cast<SerialNumber const *>(
							content.data()
							+ 2 * sizeof (Device)
						);
					{
						DEBUG_LOCK(debug_lock);
						Debug::print("DEBUG: LORA::Receive::ACK serial=");
						Debug::println(serial);
					}
					DAEMON::Push::ack(serial);
					{
						OLED_LOCK(olec_lock);
						OLED::draw_received();
					}

					if (content_size >= minimal_content_size + sizeof (class Configuration)) {
						class Configuration const configuration =
							*reinterpret_cast<class Configuration const *>(
								content.data()
								+ 2 * sizeof (Device)
								+ sizeof serial
							);
						configuration.apply();
					}
				}
				else {
					size_t const Device2 = 2 * sizeof (Device);
					Device const router1 = *reinterpret_cast<Device const *>(content.data() + Device2);
					{
						DEBUG_LOCK(debug_lock);
						Debug::print("DEBUG: LORA::Receive::ACK router=");
						Debug::print(router1);
						Debug::print(" terminal=");
						Debug::println(terminal);
					}
					std::vector<char> bounce(content.size() - sizeof terminal);
					std::memcpy(bounce.data(), &terminal, sizeof terminal);
					std::memcpy(bounce.data() + sizeof terminal, content.data() + Device2, content.size() - Device2);
					LORA::Send::packet("ACK+", PACKET_ACK, router1, bounce.data(), bounce.size());
				}
			}
		}

		static void decode(std::vector<uint8_t> const packet) {
			static size_t const overhead_size = sizeof (PacketType) + sizeof (Device) + CIPHER_IV_LENGTH + CIPHER_TAG_SIZE;
			if (packet.size() < overhead_size) {
				DEBUG_LOCK(debug_lock);
				Debug::print("DEBUG: LORA::Receive::decode packet too short ");
				Debug::println(packet.size());
				return;
			}

			PacketType const *const packet_type = packet.data();
			Device const *const device = pointer_offset<Device>(packet_type, sizeof *packet_type);
			uint8_t const *const nonce = pointer_offset<uint8_t>(device, sizeof *device);
			size_t const content_size = packet.size() - overhead_size;
			uint8_t const *const ciphertext = nonce + CIPHER_IV_LENGTH;
			uint8_t const *const tag = ciphertext + content_size;
			if (!((char *)packet.data() + sizeof (PacketType) + sizeof (Device) == (char *)nonce)) {
				DEBUG_LOCK(debug_lock);
				Debug::println("DEBUG: LORA::Receive::decode incorrect nonce position");
				return;
			}
			if (!((char *)packet.data() + packet.size() == (char *)tag + CIPHER_TAG_SIZE)) {
				DEBUG_LOCK(debug_lock);
				Debug::println("DEBUG: LORA::Receive::decode incorrect content size");
				return;
			}

			switch (*packet_type) {
				case PACKET_TIME:
				case PACKET_ASKTIME:
				case PACKET_SEND:
				case PACKET_ACK:
					break;
				default:
					DEBUG_LOCK(debug_lock);
					Debug::print("DEBUG: LORA::Receive::decode unknown packet type ");
					Debug::println(*packet_type);
					return;
			}

			if (!(*device >= 0 && *device < number_of_device)) {
				DEBUG_LOCK(debug_lock);
				Debug::print("DEBUG: LORA::Receive::decode unknown device ");
				Debug::println(*device);
				return;
			}

			AuthCipher cipher;
			std::vector<uint8_t> cleantext(content_size);
			if (!cipher.setKey((uint8_t const *)secret_key, sizeof secret_key)) {
				COM::print("ERROR: LORA::Receive::decode ");
				COM::print(*packet_type);
				COM::println(" fail to set cipher key");
				OLED::println(String("LoRa ") + *packet_type + ": fail to set cipher key");
				return;
			}
			if (!cipher.setIV(nonce, CIPHER_IV_LENGTH)) {
				COM::print("ERROR: LORA::Receive::decode ");
				COM::print(*packet_type);
				COM::println(" fail to set cipher nonce");
				OLED::println(String("LoRa ") + *packet_type + ": fail to set cipher nonce");
				return;
			}
			cipher.decrypt(cleantext.data(), ciphertext, content_size);
			if (!cipher.checkTag(tag, sizeof tag)) {
				Debug::print("DEBUG: LORA::Receive::decode ");
				Debug::print(*packet_type);
				Debug::println(" invalid cipher tag");
				return;
			}
			{
				DEBUG_LOCK(debug_lock);
				Debug::dump("DEBUG: LORA::Receive::decode", cleantext.data(), cleantext.size());
			}

			switch (*packet_type) {
			case PACKET_TIME:
				{
					DEBUG_LOCK(debug_lock);
					Debug::println("DEBUG: LORA::Receive::packet TIME");
				}
				TIME(*device, cleantext);
				break;
			case PACKET_ASKTIME:
				{
					DEBUG_LOCK(debug_lock);
					Debug::println("DEBUG: LORA::Receive::packet ASKTIME");
				}
				ASKTIME(*device, cleantext);
				break;
			case PACKET_SEND:
				{
					DEBUG_LOCK(debug_lock);
					Debug::println("DEBUG: LORA::Receive::packet SEND");
				}
				SEND(*device, cleantext);
				break;
			case PACKET_ACK:
				{
					DEBUG_LOCK(debug_lock);
					Debug::println("DEBUG: LORA::Receive::packet ACK");
				}
				ACK(*device, cleantext);
				break;
			default:
				COM::print("ERROR: incorrect LoRa packet type: ");
				COM::println(*packet_type);
			}
		}

		static void decode_thread(std::vector<uint8_t> const packet) {
			struct DAEMON::Alarm alarm;
			DAEMON::Schedule::add_timer(&alarm, "LoRA::Receive::decode");
			try {
				decode(packet);
			}
			catch (...) {
				COM::println("ERROR: exception thrown from LoRa packet decode");
			}
			DAEMON::Schedule::remove_timer(&alarm);
		}

		void packet(void) {
			DEVICE_LOCK(device_lock);
			signed int const parse_size = LoRa.parsePacket();
			if (parse_size < 1) return;
			{
				Debug::print("DEBUG: LORA::Receive::packet packet_size ");
				Debug::println(parse_size);
			}
			size_t const packet_size = static_cast<size_t>(LoRa.available());
			if (packet_size != static_cast<size_t>(parse_size)) {
				Display::println("ERROR: LORA::Receive::packet LoRa.parsePacker != LoRa.available");
				return;
			}
			std::vector<uint8_t> buffer(packet_size);
			if (LoRa.readBytes(buffer.data(), buffer.size()) != buffer.size()) {
				Display::println("ERROR: LORA::Receive::packet unable read data from LoRa");
				return;
			}
			RNG.stir(buffer.data(), buffer.size(), buffer.size() << 2);
			std::thread(decode_thread, buffer).detach();
		}
	}
}

/* ************************************************************************** */
