#pragma once

#include "common.hh"

/*
 * The most recent stable snapshot of all the data sources we're
 * monitoring.
 *
 * Updated by user compartment before entering userJS to respond.
 */
struct userjs_snapshot
{
	struct sensor_data_payload sensor_data;

	struct grid_planned_outage_payload grid_outage;
	struct grid_request_payload        grid_request;

	struct provider_schedule_payload provider_schedule;
	struct provider_variance_payload provider_variance;
};
static_assert(sizeof(struct userjs_snapshot) == 168,
              "userjs_snapshot object bad size; update xmake.lua");

/*
 * Export values to JS by index.  This table is also known to the VM.
 */
enum userjs_snapshot_index
{
	DATA_SENSOR_TIMESTAMP = 1,
	DATA_SENSOR_SAMPLE_0,
	DATA_SENSOR_SAMPLE_7 = DATA_SENSOR_SAMPLE_0 + 7,
	DATA_GRID_OUTAGE_START,
	DATA_GRID_OUTAGE_DURATION,
	DATA_GRID_REQUEST_SEVERITY,
	DATA_GRID_REQUEST_DURATION,
	// TODO
};
