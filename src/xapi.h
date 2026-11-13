/* What TwitterPS5 needs from X's web API, apart from the network: the
user's session from the cookie file, the requests x.com makes for the
timelines, a post's replies, profiles, notifications and bookmarks, and for
likes, reposts and posts, and what their answers hold. */

#pragma once

#include <stddef.h>

#include "https.h"
#include "json.h"
#include "play.h"

// the user copies two cookies of their signed-in x.com into this file
#define X_SESSION_PATH "/data/TwitterPS5/session.txt"

// every x.com page sends this token; it is the same for everyone, the
// signed-in user comes from the cookies
#define X_BEARER "Bearer AAAAAAAAAAAAAAAAAAAAANRILgAAAAAAnNwIzUejRCOuH5E6I8xnZz4puTs" \
  "%3D1Zv7ttfk8LF81IUq16cHjhLTvJu4FA33AGWWjCpTnA"

#define X_USER_AGENT "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
  "(KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36"


#define X_HEADER_COUNT 9


typedef struct x_session {
  char auth_token[128];
  char ct0[256];
} x_session_t;

typedef struct x_request {
  char           cookie[512];
  https_header_t headers[X_HEADER_COUNT];
} x_request_t;

#define X_MAX_MEDIA  4
#define X_CURSOR_MAX 512
#define X_REPLY_MAX  8000 // bytes: 2000 characters of 4 bytes, a long post of X Premium
#define X_POST_CHARS 280  // as X counts them (x_post_length), over which a post is a long one
#define X_QUERY_MAX  200  // bytes of a search
#define X_BODY_MAX   (3072 + 6 * X_REPLY_MAX) // of a POST, with every byte escaped

// the badge next to a name
enum { X_BADGE_NONE, X_BADGE_BLUE, X_BADGE_GOLD, X_BADGE_GREY };

#define X_VIDEO_URL  200
#define X_MEDIA_IDS  4    // pictures a post takes, or one video

typedef struct x_media {
  char url[192]; // on pbs.twimg.com; for a video or GIF, its still
  int  width;    // of the original, 0 when unknown
  int  height;
  int  video;
  // a video's or GIF's MP4 on video.twimg.com: the sharpest X has (its
  // 1080p, when it has one) that is not too big, in the timeline and over
  // the whole screen alike, and the smallest; empty when X sent none
  char video_url[X_VIDEO_URL];
  char video_small[X_VIDEO_URL];
  int  duration_ms;  // 0 for a GIF, or when unknown
} x_media_t;

// what an x_post_t is: a post, a notice, a conversation of Chat (its
// number in id, who it is with in user, name and avatar, the last message in
// text) or a direct message (its number in id, who sent it in user_id,
// user, name and avatar); a trend of Explore (its name in name, what to search
// for it in text, where it trends in quote_name, how much it is posted in
// quote_text, its rank in icon, its picture in avatar, and group set for a
// story of the news); or a list (its number in id, its name in name, its
// description in quote_text, its banner in avatar, its owner in user, user_id,
// quote_name and quote_avatar, its members in likes and followers in reposts,
// request set when it is private, following when the user follows it)
// X_KIND_SCHEDULED: a post of the user's that X sends later: its number (id),
// when (time), its text, and icon 1 when X says sending it failed
enum { X_KIND_POST, X_KIND_NOTICE, X_KIND_CHAT, X_KIND_DM, X_KIND_TREND, X_KIND_LIST, X_KIND_USER,
       X_KIND_SCHEDULED };

// what a direct message carries besides its text, in icon; X_DM_INVITE: its
// text is an invite to play (play_invite_parse)
enum { X_DM_TEXT, X_DM_PHOTO, X_DM_VIDEO, X_DM_GIF, X_DM_POST, X_DM_INVITE };

#define X_ID_MAX 64 // a post's number, or a conversation's ("123-456")

// what a notice is about, from its icon
enum { X_ICON_OTHER, X_ICON_LIKE, X_ICON_REPOST, X_ICON_FOLLOW, X_ICON_REPLY };

#define X_MAX_FACES 4

// a poll's choices, and the bytes of one's label (25 characters)
#define X_POLL_MAX   4
#define X_POLL_LABEL 104

