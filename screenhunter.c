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
    char *name;       /* base file name */
    ushort colors;    /* colors per pixel */
    uint width;       /* width in pixels */
    uint height;      /* height in pixels */
    png_byte type;    /* png color type */
    png_infop info;   /* png info struct*/
    png_bytep *rows;  /* png row pointers */
    png_structp png;  /* png struct pointer */
} Target;


/* Default options' values */
char optDontClick = 0;
char optOneMatch = 0;
char optHoldCursor = 0;
char optRandom = 0;


void msleep(int milliseconds)
{
    struct timespec ts;

    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;

    nanosleep(&ts, NULL);
}

int randr(int min, int max)
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

void click(Display *display, Window *window, int button)
{
    XEvent event;

    memset(&event, 0, sizeof(event));

    event.type = ButtonPress;
    event.xbutton.button = button;
    event.xbutton.same_screen = True;

    XQueryPointer(display, *window, &event.xbutton.root, &event.xbutton.window,
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
        fprintf(stderr, "Error on mousedown\n");
    }
    XFlush(display);

    msleep((optRandom) ? randr(9, 59) : 10);

    event.type = ButtonRelease;
    event.xbutton.state = 0x100;

    if (XSendEvent(display, PointerWindow, True, 0xfff, &event) == 0) {
        fprintf(stderr, "Error on mouseup\n");
    }
    XFlush(display);
}

char seekandclick(char *file_name, Display *display, Window window, XImage *screenshot)
{
    FILE *fp;
    Target target;
    uchar header[8];
    Window root, child;
    int root_x, root_y, win_x, win_y;
    uint mask;
    char res = 0;

    target.name = basename(file_name);

    /* Open file */
    if (!(fp = fopen(file_name, "rb"))) {
        fprintf(stderr, "%s: could not be opened for reading", target.name);
        res = -1;
        goto exit;
    }

    fread(header, 1, 8, fp);
    if (png_sig_cmp(header, 0, 8)) {
        fprintf(stderr, "%s: not a PNG file", target.name);
        res = -1;
        goto exit;
    }

    /* Initialize stuff */
    target.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!target.png) {
        fprintf(stderr, "%s: png_create_read_struct failed", target.name);
        res = -1;
        goto exit;
    }

    target.info = png_create_info_struct(target.png);
    if (!target.info) {
        fprintf(stderr, "%s: png_create_info_struct failed", target.name);
        res = -1;
        goto exit;
    }

    if (setjmp(png_jmpbuf(target.png))) {
        fprintf(stderr, "%s: error during init_io", target.name);
        res = -1;
        goto exit;
    }

    png_init_io(target.png, fp);
    png_set_sig_bytes(target.png, 8);

    png_read_info(target.png, target.info);

    target.type = png_get_color_type(target.png, target.info);

    if (target.type == PNG_COLOR_TYPE_RGB) {
        target.colors = 3;
    } else if (target.type == PNG_COLOR_TYPE_RGBA) {
        target.colors = 4;
        /* Give a warning about non-opaque pixels */
        fprintf(stderr, "%s: alpha-channel found, screenhunter will treat all non-opaque pixels as positive matches\n", target.name);
    } else {
        fprintf(stderr, "%s: color type of input file must be PNG_COLOR_TYPE_RGB (%d)"
                " or PNG_COLOR_TYPE_RGBA (%d)"
                ", (detected %d)\n",
                target.name,
                PNG_COLOR_TYPE_RGB,
                PNG_COLOR_TYPE_RGBA,
                target.type);
        res = -1;
        goto exit;
    }

    if (png_get_bit_depth(target.png, target.info) != 8) {
        fprintf(stderr, "%s: incorrect bit depth of %d (expected 8)\n", target.name, png_get_bit_depth(target.png, target.info));
        res = -1;
        goto exit;
    }

    /* Read file */
    if (setjmp(png_jmpbuf(target.png))) {
        fprintf(stderr, "%s: error during read_image", target.name);
        res = -1;
        goto exit;
    }

    target.width = png_get_image_width(target.png, target.info);
    target.height = png_get_image_height(target.png, target.info);

    target.rows = (png_bytep*)malloc(sizeof(png_bytep) * target.height);
    for (uint target_y = 0 ; target_y < target.height ; target_y++) {
        target.rows[target_y] = (png_byte*)malloc(png_get_rowbytes(target.png, target.info));
    }

    png_read_image(target.png, target.rows);

    /* Get current cursor position */
    XQueryPointer(display, window, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);

