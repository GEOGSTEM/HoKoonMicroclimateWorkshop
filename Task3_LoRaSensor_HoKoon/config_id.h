#ifndef INCLUDE_CONFIG_ID_H
#define INCLUDE_CONFIG_ID_H

/* Device identity */
#define DEVICE_ID 1
#define NUMBER_OF_DEVICES 100

#if DEVICE_ID == 0
	#define ENABLE_GATEWAY
#else
	#define ENABLE_MEASURE
#endif

#endif // INCLUDE_CONFIG_ID_H
