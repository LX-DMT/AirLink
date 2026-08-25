.PHONY: clean all
.SILENT: clean all

include audio.mk

all:
	cp lib/$(SDK_VER)/lib* $(MW_LIB)

clean:
	rm -f $(MW_LIB)/libaac*
	rm -f $(MW_LIB)/libcvi_audio*
	rm -f $(MW_LIB)/libcvi_RES1*
	rm -f $(MW_LIB)/libcvi_ssp*
	rm -f $(MW_LIB)/libcvi_VoiceEngine*
	rm -f $(MW_LIB)/libcvi_vqe*
	rm -f $(MW_LIB)/libdnvqe*
	rm -f $(MW_LIB)/libtinyalsa*
