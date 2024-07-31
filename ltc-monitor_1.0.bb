# meta-dynagate-10-14/recipes-support/ltc-monitor/ltc-monitor.bb

SUMMARY = "Program to access LTC system monitor data"
SECTION = "support"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://COPYING.MIT;md5=3da9cfbcb788c80a0384361b4de20420"

S = "${WORKDIR}"

SRC_URI = " \
    file://COPYING.MIT \
    file://main.c \
    file://Makefile \
    "


# OECORE_TARGET_ARCH is used in the Makefile to create a machine specific build directory.
# Here we use EXTRA_OEMAKE to set it to the currently defined TARGET_ARCH. This allows the
# same Makefile to be used in a Yocto recipe or with an SDK command line build. 

EXTRA_OEMAKE = "OECORE_TARGET_ARCH=${TARGET_ARCH}"

# Need to make sure that we include the Yocto LDFLAGs wen we compile, so add them to the
# compiler name here

TARGET_CC_ARCH += "${LDFLAGS}"

do_compile () {
  oe_runmake
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${S}/Release/${TARGET_ARCH}/ltc-monitor ${D}${bindir}/
}
