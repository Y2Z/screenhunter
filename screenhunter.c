/* See LICENSE for license details. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <libgen.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>
#include <X11/Xlib.h>
#define PNG_DEBUG 3
#include <png.h>


typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned short ushort;

typedef struct {
    char *filename;         /* base file name */
    uint width;             /* width in pixels */
    uint height;            /* height in pixels */
    ushort colors;          /* colors per pixel */
    png_byte color_type;    /* png color type */
    png_infop info_ptr;     /* png info struct*/
    png_structp png_ptr;    /* png struct pointer */
    png_bytep *image_data;  /* png row pointers */
} Target;


/* Executable name */
const char *progname;

/* Default options' values */
uchar optJustScan = 0;
uchar optOneMatch = 0;
uchar optKeepPosition = 0;
uchar optRandom = 0;
uchar optVerbose = 0;
uchar optClicksPerMatch = 1;


void msleep(const uint milliseconds)
{
    struct timespec ts;

    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;

    nanosleep(&ts, NULL);
}

int randr(const int min, const int max)
{
    return rand() % (max + 1 - min) + min;
}

void aim(Display *display, Window *window, uint x1, uint y1, uint x2, uint y2)
{
    if (optRandom) { /* random point within the matching zone */
        XWarpPointer(display, None, *window, 0, 0, 0, 0,
                     randr(x1, x2), randr(y1, y2));
    } else { /* center */
        XWarpPointer(display, None, *window, 0, 0, 0, 0,
                     x1 + (x2 - x1) / 2, y1 + (y2 - y1) / 2);
    }
}

void click(Display *display, const ushort button)
{
    XEvent event;

    /* The click has to go from the root window */
    Window window = DefaultRootWindow(display);

    memset(&event, 0, sizeof(event));

    event.type = ButtonPress;
    event.xbutton.button = button;
    event.xbutton.same_screen = True;

    XQueryPointer(display, window, &event.xbutton.root, &event.xbutton.window,
                  &event.xbutton.x_root, &event.xbutton.y_root,
                  &event.xbutton.x, &event.xbutton.y, &event.xbutton.state);

    event.xbutton.subwindow = event.xbutton.window;

    while (event.xbutton.subwindow) {
        event.xbutton.window = event.xbutton.subwindow;
        XQueryPointer(display, event.xbutton.window, &event.xbutton.root,
                      &event.xbutton.subwindow, &event.xbutton.x_root,
                      &event.xbutton.y_root, &event.xbutton.x,
                      &event.xbutton.y, &event.xbutton.state);
    }

    if (XSendEvent(display, PointerWindow, True, 0xfff, &event) == 0) {
        fprintf(stderr, "%s: mousedown\n", progname);
    }

    XFlush(display);

    msleep((optRandom) ? randr(21, 99) : 30);

    event.type = ButtonRelease;
    event.xbutton.state = 0x100;

    if (XSendEvent(display, PointerWindow, True, 0xfff, &event) == 0) {
        fprintf(stderr, "%s: mouseup\n", progname);
    }

    XFlush(display);
}

