
#if defined(WIN32)

#ifndef _WIN32_TAP_H_
#define _WIN32_TAP_H_

#include <windows.h>

#define WIN32_EXPORT __declspec(dllexport)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  HANDLE hand;
  OVERLAPPED read, write;
} windows_tap;

int read_tap(windows_tap * fd, char * data, int len);
int write_tap(windows_tap * fd, char * data, int len);
WIN32_EXPORT windows_tap* open_tap(const char *device_name, char *mac);

#ifdef __cplusplus
}
#endif

#endif
#endif
