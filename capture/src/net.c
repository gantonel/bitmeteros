/*
 * BitMeterOS
 * http://codebox.org.uk/bitmeterOS
 *
 * Copyright (c) 2011 Rob Dawson
 *
 * Licensed under the GNU General Public License
 * http://www.gnu.org/licenses/gpl.txt
 *
 * This file is part of BitMeterOS.
 *
 * BitMeterOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * BitMeterOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with BitMeterOS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include "capture.h"
#include "common.h"

/*
Contains platform-specific code for obtaining the network stats that we need.
*/

#ifdef __APPLE__
	#include <sys/socket.h>
	#include <net/if.h>
	#include <net/if_types.h>
	#include <sys/sysctl.h>
	#include <string.h>
	#include <net/route.h>

	struct Data* extractDataFromIf(struct if_msghdr2 *);

	struct Data* getData(){
		int mib[6] = {CTL_NET, PF_ROUTE, 0, 0, NET_RT_IFLIST2, 0};
		size_t len;
		int rc = sysctl(mib, 6, NULL, &len, NULL, 0);
		if (rc < 0){
		    logMsg(LOG_ERR, "sysctl returned %d", rc);
			return NULL;
		}

		char *buf = malloc(len);
		rc = sysctl(mib, 6, buf, &len, NULL, 0);
		if (rc < 0){
			free(buf);
			logMsg(LOG_ERR, "sysctl returned %d", rc);
			return NULL;
		}

		int offset=0;
		int isLoopback;
		struct Data* firstData = NULL;
		struct Data* thisData = NULL;
		struct if_msghdr* hdr;

		while (offset < len){
			hdr = (struct if_msghdr *) (buf + offset);

			if ((hdr->ifm_type == RTM_IFINFO2)) {
				struct if_msghdr2 *hdr2 = (struct if_msghdr2 *)hdr;

				isLoopback = (hdr2->ifm_data.ifi_type == IFT_LOOP);
			 // Ignore loopback traffic
				if (!isLoopback){
					struct Data* thisData = extractDataFromIf(hdr2);

					appendData(&firstData, thisData);
				}
			}

			offset += hdr->ifm_msglen;
		}
		free(buf);

		return firstData;
	}

	struct Data* extractDataFromIf(struct if_msghdr2 *ifHdr){
	 // Allocate a new Data struct, and populate the dl, ul, and ad parts of it
		struct Data* data = allocData();
		data->dl = (BW_INT) ifHdr->ifm_data.ifi_ibytes;
	    data->ul = (BW_INT) ifHdr->ifm_data.ifi_obytes;

		char ifName[IF_NAMESIZE];
		if_indextoname(ifHdr->ifm_index, ifName);

	    setAddress(data, ifName);
	    setHost(data, "");

	    return data;
	}
#endif

#ifdef __linux__
	#include <string.h>
	static struct Data* parseProcNetDevLine(char*, char*);
	static int isLoopback(struct Data* data);
    #define PROC_NET_DEV "/proc/net/dev"

	struct Data* getData(){
		FILE* fProcNetDev = fopen(PROC_NET_DEV, "r");

		if (fProcNetDev == NULL){
			logMsg(LOG_ERR, "Unable to open " PROC_NET_DEV);
			return NULL;
		}

		const int MAX_LINE_SIZE = 256;
		char line[MAX_LINE_SIZE];
		char* colonPos;

		struct Data* firstData = NULL;
		struct Data* thisData  = NULL;

		while (fgets(line, MAX_LINE_SIZE, fProcNetDev) != NULL) {
			if ((colonPos = strchr(line, ':')) != NULL ){
				char* ifName = calloc(32, 1);
				strncpy(ifName, line, colonPos-line);
				thisData = parseProcNetDevLine(ifName, colonPos + 1);
				free(ifName);

                if (isLoopback(thisData) == FALSE){
                    appendData(&firstData, thisData);
                } else {
                    freeData(thisData);
                }
			}
		}
		fclose(fProcNetDev);

		return firstData;
	}

	static int isLoopback(struct Data* data){
	 // TODO do this properly
        return (strcmp(data->ad, "lo") == 0 ? TRUE : FALSE);
	}

	struct Data* parseProcNetDevLine(char* ifName, char* line){
	 // Allocate a new Data struct, and populate the dl, ul, and ad parts of it
		unsigned long dummy, dl, ul;
		sscanf(line, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
				&dl, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
				&ul, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy);

		struct Data* data = allocData();
		data->dl = dl;
	    data->ul = ul;
	    setAddress(data, ifName);
	    setHost(data, "");

	    return data;
	}

