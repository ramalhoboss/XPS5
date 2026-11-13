/* The title's screen: a 1920x1080, 2560x1440 or 3840x2160 picture drawn
by the CPU in ordinary memory and copied each frame into one of two
VideoOut buffers, by the workers (workers.h) when there are some. */

#pragma once

#include <stdint.h>

/**
 * open VideoOut and allocate the buffers for a picture height rows high
 * (1080, 1440 or 2160; 0 for the TV's own), or 1080 when the PS5 does not
 * take that; returns 0, or a negative code and *step names what failed
 **/
int video_open(int height, const char **step);

/**
 * when the size asked was more than the picture got: its rows, the step
 * that failed at it and that step's code; returns 1 then, else 0
 **/
int video_refused(int *height, int *code, const char **step);

/**
 * the picture's size
 **/
int video_width(void);
int video_height(void);

/**
 * the resolution the PS5 sends the TV, as VideoOut gave it when it opened;
 * returns its code (negative when it did not say)
 **/
int video_tv(int *w, int *h);

/**
 * the picture to draw the next frame into, video_width x video_height
 * pixels of 0xAABBGGRR, one row after the other
 **/
uint32_t *video_pixels(void);

/**
 * show the picture at the next vertical blank and wait for it
 **/
void video_present(void);

/**
 * wait for the next vertical blank without changing the screen
 **/
void video_wait(void);
