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

// gp_bits is used as boolean
#define is_connected			gp_bits

#define PRIVATE_DATA			((pulsar_private_data *)device->private_data)

typedef struct {
	int handle;
	pthread_mutex_t property_mutex;
} pulsar_private_data;

#define PROPERTY_LOCK()		pthread_mutex_lock(&PRIVATE_DATA->property_mutex)
#define PROPERTY_UNLOCK()	pthread_mutex_unlock(&PRIVATE_DATA->property_mutex)

static indigo_result dome_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property) {
	return indigo_dome_enumerate_properties(device, NULL, NULL);
}

static indigo_result dome_attach(indigo_device *device) {
	assert(device != NULL);
	assert(PRIVATE_DATA != NULL);
	if (indigo_dome_attach(device, DRIVER_NAME, DRIVER_VERSION) == INDIGO_OK) {
		pthread_mutex_init(&PRIVATE_DATA->property_mutex, NULL);
		INDIGO_DEVICE_ATTACH_LOG(DRIVER_NAME, device->name);
		return dome_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}

static void dome_connection_handler(indigo_device *device) {
	if (CONNECTION_CONNECTED_ITEM->sw.value) {
		if (!device->is_connected) {
			PROPERTY_LOCK();
			if (indigo_try_global_lock(device) != INDIGO_OK) {
				PROPERTY_UNLOCK();
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "indigo_try_global_lock(): failed to get lock.");
				CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
				indigo_update_property(device, CONNECTION_PROPERTY, NULL);
			} else {
				PROPERTY_UNLOCK();
				INDIGO_DRIVER_LOG(DRIVER_NAME, "%s connected.", "Pulsar");
				CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
				device->is_connected = true;
			}
		}
	} else {
		if (device->is_connected) {
			// disconnect
			indigo_global_unlock(device);
			device->is_connected = false;
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Disconnected");
			CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
		}
	}
	indigo_dome_change_property(device, NULL, CONNECTION_PROPERTY);
}

static indigo_result dome_change_property(indigo_device *device, indigo_client *client, indigo_property *property) {
	assert(device != NULL);
	assert(DEVICE_CONTEXT != NULL);
	assert(property != NULL);
	if (indigo_property_match_changeable(CONNECTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- CONNECTION
		if (indigo_ignore_connection_change(device, property))
			return INDIGO_OK;
		indigo_property_copy_values(CONNECTION_PROPERTY, property, false);
		CONNECTION_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, CONNECTION_PROPERTY, NULL);
		indigo_set_timer(device, 0, dome_connection_handler, NULL);
		return INDIGO_OK;
	}
	return indigo_dome_change_property(device, client, property);
}

static indigo_result dome_detach(indigo_device *device) {
	assert(device != NULL);
	if (IS_CONNECTED) {
		indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
		dome_connection_handler(device);
	}
	indigo_global_unlock(device);
	pthread_mutex_destroy(&PRIVATE_DATA->property_mutex);
	INDIGO_DEVICE_DETACH_LOG(DRIVER_NAME, device->name);
	return indigo_dome_detach(device);
}

// --------------------------------------------------------------------------------

static pulsar_private_data *private_data = NULL;

static indigo_device *dome = NULL;

indigo_result indigo_dome_pulsar(indigo_driver_action action, indigo_driver_info *info) {
	static indigo_device dome_template = INDIGO_DEVICE_INITIALIZER(
		DOME_PULSAR_NAME,
		dome_attach,
		dome_enumerate_properties,
		dome_change_property,
		NULL,
		dome_detach
	);

	static indigo_driver_action last_action = INDIGO_DRIVER_SHUTDOWN;

	SET_DRIVER_INFO(info, DOME_PULSAR_NAME, __FUNCTION__, DRIVER_VERSION, false, last_action);

	if (action == last_action)
		return INDIGO_OK;

	switch(action) {
		case INDIGO_DRIVER_INIT:
			last_action = action;
			private_data = indigo_safe_malloc(sizeof(pulsar_private_data));
			dome = indigo_safe_malloc_copy(sizeof(indigo_device), &dome_template);
			dome->private_data = private_data;
			indigo_attach_device(dome);
			break;

		case INDIGO_DRIVER_SHUTDOWN:
			VERIFY_NOT_CONNECTED(dome);
			last_action = action;
			if (dome != NULL) {
				indigo_detach_device(dome);
				free(dome);
				dome = NULL;
			}
			if (private_data != NULL) {
				free(private_data);
				private_data = NULL;
			}
			break;

		case INDIGO_DRIVER_INFO:
			break;
	}

	return INDIGO_OK;
}
