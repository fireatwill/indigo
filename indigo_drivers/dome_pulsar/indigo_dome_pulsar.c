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

#include <sys/termios.h>

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
	pthread_mutex_t port_mutex;
	indigo_timer *dome_timer;
} pulsar_private_data;

#define PROPERTY_LOCK()		pthread_mutex_lock(&PRIVATE_DATA->property_mutex)
#define PROPERTY_UNLOCK()	pthread_mutex_unlock(&PRIVATE_DATA->property_mutex)

static bool dome_set_serial_options(int handle) {
	struct termios to;

	if (tcgetattr(handle, &to) == -1) {
		return false;
	}
	to.c_iflag |= ICRNL; // translate cr to lf on input
	if (tcsetattr(handle, TCSANOW, &to) == -1) {
		return false;
	}
	return true;
}

static bool dome_command(indigo_device *device, char *command, char *response, int max) {
	char wrapped_cmd[64];

	snprintf(wrapped_cmd, 64, "%s\r", command);
	pthread_mutex_lock(&PRIVATE_DATA->port_mutex);
	tcflush(PRIVATE_DATA->handle, TCIOFLUSH);
	indigo_write(PRIVATE_DATA->handle, wrapped_cmd, strlen(wrapped_cmd));
	if (response != NULL) {
		if (indigo_read_line(PRIVATE_DATA->handle, response, max) == -1) {
			pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
			INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Command %s -> no response", command);
			return false;
		}
	}
	pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Command %s -> %s", command, response != NULL ? response : "NULL");
	return true;
}

static bool dome_handshake(indigo_device *device) {
	char response[64];

	for(int i = 0; i < 2; i++) { // send twice to sync if necessary
		if (dome_command(device, "PULSAR", response, sizeof(response))) {
			if (dome_command(device, "VER", response, sizeof(response))) {
				return true;
			}
			return false;
		}
	}
	return false;
}

static bool dome_goto_azimuth(indigo_device *device, double target_az) {
	char command[64];
	char response[4];

	sprintf(command, "GO %.1f", target_az);
	if (!dome_command(device, command, response, sizeof(response))) {
		return false;
	}
	return strcmp(response, "A") == 0;
}

static bool dome_stop(indigo_device *device) {
	char response[64];

	if (!dome_command(device, "STOP", response, sizeof(response))) {
		return false;
	}
	return strcmp(response, "A") == 0;
}

static bool dome_get_azimuth(indigo_device *device, double *current_az) {
	char response[16];

	if (!dome_command(device, "ANGLE", response, sizeof(response))) {
		return false;
	}
	return sscanf(response, "%lf", current_az) == 1;
}

static bool dome_get_extended_status(indigo_device *device, double *current_az, double *target_az, int *motor_state, int *direction_state, int *shutter_state) {
	char response[128];

	if (!dome_command(device, "V", response, sizeof(response))) {
		return false;
	}
	return sscanf(response, "%lf %d %*lf %lf %d %d %*d %*d %*d %*d %*d %*d %*d", current_az, motor_state, target_az, direction_state, shutter_state) == 5;
}

static indigo_result dome_enumerate_properties(indigo_device *device, indigo_client *client, indigo_property *property) {
	return indigo_dome_enumerate_properties(device, NULL, NULL);
}

static indigo_result dome_attach(indigo_device *device) {
	assert(device != NULL);
	assert(PRIVATE_DATA != NULL);
	if (indigo_dome_attach(device, DRIVER_NAME, DRIVER_VERSION) == INDIGO_OK) {
		pthread_mutex_init(&PRIVATE_DATA->property_mutex, NULL);
		pthread_mutex_init(&PRIVATE_DATA->port_mutex, NULL);
		// -------------------------------------------------------------------------------- DOME_SPEED
		DOME_SPEED_PROPERTY->hidden = true;
		// -------------------------------------------------------------------------------- DOME_STEPS_PROPERTY
		indigo_copy_value(DOME_STEPS_ITEM->label, "Relative move (°)");
		// -------------------------------------------------------------------------------- DEVICE_PORT
		DEVICE_PORT_PROPERTY->hidden = false;
		// -------------------------------------------------------------------------------- DEVICE_PORTS
		DEVICE_PORTS_PROPERTY->hidden = false;
		// -------------------------------------------------------------------------------- DOME_ON_HORIZONTAL_COORDINATES_SET
		DOME_ON_HORIZONTAL_COORDINATES_SET_PROPERTY->hidden = false;
		// -------------------------------------------------------------------------------- DOME_HORIZONTAL_COORDINATES
		DOME_HORIZONTAL_COORDINATES_PROPERTY->perm = INDIGO_RW_PERM;
		// -------------------------------------------------------------------------------- DOME_SLAVING_PARAMETERS
		DOME_SLAVING_PARAMETERS_PROPERTY->hidden = false;
		// --------------------------------------------------------------------------------
		ADDITIONAL_INSTANCES_PROPERTY->hidden = DEVICE_CONTEXT->base_device != NULL;
		INDIGO_DEVICE_ATTACH_LOG(DRIVER_NAME, device->name);
		return dome_enumerate_properties(device, NULL, NULL);
	}
	return INDIGO_FAILED;
}

