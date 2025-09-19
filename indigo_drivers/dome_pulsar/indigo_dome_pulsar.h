/** INDIGO DOME Pulsar driver
 \file indigo_dome_pulsar.h
*/

#ifndef dome_pulsar_h
#define dome_pulsar_h

#include <indigo/indigo_driver.h>
#include <indigo/indigo_dome_driver.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOME_PULSAR_NAME	"Pulsar"

/** Create DOME Pulsar device instance
 */

extern indigo_result indigo_dome_pulsar(indigo_driver_action action, indigo_driver_info *info);

#ifdef __cplusplus
}
#endif

#endif /* dome_pulsar_h */
