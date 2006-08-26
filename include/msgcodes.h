/*
Copyright (C) 2006  Adam Charrett

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA

msgcodes.py

Binary Communications protocol message codes.

(AUTOGENERATED Wed Aug 09 12:31:39 GMTST 2006)
*/

#ifndef _MSGCODES_H
#define _MSGCODES_H

/* Start of RERR codes */
#define RERR_OK            0x00 /* Success */
#define RERR_NOTAUTHORISED 0x01 /* Not authorised */
#define RERR_EXISTS        0x02 /* Output Already Exists */
#define RERR_NOTFOUND      0x03 /* Not found */
#define RERR_STREAMING     0x04 /* Already streaming to requested ip:port */
#define RERR_GENERIC       0xFF /* Generic Error (see message) */
/* End of RERR codes */

/* Start of INFO codes */
#define INFO_NAME          0x00 /* Name (user defined) of this server */
#define INFO_FETYPE        0x01 /* Front End Type */
#define INFO_AUTHENTICATED 0x02 /* Is connection authenticated  */
#define INFO_UPSECS        0xFE /* Uptime string in seconds ie "61"  */
#define INFO_UPTIME        0xFF /* Uptime string "%d days %d hours %d minutes %d seconds" */
/* End of INFO codes */

/* Start of MSGCODE codes */
#define MSGCODE_INFO 0x0000 /* Information message */
#define MSGCODE_AUTH 0x0001 /* Authenticate */
#define MSGCODE_QUOT 0x0002 /* Quote command */
#define MSGCODE_CSPS 0x1101 /* Control Service Primary Select - Select Primary Service */
#define MSGCODE_CSSA 0x1102 /* Control Service Secondary Add - Add secondary service */
#define MSGCODE_CSSS 0x1103 /* Control Service Secondary Select - Select a secondary service */
#define MSGCODE_CSSR 0x1104 /* Control Service Secondary Remove - Remove secondary service */
#define MSGCODE_CSSD 0x1105 /* Control Service Set Destination - Set the destination of a service filter */
#define MSGCODE_COAO 0x1201 /* Control Output Add Output - Add a new output destination */
#define MSGCODE_CORO 0x1202 /* Control Output Remove Output - Remove an output destination */
#define MSGCODE_COAP 0x1203 /* Control Output Add PIDs - Add pids to an output. */
#define MSGCODE_CORP 0x1204 /* Control Output remove PIDs - Remove pids from an output. */
#define MSGCODE_COSD 0x1205 /* Control Output Set destination - Set the destination of a manual filter */
#define MSGCODE_SSPS 0x2101 /* Status Service Primary Service - Return current service name for primary service output. */
#define MSGCODE_SSFL 0x2102 /* Status Service Filter List - List service filters outputs. */
#define MSGCODE_SSPC 0x2103 /* Status Service Packet Count */
#define MSGCODE_SOLO 0x2201 /* Status Outputs List outputs */
#define MSGCODE_SOLP 0x2202 /* Status Output List pids */
#define MSGCODE_SOPC 0x2203 /* Status Output Packet Count */
#define MSGCODE_STSS 0x2F01 /* Status TS Stats */
#define MSGCODE_SFES 0x2F02 /* Status Front End Status */
#define MSGCODE_SSLA 0x2F03 /* Status Services List All - List all avaialable services */
#define MSGCODE_SSLM 0x2F04 /* Status Services List Multiplex - List services avaialable of the current multiplex */
#define MSGCODE_SSPL 0x2F05 /* Status Services List PIDs */
#define MSGCODE_RSSL 0xF001 /* Response Service Secondary List */
#define MSGCODE_ROLO 0xF002 /* Response Outputs List outputs */
#define MSGCODE_RLP  0xF003 /* Response Output List pids */
#define MSGCODE_ROPC 0xF004 /* Response Output Packet Count */
#define MSGCODE_RTSS 0xF005 /* Response TS Stats */
#define MSGCODE_RFES 0xF006 /* Response Front End Status */
#define MSGCODE_RLS  0xF007 /* Response Services List */
#define MSGCODE_RTXT 0xF008 /* Response Text */
#define MSGCODE_RERR 0xFFFF /* Response Error */
/* End of MSGCODE codes */


#endif