#pragma once
#include <atomic>
#include <compartment.h>
#include <errno.h>
#include <stdint.h>

/**
 * @defgroup entryvectors Compartment entry vectors
 * @{
 */

int __cheri_compartment("grid") grid_entry();
int __cheri_compartment("housekeeping") housekeeping_entry();
int __cheri_compartment("provider") provider_entry();
int __cheri_compartment("sensor") sensor_entry();
int __cheri_compartment("user") user_data_entry();
int __cheri_compartment("user") user_net_entry();

/* @} */

/**
 * @defgroup crosscalls Compartment cross-calls
 * @{
 */

/**
 * Replace the user policy code in the JS compartment
 *
 * Called by user
 */
int __cheri_compartment("userJS")
  user_javascript_load(const uint8_t *bytecode, size_t size);

/**
 * Run user policy code
 *
 * Called by user
 */
int __cheri_compartment("userJS") user_javascript_run(/* XXX */);

/* @} */

/**
 * @defgroup sharedstate Shared object types
 *
 * Version fields will be treated as futexes and will be odd during updates.
 *
 * @{
 */

template<typename Payload>
struct FutexVersioned
{
	/*
	 * If the version is congruent (mod 4) to ..., then payloads are ... :
	 *   - 0, uninitialized;
	 *   - 1, initialized and stable; or
	 *   - 3, being updated.
	 */

	uint32_t version;
	Payload  payload;

	void write(Payload &update)
	{
		// XXX: it's a pity we don't have C++20's atomic_ref

		__c11_atomic_thread_fence(__ATOMIC_RELEASE);
		this->version |= 0x3;
		this->payload = update;
		__c11_atomic_thread_fence(__ATOMIC_RELEASE);
		this->version += 2;
		futex_wake(&this->version, UINT32_MAX);
	}

	int read(Timeout *t, uint32_t &version, Payload &out)
	{
		uint32_t version_post;

		uint32_t version_pre = this->version;

		if (version_pre == version)
			return ENOMSG;

		do
		{
			while ((version_pre & 0x2) != 0)
			{
				int res = futex_timed_wait(t, &this->version, version_pre);
				if (res < 0)
				{
					return res;
				}
				version_pre = this->version;
			}

			out = this->payload;
			__c11_atomic_thread_fence(__ATOMIC_ACQUIRE);
			version_post = this->version;

		} while (version_pre != version_post);

		version = version_pre;

		return 0;
	}
};

struct sensor_data_payload
{
	uint32_t timestamp;  // of samples[0], each next a minute back in the past
	uint32_t samples[8]; // The past few minutes of sensor data
};

using sensor_data = FutexVersioned<sensor_data_payload>;
static_assert(sizeof(sensor_data) == 40,
              "sensor_data object bad size; update xmake.lua");

/**
 * The next planned outage, if any.
 *
 * 0-duration outages do not exist.
 */
struct grid_planned_outage_payload
{
	uint32_t start_time; // Outage start time
	uint32_t duration;   // seconds after start
};

using grid_planned_outage = FutexVersioned<grid_planned_outage_payload>;
static_assert(sizeof(grid_planned_outage) == 12,
              "grid_planned_outage object bad size; update xmake.lua");

/**
 * A grid request for load modulation.
 *
 * These are meant to take effect immediately.
 *
 * Sign of `severity` indicates direction of request:
 *   - push (negative; grid running low) or
 *   - pull (positive; grid has excess power)
 * Magnitude is arbitrary but intended to reflect the risk of grid failure.
 */
struct grid_request_payload
{
	int16_t  severity;
	uint16_t duration; // seconds relative to update timestamp
};

using grid_request = FutexVersioned<grid_request_payload>;
static_assert(sizeof(grid_request) == 8,
              "grid_request object bad size; update xmake.lua");

/**
 * Provider specified rate schedule
 */
struct provider_schedule_payload
{
	uint32_t timestamp_day;     // Day boundary between "today" and "tomorrow"
	int16_t  today_rate[24];    // Today's hourly rates, from midnight, p/kWh
	int16_t  tomorrow_rate[24]; // Tomorrow's
};

using provider_schedule = FutexVersioned<provider_schedule_payload>;
static_assert(sizeof(provider_schedule) == 104,
              "provider_schedule object bad size; update xmake.lua");

/**
 * Provider-signaled variance in pricing.
 *
 * Up to two variances may be reported at once, so that we can report both
 * the current one and an impending one.  A zero duration variance does not
 * exist.
 */
struct provider_variance_payload
{
	uint32_t version;
	uint32_t timestamp_base;

	int16_t  start[2];    // seconds relative to timestamp_update, negative past
	uint16_t duration[2]; // seconds from start that variance applies
	int16_t  rate[2];     // Metering rate during this interval, p/kWh
};

using provider_variance = FutexVersioned<provider_variance_payload>;
static_assert(sizeof(provider_variance) == 24,
              "provider_variance object bad size; update xmake.lua");

/* @} */
