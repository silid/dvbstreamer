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
 
parsezap.h
 
Parse channels.conf file and add services to the database.
 
Majority of the parsing code taken from the xine input_dvb plugin code.
 
*/
#ifndef _PARSEZAP_H
#define _PARSEZAP_H
#include "dvbadapter.h"

int parsezapfile(char *path, DVBDeliverySystem_e delSys);
#endif
