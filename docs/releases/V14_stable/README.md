# V14_stable - 19 November 2026

Saved current ESP32-C3 SuperMini gateway, before the Christmas feature.
Includes the current virtual-promotion negotiation in the standard build.
This package preserves the exact 803664-byte firmware already flashed on
14 November. Its original embedded build timestamp is retained.

No Virgin engine or Christmas gesture is compiled into this version.
The independent source/header closure is in src/v14_stable; build V14_stable.
The source closure also rebuilt successfully with Espressif32 7.0.1,
Arduino 2.0.17 and NimBLE-Arduino 2.5.1. Rebuilding can differ due to paths
and timestamps; published firmware bytes are kept unchanged.

The standard gateway, VirtualOperator, physical-promotion and default
virtual-promotion test suites passed. Their transport doubles do not prove
every physical board combination. No new hardware certification is implied
by saving the current version under the requested V14_stable name.

Previous V13 Move, V12 and historical beta packages are retained unchanged.
source.zip contains the standalone frozen build. FREEZE.json records source
and original firmware hashes; SHA256.json covers this package.