typedef struct x_post {
  int       kind;               // X_KIND_*
  char      id[X_ID_MAX];       // the post's number (rest_id), or empty; of a notice, the
                                // post it is about; of a conversation, its number
  char      user[64];           // screen name of the author
  char      user_id[24];        // the author's number, or empty
  char      name[128];          // display name of the author
  char      avatar[192];        // the author's picture on pbs.twimg.com, or empty
  int       badge;              // X_BADGE_*
  char      reposter[64];       // who reposted it into the timeline, or empty
  char      reposter_name[128];
  long long time;               // when it was posted, Unix time, or 0
  long long replies;            // the counts, -1 when the answer has none
  long long reposts;
  long long likes;
  long long views;
  long long bookmarks;          // how many keep it among their bookmarks, -1 when not said
  int       liked;              // the signed-in user liked it
  int       reposted;           // or reposted it
  int       bookmarked;         // or keeps it among their bookmarks
  int       pinned;             // on top of its author's profile
  int       media_count;
  x_media_t media[X_MAX_MEDIA];
  char      quote_user[64];     // the post it quotes, if any; of a notice, the post it is about
  char      quote_name[128];
  char      quote_avatar[192];
  int       quote_badge;
  char      quote_text[320];
  char      text[1024];         // links shown as X shows them, media links gone; of a
                                // notice, what it says
  int       icon;               // of a notice: X_ICON_*
  int       face_count;         // and the people in it, the first being user
  char      faces[X_MAX_FACES][192];
  int       unread;             // a conversation with a message the user has not read
  int       request;            // a conversation from someone the user does not follow yet
  int       mine;               // a message the user sent, or a conversation whose last one is
  int       group;              // a conversation of more than two
  int       following;          // the user follows its author
  long long edit_until;         // until when its author may still edit it (Unix time), or 0
  // its poll: 2 to 4 choices, or 0 for none
  int       poll_count;
  char      poll_label[X_POLL_MAX][X_POLL_LABEL];
  long long poll_votes[X_POLL_MAX];
  long long poll_end;           // when it closes, Unix time, or 0
  int       poll_final;         // X counted its last votes
  int       poll_voted;         // the signed-in user's choice, 1 to 4, or 0
  char      poll_card[48];      // its card ("card://<number>") to vote with, or empty
  char      poll_name[32];      // the card's name ("poll2choice_text_only")
  // the game its author plays, when their profile's location says it
  // (play_game_of); of a conversation, of who it is with; else empty
  char      playing[PLAY_GAME];
} x_post_t;

// a profile, as its page shows it on top
typedef struct x_user {
  char      id[24];             // rest_id
  char      user[64];           // screen name
  char      name[128];
  char      avatar[192];
  char      banner[192];        // on pbs.twimg.com, or empty
  int       badge;
  char      bio[640];
  char      location[128];
  long long following;          // -1 when the answer has none
  long long followers;
  long long posts;
  long long joined;             // Unix time, or 0
  int       followed;           // the signed-in user follows them
  int       follows_you;        // they follow the signed-in user
  int       muted;              // the signed-in user muted them
  int       blocked;            // or blocked them
} x_user_t;

// the lists of posts x_feed_url asks for
enum {
  X_FEED_FOR_YOU,       // HomeTimeline
  X_FEED_FOLLOWING,     // HomeLatestTimeline
  X_FEED_NOTIFICATIONS, // NotificationsTimeline
  X_FEED_BOOKMARKS,     // Bookmarks
  X_FEED_USER,          // UserTweets, of the user with the number id
  X_FEED_POST,          // TweetDetail: the post with the number id and its replies
  X_FEED_SEARCH,        // SearchTimeline, the top posts for the text id
  X_FEED_SEARCH_LATEST, // the latest posts for it
  X_FEED_ARTICLES,      // UserArticlesTweets: the Articles of the user with the number id
  X_FEED_TRENDING,      // GenericTimelineById: the trends of Explore
  X_FEED_NEWS,          // and its news
  X_FEED_LISTS,         // ListsManagementPageTimeline: the user's lists
  X_FEED_LIST,          // ListLatestTweetsTimeline: the posts of the list with the number id
  X_FEED_SIDEBAR,       // ExploreSidebar: the trends of x.com's side column
  X_FEED_WHO,           // ConnectTabTimeline: who to follow (X_KIND_USER)
  X_FEED_FRIENDS,       // Following: the people the user with the number id follows (X_KIND_USER)
};


/**
 * read auth_token and ct0 from the cookie file, in the forms people copy
 * them in: name=value lines, a Cookie header, or rows of the browser's
 * developer tools; returns 0, -1 without auth_token, -2 without ct0
 **/
int x_session_parse(x_session_t *s, const char *text, size_t len);

/**
 * x_session_parse on the file at path; -3 when it is missing or empty
 **/
int x_session_read(x_session_t *s, const char *path);

