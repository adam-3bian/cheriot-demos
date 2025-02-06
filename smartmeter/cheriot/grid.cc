// Copyright SCI Semiconductor and CHERIoT Contributors.
// SPDX-License-Identifier: MIT

/*
 * The grid compartment.
 *
 * Maintains a MQTT connection and reports sensor data
 */

#define MALLOC_QUOTA 24000

#include "common.hh"

#include <NetAPI.h>
#include <cstdlib>
#include <debug.hh>
#include <errno.h>
#include <fail-simulator-on-error.h>
#include <futex.h>
#include <mqtt.h>
#include <sntp.h>
#include <thread.h>
#include <tick_macros.h>

#include "mosquitto.org.h"

using CHERI::Capability;

using Debug = ConditionalDebug<true, "grid">;

/// Maximum permitted MQTT client identifier length (from the MQTT
/// specification)
constexpr size_t MQTTMaximumClientLength = 23;
/// Prefix for MQTT client identifier
constexpr std::string_view clientIDPrefix{"cheriotMQTT"};
/// Space for the random client ID.
std::array<char, MQTTMaximumClientLength> clientID;
static_assert(clientIDPrefix.size() < clientID.size());

// MQTT network buffer sizes
constexpr const size_t networkBufferSize    = 1024;
constexpr const size_t incomingPublishCount = 2;
constexpr const size_t outgoingPublishCount = 2;

// MQTT test broker: https://test.mosquitto.org/
// Note: port 8883 is encrypted and unautenticated
DECLARE_AND_DEFINE_CONNECTION_CAPABILITY(MosquittoOrgMQTT,
                                         "test.mosquitto.org",
                                         8883,
                                         ConnectionTypeTCP);

constexpr std::string_view outageTopic{"cheriot-smartmeter/g/outage"};
constexpr std::string_view requestTopic{"cheriot-smartmeter/g/request"};
constexpr std::string_view publishTopic{"cheriot-smartmeter/g/update"};

void __cheri_callback publishCallback(const char *topicName,
                                      size_t      topicNameLength,
                                      const void *payload,
                                      size_t      payloadLength)
{
	// Check input pointers (can be skipped if the MQTT library is trusted)
	Timeout t{MS_TO_TICKS(5000)};
	if (heap_claim_ephemeral(&t, topicName) != 0 ||
	    !CHERI::check_pointer(topicName, topicNameLength))
	{
		Debug::log(
		  "Cannot claim or verify PUBLISH callback topic name pointer.");
		return;
	}

	if (heap_claim_ephemeral(&t, payload) != 0 ||
	    !CHERI::check_pointer(payload, payloadLength))
	{
		Debug::log("Cannot claim or verify PUBLISH callback payload pointer.");
		return;
	}

	auto topicView = std::string_view{topicName, topicNameLength};
	auto payloadView =
	  std::string_view{static_cast<const char *>(payload), payloadLength};

	if (topicView == outageTopic)
	{
		char buf[22]; // uint32_t is up to 10 decimal chars; add SP and NUL

		Debug::log("Got outage PUBLISH: {}", payloadView);

		if (payloadLength >= sizeof(buf))
		{
			Debug::log("Overlong outage PUBLISH, discarding");
		}
		memcpy(buf, payload, payloadLength);
		buf[payloadLength] = '\0';

		struct grid_planned_outage_payload payload;
		char                              *ptr;
		payload.start_time = strtoul(buf, &ptr, 10);
		payload.duration   = strtoul(ptr, nullptr, 10);

		auto gridOutage = SHARED_OBJECT_WITH_PERMISSIONS(
		  grid_planned_outage, grid_planned_outage, true, true, false, false);
		static_assert(sizeof(*gridOutage) == 12,
		              "grid_planned_outage object bad size; update xmake.lua");
		gridOutage->write(payload);
	}
	else if (topicView == requestTopic)
	{
		char buf[22]; // uint32_t is up to 10 decimal chars; add SP and NUL

		Debug::log("Got request PUBLISH: {}", payloadView);

		if (payloadLength >= sizeof(buf))
		{
			Debug::log("Overlong outage PUBLISH, discarding");
		}
		memcpy(buf, payload, payloadLength);
		buf[payloadLength] = '\0';

		struct grid_request_payload payload;
		char                       *ptr;
		payload.severity = strtoul(buf, &ptr, 10);
		payload.duration = strtoul(ptr, nullptr, 10);

		auto gridRequest = SHARED_OBJECT_WITH_PERMISSIONS(
		  grid_request, grid_request, true, true, false, false);
		static_assert(sizeof(*gridRequest) == 8,
		              "grid_request object bad size; update xmake.lua");
		gridRequest->write(payload);
	}
	else
	{
		Debug::log("Unknown topic in PUBLISH callback: {}", topicView);
	}
}

