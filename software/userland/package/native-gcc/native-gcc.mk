# Native compiler is intentionally an external package: upstream Buildroot
# only ships a cross compiler. Keep it aligned with the pinned toolchain.
NATIVE_GCC_VERSION = $(GCC_VERSION)
NATIVE_GCC_SITE = $(GCC_SITE)
NATIVE_GCC_SOURCE = $(GCC_SOURCE)
NATIVE_GCC_DL_SUBDIR = gcc
NATIVE_GCC_LICENSE = GPL-3.0+, LGPL-2.1+, GPL-3.0-with-GCC-exception
NATIVE_GCC_LICENSE_FILES = COPYING COPYING3 COPYING.RUNTIME
NATIVE_GCC_DEPENDENCIES = host-gcc-final gmp mpfr mpc zlib binutils
NATIVE_GCC_POST_PATCH_HOOKS += HOST_GCC_APPLY_PATCHES

define NATIVE_GCC_CONFIGURE_CMDS
	mkdir -p $(@D)/build
	(cd $(@D)/build; $(TARGET_CONFIGURE_OPTS) \
		CFLAGS="$(TARGET_CFLAGS)" CXXFLAGS="$(TARGET_CXXFLAGS)" \
		CFLAGS_FOR_BUILD="$(HOST_CFLAGS)" CXXFLAGS_FOR_BUILD="$(HOST_CXXFLAGS)" \
		LDFLAGS_FOR_BUILD="$(HOST_LDFLAGS)" \
		../configure --build=$(GNU_HOST_NAME) \
		--host=$(GNU_TARGET_NAME) --target=$(GNU_TARGET_NAME) \
		--prefix=/usr --with-sysroot=/ --with-build-sysroot=$(STAGING_DIR) \
		--with-native-system-header-dir=/usr/include \
		--with-gxx-include-dir=/usr/include/c++/$(GCC_VERSION) \
		--with-build-time-tools=$(HOST_DIR)/$(GNU_TARGET_NAME)/bin \
		--with-arch=rv32ima_zicsr_zifencei --with-abi=ilp32 \
		--with-gmp=$(STAGING_DIR)/usr --with-mpfr=$(STAGING_DIR)/usr \
		--with-mpc=$(STAGING_DIR)/usr --with-system-zlib \
		--enable-languages=c,c++ --disable-bootstrap --disable-multilib \
		--disable-nls --disable-werror --disable-libsanitizer \
		--disable-libssp --disable-libquadmath --disable-libgomp \
		--disable-libstdcxx-pch --disable-lto --disable-plugin \
		--disable-gcov --enable-checking=release)
endef

define NATIVE_GCC_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/build MAKEINFO=true all-gcc
endef

define NATIVE_GCC_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/build MAKEINFO=true DESTDIR=$(TARGET_DIR) install-gcc
	ln -sf gcc $(TARGET_DIR)/usr/bin/cc
endef

$(eval $(generic-package))