/*
 *
 * 1. loop through screen's pixels, searching for the first pixel of target PNG image
 * 2. if the pixel matches, initiate a new nested loop
 * 3. within that loop we continue scanning the screen's pixels along with the target's
 * 4. if any of the pixels mismatch, we break that loop and it brings us back to #1
 * 5. if all pixels match, we generate a click in the middle of the matched sector
 *
 */

    png_byte *target_row;
    png_byte *target_pixel;
    uchar *screenshot_pixel;
    uchar target_has_alpha = target.colors - 3;
    ulong target_amount_of_pixels = target.width * target.height;
    ulong so_far_so_good = 0;
    ulong found = 0;

    for (uint y = 0 ; y < screenshot->height - target.height ; y++) {
        for (uint x = 0 ; x < screenshot->width - target.width ; x++) {
            screenshot_pixel = (uchar*)&(screenshot->data[screenshot->bytes_per_line * y + screenshot->bits_per_pixel * x / NBBY]);

            if ((target_has_alpha && (*target.rows)[3] < 255) ||
                (screenshot_pixel[2] == (*target.rows)[0] &&
                 screenshot_pixel[1] == (*target.rows)[1] &&
                 screenshot_pixel[0] == (*target.rows)[2])
            ) {
                so_far_so_good = 1;

                for (uint ty = 0 ; ty < target.height && so_far_so_good ; ty++) {
                    target_row = target.rows[ty];

                    for (uint tx = 0 ; tx < target.width && so_far_so_good ; tx++) {
                        target_pixel = &(target_row[tx * target.colors]);

                        screenshot_pixel = (uchar*)&(screenshot->data[screenshot->bytes_per_line * (y+ty) + screenshot->bits_per_pixel * (x+tx) / NBBY]);

                        if ((target_has_alpha && target_pixel[3] < 255) ||
                            (screenshot_pixel[2] == target_pixel[0] &&
                             screenshot_pixel[1] == target_pixel[1] &&
                             screenshot_pixel[0] == target_pixel[2])
                        ) {
                            so_far_so_good++;

                            if (so_far_so_good == target_amount_of_pixels) {
                                found++;

                                if (!optDontClick) {
                                    aim(display, &window, x, y, x + target.width, y + target.height);
                                    click(display, &window, Button1);
                                }

                                if (optOneMatch) {
                                    y = screenshot->height - target.height; /* break out of the topmost loop */
                                }
                            }
                        } else {
                            so_far_so_good = 0;
                        }
                    }
                }
            }
        }
    }

    if (found) {
        res = 1;

        if (!optDontClick && !optHoldCursor) {
            /* Return cursor to its original position */
            aim(display, &window, root_x, root_y, root_x, root_y);
        }
    }

    /* Clean-up */
    for (uint j = 0 ; j < target.height ; j++) {
        free(target.rows[j]);
    }
    free(target.rows);

exit:
    png_destroy_read_struct(&target.png, &target.info, NULL);
    fclose(fp);

    return res;
}

int main(int argc, char **argv)
{
    char res = 0;
    int opt;

    Display *display = XOpenDisplay(0);

    if (!display) {
        fprintf(stderr, "Error opening display\n");
        res = -1;
        goto free_display_and_exit;
    }

    while ((opt = getopt(argc, argv, "sohr")) != -1)
    {
        switch (opt)
        {
            case 's': optDontClick = 1; break;
            case 'o': optOneMatch = 1; break;
            case 'h': optHoldCursor = 1; break;
            case 'r': optRandom = 1; break;

            default:
                /* No arguments given */
                fprintf(stderr, "Usage: %s [-sohr] target.png\n", argv[0]);
                res = -1;
                goto free_display_and_exit;
        }
    }

    Window window = DefaultRootWindow(display);
    XWindowAttributes windowAttrs;

    XGetWindowAttributes(display, window, &windowAttrs);

    XImage *screenshot = XGetImage(display, window, 0, 0,
                           windowAttrs.width, windowAttrs.height,
                           AllPlanes, ZPixmap);

    if (optRandom) {
        srand(time(NULL));
    }

    //printf("%d (%d)\n", optind, argc);
    if (optind < argc) {
        do {
            res = seekandclick(argv[optind], display, window, screenshot);
        } while (++optind < argc);
    }

//free_everything_and_exit:
    XFree(screenshot);
free_display_and_exit:
    XFlush(display);
    XCloseDisplay(display);
//exit:
    return res;
}