int seekandclick(char *filename, Display *display,
                  Window window, XImage *snapshot)
{
    FILE *file_ptr;
    Target target;
    uchar header[8];
    int res = 0;

    target.filename = basename(filename);

    /* Open file */
    if (!(file_ptr = fopen(filename, "rb"))) {
        fprintf(stderr, "%s: file '%s' could not be opened for reading\n",
                progname, target.filename);
        res = -1;
        goto exit;
    }

    fread(header, 1, 8, file_ptr);
    if (png_sig_cmp(header, 0, 8)) {
        fprintf(stderr, "%s: '%s' is not a valid PNG file\n",
                progname, target.filename);
        res = -1;
        goto fclose_and_exit;
    }

    /* Initialize an instance of libpng read structure */
    target.png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL,
                                            NULL);
    if (!target.png_ptr) {
        fprintf(stderr, "%s: unable to process file '%s'"
                        "(png_create_read_struct failed)\n",
                progname, target.filename);
        res = -1;
        goto free_read_struct_and_exit;
    }

    target.info_ptr = png_create_info_struct(target.png_ptr);
    if (!target.info_ptr) {
        fprintf(stderr, "%s: unable to process file '%s'"
                        "(png_create_info_struct failed)\n",
                progname, target.filename);
        res = -1;
        goto free_read_struct_and_exit;
    }

    if (setjmp(png_jmpbuf(target.png_ptr))) {
        fprintf(stderr, "%s: unable to process file '%s' (init_io failed)\n",
                progname, target.filename);
        res = -1;
        goto free_read_struct_and_exit;
    } else {
        res = 0;
    }

    png_init_io(target.png_ptr, file_ptr);
    png_set_sig_bytes(target.png_ptr, 8);

    png_read_info(target.png_ptr, target.info_ptr);

    target.color_type = png_get_color_type(target.png_ptr, target.info_ptr);
    if (target.color_type == PNG_COLOR_TYPE_RGB) {
        target.colors = 3;
    } else if (target.color_type == PNG_COLOR_TYPE_RGBA) {
        target.colors = 4;
        if (optVerbose) {
            /* Give a warning about non-opaque pixels */
            fprintf(stderr, "%s: file '%s' has an alpha channel, "
                            "all non-opaque pixels "
                            "will be treated as positive matches\n",
                    progname, target.filename);
        }
    } else {
        fprintf(stderr, "%s: the required color type is either "
                        "PNG_COLOR_TYPE_RGB (%d) or PNG_COLOR_TYPE_RGBA (%d), "
                        "the input file '%s' has unsupported color type (%d)\n",
                progname,
                PNG_COLOR_TYPE_RGB, PNG_COLOR_TYPE_RGBA,
                target.filename, target.color_type);
        res = -1;
        goto free_read_struct_and_exit;
    }

    if (png_get_bit_depth(target.png_ptr, target.info_ptr) != 8) {
        fprintf(stderr, "%s: file '%s' has bit depth of %d (expected 8)\n",
                progname, target.filename,
                png_get_bit_depth(target.png_ptr, target.info_ptr));
        res = -1;
        goto free_read_struct_and_exit;
    }

    target.width = png_get_image_width(target.png_ptr, target.info_ptr);
    target.height = png_get_image_height(target.png_ptr, target.info_ptr);

    target.image_data = (png_bytep*)malloc(sizeof(png_bytep) * target.height);
    for (uint target_y = 0 ; target_y < target.height ; target_y++) {
        target.image_data[target_y] = (png_byte*)malloc(
            png_get_rowbytes(target.png_ptr, target.info_ptr)
        );
    }

    /* Read file */
    if (setjmp(png_jmpbuf(target.png_ptr))) {
        fprintf(stderr, "%s: unable to perform read_image on file '%s'\n",
                progname, target.filename);
        res = -1;
        goto free_image_data_and_exit;
    } else {
        res = 0;
    }

    png_read_image(target.png_ptr, target.image_data);

/*
 *
 * 1. we loop through window snapshot's pixels
 * 2. then we initiate a new nested loop, going through target's pixels
 * 3. as long as pixels match, we continue looping
 * 4. if any of the pixels mismatch, we break that loop and go back to 1.
 * 5. if all pixels match, we generate a click within the matching sector
 *
 */

    png_byte *target_row, *target_pixel;
    uchar *snapshot_pixel;
    uchar target_has_alpha = target.colors - 3;
    ulong target_amount_of_pixels = target.width * target.height;
    ulong ok, matches = 0;

    /* the snapshot loop */
    for (uint y = 0, ty ; y < snapshot->height - target.height ; y++)
    {
        for (uint x = 0, tx ; x < snapshot->width - target.width ; x++)
        {
            ok = 0;

            /* the target loop */
            for (ty = 0 ; ty < target.height && (ok || ty == 0) ; ty++)
            {
                target_row = target.image_data[ty];

                for (tx = 0 ; tx < target.width && (ok || tx == 0) ; tx++)
                {
                    target_pixel = &(target_row[tx * target.colors]);

                    snapshot_pixel = (uchar*)&(snapshot->data[
                                        snapshot->bytes_per_line * (y+ty) +
                                        snapshot->bits_per_pixel * (x+tx) / NBBY
                                     ]);

                    if ((target_has_alpha && target_pixel[3] < 255) ||
                        (snapshot_pixel[2] == target_pixel[0] &&
                         snapshot_pixel[1] == target_pixel[1] &&
                         snapshot_pixel[0] == target_pixel[2])
                    ) {
                        ok++;

                        if (ok == target_amount_of_pixels) {
                            matches++;

                            if (!optJustScan) {
                                aim(display, &window, x, y,
                                    x + target.width, y + target.height);

                                /* small delay before the click */
                                msleep((optRandom) ? randr(99, 399) : 30);

                                /* simulate a left mouse button click */
                                for (uchar c = optClicksPerMatch ; c ; c--) {
                                    click(display, Button1);
                                }

                                /* small delay after the click */
                                msleep((optRandom) ? randr(99, 399) : 30);
                            }

                            if (optOneMatch) {
                                /* break out of the topmost loop */
                                y = snapshot->height - target.height;
                            }
                        }
                    } else {
                        ok = 0;
                    }
                }
            }
        }
    }

    if (matches) {
        res = matches;
    }

