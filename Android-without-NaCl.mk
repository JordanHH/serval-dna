LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_LDLIBS := -L$(LOCAL_PATH) -lsqlite
LOCAL_SRC_FILES:= \
        client.c        \
        export.c        \
        overlay.c       \
	overlay_abbreviations.c \
	overlay_advertise.c \
	overlay_buffer.c        \
	overlay_interface.c     \
	overlay_packetformats.c \
	overlay_payload.c       \
	overlay_route.c         \
        responses.c     \
        srandomdev.c    \
        batman.c        \
        dataformats.c   \
        gateway.c       \
        packetformats.c \
        server.c        \
        ciphers.c       \
        dna.c           \
        hlrdata.c       \
        peers.c         \
	randombytes.c	\
        simulate.c \
	jni.c \
	sha2.c \
	rhizome.c

LOCAL_MODULE:= dnalib

LOCAL_CFLAGS += \
        -DSHELL -DPACKAGE_NAME=\"\" -DPACKAGE_TARNAME=\"\" -DPACKAGE_VERSION=\"\" \
        -DPACKAGE_STRING=\"\" -DPACKAGE_BUGREPORT=\"\" -DPACKAGE_URL=\"\" \
        -DHAVE_LIBC=1 -DSTDC_HEADERS=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_SYS_STAT_H=1 \
        -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1 -DHAVE_MEMORY_H=1 -DHAVE_STRINGS_H=1 \
        -DHAVE_INTTYPES_H=1 -DHAVE_STDINT_H=1 -DHAVE_UNISTD_H=1 -DHAVE_STDIO_H=1 \
        -DHAVE_ERRNO_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRINGS_H=1 -DHAVE_UNISTD_H=1 \
        -DHAVE_STRING_H=1 -DHAVE_ARPA_INET_H=1 -DHAVE_SYS_SOCKET_H=1 \
        -DHAVE_SYS_MMAN_H=1 -DHAVE_SYS_TIME_H=1 -DHAVE_POLL_H=1 -DHAVE_NETDB_H=1 \
	-DHAVE_SYS_ENDIAN_H=1

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE:= dna
LOCAL_SRC_FILES:= dnawrap.c

include $(BUILD_EXECUTABLE)
