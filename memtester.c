/*
 * memtester version 4
 *
 * Very simple but very effective user-space memory tester.
 * Originally by Simon Kirby <sim@stormix.com> <sim@neato.org>
 * Version 2 by Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Version 3 not publicly released.
 * Version 4 rewrite:
 * Copyright (C) 2004-2020 Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Licensed under the terms of the GNU General Public License version 2 (only).
 * See the file COPYING for details.
 *
 */

#define __version__ "4.5.1"

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#ifdef __VMS
#define __NEW_STARLET 1
#include <lib$routines.h>
#include <psldef.h>
#include <ssdef.h>
#include <starlet.h>
#endif

#include "types.h"
#include "sizes.h"
#include "tests.h"
#include "output.h"

#define EXIT_FAIL_NONSTARTER    0x01
#define EXIT_FAIL_ADDRESSLINES  0x02
#define EXIT_FAIL_OTHERTEST     0x04

struct test tests[] = {
    { "Random Value", test_random_value },
    { "Compare XOR", test_xor_comparison },
    { "Compare SUB", test_sub_comparison },
    { "Compare MUL", test_mul_comparison },
    { "Compare DIV",test_div_comparison },
    { "Compare OR", test_or_comparison },
    { "Compare AND", test_and_comparison },
    { "Sequential Increment", test_seqinc_comparison },
    { "Solid Bits", test_solidbits_comparison },
    { "Block Sequential", test_blockseq_comparison },
    { "Checkerboard", test_checkerboard_comparison },
    { "Bit Spread", test_bitspread_comparison },
    { "Bit Flip", test_bitflip_comparison },
    { "Walking Ones", test_walkbits1_comparison },
    { "Walking Zeroes", test_walkbits0_comparison },
#ifdef TEST_NARROW_WRITES
    { "8-bit Writes", test_8bit_wide_random },
    { "16-bit Writes", test_16bit_wide_random },
#endif
    { NULL, NULL }
};

/* Sanity checks and portability helper macros. */
#ifdef _SC_VERSION
void check_posix_system(void) {
    if (sysconf(_SC_VERSION) < 198808L) {
        fprintf(stderr, "A POSIX system is required.  Don't be surprised if "
            "this craps out.\n");
        fprintf(stderr, "_SC_VERSION is %lu\n", sysconf(_SC_VERSION));
    }
}
#else
#define check_posix_system()
#endif

#ifdef _SC_PAGE_SIZE
int memtester_pagesize(void) {
    int pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize == -1) {
        perror("get page size failed");
        exit(EXIT_FAIL_NONSTARTER);
    }
    printf("pagesize is %ld\n", (long) pagesize);
    return pagesize;
}
#else
int memtester_pagesize(void) {
    printf("sysconf(_SC_PAGE_SIZE) not supported; using pagesize of 8192\n");
    return 8192;
}
#endif

/* Some systems don't define MAP_LOCKED.  Define it to 0 here
   so it's just a no-op when ORed with other constants. */
#ifndef MAP_LOCKED
  #define MAP_LOCKED 0
#endif

/* Function declarations */
void usage(char *me);

/* Global vars - so tests have access to this information */
int use_phys = 0;
off_t physaddrbase = 0;

/* Function definitions */
void usage(char *me) {
    fprintf(stderr, "\n"
#ifdef __VMS
            "Usage: %s <mem>[B|K|M|G] [loops]\n",
#else
            "Usage: %s [-p physaddrbase [-d device] [-u]] <mem>[B|K|M|G] [loops]\n",
#endif
            me);
    exit(EXIT_FAIL_NONSTARTER);
}