free_image_data_and_exit:
    for (uint j = 0 ; j < target.height ; j++) {
        free(target.image_data[j]);
    }
    free(target.image_data);
free_read_struct_and_exit:
    png_destroy_read_struct(&target.png_ptr, &target.info_ptr, NULL);
fclose_and_exit:
    fclose(file_ptr);
exit:
    return res;
}

void print_usage()
{
    fprintf(stderr,
            "Usage: %s [-hvsokrc] [-w <window_id>] target1.png [target2.png]\n",
            progname);
}

int main(int argc, char **argv)
{
    int opt, ret, res;
    ulong win_id = 0;
    int root_x, root_y, win_x, win_y;
    uint mask;
    char cursorMoved = 0;

    progname = argv[0];

    if (argc < 2) {
        fprintf(stderr, "%s: missing file operand\n", progname);
        printf("Try '%s -h' for more information.\n", progname);
        res = EXIT_FAILURE;
        goto exit;
    }

    while ((opt = getopt(argc, argv, "hvsokrw:c:")) != -1)
    {
        switch (opt)
        {
            case 'v': optVerbose++; break;
            case 's': optJustScan++; break;
            case 'o': optOneMatch++; break;
            case 'k': optKeepPosition++; break;
            case 'r': optRandom++; break;
            case 'w': win_id = (unsigned)strtol(optarg, NULL, 0); break;
            case 'c': optClicksPerMatch = (unsigned)atoi(optarg); break;

            case 'h':
                print_usage();
                res = EXIT_FAILURE;
                goto exit;

            case '?':
                printf("Try '%s -h' for more information.\n", progname);
                res = EXIT_FAILURE;
                goto exit;
        }
    }

    Display *display = XOpenDisplay(0);

    if (!display) {
        fprintf(stderr, "%s: unable to open display :%d\n", progname, 0);
        res = EXIT_FAILURE;
        goto free_display_and_exit;
    }

    Window window = (win_id) ? win_id : DefaultRootWindow(display);
    Window root, child;
    XWindowAttributes windowAttrs;
    XGetWindowAttributes(display, window, &windowAttrs);
    XImage *snapshot = XGetImage(display, window, 0, 0,
                           windowAttrs.width, windowAttrs.height,
                           AllPlanes, ZPixmap);

    /* Remember the initial cursor position */
    XQueryPointer(display, window, &root, &child,
                  &root_x, &root_y, &win_x, &win_y, &mask);

    if (optRandom) {
        srand(time(NULL));
    }

    if (optind < argc) {
        do {
            ret = seekandclick(argv[optind], display, window, snapshot);

            /* Set status to failure if unable to read any of target images */
            if (ret < 0) {
                res = EXIT_FAILURE;
                break;
            } else if (ret > 0) {
                res = EXIT_SUCCESS;

                if (!optJustScan) {
                    cursorMoved = 1;
                }

                if (optOneMatch) {
                    /* Do not proceed with the rest of target files (if any) */
                    break;
                }
            }
        } while (++optind < argc);

        if (cursorMoved && !optKeepPosition) {
            /* Return cursor to its original position */
            aim(display, &window, win_x, win_y, win_x, win_y);
        }
    } else {
        fprintf(stderr, "%s: input file required\n", progname);
        print_usage();
        res = EXIT_FAILURE;
    }

//free_everything_and_exit:
    XFree(snapshot);
free_display_and_exit:
    XFlush(display);
    XCloseDisplay(display);
exit:
    return res;
}
