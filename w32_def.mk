E=@echo.
Q=@
RM=@del /q
TEMPFILES=*.obj
CC=cl
CFLAGS=-DPDC_EXP_EXTRAS=1 -DWIN32=1 -DWIN32_LEAN_AND_MEAN=1 -MD -W3 -nologo
LDFLAGS=
LDLIBS=pdcurses.lib ws2_32.lib