/* Copyright (C) 2011-2012 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "suricata-common.h"
#include "conf.h"
#include "util-device.h"

/**
 * \file
 *
 * \author Eric Leblond <eric@regit.org>
 *
 *  \brief Utility functions to handle device list
 */

/** private device list */
static TAILQ_HEAD(, LiveDevice_) live_devices =
    TAILQ_HEAD_INITIALIZER(live_devices);


/**
 *  \brief Add a pcap device for monitoring
 *
 *  \param dev string with the device name
 *
 *  \retval 0 on success.
 *  \retval -1 on failure.
 */
int LiveRegisterDevice(char *dev)
{
    LiveDevice *pd = SCMalloc(sizeof(LiveDevice));
    if (pd == NULL) {
        return -1;
    }

    pd->dev = SCStrdup(dev);
    SC_ATOMIC_INIT(pd->pkts);
    SC_ATOMIC_INIT(pd->invalid_checksums);
    pd->ignore_checksum = 0;
    TAILQ_INSERT_TAIL(&live_devices, pd, next);

    SCLogDebug("Pcap device \"%s\" registered.", dev);
    return 0;
}

/**
 *  \brief Get the number of registered devices
 *
 *  \retval cnt the number of registered devices
 */
int LiveGetDeviceCount(void) {
    int i = 0;
    LiveDevice *pd;

    TAILQ_FOREACH(pd, &live_devices, next) {
        i++;
    }

    return i;
}

/**
 *  \brief Get a pointer to the device name at idx
 *
 *  \param number idx of the device in our list
 *
 *  \retval ptr pointer to the string containing the device
 *  \retval NULL on error
 */
char *LiveGetDeviceName(int number) {
    int i = 0;
    LiveDevice *pd;

    TAILQ_FOREACH(pd, &live_devices, next) {
        if (i == number) {
            return pd->dev;
        }

        i++;
    }

    return NULL;
}

/**
 *  \brief Get a pointer to the device at idx
 *
 *  \param number idx of the device in our list
 *
 *  \retval ptr pointer to the string containing the device
 *  \retval NULL on error
 */
LiveDevice *LiveGetDevice(char *name) {
    int i = 0;
    LiveDevice *pd;

    if (name == NULL) {
        SCLogWarning(SC_ERR_INVALID_VALUE, "Name of device should not be null");
        return NULL;
    }

    TAILQ_FOREACH(pd, &live_devices, next) {
        if (!strcmp(name, pd->dev)) {
            return pd;
        }

        i++;
    }

    return NULL;
}



int LiveBuildDeviceList(char * runmode)
{
    ConfNode *base = ConfGetNode(runmode);
    ConfNode *child;
    int i = 0;

    if (base == NULL)
        return 0;

    TAILQ_FOREACH(child, &base->head, next) {
        if (!strcmp(child->val, "interface")) {
            ConfNode *subchild;
            TAILQ_FOREACH(subchild, &child->head, next) {
                if ((!strcmp(subchild->name, "interface"))) {
                    SCLogInfo("Adding interface %s from config file",
                              subchild->val);
                    LiveRegisterDevice(subchild->val);
                    i++;
                }
            }
        }
    }

    return i;
}