#endif

#ifdef _WIN32
	#ifndef __USE_MINGW_ANSI_STDIO
		#define __USE_MINGW_ANSI_STDIO 1
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
	#include <iphlpapi.h>

    /*static void dumpRow(MIB_IFROW* pIfRow){
        logMsg(LOG_ERR, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", 
                time(NULL),
                pIfRow->dwIndex, pIfRow->dwType, pIfRow->dwMtu, pIfRow->dwSpeed, 
                pIfRow->dwPhysAddrLen, pIfRow->dwAdminStatus, pIfRow->dwOperStatus, pIfRow->dwLastChange, 
                pIfRow->dwInOctets, pIfRow->dwInUcastPkts, pIfRow->dwInNUcastPkts, pIfRow->dwInDiscards,
                pIfRow->dwInErrors, pIfRow->dwInUnknownProtos, pIfRow->dwOutOctets, pIfRow->dwOutUcastPkts,
                pIfRow->dwOutNUcastPkts, pIfRow->dwOutDiscards, pIfRow->dwOutErrors, pIfRow->dwOutQLen,
                pIfRow->dwDescrLen);
    }*/
	struct Data* getData(){
		MIB_IFTABLE* pIfTable = (MIB_IFTABLE *) malloc(sizeof (MIB_IFTABLE));
		unsigned long dwSize = sizeof (MIB_IFTABLE);

		if (GetIfTable(pIfTable, &dwSize, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
	        free(pIfTable);
	        pIfTable = (MIB_IFTABLE *) malloc(dwSize);
	    }

		int numEntries;
		struct Data* firstData = NULL;
		struct Data* thisData  = NULL;

		int rc = GetIfTable(pIfTable, &dwSize, FALSE);
		if (rc == NO_ERROR){
			MIB_IFROW* pIfRow;

			numEntries = (int) pIfTable->dwNumEntries;

			int i;
			for (i = 0; i < numEntries; i++) {
			    pIfRow = (MIB_IFROW *) & pIfTable->table[i];
			    
			 /* Ignore loopback traffic. Ignore adapters that are not operational - fixes bug where some
			    users were seeing a large spike in ul/dl readings when PC wakes up from sleep mode, caused
			    by network adapters being disabled (and returning dwInOctets/dwOutOctets values of 0) for
			    a few seconds before the machine enters sleep, causing a large delta when the adapter is
			    enabled again. */
			    if ((pIfRow->dwType != IF_TYPE_SOFTWARE_LOOPBACK) && (pIfRow->dwOperStatus == MIB_IF_OPER_STATUS_OPERATIONAL)){
   					thisData = allocData();
				    thisData->dl = pIfRow->dwInOctets;
				    thisData->ul = pIfRow->dwOutOctets;

				    char hexString[MAC_ADDR_LEN * 2 + 1];
				    makeHexString(hexString, (char*) &(pIfRow->bPhysAddr), MAC_ADDR_LEN);
				    setAddress(thisData, hexString);
				    setHost(thisData, "");

                 // Windows Vista and later include duplicate entries in the IF table results, filter them out here
                    int isDuplicate = FALSE;
                    struct Data* prevData = firstData;
                    while (prevData != NULL) {
                        if (strcmp(prevData->ad, thisData->ad) == 0){
                            isDuplicate = TRUE;
                            break;
                        }
                        prevData = prevData->next;
                    }

                    if (isDuplicate == FALSE){
                        //dumpRow(pIfRow);
                        appendData(&firstData, thisData);
                    } else {
                        freeData(thisData);
                    }
				}
			}

		} else {
			logWin32ErrMsg("GetIfTable Error", rc);
		}

		if (pIfTable != NULL) {
		    free(pIfTable);
		    pIfTable = NULL;
		}

		return firstData;
	}

#endif