/**
 * the headers x.com sends with its API requests, for session s, which must
 * stay alive while the headers are used
 **/
void x_request_init(x_request_t *r, const x_session_t *s);

/**
 * URL of the Following timeline (HomeLatestTimeline) or, when following is
 * 0, of For You (HomeTimeline), asking for count posts: from the top, or
 * with a cursor from x_timeline_cursor, the posts after an answer's
 **/
int x_timeline_url(char *buf, size_t size, int count, int following, const char *cursor);

/**
 * URL of one of the X_FEED_* lists, asking for count posts, with id the
 * number of the user or the post it is about, or the text searched for
 * (UTF-8, at most X_QUERY_MAX bytes), else NULL; from the top or with a
 * cursor from x_timeline_cursor; returns 0 or -1
 **/
int x_feed_url(char *buf, size_t size, int feed, const char *id, int count, const char *cursor);

/**
 * URL of the profile of the user with the number id (UserByRestId) or, with
 * id NULL or empty, of the signed-in user (Viewer); returns 0 or -1
 **/
int x_user_url(char *buf, size_t size, const char *id);

/**
 * the profile in an answer to x_user_url's request; returns 0, or -1 when
 * it has none
 **/
int x_user_parse(const json_node_t *root, x_user_t *user);

/**
 * the cursor of the posts that come after a timeline answer's; returns 0,
 * or -1 with an empty buf when it has none
 **/
int x_timeline_cursor(const json_node_t *root, char *buf, size_t size);

/**
 * posts of a timeline answer in order, a pinned one first, and the notices
 * of a notifications one among them; stores at most max of them, returns
 * how many it stored and sets *total to how many the answer has
 **/
int x_timeline_posts(const json_node_t *root, x_post_t *posts, int max, int *total);

/**
 * URL and JSON body of the POST that likes the post with that number or,
 * with like 0, takes the like back; returns 0, or -1 when id is not a post
 * number or they do not fit
 **/
int x_like_request(char *url, size_t usize, char *body, size_t bsize, const char *id, int like);

/**
 * URL and JSON body of the POST of a reply, text (UTF-8, at most
 * X_REPLY_MAX bytes), to the post with that number, or of a post of its own
 * when id is NULL or empty; returns 0 or -1
 **/
int x_reply_request(char *url, size_t usize, char *body, size_t bsize, const char *id,
                    const char *text);

/**
 * the same with up to X_MEDIA_IDS pictures, or one video, that upload.x.com
 * took (their media_id_string); the text may then be empty
 **/
int x_post_request(char *url, size_t usize, char *body, size_t bsize, const char *id,
                   const char *text, const char *const *media, int media_count);

// what else a post is (x_compose_request)
typedef struct x_compose {
  const char *reply_to;    // the number of the post it answers, or NULL
  const char *quote_user;  // the screen name and number of the post it quotes, or NULL
  const char *quote_id;
  const char *edit_of;     // the number of the user's post it edits, or NULL
  const char *card_uri;    // its poll's card (x_poll_card_request), or NULL
} x_compose_t;

/**
 * URL and form body of the POST that makes the card of a poll with count
 * choices (2 to 4, 25 characters each at most) open for minutes (5 to
 * 10080), as x.com's composer makes it on caps.x.com; 0 or -1
 **/
int x_poll_card_request(char *url, size_t usize, char *body, size_t bsize, const char *const *choices,
                        int count, int minutes);

/**
 * the card that answer made ("card://<number>"); 0 or -1
 **/
int x_card_uri(const json_node_t *root, char *uri, size_t size);

/**
 * URL and form body of the POST that votes choice (1 to 4) in the poll of
 * the post id, whose card and card name x_timeline_posts gave; 0 or -1
 **/
int x_vote_request(char *url, size_t usize, char *body, size_t bsize, const char *card, const char *id,
                   const char *name, int choice);

/**
 * URL and JSON body of the POST of a post, as x.com's composer sends it:
 * CreateTweet, or CreateNoteTweet when the text is longer than X_POST_CHARS
 * (X Premium); a reply, a quote or an edit of the user's post by how; 0 or -1
 **/
int x_compose_request(char *url, size_t usize, char *body, size_t bsize, const x_compose_t *how,
                      const char *text, const char *const *media, int media_count);

/**
 * a text's length as X counts it: most scripts' characters 1, the rest
 * (CJK, emoji) 2
 **/
int x_post_length(const char *s);

/**
 * URL and JSON body of the POST that pins the user's post with that number
 * to their profile or, with pin 0, unpins it; 0 or -1
 **/