int main(int argc, char **argv) {
#ifdef __VMS
    int loops, loop, i;
    size_t pagesize, wantraw, wantmb;
    __int64 wantbytes, wantbytes_orig;
    ul bufsize, halflen, count;     /* size_t is only 32 bits in VMS */
#else
    ul loops, loop, i;
    size_t pagesize, wantraw, wantmb, wantbytes, wantbytes_orig, bufsize,
         halflen, count;
#endif
    char *memsuffix, *addrsuffix, *loopsuffix;
    ptrdiff_t pagesizemask;
#ifdef __VMS
    void *buf, *aligned;
#else
    void volatile *buf, *aligned;
#endif
    ulv *bufa, *bufb;
    int do_mlock = 1, done_mem = 0;
    int exit_code = 0;
    int memfd, opt, memshift;
#ifdef __VMS
    ul maxbytes = -1; /* addressable memory, in bytes */
    ul maxmb = (ul) (maxbytes >> 20) + 1; /* addressable memory, in MB */
#else
    size_t maxbytes = -1; /* addressable memory, in bytes */
    size_t maxmb = (maxbytes >> 20) + 1; /* addressable memory, in MB */
#endif
#ifndef __VMS
    /* Device to mmap memory from with -p, default is normal core */
    char *device_name = "/dev/mem";
    struct stat statbuf;
    int device_specified = 0;
#endif
    char *env_testmask = 0;
    ul testmask = 0;
    int o_flags = O_RDWR | O_SYNC;

    out_initialize();

    printf("memtester version " __version__ " (%d-bit)\n", UL_LEN);
    printf("Copyright (C) 2001-2020 Charles Cazabon.\n");
    printf("Licensed under the GNU General Public License version 2 (only).\n");
    printf("\n");
    check_posix_system();
    pagesize = memtester_pagesize();
    pagesizemask = (ptrdiff_t) ~(pagesize - 1);
#ifdef __VMS
    printf("pagesizemask is 0x%lx\n", pagesizemask);
#else
    printf("pagesizemask is 0x%tx\n", pagesizemask);
#endif

    /* If MEMTESTER_TEST_MASK is set, we use its value as a mask of which
       tests we run.
     */
    if (env_testmask = getenv("MEMTESTER_TEST_MASK")) {
        errno = 0;
        testmask = strtoul(env_testmask, 0, 0);
        if (errno) {
            fprintf(stderr, "error parsing MEMTESTER_TEST_MASK %s: %s\n",
                    env_testmask, strerror(errno));
            usage(argv[0]); /* doesn't return */
        }
#ifdef __VMS
        printf("using testmask 0x%llx\n", testmask);
#else
        printf("using testmask 0x%lx\n", testmask);
#endif
    }

#ifdef __VMS
    optind = 1;
#else
    while ((opt = getopt(argc, argv, "p:d:u")) != -1) {
        switch (opt) {
            case 'p':
                errno = 0;
                physaddrbase = (off_t) strtoull(optarg, &addrsuffix, 16);
                if (errno != 0) {
                    fprintf(stderr,
                            "failed to parse physaddrbase arg; should be hex "
                            "address (0x123...)\n");
                    usage(argv[0]); /* doesn't return */
                }
                if (*addrsuffix != '\0') {
                    /* got an invalid character in the address */
                    fprintf(stderr,
                            "failed to parse physaddrbase arg; should be hex "
                            "address (0x123...)\n");
                    usage(argv[0]); /* doesn't return */
                }
                if (physaddrbase & (pagesize - 1)) {
                    fprintf(stderr,
                            "bad physaddrbase arg; does not start on page "
                            "boundary\n");
                    usage(argv[0]); /* doesn't return */
                }
                /* okay, got address */
                use_phys = 1;
                break;
            case 'd':
                if (stat(optarg,&statbuf)) {
                    fprintf(stderr, "can not use %s as device: %s\n", optarg,
                            strerror(errno));
                    usage(argv[0]); /* doesn't return */
                } else {
                    if (!S_ISCHR(statbuf.st_mode)) {
                        fprintf(stderr, "can not mmap non-char device %s\n",
                                optarg);
                        usage(argv[0]); /* doesn't return */
                    } else {
                        device_name = optarg;
                        device_specified = 1;
                    }
                }
                break;
            case 'u':
		o_flags &= ~O_SYNC;
		break;
            default: /* '?' */
                usage(argv[0]); /* doesn't return */
        }
    }

    if (device_specified && !use_phys) {
        fprintf(stderr,
                "for mem device, physaddrbase (-p) must be specified\n");
        usage(argv[0]); /* doesn't return */
    }
#endif

    if (optind >= argc) {
        fprintf(stderr, "need memory argument, in MB\n");
        usage(argv[0]); /* doesn't return */
    }

    errno = 0;
    wantraw = (size_t) strtoul(argv[optind], &memsuffix, 0);
    if (errno != 0) {
        fprintf(stderr, "failed to parse memory argument");
        usage(argv[0]); /* doesn't return */
    }
    switch (*memsuffix) {
        case 'G':
        case 'g':
            memshift = 30; /* gigabytes */
            break;
        case 'M':
        case 'm':
            memshift = 20; /* megabytes */
            break;
        case 'K':
        case 'k':
            memshift = 10; /* kilobytes */
            break;
        case 'B':
        case 'b':
            memshift = 0; /* bytes*/
            break;
        case '\0':  /* no suffix */
            memshift = 20; /* megabytes */
            break;
        default:
            /* bad suffix */
            usage(argv[0]); /* doesn't return */
    }
#ifdef __VMS
    wantbytes_orig = wantbytes = ((ul) wantraw << memshift);
#else
    wantbytes_orig = wantbytes = ((size_t) wantraw << memshift);
#endif
    wantmb = (wantbytes_orig >> 20);
    optind++;
    if (wantmb > maxmb) {
        fprintf(stderr, "This system can only address %llu MB.\n", (ull) maxmb);
        exit(EXIT_FAIL_NONSTARTER);
    }
    if (wantbytes < pagesize) {
#ifdef __VMS
        fprintf(stderr, "bytes %lld < pagesize %ld -- memory argument too large?\n",
                wantbytes, pagesize);
#else
        fprintf(stderr, "bytes %ld < pagesize %ld -- memory argument too large?\n",
                wantbytes, pagesize);
#endif
        exit(EXIT_FAIL_NONSTARTER);
    }

    if (optind >= argc) {
        loops = 0;
    } else {
        errno = 0;
        loops = strtoul(argv[optind], &loopsuffix, 0);
        if (errno != 0) {
            fprintf(stderr, "failed to parse number of loops");
            usage(argv[0]); /* doesn't return */
        }
        if (*loopsuffix != '\0') {
            fprintf(stderr, "loop suffix %c\n", *loopsuffix);
            usage(argv[0]); /* doesn't return */
        }
    }

    printf("want %lluMB (%llu bytes)\n", (ull) wantmb, (ull) wantbytes);
    buf = NULL;

#ifndef __VMS
    if (use_phys) {
        memfd = open(device_name, o_flags);
        if (memfd == -1) {
            fprintf(stderr, "failed to open %s for physical memory: %s\n",
                    device_name, strerror(errno));
            exit(EXIT_FAIL_NONSTARTER);
        }
        buf = (void volatile *) mmap(0, wantbytes, PROT_READ | PROT_WRITE,
                                     MAP_SHARED | MAP_LOCKED, memfd,
                                     physaddrbase);
        if (buf == MAP_FAILED) {
            fprintf(stderr, "failed to mmap %s for physical memory: %s\n",
                    device_name, strerror(errno));
            exit(EXIT_FAIL_NONSTARTER);
        }

        if (mlock((void *) buf, wantbytes) < 0) {
            fprintf(stderr, "failed to mlock mmap'ed space\n");
            do_mlock = 0;
        }

        bufsize = wantbytes; /* accept no less */
        aligned = buf;
        done_mem = 1;
    }
#endif

    while (!done_mem) {
        while (!buf && wantbytes) {
#ifdef __VMS
            if (lib$get_vm_64(&wantbytes, &buf) != SS$_NORMAL) {
                buf = NULL;
            }
#if 0
            /* The default limits are too small for this to be useful. */
            void *locked_buf;
            unsigned __int64 locked_len;
            int rc = sys$lkwset_64(buf, wantbytes, PSL$C_USER,
                                   &locked_buf, &locked_len);
            switch (rc) {
            case SS$_WASCLR:
            case SS$_WASSET:
               printf("locked pages.\n");
               break;

            case SS$_LCKPAGFUL:
               printf("system-wide page lock limit reached\n");
               lib$free_vm_64(&wantbytes, &buf);
               buf = NULL;
               break;

            case SS$_LKWSETFUL:
               printf("locked working set is full\n");
               lib$free_vm_64(&wantbytes, &buf);
               buf = NULL;
               break;

            default:
               printf("VMS error: %d\n", rc);
               /* continue anyway */
               break;
            }
#endif
#else
            buf = (void volatile *) malloc(wantbytes);
#endif
            if (!buf) wantbytes -= pagesize;
        }
        bufsize = wantbytes;
        printf("got  %lluMB (%llu bytes)", (ull) wantbytes >> 20,
            (ull) wantbytes);
        fflush(stdout);
#ifndef __VMS
        if (do_mlock) {
            printf(", trying mlock ...");
            fflush(stdout);
            if ((size_t) buf % pagesize) {
                /* printf("aligning to page -- was 0x%tx\n", buf); */
                aligned = (char volatile *) ((size_t) buf & pagesizemask) + pagesize;
                /* printf("  now 0x%tx -- lost %d bytes\n", aligned,
                 *      (size_t) aligned - (size_t) buf);
                 */
                bufsize -= ((size_t) aligned - (size_t) buf);
            } else {
                aligned = buf;
            }
            /* Try mlock */
            if (mlock((void *) aligned, bufsize) < 0) {
                switch(errno) {
                    case EAGAIN: /* BSDs */
                        printf("over system/pre-process limit, reducing...\n");
                        free((void *) buf);
                        buf = NULL;
                        wantbytes -= pagesize;
                        break;
                    case ENOMEM:
                        printf("too many pages, reducing...\n");
                        free((void *) buf);
                        buf = NULL;
                        wantbytes -= pagesize;
                        break;
                    case EPERM:
                        printf("insufficient permission.\n");
                        printf("Trying again, unlocked:\n");
                        do_mlock = 0;
                        free((void *) buf);
                        buf = NULL;
                        wantbytes = wantbytes_orig;
                        break;
                    default:
                        printf("failed for unknown reason.\n");
                        do_mlock = 0;
                        done_mem = 1;
                }
            } else {
                printf("locked.\n");
                done_mem = 1;
            }
        } else
#endif
        {
            done_mem = 1;
            printf("\n");
        }
    }

    if (!do_mlock) fprintf(stderr, "Continuing with unlocked memory; testing "
                           "will be slower and less reliable.\n");

    /* Do alighnment here as well, as some cases won't trigger above if you
       define out the use of mlock() (cough HP/UX 10 cough). */
#ifdef __VMS
    if ((ul) buf % pagesize) {
        aligned = (char *) ((ul) buf & pagesizemask) + pagesize;
        bufsize -= ((ul) aligned - (ul) buf);
    } else {
        aligned = buf;
    }
#else
    if ((size_t) buf % pagesize) {
        /* printf("aligning to page -- was 0x%tx\n", buf); */
        aligned = (char volatile *) ((size_t) buf & pagesizemask) + pagesize;
        /* printf("  now 0x%tx -- lost %d bytes\n", aligned,
         *      (size_t) aligned - (size_t) buf);
         */
        bufsize -= ((size_t) aligned - (size_t) buf);
    } else {
        aligned = buf;
    }
#endif

    halflen = bufsize / 2;
    count = halflen / sizeof(ul);
    bufa = (ulv *) aligned;
#ifdef __VMS
    bufb = (ulv *) ((ul) aligned + halflen);
#else
    bufb = (ulv *) ((size_t) aligned + halflen);
#endif

    for(loop=1; ((!loops) || loop <= loops); loop++) {
        printf("Loop %lu", loop);
        if (loops) {
            printf("/%lu", loops);
        }
        printf(":\n");
        printf("  %-20s: ", "Stuck Address");
        fflush(stdout);
        if (!test_stuck_address(aligned, bufsize / sizeof(ul))) {
             printf("ok\n");
        } else {
            exit_code |= EXIT_FAIL_ADDRESSLINES;
        }
        for (i=0;;i++) {
            if (!tests[i].name) break;
            /* If using a custom testmask, only run this test if the
               bit corresponding to this test was set by the user.
             */
            if (testmask && (!((1 << i) & testmask))) {
                continue;
            }
            printf("  %-20s: ", tests[i].name);
            fflush(stdout);
            if (!tests[i].fp(bufa, bufb, count)) {
                printf("ok\n");
            } else {
                exit_code |= EXIT_FAIL_OTHERTEST;
            }
            fflush(stdout);
            /* clear buffer */
#ifdef __VMS
            ul clearbytes = wantbytes;
            char *clearbuf = (char *) buf;
            const ul max_clear_bytes = 0xffff0000;    /* max size that works */
            while (clearbytes > max_clear_bytes) {
                memset(clearbuf, 255, max_clear_bytes);
                clearbuf += max_clear_bytes;
                clearbytes -= max_clear_bytes;
            }
            memset(clearbuf, 255, clearbytes);
#else
            memset((void *) buf, 255, wantbytes);
#endif
        }
        printf("\n");
        fflush(stdout);
    }
#ifndef __VMS
    if (do_mlock) munlock((void *) aligned, bufsize);
#endif
    printf("Done.\n");
    fflush(stdout);
    exit(exit_code);
}
