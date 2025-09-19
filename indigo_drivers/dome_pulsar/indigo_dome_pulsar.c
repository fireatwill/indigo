/** INDIGO DOME Pulsar driver
 \file indigo_dome_pulsar.c
 */

#define DRIVER_VERSION	0x0001
#define DRIVER_NAME	"indigo_dome_pulsar"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>

#include <indigo/indigo_driver_xml.h>
#include <indigo/indigo_dome_driver.h>
#include <indigo/indigo_io.h>

#include "indigo_dome_pulsar.h"

indigo_result indigo_dome_pulsar(indigo_driver_action action, indigo_driver_info *info) {

	return INDIGO_OK;
}
