/* See LICENSE for license details. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <libgen.h>
#include <string.h>
#include <sys/param.h>
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
    uint32_t width;   /* width in pixels */
    uint32_t height;  /* height in pixels */
    png_byte type;    /* png color type */
    png_infop info;   /* png info struct*/
    png_bytep *rows;  /* png row pointers */
    png_structp png;  /* png struct pointer */
} Target;

void msleep(int milliseconds)
{
    struct timespec ts;

    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;

    nanosleep(&ts, NULL);
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

    msleep(10);

    event.type = ButtonRelease;
    event.xbutton.state = 0x100;

    if (XSendEvent(display, PointerWindow, True, 0xfff, &event) == 0) {
        fprintf(stderr, "Error on mouseup\n");
    }
    XFlush(display);
}

void seekandclick(char *file_name, Display *display, Window window, XImage *screenshot)
{
    FILE *fp;
    Target target;
    uchar header[8];
    Window root, child;
    int root_x, root_y, win_x, win_y;
    uint mask;

    target.name = basename(file_name);

    /* Open file */
    if (!(fp = fopen(file_name, "rb"))) {
        fprintf(stderr, "%s: could not be opened for reading", target.name);
        goto exit;
    }

    fread(header, 1, 8, fp);
    if (png_sig_cmp(header, 0, 8)) {
        fprintf(stderr, "%s: not a PNG file", target.name);
        goto exit;
    }

    /* Initialize stuff */
    target.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!target.png) {
        fprintf(stderr, "%s: png_create_read_struct failed", target.name);
        goto exit;
    }

    target.info = png_create_info_struct(target.png);
    if (!target.info) {
        fprintf(stderr, "%s: png_create_info_struct failed", target.name);
        goto exit;
    }

    if (setjmp(png_jmpbuf(target.png))) {
        fprintf(stderr, "%s: error during init_io", target.name);
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
        fprintf(stderr, "%s: alpha-channel found, clickhunter will treat all non-opaque pixels as positive matches\n", target.name);
    } else {
        fprintf(stderr, "%s: color type of input file must be PNG_COLOR_TYPE_RGB (%d)"
                " or PNG_COLOR_TYPE_RGBA (%d)"
                ", (detected %d)\n",
                target.name,
                PNG_COLOR_TYPE_RGB,
                PNG_COLOR_TYPE_RGBA,
                target.type);
        goto exit;
    }

    if (target.info->bit_depth != 8) {
        fprintf(stderr, "%s: incorrect bit depth of %d (expected 8)\n", target.name, target.info->bit_depth);
        goto exit;
    }

    /* Read file */
    if (setjmp(png_jmpbuf(target.png))) {
        fprintf(stderr, "%s: error during read_image", target.name);
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
                                XWarpPointer(display, None, window, 0, 0, 0, 0, target.width / 2 + x, target.height / 2 + y);
                                click(display, &window, Button1);
                                found++;
                            }
                        } else {
                            so_far_so_good = 0;
                        }
                    }
                }
            }
        }
    }

    /* Return the cursor to its original position if it has been moved */
    if (found) {
        XWarpPointer(display, None, window, 0, 0, 0, 0, root_x, root_y);
    }

    /* Clean-up */
    for (uint j = 0 ; j < target.height ; j++) {
        free(target.rows[j]);
    }
    free(target.rows);

exit:
    png_destroy_read_struct(&target.png, &target.info, NULL);
    fclose(fp);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        /* No arguments given */
        fprintf(stderr, "Usage: %s button.png [icon.png [...]]\n", argv[0]);
        return -1;
    }

    Display *display = XOpenDisplay(0);
    Window window = DefaultRootWindow(display);
    XWindowAttributes windowAttrs;
    XImage *screenshot;

    if (!display) {
        fprintf(stderr, "Error opening display\n");
        return -1;
    }

    XGetWindowAttributes(display, window, &windowAttrs);

    screenshot = XGetImage(display, window, 0, 0,
                           windowAttrs.width, windowAttrs.height,
                           AllPlanes, ZPixmap);

    for (int i = 1 ; i < argc ; i++) {
        seekandclick(argv[i], display, window, screenshot);
    }

    XFree(screenshot);
    XFlush(display);
    XCloseDisplay(display);

    return 0;
}
