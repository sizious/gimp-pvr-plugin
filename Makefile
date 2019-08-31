all:
	@cat README
	@echo You have gimp `gimp-config --version`
local:
	gimptool --install pvr.c

global:
	gimptool --install-admin pvr.c