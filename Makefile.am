AM_CFLAGS = -Wall -Werror

bin_PROGRAMS = partread ldmread

partread_SOURCES = partread.c mbr.c mbr.h gpt.h gpt.c
partread_LDADD = -lz -luuid

ldmread_SOURCES = ldmread.c mbr.c mbr.h gpt.h gpt.c ldm.h ldm.c
ldmread_LDADD = -lz -luuid $(GOBJECT_LIBS)
ldmread_CFLAGS = $(CFLAGS) $(AM_CFLAGS) $(GOBJECT_CFLAGS)