/**
 * URL and JSON body of the POST that schedules a post of the user's own (280
 * characters at most) with media_count uploaded media ids, to go out at the
 * Unix time at, as x.com's composer schedules it; 0 or -1
 **/
int x_schedule_request(char *url, size_t usize, char *body, size_t bsize, const char *text,
                       const char *const *media, int media_count, long long at);

int x_pin_request(char *url, size_t usize, char *body, size_t bsize, const char *id, int pin);

// what x_user_request does to someone
enum { X_FOLLOW, X_UNFOLLOW, X_MUTE, X_UNMUTE, X_BLOCK, X_UNBLOCK };

/**
 * URL and form body (application/x-www-form-urlencoded) of the POST that
 * follows, mutes or blocks the user with the number id, or takes it back,
 * as x.com does; 0 or -1
 **/
int x_user_request(char *url, size_t usize, char *body, size_t bsize, int what, const char *id);

/**
 * URL and form body of the POST that sets the signed-in user's profile
 * location (at most PLAY_LOCATION, empty to clear it), as x.com's profile
 * editor sends it; X changes only what the form names; 0 or -1
 **/
int x_location_request(char *url, size_t usize, char *body, size_t bsize, const char *location);

/**
 * the language X writes its own texts in (notifications, dates): two
 * letters such as "pt"; "en" until it is set
 **/
void x_set_language(const char *code);

/**
 * the language x_set_language set
 **/
const char *x_language(void);

// how an upload to upload.x.com stands, from its STATUS or FINALIZE answer
enum { X_UPLOAD_READY, X_UPLOAD_WAIT, X_UPLOAD_FAILED };

/**
 * the media_id_string of an INIT answer into id; returns 0 or -1
 **/
int x_upload_id(const json_node_t *root, char *id, size_t size);

/**
 * X_UPLOAD_* of a FINALIZE or STATUS answer, and the seconds to wait
 * before asking again
 **/
int x_upload_state(const json_node_t *root, int *wait_s);

// the steps of an upload to upload.x.com, as x.com's composer takes them
enum { X_UPLOAD_INIT, X_UPLOAD_APPEND, X_UPLOAD_FINALIZE, X_UPLOAD_STATUS };

#define X_UPLOAD_PART  (4 << 20) // bytes an APPEND carries, at most
#define X_UPLOAD_PHOTO (5 << 20) // the biggest picture X takes
#define X_UPLOAD_VIDEO (512LL << 20)

/**
 * URL of a step: INIT for total bytes of type ("image/jpeg", "image/png",
 * "video/mp4"), for a direct message when segment is X_UPLOAD_FOR_DM, else
 * for a post; the others for the media_id INIT gave, APPEND with the
 * part's number; returns 0 or -1
 **/
#define X_UPLOAD_FOR_DM 1
int x_upload_url(char *buf, size_t size, int step, const char *media_id, long long total,
                 const char *type, int segment);

/**
 * the multipart/form-data around an APPEND's part: what goes before its
 * bytes and after them, and the content-type header's value; returns 0 or -1
 **/
int x_upload_form(const char *boundary, char *head, size_t hsize, char *tail, size_t tsize,
                  char *type, size_t ysize);

/**
 * URL and JSON body of the POST that reposts the post with that number or,
 * with repost 0, takes the repost back; returns 0 or -1
 **/
int x_repost_request(char *url, size_t usize, char *body, size_t bsize, const char *id, int repost);

/**
 * URL and JSON body of the POST that deletes the user's own post with that
 * number (DeleteTweet, as x.com's menu sends it); returns 0 or -1
 **/
int x_delete_request(char *url, size_t usize, char *body, size_t bsize, const char *id);

/**
 * URL of the user's scheduled posts (FetchScheduledTweets, as x.com's
 * unsent posts read them); returns 0 or -1
 **/
int x_scheduled_url(char *buf, size_t size);

/**
 * the scheduled posts of that answer (X_KIND_SCHEDULED), soonest first, up
 * to max of them (none with NULL); how many there are goes into *total
 **/
int x_scheduled_posts(const json_node_t *root, x_post_t *posts, int max, int *total);

/**
 * URL and JSON body of the POST that takes back the scheduled post with
 * that number (DeleteScheduledTweet); returns 0 or -1
 **/
int x_unschedule_request(char *url, size_t usize, char *body, size_t bsize, const char *id);

/**
 * the URL of the user's conversations as x.com's Messages reads them first
 * (the direct messages; X Chat's encrypted ones are not in it); 0 or -1
 **/