static void dome_timer_callback(indigo_device *device) {
	double current_az, target_az;
	int motor_state, direction_state, shutter_state;

	if (!dome_get_extended_status(device, &current_az, &target_az, &motor_state, &direction_state, &shutter_state)) {
		INDIGO_DRIVER_ERROR(DRIVER_NAME, "dome_get_extended_status(): returned error");
	} else {
		if (DOME_HORIZONTAL_COORDINATES_AZ_ITEM->number.value != current_az) {
			INDIGO_DRIVER_LOG(DRIVER_NAME, "updating position to %f", current_az);
			DOME_HORIZONTAL_COORDINATES_AZ_ITEM->number.value = current_az;
			indigo_update_property(device, DOME_HORIZONTAL_COORDINATES_PROPERTY, NULL);
		}

		if ((direction_state == 0) && (DOME_HORIZONTAL_COORDINATES_PROPERTY->state == INDIGO_BUSY_STATE)) {
			// has now stopped
			if (indigo_azimuth_distance(DOME_HORIZONTAL_COORDINATES_AZ_ITEM->number.target, current_az) >= 5.0f) {
				INDIGO_DRIVER_LOG(DRIVER_NAME, "dome stopped in wrong position, updating position to %f", current_az);
				DOME_HORIZONTAL_COORDINATES_PROPERTY->state = INDIGO_ALERT_STATE;
				DOME_HORIZONTAL_COORDINATES_AZ_ITEM->number.value = current_az;
				indigo_update_property(device, DOME_HORIZONTAL_COORDINATES_PROPERTY, NULL);
			} else {
				INDIGO_DRIVER_LOG(DRIVER_NAME, "dome has reached position, updating position to %f", current_az);
				DOME_HORIZONTAL_COORDINATES_PROPERTY->state = INDIGO_OK_STATE;
				DOME_HORIZONTAL_COORDINATES_AZ_ITEM->number.value = current_az;
				indigo_update_property(device, DOME_HORIZONTAL_COORDINATES_PROPERTY, NULL);
			}
		}
	}

	INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Timer tick");
	indigo_reschedule_timer(device, 5, &PRIVATE_DATA->dome_timer);
}

