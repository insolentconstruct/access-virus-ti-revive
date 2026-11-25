// brokerinfo.h - the broker's INFO message (VTIMSG_INFO, after a client's HELLO): "key=value" fields separated by
// tabs, for now its release version - the plug-in shows it in PATCH UTILITY > INFO where the original shows the Access
// driver versions (E189). TIREVIVE_VERSION comes from the build (Makefile: make VERSION=0.8.21), else "dev".
#pragma once
#include <stdio.h>

#ifndef TIREVIVE_VERSION
#define TIREVIVE_VERSION "dev"
#endif

// the length of the text in `buf` (n > 0), cut to fit: it is sent as is
static inline int vti_info_text(char *buf, int n)
{
    const int r = snprintf(buf, (size_t)n, "version=%s", TIREVIVE_VERSION);
    return r < 0 ? 0 : r < n ? r : n - 1;
}
