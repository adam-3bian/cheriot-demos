// Copyright SCI Semiconductor and CHERIoT Contributors.
// SPDX-License-Identifier: MIT

/*
 * Housekeeping compartment.
 *
 * Performs device initialization and periodic SNTP resync.
 */

#include "common.hh"

#include <NetAPI.h>
#include <cstdlib>
#include <debug.hh>
#include <errno.h>
#include <fail-simulator-on-error.h>
#include <futex.h>
#include <sntp.h>
#include <thread.h>
#include <tick_macros.h>

using Debug = ConditionalDebug<true, "housekeeping">;

static void do_sntp()
{
	Timeout t{MS_TO_TICKS(5000)};

	// SNTP must be run for the TLS stack to be able to check certificate dates.
	while (sntp_update(&t) != 0)
	{
		Debug::log("Failed to update NTP time");

		t = Timeout{MS_TO_TICKS(5000)};
		thread_sleep(&t, ThreadSleepNoEarlyWake);

		t = Timeout{MS_TO_TICKS(5000)};
	}
	Debug::log("Updating NTP took {} ticks", t.elapsed);

	{
		timeval tv;
		int     ret = gettimeofday(&tv, nullptr);
		if (ret != 0)
		{
			Debug::log("Failed to get time of day: {}", ret);
		}
		else
		{
			// Truncate the epoch time to 32 bits for printing.
			Debug::log("Current UNIX epoch time: {}", (int32_t)tv.tv_sec);
		}
	}
}

int housekeeping_entry()
{
	Debug::log("entry");

	network_start();

	Debug::log("network started");

	do_sntp();

	auto barrier = SHARED_OBJECT_WITH_PERMISSIONS(
	  std::atomic<uint32_t>, housekeeping_barrier, true, true, false, false);

	barrier->store(1);
	barrier->notify_all();

	Debug::log("barrier released");

	while (1)
	{
		Timeout t{MS_TO_TICKS(60000)};
		thread_sleep(&t, ThreadSleepNoEarlyWake);

		do_sntp();

		/*
		 * For more useful reporting, flush the quarantine.
		 *
		 * XXX Should we make heap_available report the sum?  Separately expose
		 * the quarantine size?
		 */
		heap_quarantine_empty();
		Debug::log("Heap available: {}", heap_available());
	}
}
