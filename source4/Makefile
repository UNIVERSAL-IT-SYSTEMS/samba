#!gmake
# The Samba 4 Makefile.
# This file is *NOT* autogenerated.
#
.DEFAULT_GOAL := all

default: all

include mkconfig.mk

pidldir := $(srcdir)/../pidl

basedir = $(prefix)
torturedir = ../lib/torture
swatdir = $(datadir)/swat
setupdir = $(datadir)/setup
ncalrpcdir = $(localstatedir)/ncalrpc
shliboutputdir = $(builddir)/bin/shared

BNLD = $(LD)
BNLD_FLAGS = $(LDFLAGS) $(SYS_LDFLAGS)

HOSTCC_FLAGS = -D_SAMBA_HOSTCC_ $(CFLAGS)
HOSTLD_FLAGS = $(LDFLAGS) $(SYS_LDFLAGS)

$(srcdir)/version.h: $(srcdir)/VERSION
	@$(SHELL) script/mkversion.sh VERSION $(srcdir)/version.h $(srcdir)/

ifneq ($(automatic_dependencies),yes)
ALL_PREDEP = basics
.NOTPARALLEL:
endif

regen_version::
	@$(SHELL) script/mkversion.sh VERSION $(srcdir)/version.h $(srcdir)/

clean_pch::
	@echo "Removing precompiled headers"
	@-rm -f include/includes.h.gch

pch:: clean_pch include/includes.h.gch

.DEFAULT_GOAL := all

ifneq ($(automatic_dependencies),yes)
ALL_PREDEP = basics
.NOTPARALLEL:
endif

include $(srcdir)/build/make/rules.mk
include $(srcdir)/build/make/python.mk
zlibsrcdir := ../lib/zlib
dynconfigsrcdir := dynconfig
heimdalsrcdir := heimdal
dsdbsrcdir := dsdb
smbdsrcdir := smbd
clustersrcdir := cluster
libnetsrcdir := libnet
authsrcdir := auth
nsswitchsrcdir := ../nsswitch
libsrcdir := lib
libsocketsrcdir := lib/socket
libcharsetsrcdir := ../lib/util/charset
ldb_sambasrcdir := lib/ldb-samba
tdbsrcdir := ../lib/tdb
ldbsrcdir := lib/ldb
libtlssrcdir := lib/tls
libregistrysrcdir := lib/registry
smbreadlinesrcdir := lib/smbreadline
libmessagingsrcdir := lib/messaging
libteventsrcdir := ../lib/tevent
libeventssrcdir := lib/events
libcmdlinesrcdir := lib/cmdline
poptsrcdir := ../lib/popt
socketwrappersrcdir := ../lib/socket_wrapper
nsswrappersrcdir := ../lib/nss_wrapper
appwebsrcdir := lib/appweb
libstreamsrcdir := lib/stream
libutilsrcdir := ../lib/util
libtdrsrcdir := ../lib/tdr
libcryptosrcdir := ../lib/crypto
libtorturesrcdir := ../lib/torture
smb_serversrcdir := smb_server
libcompressionsrcdir := ../lib/compression
libgencachesrcdir := lib
paramsrcdir := param
rpc_serversrcdir := rpc_server
ldap_serversrcdir := ldap_server
web_serversrcdir := web_server
winbindsrcdir := winbind
nbt_serversrcdir := nbt_server
wrepl_serversrcdir := wrepl_server
cldap_serversrcdir := cldap_server
utilssrcdir := utils
clientsrcdir := client
torturesrcdir := torture
ntvfssrcdir := ntvfs
ntptrsrcdir := ntptr
librpcsrcdir := librpc
libclisrcdir := libcli
libclicommonsrcdir := ../libcli
libclinbtsrcdir := ../libcli/nbt
pyscriptsrcdir := $(srcdir)/scripting/python
kdcsrcdir := kdc
ntp_signdsrcdir := ntp_signd
wmisrcdir := lib/wmi
tallocsrcdir := ../lib/talloc
comsrcdir := $(srcdir)/lib/com
libgpodir := libgpo

include data.mk

