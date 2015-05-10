/*
 * ipop-tap
 * Copyright 2013, University of Florida
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// https://github.com/davidiw/brunet/blob/90c9f9584a2c8f5087637000980109bfd159a986/src/c-lib/windows_tap.c

#if defined(WIN32)
#include <winsock2.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <wchar.h>

#include "win32_tap.h"

#define TAP_CONTROL_CODE(request,method) \
  CTL_CODE (FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)
#define TAP_IOCTL_SET_MEDIA_STATUS      TAP_CONTROL_CODE (6, METHOD_BUFFERED)

#define NETWORK_PATH "SYSTEM\\CurrentControlSet\\Control\\Network"
#define USERMODEDEVICEDIR "\\\\.\\Global\\"
#define TAPSUFFIX         ".tap"

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))


int
network_device_name_to_guid(const char * name, char *device_guid) {
  int i_0, i_1;
  DWORD size;
  HKEY key_0, key_1, key_2;
  char name_0[255], name_1[255], name_2[255], full_path[1024];

  /* Open up Networking in the registry */
  RegOpenKeyEx(HKEY_LOCAL_MACHINE, NETWORK_PATH, 0, KEY_READ, &key_0);
    
  for(i_0 = 0; ; i_0++) {
    size = 255;
    /* Enumerate through the different keys under Network\layer0 */
    if(RegEnumKeyEx(key_0, i_0, name_0, &size, NULL, NULL, NULL, NULL) 
      != ERROR_SUCCESS) {
      i_0 = -1;
      break;
    }
    sprintf(full_path, "%s\\%s", NETWORK_PATH, name_0);
    /* Open the current key we're enumerating through Network\layer0 */
    RegOpenKeyEx(HKEY_LOCAL_MACHINE, full_path, 0, KEY_READ, &key_1); 
    for(i_1 = 0; ; i_1++) {
      size = 255;
      /* This enumerates through the next layer Network\layer0\layer1 */
      if(RegEnumKeyEx(key_1, i_1, name_1, &size, NULL, NULL, NULL, NULL) 
        != ERROR_SUCCESS) {
        i_1 = -1;
        break;
      }

      sprintf(full_path, "%s\\%s\\%s\\Connection", NETWORK_PATH, 
        name_0, name_1);
      /* This opens keys that we're looking for, if they don't exist, let's 
         continue */
      if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, full_path, 0, KEY_READ, &key_2) 
        != ERROR_SUCCESS) {
        continue;
      }
      size = 255;
      /* We get the Name of the network interface, if it matches, let's get the
         GUID and return */
      RegQueryValueEx(key_2, "Name", 0, NULL, (LPBYTE)name_2, &size);
      if(!strcmp(name, name_2)) {
        RegCloseKey(key_0);
        RegCloseKey(key_1);
        RegCloseKey(key_2);
      /* We have to create a new copy in global heap! */
        strcpy(device_guid, name_1);
        return 1;
      }
      RegCloseKey(key_2);
    }
    RegCloseKey(key_1);
  }
  RegCloseKey(key_0);
  return -1;
}

int read_tap(windows_tap * fd, char * data, int len) {
  int read;
  /* ReadFile is asynchronous and GetOverLappedResult is blocking, we have to do
     this cause Windows makes things painful when deal with asynchronous I/O */
  ReadFile(fd->hand, data, len, (LPDWORD) &read, &(fd->read));
  GetOverlappedResult(fd->hand, &fd->read, (LPDWORD) &read, TRUE);
  return read;
}

int write_tap(windows_tap * fd, char * data, int len) {
  int written;
  /* WriteFile is asynchronous and GetOverLappedResult is blocking, we have to 
     do this cause Windows makes things painful when deal with 
     asynchronous I/O */
  WriteFile(fd->hand, data, len, (LPDWORD) &written, &(fd->write));
  GetOverlappedResult(fd->hand, &fd->write, (LPDWORD) &written, TRUE);
  return written;
}

int
get_mac(const char *device_name, char *mac) {
  ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
  ULONG family = AF_INET;
  PIP_ADAPTER_ADDRESSES pAddresses = NULL;
  PIP_ADAPTER_ADDRESSES pCurAddresses = NULL;
  ULONG outBufLen = 15000;
  DWORD dwRetVal = 0;
  wchar_t tmp_name[100];
  wchar_t w_device_name[100];
  int result = 0;

  pAddresses = (IP_ADAPTER_ADDRESSES *) MALLOC(outBufLen);
  dwRetVal = GetAdaptersAddresses(family, flags, NULL, 
                                  pAddresses, &outBufLen);
  if (dwRetVal != NO_ERROR) {
    fprintf(stderr, "get_mac failed\n");
    if (pAddresses != NULL) FREE(pAddresses);
    return -1;
  }
  pCurAddresses = pAddresses;
  while(pCurAddresses) {
    swprintf(tmp_name, 100, L"%s", pCurAddresses->FriendlyName);
    swprintf(w_device_name, 100, L"%hs", device_name);
    if (wcscmp(tmp_name, w_device_name) == 0) {
      memcpy(mac, pCurAddresses->PhysicalAddress, 6);
      result = 1;
      break;
    }
    pCurAddresses = pCurAddresses->Next;
  }
  FREE(pAddresses);
  return result;
}

windows_tap * open_tap(const char *device_name, char *mac) {
  int len, status = 1;
  char device_path[255], device_guid[255];
  if (network_device_name_to_guid(device_name, device_guid) != 1 || 
     get_mac(device_name, mac) != 1) {
    return (windows_tap *)-1;
  }

  windows_tap * fd = (windows_tap *) malloc(sizeof(windows_tap));
  sprintf(device_path, "%s%s%s", USERMODEDEVICEDIR, device_guid, TAPSUFFIX);
  /* This gets us Handle (pointer) to operate on the tap device */
  fd->hand = CreateFile (device_path, GENERIC_READ | GENERIC_WRITE, 0, 0,
    OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, 0);
  if(fd->hand != INVALID_HANDLE_VALUE) {
    /* This turns "connects" the tap device */
    if(!DeviceIoControl(fd->hand, TAP_IOCTL_SET_MEDIA_STATUS, &status, 
      sizeof (status), &status, sizeof (status), (LPDWORD) &len, NULL)) {
      free(fd);
      fd = (windows_tap *) -1;
    }
    else {
      /* We do this once so we don't have to redo this every time a read or 
         write occurs! */
      fd->read.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
      fd->read.Offset = 0;
      fd->read.OffsetHigh = 0;
      fd->write.hEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
      fd->write.Offset = 0;
      fd->write.OffsetHigh = 0;
    }
  }
  else {
    fd = (windows_tap *) -1;
  }
  return fd;
}
#endif
