noinst_LIBRARIES = libsmb2.a

libsmb2_a_SOURCES = \
	lib/alloc.c \
	lib/aes.c \
	lib/aes128ccm.c \
	lib/aes_reference.c \
	lib/asn1-ber.c \
	lib/compat.c \
	lib/dcerpc.c \
	lib/dcerpc-lsa.c \
	lib/dcerpc-srvsvc.c \
	lib/errors.c \
	lib/hmac.c \
	lib/hmac-md5.c \
	lib/init.c \
	lib/libsmb2.c \
	lib/md4c.c \
	lib/md5.c \
	lib/ntlmssp.c \
	lib/pdu.c \
	lib/sha1.c \
	lib/sha224-256.c \
	lib/sha384-512.c \
	lib/smb2-cmd-close.c \
	lib/smb2-cmd-create.c \
	lib/smb2-cmd-echo.c \
	lib/smb2-cmd-error.c \
	lib/smb2-cmd-flush.c \
	lib/smb2-cmd-ioctl.c \
	lib/smb2-cmd-lock.c \
	lib/smb2-cmd-logoff.c \
	lib/smb2-cmd-negotiate.c \
	lib/smb2-cmd-notify-change.c \
	lib/smb2-cmd-oplock-break.c \
	lib/smb2-cmd-query-directory.c \
	lib/smb2-cmd-query-info.c \
	lib/smb2-cmd-read.c \
	lib/smb2-cmd-session-setup.c \
	lib/smb2-cmd-set-info.c \
	lib/smb2-cmd-tree-connect.c \
	lib/smb2-cmd-tree-disconnect.c \
	lib/smb2-cmd-write.c \
	lib/smb2-data-file-info.c \
	lib/smb2-data-filesystem-info.c \
	lib/smb2-data-reparse-point.c \
	lib/smb2-data-security-descriptor.c \
	lib/smb2-share-enum.c \
	lib/smb2-signing.c \
	lib/smb3-seal.c \
	lib/socket.c \
	lib/spnego-wrapper.c \
	lib/sync.c \
	lib/timestamps.c \
	lib/unicode.c \
	lib/usha.c \
	include/smb2.h

libsmb2_a_CPPFLAGS = \
	-I$(top_builddir) \
	-I$(top_srcdir)/libsmb2/include \
	-I$(top_srcdir)/libsmb2/lib \
	-DLIBCONFIG_STATIC \
	-D_CRT_SECURE_NO_WARNINGS \
	-D_U_="__attribute__((unused))"

libsmb2_a_CFLAGS = \
	-std=gnu99 \
	-Wall \
	-ffunction-sections \
	-fdata-sections \
	$(AM_CFLAGS)

libsmb2_a_LIBADD =