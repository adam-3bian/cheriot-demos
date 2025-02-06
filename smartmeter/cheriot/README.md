# Multi-Tenant Smart Metering

This demo showcases how one might run multiple tenants with different operational concerns on a sensing platform.
Specifically, we consider the task of "smart metering" power consumption.
Our tenants are...

1. The grid controller, who wants to

   1. receive real-time information about the local grid,
   2. communicate scheduled outages,
   3. indicate the need for corrective actions (load shedding, load increase)

2. The provider, who wants to

   1. receive usage data (even retrospectively, in the case of, say, network outages),
   2. set the price schedule ahead of time, and
   3. announce spot price variances relative to the schedule

3. The service integrator (or, perhaps, end user), who wants to

   1. receive the price schedule, variances, and grid notifications, and
   2. make policy decisions about local resources (batteries, generation, loads) based on those.

We presume that the grid controller and provider logic is minimal and can be fixed at firmware build time,
while the integrator/user policy may be subject to faster change and should not require rebuilding firmware.
To that end, that policy is run on a lightweight JS interpreter.

## Compartmentalization and Information Flow

Internally, these tenants all exist within their own compartments ("grid", "provider", and two for the "user").
These make use of the (also compartmentalized!) MQTT/TLS/TCP/IP network stack to talk to the network.

Sensing and measurement itself is done by another compartment, "sensor".
This compartment makes its measurements available via a static shared object.
This object contains a small ring buffer of recent measurements and a time-stamp of the most recent.
The time-stamp is used as a futex to notify waiters of updates.
(We presume the tenant compartments are sufficiently responsive that they will not miss updates.)

Similarly, information from the grid controller and providers are also exposed via shared objects.
Again, these contain time-stamp futexes next to their payloads.
