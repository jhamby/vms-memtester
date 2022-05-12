!
! 64-bit memtester for OpenVMS
!

.IFDEF ALPHA_EV67
OPT_FLAGS=/ARCH=EV67
.ENDIF

CFLAGS=$(OPT_FLAGS)/FLOAT=IEEE/IEEE=DENORM/POI=LONG/MAIN=POSIX_EXIT-
         /DEFINE=(_POSIX_EXIT, __UNIX_PUTC)

memtester.exe :     memtester.obj, output.obj, tests.obj
         link $(MMS$SOURCE_LIST)

memtester.obj :     memtester.c, sizes.h, tests.h, types.h, output.h

output.obj :        output.c, output.h

tests.obj :         tests.c, memtester.h, sizes.h, types.h, output.h

clean :
         delete/nolog memtester.exe;*, *.obj;*
