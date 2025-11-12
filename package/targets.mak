BIN_TARGETS := \
s6-rc-compile \
s6-rc-dryrun \
s6-rc-db \
s6-rc-bundle \
s6-rc-init \
s6-rc \
s6-rc-update \
s6-rc-format-upgrade \
s6-rc-repo-init \
s6-rc-repo-sync \
s6-rc-set-list \
s6-rc-set-new \
s6-rc-set-copy \
s6-rc-set-delete \
s6-rc-set-change \
s6-rc-set-commit \
s6-rc-set-install \

LIBEXEC_TARGETS := \
s6-rc-fdholder-filler \
s6-rc-oneshot-run

LIB_DEFS := S6RC=s6rc S6RCREPO=s6rcrepo
