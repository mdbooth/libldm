lib_LTLIBRARIES = libldm.la

libldm_ladir = $(includedir)/libldm
libldm_la_HEADERS = ldm.h
libldm_la_SOURCES = mbr.c mbr.h gpt.c gpt.h ldm.c
libldm_la_CFLAGS = $(GOBJECT_CFLAGS)
libldm_la_LIBADD = -lz -luuid $(GOBJECT_LIBS)
