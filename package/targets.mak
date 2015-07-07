BIN_TARGETS := \
s6-rc-compile \
s6-rc-dryrun \
s6-rc-db

SBIN_TARGETS := \
s6-rc-init \
s6-rc \
s6-rc-update


LIBEXEC_TARGETS :=

ifdef DO_ALLSTATIC
LIBS6RC := libs6rc.a
else
LIBS6RC := libs6rc.so
endif

ifdef DO_SHARED
SHARED_LIBS := libs6rc.so
endif

ifdef DO_STATIC
STATIC_LIBS := libs6rc.a
endif