$(foreach SCRIPT,$(wildcard scripting/bin/*),$(eval $(call binary_install_template,$(SCRIPT))))

$(DESTDIR)$(bindir)/%: scripting/bin/% installdirs
	@mkdir -p $(@D)
	@echo Installing $(@F) as $@
	@if test -f $@; then echo -n ""; rm -f $@.old; mv $@ $@.old; fi
	@cp $< $@
	@chmod $(INSTALLPERMS) $@

pythonmods:: $(PYTHON_PYS) $(PYTHON_SO)

DEP_FILES = $(patsubst %.ho,%.hd,$(patsubst %.o,%.d,$(ALL_OBJS))) \
		   include/includes.d

ifeq ($(automatic_dependencies),yes)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(MAKECMDGOALS),distclean)
ifneq ($(MAKECMDGOALS),realdistclean)
ifneq ($(SKIP_DEP_FILES),yes)
-include $(DEP_FILES)
endif
endif
endif
endif

ifneq ($(SKIP_DEP_FILES),yes)
clean::
	@echo Removing dependency files
	@find . -name '*.d' -o -name '*.hd' | xargs rm -f
endif
else
include $(srcdir)/static_deps.mk
endif

clean::
	@find ../lib ../libcli ../librpc ../libgpo ../nsswitch -name '*.o' -o -name '*.ho' | xargs rm -f

PUBLIC_HEADERS += ./version.h

libraries:: $(STATIC_LIBS) $(SHARED_LIBS)
modules:: $(PLUGINS)
headers:: $(PUBLIC_HEADERS)
manpages:: $(MANPAGES)
all:: showflags $(ALL_PREDEP) binaries modules pythonmods libraries headers
everything:: all

LD_LIBPATH_OVERRIDE = $(LIB_PATH_VAR)=$(shliboutputdir)

# 'make testsuite' creates all binaries which are
# needed by samba3's 'make test' and the build-farm
# scripts use that it as fallback in case
# 'make everything' fails
testsuite:: bin/smbclient bin/cifsdd bin/smbtorture bin/nmblookup

showlayout:: 
	@echo 'Samba will be installed into:'
	@echo '  basedir:     $(basedir)'
	@echo '  bindir:      $(bindir)'
	@echo '  sbindir:     $(sbindir)'
	@echo '  libdir:      $(libdir)'
	@echo '  modulesdir:  $(modulesdir)'
	@echo '  includedir:  $(includedir)'
	@echo '  vardir:      $(localstatedir)'
	@echo '  privatedir:  $(privatedir)'
	@echo '  piddir:      $(piddir)'
	@echo '  lockdir:     $(lockdir)'
	@echo '  logfilebase: $(logfilebase)'
	@echo '  setupdir:    $(setupdir)'
	@echo '  swatdir:     $(swatdir)'
	@echo '  mandir:      $(mandir)'
	@echo '  torturedir:  $(torturedir)'
	@echo '  datadir:     $(datadir)'
	@echo '  winbindd_socket_dir:  $(winbindd_socket_dir)'
	@echo '  ntp_signd_socket_dir:  $(ntp_signd_socket_dir)'

showflags::
	@echo '  srcdir     = $(srcdir)'
	@echo '  builddir   = $(builddir)'

# The permissions to give the executables
INSTALLPERMS = 0755

install:: showlayout everything installbin installsbin installdat installmisc \
	      installlib installheader installpc installplugins

# DESTDIR is used here to prevent packagers wasting their time
# duplicating the Makefile. Remove it and you will have the privilege
# of packaging each samba release for multiple versions of multiple
# distributions and operating systems, or at least supplying patches
# to all the packaging files required for this, prior to committing
# the removal of DESTDIR. Do not remove it even though you think it
# is not used.

installdirs::
	@$(SHELL) $(srcdir)/script/installdirs.sh \
		$(DESTDIR)$(basedir) \
		$(DESTDIR)$(bindir) \
		$(DESTDIR)$(sbindir) \
		$(DESTDIR)$(torturedir) \
		$(DESTDIR)$(libdir) \
		$(DESTDIR)$(modulesdir) \
		$(DESTDIR)$(mandir) \
		$(DESTDIR)$(localstatedir) \
		$(DESTDIR)$(localstatedir)/lib \
		$(DESTDIR)$(localstatedir)/run \
		$(DESTDIR)$(privatedir) \
		$(DESTDIR)$(datadir) \
		$(DESTDIR)$(piddir) \
		$(DESTDIR)$(lockdir) \
		$(DESTDIR)$(logfilebase) \
		$(DESTDIR)$(privatedir)/tls \
		$(DESTDIR)$(includedir) \
		$(DESTDIR)$(pkgconfigdir) \
		$(DESTDIR)$(sysconfdir)

installbin:: installdirs

installplugins::

installlib:: $(SHARED_LIBS) $(STATIC_LIBS) installdirs
	@$(SHELL) $(srcdir)/script/installlib.sh $(DESTDIR)$(libdir) "$(SHLIBEXT)" $(SHARED_LIBS) 
	#@$(SHELL) $(srcdir)/script/installlib.sh $(DESTDIR)$(libdir) "$(STLIBEXT)" $(STATIC_LIBS)

installheader:: headers installdirs
	@srcdir=$(srcdir) builddir=$(builddir) $(PERL) $(srcdir)/script/installheader.pl $(DESTDIR)$(includedir) $(PUBLIC_HEADERS)

installdat:: installdirs
	@$(SHELL) $(srcdir)/script/installdat.sh $(DESTDIR)$(datadir) $(srcdir)

installman:: manpages installdirs
	@$(SHELL) $(srcdir)/script/installman.sh $(DESTDIR)$(mandir) $(MANPAGES)

installmisc:: installdirs
	@$(SHELL) $(srcdir)/script/installmisc.sh $(srcdir) $(DESTDIR)$(setupdir)

installpc:: installdirs
	@$(SHELL) $(srcdir)/script/installpc.sh $(builddir) $(DESTDIR)$(pkgconfigdir) $(PC_FILES)

uninstall:: uninstallbin uninstallsbin uninstalldat uninstallmisc uninstalllib uninstallheader \
			uninstallman uninstallpc uninstallplugins

uninstallmisc::
	@echo "Removing MISC files"
	@rm -rf $(DESTDIR)$(setupdir)/*

$(DESTDIR)$(bindir)/%: bin/% installdirs
	@mkdir -p $(@D)
	@echo Installing $(@F) as $@
	@if test -f $@; then echo -n ""; rm -f $@.old; mv $@ $@.old; fi
	@cp $< $@
	@chmod $(INSTALLPERMS) $@

$(DESTDIR)$(sbindir)/%: bin/% installdirs
	@mkdir -p $(@D)
	@echo Installing $(@F) as $@
	@if test -f $@; then echo -n ""; rm -f $@.old; mv $@ $@.old; fi
	@cp $< $@
	@chmod $(INSTALLPERMS) $@

uninstalldat::
	@echo "Removing DAT files"
	@rm -fr $(DESTDIR)$(datadir)/*

uninstallbin::

uninstalllib::
	@echo "Removing libraries"
	@$(SHELL) $(srcdir)/script/uninstalllib.sh $(DESTDIR)$(libdir) $(SHARED_LIBS)

uninstallheader::
	@echo "Removing headers"
	@rm -fr $(DESTDIR)$(includedir)/*

uninstallman::
	@echo "Removing manpages"
	@$(SHELL) $(srcdir)/script/uninstallman.sh $(DESTDIR)$(mandir) $(MANPAGES)

uninstallplugins::

uninstallpc::
	@echo "Removing package configurations"
	@cd $(DESTDIR)$(pkgconfigdir); rm -f $(notdir $(PC_FILES))

config.status:
	@echo "config.status does not exist. Please run ./configure."
	@/bin/false

data.mk: config.status $(MK_FILES)
	./config.status

testcov-html:: 

include $(pidldir)/config.mk
selftestdir := $(srcdir)/../selftest
include $(srcdir)/selftest/config.mk

showflags::
	@echo '  pwd        = '`/bin/pwd`