static void dome_connection_callback(indigo_device *device) {
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
				char *device_name = DEVICE_PORT_ITEM->text.value;
				PRIVATE_DATA->handle = indigo_open_serial_with_speed(device_name, 115200);
				if (PRIVATE_DATA->handle < 0) {
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "Opening device %s: failed", DEVICE_PORT_ITEM->text.value);
					device->is_connected = false; // think this is redundant
					CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
					indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
					indigo_update_property(device, CONNECTION_PROPERTY, NULL);
					indigo_global_unlock(device);
					return;
				} else if (!dome_set_serial_options(PRIVATE_DATA->handle)) {
					int res = close(PRIVATE_DATA->handle);
					if (res < 0) {
						INDIGO_DRIVER_ERROR(DRIVER_NAME, "close(%d) = %d", PRIVATE_DATA->handle, res);
					} else {
						INDIGO_DRIVER_DEBUG(DRIVER_NAME, "close(%d) = %d", PRIVATE_DATA->handle, res);
					}
					device->is_connected = false; // think this is redundant
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "Connect failed, could not set serial options");
					CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
					indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
					indigo_update_property(device, CONNECTION_PROPERTY, NULL); // could display message
					indigo_global_unlock(device);
					return;

				} else if (!dome_handshake(device)) {
					int res = close(PRIVATE_DATA->handle);
					if (res < 0) {
						INDIGO_DRIVER_ERROR(DRIVER_NAME, "close(%d) = %d", PRIVATE_DATA->handle, res);
					} else {
						INDIGO_DRIVER_DEBUG(DRIVER_NAME, "close(%d) = %d", PRIVATE_DATA->handle, res);
					}
					device->is_connected = false; // think this is redundant
					INDIGO_DRIVER_ERROR(DRIVER_NAME, "Connect failed, no response or invalid response");
					CONNECTION_PROPERTY->state = INDIGO_ALERT_STATE;
					indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
					indigo_update_property(device, CONNECTION_PROPERTY, NULL); // could display message
					indigo_global_unlock(device);
					return;
				}
				INDIGO_DRIVER_LOG(DRIVER_NAME, "%s connected.", DOME_PULSAR_NAME);
				CONNECTION_PROPERTY->state = INDIGO_OK_STATE;
				device->is_connected = true;
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "Connected = %d", PRIVATE_DATA->handle);
				indigo_set_timer(device, 0.5, dome_timer_callback, &PRIVATE_DATA->dome_timer);
			}
		}
	} else {
		if (device->is_connected) {
			indigo_cancel_timer_sync(device, &PRIVATE_DATA->dome_timer);
			pthread_mutex_lock(&PRIVATE_DATA->port_mutex);
			int res = close(PRIVATE_DATA->handle);
			if (res < 0) {
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "close(%d) = %d", PRIVATE_DATA->handle, res);
			} else {
				INDIGO_DRIVER_DEBUG(DRIVER_NAME, "close(%d) = %d", PRIVATE_DATA->handle, res);
			}
			indigo_global_unlock(device);
			pthread_mutex_unlock(&PRIVATE_DATA->port_mutex);
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
		indigo_set_timer(device, 0, dome_connection_callback, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(DOME_PARK_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- DOME_PARK
		indigo_property_copy_values(DOME_PARK_PROPERTY, property, false);
		if (DOME_PARK_UNPARKED_ITEM->sw.value) {
			// unpark the dome
			DOME_PARK_PROPERTY->state = INDIGO_OK_STATE;
		} else if (DOME_PARK_PARKED_ITEM->sw.value) {
			// park the dome
			DOME_PARK_PROPERTY->state = INDIGO_OK_STATE;
		}
		indigo_update_property(device, DOME_PARK_PROPERTY, NULL);
		return INDIGO_OK;
	} else if (indigo_property_match_changeable(DOME_ABORT_MOTION_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- DOME_ABORT_MOTION
		indigo_property_copy_values(DOME_ABORT_MOTION_PROPERTY, property, false);
		if (DOME_ABORT_MOTION_ITEM->sw.value) {
			if (!dome_stop(device)) {
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "dome_stop(%d): returned error", PRIVATE_DATA->handle);
				DOME_ABORT_MOTION_PROPERTY->state = INDIGO_ALERT_STATE;
				DOME_ABORT_MOTION_ITEM->sw.value = false;
				indigo_update_property(device, DOME_ABORT_MOTION_PROPERTY, NULL);
				return INDIGO_OK;
			}
			if (DOME_PARK_PROPERTY->state == INDIGO_BUSY_STATE) {
				DOME_PARK_PROPERTY->state = INDIGO_ALERT_STATE;
				indigo_update_property(device, DOME_PARK_PROPERTY, NULL);
			}
		}
		DOME_ABORT_MOTION_PROPERTY->state = INDIGO_OK_STATE;
		DOME_ABORT_MOTION_ITEM->sw.value = false;
		indigo_update_property(device, DOME_ABORT_MOTION_PROPERTY, NULL);
	} else if (indigo_property_match_changeable(DOME_HORIZONTAL_COORDINATES_PROPERTY, property)) {
		// -------------------------------------------------------------------------------- DOME_HRIZONTAL_COORDINATES
		indigo_property_copy_values(DOME_HORIZONTAL_COORDINATES_PROPERTY, property, false);
		if (DOME_PARK_PARKED_ITEM->sw.value) {
			double current_az;
			if (!dome_get_azimuth(device, &current_az)) {
				INDIGO_DRIVER_ERROR(DRIVER_NAME, "dome_get_azimuth(%d): returned error", PRIVATE_DATA->handle);
			} else {
				DOME_HORIZONTAL_COORDINATES_AZ_ITEM->number.value = current_az;
			}
			DOME_HORIZONTAL_COORDINATES_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, DOME_HORIZONTAL_COORDINATES_PROPERTY, "Dome is parked.");
			return INDIGO_OK;
		}
		double target_az = DOME_HORIZONTAL_COORDINATES_AZ_ITEM->number.target;
		if (!dome_goto_azimuth(device, target_az)) {
			INDIGO_DRIVER_ERROR(DRIVER_NAME, "dome_goto_azimuth(%d): returned error", PRIVATE_DATA->handle);
			DOME_HORIZONTAL_COORDINATES_PROPERTY->state = INDIGO_ALERT_STATE;
			indigo_update_property(device, DOME_HORIZONTAL_COORDINATES_PROPERTY, NULL);
			return INDIGO_OK;
		}
		DOME_HORIZONTAL_COORDINATES_PROPERTY->state = INDIGO_BUSY_STATE;
		indigo_update_property(device, DOME_HORIZONTAL_COORDINATES_PROPERTY, NULL);
		return INDIGO_OK;
	}
	return indigo_dome_change_property(device, client, property);
}

static indigo_result dome_detach(indigo_device *device) {
	assert(device != NULL);
	if (IS_CONNECTED) {
		indigo_set_switch(CONNECTION_PROPERTY, CONNECTION_DISCONNECTED_ITEM, true);
		dome_connection_callback(device);
	}
	indigo_global_unlock(device);
	pthread_mutex_destroy(&PRIVATE_DATA->property_mutex);
	pthread_mutex_destroy(&PRIVATE_DATA->port_mutex);
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