int x_dm_inbox_url(char *buf, size_t size);

/**
 * the URL of a conversation's newest messages; 0 or -1
 **/
int x_dm_conversation_url(char *buf, size_t size, const char *conversation);

/**
 * the conversations of an inbox answer into posts (X_KIND_CHAT), newest
 * first, me being the signed-in user's number; with posts NULL, how many
 * there are
 **/
int x_dm_inbox(const json_node_t *root, const char *me, x_post_t *posts, int max);

/**
 * the messages of a conversation's answer into posts (X_KIND_DM), oldest
 * first; with posts NULL, how many there are
 **/
int x_dm_messages(const json_node_t *root, const char *conversation, x_post_t *posts, int max);

/**
 * URL and form body of the POST that marks a conversation read up to its
 * message last (a number), as x.com's Messages does when it is opened; 0 or -1
 **/
int x_dm_read_request(char *url, size_t usize, char *body, size_t bsize, const char *conversation,
                      const char *last);

/**
 * URL and JSON body of the POST that sends text to a conversation, as
 * x.com's Messages does (new2.json), request_id a new UUID; 0 or -1
 **/
int x_dm_send_request(char *url, size_t usize, char *body, size_t bsize, const char *conversation,
                      const char *text, const char *request_id);

/**
 * the same with a picture or a video that upload.x.com took for a direct
 * message (X_UPLOAD_FOR_DM), its media_id; the text may then be empty
 **/
int x_dm_media_request(char *url, size_t usize, char *body, size_t bsize, const char *conversation,
                       const char *text, const char *media_id, const char *request_id);

/**
 * URL and JSON body of the POST that adds the post with that number to the
 * bookmarks or, with add 0, takes it out; returns 0 or -1
 **/
int x_bookmark_request(char *url, size_t usize, char *body, size_t bsize, const char *id, int add);

/**
 * whether the answer to one of those POSTs says it was done: 0, X's error
 * code (*message points at its text), or -1 when it says neither
 **/
int x_action_done(const json_node_t *root, const char **message);

/**
 * code of the first error in an answer, -1 when it has none; *message
 * points at its text, or NULL
 **/
int x_first_error(const json_node_t *root, const char **message);

/*
 * Grok, as x.com asks it with the user's session: CreateGrokConversation
 * starts a conversation, and add_response.json on grok.x.com (api.x.com
 * before) adds a message to it, whose answer comes as one JSON object a
 * line, the words in result.message. The request's shape follows what
 * x.com's own page sends (community notes of it; not documented by X).
 */
#define X_GROK_HOST     "grok.x.com"
#define X_GROK_HOST_OLD "api.x.com"
#define X_GROK_MODEL    "grok-3"
#define X_GROK_ID_MAX   64   // a conversation's id
#define X_GROK_ANSWER   4096 // bytes of an answer kept
#define X_GROK_QUESTION 600  // bytes of a question

/**
 * URL and JSON body of the POST that starts a conversation with Grok;
 * 0 or -1
 **/
int x_grok_create_request(char *url, size_t usize, char *body, size_t bsize);

/**
 * the id of the conversation in the answer to it, into id; 0, or -1
 **/
int x_grok_conversation(const json_node_t *root, char *id, size_t size);

/**
 * what Grok is asked for a hint: the question about the game named game
 * (or none), with the rules that keep it short and without spoilers, to be
 * answered in the language named language ("European Portuguese"); 0, or -1
 * when it does not fit
 **/
int x_grok_prompt(char *out, size_t size, const char *game, const char *question, const char *language);

/**
 * URL and JSON body of the POST that sends message to the conversation id,
 * on host (X_GROK_HOST), with the model named model, or without one for
 * X's own choice; 0 or -1
 **/
int x_grok_request(char *url, size_t usize, char *body, size_t bsize, const char *host, const char *id,
                   const char *model, const char *message);

/**
 * Grok's answer in body (len bytes, changed in place), its lines' words
 * one after another without what it thinks before and without its marks
 * of bold, into out, cut at a whole character to fit size; returns how
 * many lines had words, 0 when none did, with *x_code X's error code in it
 * or -1
 **/
int x_grok_answer(char *body, size_t len, char *out, size_t size, int *x_code);

/**
 * a post on one line, cut at a whole character to fit size (at least 16)
 **/
void x_post_line(const x_post_t *post, char *buf, size_t size);

/**
 * Unix time of a created_at text such as "Tue Nov 03 21:00:00 +0000 2026",
 * or 0 when it does not read as one
 **/
long long x_parse_time(const char *created_at);