int grid_entry()
{
	int     ret;
	Timeout noTimeout{UnlimitedTimeout};

	Debug::log("entry");

	auto barrier = SHARED_OBJECT_WITH_PERMISSIONS(
	  std::atomic<uint32_t>, housekeeping_barrier, true, false, false, false);

	while (barrier->load() == 0)
	{
		barrier->wait(0);
	}

	Debug::log("initialization barrier down");

	Debug::log("Generating client ID...");
	// Prefix with something recognizable, for convenience.
	memcpy(clientID.data(), clientIDPrefix.data(), clientIDPrefix.size());
	// Suffix with random character chain.
	mqtt_generate_client_id(clientID.data() + clientIDPrefix.size(),
	                        clientID.size() - clientIDPrefix.size());

	while (true)
	{
		int tick = 0;

		Debug::log("Connecting to MQTT broker...");

		MQTTConnection handle =
		  mqtt_connect(&noTimeout,
		               MALLOC_CAPABILITY,
		               CONNECTION_CAPABILITY(MosquittoOrgMQTT),
		               publishCallback,
		               nullptr /* XXX should watch our ACK stream */,
		               TAs,
		               TAs_NUM,
		               networkBufferSize,
		               incomingPublishCount,
		               outgoingPublishCount,
		               clientID.data(),
		               clientID.size());

		if (!Capability{handle}.is_valid())
		{
			Debug::log("Failed to connect.");
			goto retry;
		}

		Debug::log("Connected to MQTT broker!");

		ret = mqtt_subscribe(&noTimeout,
		                     handle,
		                     1, // QoS 1 = delivered at least once
		                     outageTopic.data(),
		                     outageTopic.size());

		if (ret < 0)
		{
			Debug::log("Failed to subscribe for outages: {}", ret);
			goto retry;
		}

		// XXX clobbers packet ID; we should be watching our ACK stream
		ret = mqtt_subscribe(&noTimeout,
		                     handle,
		                     1, // QoS 1 = delivered at least once
		                     requestTopic.data(),
		                     requestTopic.size());

		if (ret < 0)
		{
			Debug::log("Failed to subscribe for requests: {}", ret);
			goto retry;
		}

		{
			auto sensorData = SHARED_OBJECT_WITH_PERMISSIONS(
			  sensor_data, sensor_data, true, false, false, false);

			sensor_data localSensorData = {0};

			Timeout loopTimeout{MS_TO_TICKS(5000)};
			while (true)
			{
				ret = mqtt_run(&loopTimeout, handle);

				if (ret < 0)
				{
					Debug::log("Failed to run MQTT, error {}; hanging up", ret);
					goto retry;
				}
				else if (loopTimeout.remaining == 0)
				{
					/*
					 * XXX We'd love to be multi-waiting on the network / MQTT
					 * and the sensorData->timestamp, but the APIs we have make
					 * that harder than it should be.
					 */

					Timeout readTimeout{MS_TO_TICKS(1000)};
					ret = sensorData->read(&readTimeout,
					                       localSensorData.version,
					                       localSensorData.payload);
					if (ret == 0)
					{
						Debug::log("Awake and publishing {}",
						           localSensorData.version);

						char    msg[32];
						ssize_t msglen = snprintf(
						  msg, sizeof(msg), "Tick %d", localSensorData.version);

						Timeout t{MS_TO_TICKS(5000)};
						ret = mqtt_publish(&t,
						                   handle,
						                   1, // QoS 1 = delivered at least once
						                   publishTopic.data(),
						                   publishTopic.size(),
						                   msg,
						                   msglen);

						if (ret < 0)
						{
							Debug::log("Failed to publish, error {}.", ret);
							goto retry;
						}
					}
					else
					{
						Debug::log("Awake but skipping publish: {}", ret);
					}

					loopTimeout = Timeout{MS_TO_TICKS(5000)};
				}
			}
		}

	retry:
		if (!Capability{handle}.is_valid())
		{
			mqtt_disconnect(&noTimeout, MALLOC_CAPABILITY, handle);
		}

		Timeout t{MS_TO_TICKS(5000)};
		thread_sleep(&t, ThreadSleepNoEarlyWake);
	}

	return 0;
}
