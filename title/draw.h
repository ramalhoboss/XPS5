/* Drawing for the title's screen into any 0xAABBGGRR buffer: boxes with
round corners, circles and arcs with smooth edges, pictures, and icons, which
are drawn from lines and shapes on a 24-unit grid rather than from images.
A color's alpha byte makes it see-through. */

#pragma once

#include <stdint.h>

#include "text.h"


enum {
  ICON_REPLY,
  ICON_REPOST,
  ICON_LIKE,
  ICON_LIKED,     // the heart filled
  ICON_VIEWS,
  ICON_BOOKMARK,
  ICON_SHARE,
  ICON_HOME,
  ICON_BADGE,     // the shape of a badge; ICON_CHECK goes on it
  ICON_CHECK,
  ICON_PLAY,      // a triangle, to go on a circle
  ICON_TRIANGLE,  // the controller's buttons
  ICON_CIRCLE,
  ICON_CROSS,
  ICON_SQUARE,
  ICON_UP_DOWN,
  ICON_LEFT_RIGHT,
  ICON_REFRESH,   // an arrow going round
  ICON_TOP,       // an arrow up to a line
  ICON_SEND,      // a paper plane
  ICON_BELL,      // notifications
  ICON_PERSON,    // a profile
  ICON_PLUS,      // a new post
  ICON_BACK,      // an arrow to the left
  ICON_BOOKMARKED, // the bookmark filled
  ICON_SEARCH,    // a magnifying glass
  ICON_GEAR,      // the settings
  ICON_PHOTO,     // a picture: mountains under a sun
  ICON_VIDEO,     // a film's frame with a play triangle
  ICON_PAUSE,
  ICON_SOUND,     // a speaker with its waves
  ICON_MUTE,      // a speaker crossed out
  ICON_GLOBE,     // the languages
  ICON_CHAT,      // an envelope: the messages
  ICON_CALL,      // a telephone's handset
  ICON_SPARK,     // a four-pointed star: the assistant tab
  ICON_PREMIUM,   // a seal with a tick: the subscription
  ICON_HISTORY,   // a clock going back
  ICON_ARTICLE,   // a page with lines
  ICON_MORE,      // three dots in a ring
  ICON_DOTS,      // three dots: a post's menu
  ICON_TRASH,     // a bin: delete
  ICON_LIST,      // a page of lines with dots: the lists
  ICON_LOCK,      // a padlock: private
  ICON_CLOCK,     // a clock: a post scheduled
  ICON_MOON,      // a crescent: the theme
  ICON_GAME,      // a controller: the game that runs
  ICON_POLL,      // three bars of results: a poll
  ICON_SCREEN,    // a screen on its stand: the resolution
  ICON_RISING,    // a line climbing with its arrow: a post rising
  ICON_EYE_OFF,   // an eye crossed out: a post covered for its spoilers
  ICON_LIGHT,     // a bulb: the controller's light bar
  ICON_CUP,       // a cup with its steam: a break
  ICON_PAD_BODY,  // a DualSense-like controller, in layers of their own colours:
  ICON_PAD_CORE,  //   its body; the black middle; the sticks and the PS button;
  ICON_PAD_STICKS,//   the D-pad and the face buttons; the light bar beside the
  ICON_PAD_MARKS, //   touchpad (the playing badge, ui.c)
  ICON_PAD_LIGHT,
  ICON_COUNT
};


/*
 * Places, sizes, radii and widths are in the surface's points (see
 * text_surface_t), drawn in its pixels.
 */

void draw_rect(const text_surface_t *dst, int x, int y, int w, int h, uint32_t color);

/**
 * a box with corners of radius r; with t above 0, only its border, t wide
 **/
void draw_box(const text_surface_t *dst, int x, int y, int w, int h, float r, float t,
              uint32_t color);

/**
 * the soft shadow of a box with corners of radius r: color at its edge,
 * fading out over spread pixels around it; inside, only along its edges
 * (what casts it covers the rest)
 **/
void draw_shadow(const text_surface_t *dst, int x, int y, int w, int h, float r, float spread,
                 uint32_t color);

/**
 * the border of a box, t wide, lit from above: color at its top, going down
 * to bottom times its alpha at its bottom
 **/
void draw_rim(const text_surface_t *dst, int x, int y, int w, int h, float r, float t,
              uint32_t color, float bottom);

/**
 * paint color over what lies outside the round corners of radius r of the
 * box, which cuts a picture drawn there to that box (or, with r = w / 2, to
 * a circle)
 **/
void draw_corners(const text_surface_t *dst, int x, int y, int w, int h, float r,
                  uint32_t color);

/**
 * a picture of text_size(dst, w) x text_size(dst, h) pixels (w x h at
 * scale 1), one row after the other
 **/
void draw_picture(const text_surface_t *dst, int x, int y, int w, int h,
                  const uint32_t *pixels);

/**
 * the same cut to a box with corners of radius r (a circle with r = w / 2),
 * with smooth edges over whatever is under it
 **/
void draw_picture_round(const text_surface_t *dst, int x, int y, int w, int h, float r,
                        const uint32_t *pixels);

/**
 * a picture of pw x ph pixels over the box of w x h points, cut to its
 * corners of radius r; at another size than the box's pixels, each pixel
 * takes the picture's nearest one (a video's 1080p frame on a 4K screen)
 **/
void draw_picture_scaled(const text_surface_t *dst, int x, int y, int w, int h, float r,
                         const uint32_t *pixels, int pw, int ph);

/**
 * how far the point (px, py) is from the edge of a box with corners of
 * radius r; below 0 inside
 **/
float draw_distance(float px, float py, int x, int y, int w, int h, float r);

void draw_circle(const text_surface_t *dst, float cx, float cy, float r, uint32_t color);

/**
 * the part of a ring of radius r, t wide, from angle from to angle to, in
 * radians clockwise from the top, with round ends
 **/
void draw_arc(const text_surface_t *dst, float cx, float cy, float r, float t,
              float from, float to, uint32_t color);

/**
 * an icon filling the size x size square at x, y; at most 256 pixels
 **/
void draw_icon(const text_surface_t *dst, int icon, int x, int y, int size, uint32_t color);
