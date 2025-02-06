// Copyright SCI Semiconductor and CHERIoT Contributors.
// SPDX-License-Identifier: MIT

/**
 * The sensor compartment.
 *
 * Responsible for reading power consumption (or a simulation thereof) and
 * calling callbacks in the other compartments.
 */

#include "common.hh"

#include <debug.hh>
#include <futex.h>
#include <sntp.h>
#include <thread.h>

using Debug = ConditionalDebug<true, "sensor">;

int sensor_entry()
{
	int i = 0;

	auto barrier = SHARED_OBJECT_WITH_PERMISSIONS(
	  std::atomic<uint32_t>, housekeeping_barrier, true, false, false, false);

	while (barrier->load() == 0)
	{
		barrier->wait(0);
	}

	Debug::log("initialization barrier down");

	auto sensorData = SHARED_OBJECT_WITH_PERMISSIONS(
	  sensor_data, sensor_data, true, true, false, false);

	while (1)
	{
		timeval tv;
		int     ret = gettimeofday(&tv, nullptr);
		if (ret == 0)
		{
			// TODO: update array with meaningful numbers
			struct sensor_data_payload nextPayload = {0};
			nextPayload.timestamp                  = tv.tv_sec;

			sensorData->write(nextPayload);
		}

		Debug::log("Tick {}...", tv.tv_sec);

		Timeout t{MS_TO_TICKS(30000)};
		thread_sleep(&t, ThreadSleepNoEarlyWake);
		i++;
	}
}
