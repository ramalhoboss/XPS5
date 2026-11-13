/* X's web API without the network, see xapi.h.

The timeline query id and its features are what x.com sends today; they
change now and then, and X answers 404 or 400 for stale ones. These are from
TwitterInternalAPIDocument (github.com/fa0311), 2026-10-31. */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "xapi.h"

#define FOLLOWING_QUERY "GjleyyGw4xvWe5tn8EQt9w" // HomeLatestTimeline
#define FOR_YOU_QUERY   "fedFLLzmiCAuzPgA8lyh8Q" // HomeTimeline, same features

// the other lists take the same features (2026-11-10)
#define NOTIFICATIONS_QUERY "XANkUc-HLIcCl8q85rGC2A" // NotificationsTimeline
#define BOOKMARKS_QUERY     "fjf5b5Lu82lYMSBRhppK2g" // Bookmarks
#define USER_POSTS_QUERY    "D6z-FJI2pKuwEeYYxVIGbg" // UserTweets
#define ARTICLES_QUERY      "6mCid4M47t6z7U0TIQ55qA" // UserArticlesTweets, the same features
#define POST_QUERY          "dQDdKtt882nl-Km98FJGSQ" // TweetDetail
// Explore's tabs and the lists, the same features too (2026-11-11)
#define GENERIC_QUERY       "UToImY30uYcerTaw8gu21w" // GenericTimelineById
#define SIDEBAR_QUERY       "cM7LCaFTQnUWRA1jMWH6Pg" // ExploreSidebar
#define CONNECT_QUERY       "A_0eI5aoTb8yUzDUzwm5yw" // ConnectTabTimeline
#define FRIENDS_QUERY       "eDqlbOS46zda8jDT8dEQ_Q" // Following, the same features (2026-11-13)
#define LISTS_QUERY         "HeY9oqJxWKHSTCoBXaWnHg" // ListsManagementPageTimeline
#define LIST_QUERY          "UMc_lPEeKz_WiKSY8ZT7yw" // ListLatestTweetsTimeline
// the timelines of Explore's tabs: "Timeline:" and a Thrift struct naming them
#define TRENDING_TIMELINE   "VGltZWxpbmU6DAC2CwABAAAACHRyZW5kaW5nAAA"
#define NEWS_TIMELINE       "VGltZWxpbmU6DAC2CwABAAAABG5ld3MAAA"

// profiles, which take features of their own
#define USER_QUERY   "mxaYWwt52eAb6OAzYHMVzg" // UserByRestId
#define VIEWER_QUERY "TpQxQtWMTMnIAMPlMGMqNw" // Viewer

static const char user_features[] =
  "{\"hidden_profile_subscriptions_enabled\":true,"
  "\"profile_label_improvements_pcf_label_in_post_enabled\":true,"
  "\"responsive_web_profile_redirect_enabled\":true,"
  "\"rweb_tipjar_consumption_enabled\":false,"
  "\"verified_phone_label_enabled\":false,"
  "\"highlights_tweets_tab_ui_enabled\":true,"
  "\"responsive_web_twitter_article_notes_tab_enabled\":true,"
  "\"subscriptions_feature_can_gift_premium\":false,"
  "\"creator_subscriptions_tweet_preview_api_enabled\":true,"
  "\"responsive_web_graphql_timeline_navigation_enabled\":true}";

static const char viewer_features[] =
  "{\"subscriptions_upsells_api_enabled\":false,"
  "\"profile_label_improvements_pcf_label_in_post_enabled\":true,"
  "\"responsive_web_profile_redirect_enabled\":true,"
  "\"rweb_tipjar_consumption_enabled\":false,"
  "\"verified_phone_label_enabled\":false,"
  "\"creator_subscriptions_tweet_preview_api_enabled\":true,"
  "\"responsive_web_graphql_timeline_navigation_enabled\":true}";

static const char timeline_features[] =
  "{\"rweb_video_screen_enabled\":false,\"rweb_cashtags_enabled\":true,"
  "\"profile_label_improvements_pcf_label_in_post_enabled\":true,"
  "\"responsive_web_profile_redirect_enabled\":true,"
  "\"rweb_tipjar_consumption_enabled\":false,"
  "\"verified_phone_label_enabled\":false,"
  "\"creator_subscriptions_tweet_preview_api_enabled\":true,"
  "\"responsive_web_graphql_timeline_navigation_enabled\":true,"
  "\"premium_content_api_read_enabled\":false,"
  "\"communities_web_enable_tweet_community_results_fetch\":true,"
  "\"c9s_tweet_anatomy_moderator_badge_enabled\":true,"
  "\"responsive_web_grok_analyze_button_fetch_trends_enabled\":false,"
  "\"responsive_web_grok_analyze_post_followups_enabled\":true,"
  "\"rweb_cashtags_composer_attachment_enabled\":true,"
  "\"responsive_web_jetfuel_frame\":true,"
  "\"rweb_sports_post_context_enabled\":true,"
  "\"x_elections_post_context_enabled\":true,"
  "\"responsive_web_grok_share_attachment_enabled\":true,"
  "\"responsive_web_grok_annotations_enabled\":true,"
  "\"articles_preview_enabled\":true,"
  "\"responsive_web_edit_tweet_api_enabled\":true,"
  "\"graphql_is_translatable_rweb_tweet_is_translatable_enabled\":true,"
  "\"view_counts_everywhere_api_enabled\":true,"
  "\"longform_notetweets_consumption_enabled\":true,"
  "\"responsive_web_twitter_article_tweet_consumption_enabled\":true,"
  "\"content_disclosure_indicator_enabled\":true,"
  "\"content_disclosure_ai_generated_indicator_enabled\":true,"
  "\"responsive_web_grok_show_grok_translated_post\":true,"
  "\"responsive_web_grok_analysis_button_from_backend\":true,"
  "\"post_ctas_fetch_enabled\":false,"
  "\"freedom_of_speech_not_reach_fetch_enabled\":true,"
  "\"standardized_nudges_misinfo\":true,"
  "\"tweet_with_visibility_results_prefer_gql_limited_actions_policy_enabled\":true,"
  "\"longform_notetweets_rich_text_read_enabled\":true,"
  "\"longform_notetweets_inline_media_enabled\":false,"
  "\"responsive_web_nested_quote_preview_enabled\":true,"
  "\"responsive_web_grok_image_annotation_enabled\":true,"
  "\"responsive_web_grok_imagine_annotation_enabled\":true,"
  "\"responsive_web_grok_community_note_auto_translation_is_enabled\":true,"
  "\"responsive_web_enhance_cards_enabled\":false}";

// a like, and replies, the way x.com sends them (2026-11-04)
#define LIKE_QUERY   "lI07N6Otwv1PhnEgXILM7A" // FavoriteTweet
#define UNLIKE_QUERY "ZYKSe-w7KEslx3JhSIk5LA" // UnfavoriteTweet
#define REPLY_QUERY  "l4hk8uVGvR-a8kzQ4nOhPA" // CreateTweet

// reposts (2026-11-10)
#define REPOST_QUERY   "mbRO74GrOvSfRcJnlMapnQ" // CreateRetweet
#define UNREPOST_QUERY "ZyZigVsNiFO6v1dEks1eWg" // DeleteRetweet
#define DELETE_QUERY   "nxpZCY2K-I6QoFHAHeojFQ" // DeleteTweet (2026-11-10)
#define SCHEDULED_QUERY   "H2elmT2R9DLhWoo0DZFNkA" // FetchScheduledTweets, variables {} (2026-11-12)
#define UNSCHEDULE_QUERY  "CTOVqej0JBXAZSwkp1US0g" // DeleteScheduledTweet, {scheduled_tweet_id}
#define NOTE_QUERY     "seOZkIS8AysCGfPo6yYPVA" // CreateNoteTweet, the replies' features (2026-11-11)
#define SCHEDULE_QUERY "LCVzRQGxOaGnOnYH01NQXg" // CreateScheduledTweet, no features (2026-11-11)
#define PIN_QUERY      "VIHsNu89pK-kW35JpHq7Xw" // PinTweet
#define UNPIN_QUERY    "BhKei844ypCyLYCg0nwigw" // UnpinTweet

// search, with the timelines' features, and bookmarks (2026-11-10)
#define SEARCH_QUERY     "vLHjqbDv7TPFMBkIIO1nOA" // SearchTimeline
#define BOOKMARK_QUERY   "aoDbu3RHznuiSkQ9aNM67Q" // CreateBookmark
#define UNBOOKMARK_QUERY "Wlmlj2-xzyS1GN3a6cj-mQ" // DeleteBookmark

static const char reply_features[] =
  "{\"premium_content_api_read_enabled\":false,"
  "\"communities_web_enable_tweet_community_results_fetch\":true,"
  "\"c9s_tweet_anatomy_moderator_badge_enabled\":true,"
  "\"responsive_web_grok_analyze_button_fetch_trends_enabled\":false,"
  "\"responsive_web_grok_analyze_post_followups_enabled\":true,"
  "\"rweb_cashtags_composer_attachment_enabled\":true,"
  "\"responsive_web_jetfuel_frame\":true,"
  "\"rweb_sports_post_context_enabled\":true,"
  "\"x_elections_post_context_enabled\":true,"
  "\"responsive_web_grok_share_attachment_enabled\":true,"
  "\"responsive_web_grok_annotations_enabled\":true,"
  "\"responsive_web_edit_tweet_api_enabled\":true,"
  "\"graphql_is_translatable_rweb_tweet_is_translatable_enabled\":true,"
  "\"view_counts_everywhere_api_enabled\":true,"
  "\"longform_notetweets_consumption_enabled\":true,"
  "\"responsive_web_twitter_article_tweet_consumption_enabled\":true,"
  "\"content_disclosure_indicator_enabled\":true,"
  "\"content_disclosure_ai_generated_indicator_enabled\":true,"
  "\"responsive_web_grok_show_grok_translated_post\":true,"
  "\"responsive_web_grok_analysis_button_from_backend\":true,"
  "\"post_ctas_fetch_enabled\":false,"
  "\"longform_notetweets_rich_text_read_enabled\":true,"
  "\"longform_notetweets_inline_media_enabled\":false,"
  "\"profile_label_improvements_pcf_label_in_post_enabled\":true,"
  "\"responsive_web_profile_redirect_enabled\":true,"
  "\"rweb_tipjar_consumption_enabled\":false,"
  "\"verified_phone_label_enabled\":false,"
  "\"responsive_web_nested_quote_preview_enabled\":true,"
  "\"articles_preview_enabled\":true,"
  "\"rweb_cashtags_enabled\":true,"
  "\"responsive_web_grok_community_note_auto_translation_is_enabled\":true,"
  "\"freedom_of_speech_not_reach_fetch_enabled\":true,"
  "\"standardized_nudges_misinfo\":true,"
  "\"tweet_with_visibility_results_prefer_gql_limited_actions_policy_enabled\":true,"
  "\"responsive_web_grok_image_annotation_enabled\":true,"
  "\"responsive_web_grok_imagine_annotation_enabled\":true,"
  "\"responsive_web_graphql_timeline_navigation_enabled\":true}";


typedef struct collect {
  x_post_t *posts;
  int       max;
  int       stored;
  int       total;
  int       pinned; // inside a TimelinePinEntry
} collect_t;


static int post_number(const char *id);
static int append_json(char *buf, size_t size, size_t *n, const char *s);


static int
is_word(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
    (c >= 'A' && c <= 'Z') || c == '_';
}


/**
 * value of the cookie name: the name as a whole word, then any of = : tab
 * space or quote, then the value
 **/
static int
find_cookie(const char *text, size_t len, const char *name, char *out, size_t size) {
  size_t n = strlen(name);
  size_t i, j, k;

  for(i=0; i + n <= len; i++) {
    if(memcmp(text + i, name, n) || (i > 0 && is_word(text[i - 1]))) {
      continue;
    }
    for(j=i + n; j < len && memchr("=: \t\"", text[j], 5); j++) {
    }
    if(j == i + n) {
      continue;
    }
    for(k=0; j < len && (is_word(text[j]) || text[j] == '%' || text[j] == '-'); j++) {
      if(k + 1 == size) {
        return -1;
      }
      out[k++] = text[j];
    }
    out[k] = 0;
    if(k) {
      return 0;
    }
  }
  return -1;
}


int
x_session_parse(x_session_t *s, const char *text, size_t len) {
  memset(s, 0, sizeof *s);
  if(find_cookie(text, len, "auth_token", s->auth_token, sizeof s->auth_token)) {
    return -1;
  }
  if(find_cookie(text, len, "ct0", s->ct0, sizeof s->ct0)) {
    return -2;
  }
  return 0;
}


int
x_session_read(x_session_t *s, const char *path) {
  char text[4096];
  ssize_t n;
  int fd, ret;

  if((fd=open(path, O_RDONLY)) < 0) {
    return -3;
  }
  n = read(fd, text, sizeof text);
  close(fd);
  if(n <= 0) {
    return -3;
  }
  ret = x_session_parse(s, text, n);
  memset(text, 0, sizeof text);
  return ret;
}


static char g_language[8] = "en";


const char *
x_language(void) {
  return g_language;
}


void
x_set_language(const char *code) {
  if(code && strlen(code) == 2) {
    snprintf(g_language, sizeof g_language, "%s", code);
  }
}


void
x_request_init(x_request_t *r, const x_session_t *s) {
  snprintf(r->cookie, sizeof r->cookie, "auth_token=%s; ct0=%s", s->auth_token, s->ct0);

  const https_header_t headers[X_HEADER_COUNT] = {
    {"authorization", X_BEARER},
    {"x-csrf-token", s->ct0},
    {"cookie", r->cookie},
    {"x-twitter-auth-type", "OAuth2Session"},
    {"x-twitter-active-user", "yes"},
    {"x-twitter-client-language", g_language},
    {"content-type", "application/json"},
    {"accept", "*/*"},
    {"referer", "https://x.com/home"},
  };
  memcpy(r->headers, headers, sizeof headers);
}


static int
append(char *buf, size_t size, size_t *n, const char *s, int escape) {
  static const char hex[] = "0123456789ABCDEF";
  unsigned char c;

  for(; *s; s++) {
    c = *s;
    if(!escape || is_word(c) || c == '-' || c == '.' || c == '~') {
      if(*n + 1 >= size) {
        return -1;
      }
      buf[(*n)++] = c;
    } else {
      if(*n + 3 >= size) {
        return -1;
      }
      buf[(*n)++] = '%';
      buf[(*n)++] = hex[c >> 4];
      buf[(*n)++] = hex[c & 15];
    }
  }
  buf[*n] = 0;
  return 0;
}


/**
 * whether a cursor can go into the variables as it is: X's are base64 and
 * a few separators, never quotes or escapes
 **/
static int
plain_cursor(const char *s) {
  for(; *s; s++) {
    if(!is_word(*s) && !strchr("+/=-.:|", *s)) {
      return 0;
    }
  }
  return 1;
}


int
x_feed_url(char *buf, size_t size, int feed, const char *id, int count, const char *cursor) {
  static const struct { const char *path; const char *toggles; } feeds[] = {
    [X_FEED_FOR_YOU] = {FOR_YOU_QUERY "/HomeTimeline", NULL},
    [X_FEED_FOLLOWING] = {FOLLOWING_QUERY "/HomeLatestTimeline", NULL},
    [X_FEED_NOTIFICATIONS] = {NOTIFICATIONS_QUERY "/NotificationsTimeline", NULL},
    [X_FEED_BOOKMARKS] = {BOOKMARKS_QUERY "/Bookmarks", NULL},
    [X_FEED_USER] = {USER_POSTS_QUERY "/UserTweets", "{\"withArticlePlainText\":false}"},
    [X_FEED_POST] = {POST_QUERY "/TweetDetail", "{\"withArticleRichContentState\":true,"
                     "\"withArticlePlainText\":false,\"withGrokAnalyze\":false,"
                     "\"withDisallowedReplyControls\":false}"},
    [X_FEED_SEARCH] = {SEARCH_QUERY "/SearchTimeline", NULL},
    [X_FEED_SEARCH_LATEST] = {SEARCH_QUERY "/SearchTimeline", NULL},
    [X_FEED_ARTICLES] = {ARTICLES_QUERY "/UserArticlesTweets", "{\"withArticlePlainText\":false}"},
    [X_FEED_TRENDING] = {GENERIC_QUERY "/GenericTimelineById", NULL},
    [X_FEED_NEWS] = {GENERIC_QUERY "/GenericTimelineById", NULL},
    [X_FEED_LISTS] = {LISTS_QUERY "/ListsManagementPageTimeline", NULL},
    [X_FEED_LIST] = {LIST_QUERY "/ListLatestTweetsTimeline", NULL},
    [X_FEED_SIDEBAR] = {SIDEBAR_QUERY "/ExploreSidebar", NULL},
    [X_FEED_WHO] = {CONNECT_QUERY "/ConnectTabTimeline", NULL},
    [X_FEED_FRIENDS] = {FRIENDS_QUERY "/Following", NULL},
  };
  char variables[400 + X_CURSOR_MAX + 6 * X_QUERY_MAX], after[16 + X_CURSOR_MAX] = "";
  int search = feed == X_FEED_SEARCH || feed == X_FEED_SEARCH_LATEST;
  size_t n = 0, v = 0;

  if(feed < X_FEED_FOR_YOU || feed > X_FEED_FRIENDS ||
     ((feed == X_FEED_USER || feed == X_FEED_POST || feed == X_FEED_ARTICLES || feed == X_FEED_LIST ||
       feed == X_FEED_FRIENDS) && !(id && post_number(id))) ||
     (search && !(id && id[0] && strlen(id) <= X_QUERY_MAX))) {
    return -1;
  }
  if(cursor && cursor[0]) {
    // the next page, the way x.com asks for it when the list scrolls down
    if(strlen(cursor) >= X_CURSOR_MAX || !plain_cursor(cursor)) {
      return -1;
    }
    snprintf(after, sizeof after, ",\"cursor\":\"%s\"", cursor);
  }

  switch(feed) {
  case X_FEED_FOR_YOU:
  case X_FEED_FOLLOWING:
    snprintf(variables, sizeof variables, "{\"count\":%d%s,\"includePromotedContent\":false,"
             "\"latestControlAvailable\":true%s}", count, after,
             after[0] ? "" : ",\"requestContext\":\"launch\"");
    break;
  case X_FEED_NOTIFICATIONS:
    snprintf(variables, sizeof variables, "{\"timeline_type\":\"All\",\"count\":%d%s}", count, after);
    break;
  case X_FEED_BOOKMARKS:
    snprintf(variables, sizeof variables, "{\"count\":%d%s,\"includePromotedContent\":false}", count,
             after);
    break;
  case X_FEED_USER:
  case X_FEED_ARTICLES:
    snprintf(variables, sizeof variables, "{\"userId\":\"%s\",\"count\":%d%s,"
             "\"includePromotedContent\":false,\"withQuickPromoteEligibilityTweetFields\":true,"
             "\"withVoice\":true}", id, count, after);
    break;
  case X_FEED_TRENDING:
  case X_FEED_NEWS:
    snprintf(variables, sizeof variables, "{\"timelineId\":\"%s\",\"count\":%d%s,"
             "\"withQuickPromoteEligibilityTweetFields\":true}",
             feed == X_FEED_TRENDING ? TRENDING_TIMELINE : NEWS_TIMELINE, count, after);
    break;
  case X_FEED_LISTS:
    snprintf(variables, sizeof variables, "{\"count\":100%s}", after);
    break;
  case X_FEED_SIDEBAR:
    snprintf(variables, sizeof variables, "{}"); // as x.com's side column asks
    break;
  case X_FEED_WHO:
    // x.com's "Who to follow": the context of no one in particular
    snprintf(variables, sizeof variables, "{\"count\":%d%s,\"context\":\"{}\"}", count, after);
    break;
  case X_FEED_LIST:
    snprintf(variables, sizeof variables, "{\"listId\":\"%s\",\"count\":%d%s}", id, count, after);
    break;
  case X_FEED_FRIENDS:
    // as x.com's profile asks for the accounts someone follows
    snprintf(variables, sizeof variables, "{\"userId\":\"%s\",\"count\":%d%s,\"includePromotedContent\":false}",
             id, count, after);
    break;
  case X_FEED_SEARCH:
  case X_FEED_SEARCH_LATEST:
    // what x.com asks when the text is typed in the search box
    if(append(variables, sizeof variables, &v, "{\"rawQuery\":\"", 0) ||
       append_json(variables, sizeof variables, &v, id) ||
       snprintf(variables + v, sizeof variables - v, "\",\"count\":%d%s,\"querySource\":"
                "\"typed_query\",\"product\":\"%s\"}", count, after,
                feed == X_FEED_SEARCH ? "Top" : "Latest") >= (int)(sizeof variables - v)) {
      return -1;
    }
    break;
  default:
    snprintf(variables, sizeof variables, "{\"focalTweetId\":\"%s\"%s,\"referrer\":\"tweet\","
             "\"with_rux_injections\":false,\"rankingMode\":\"Relevance\","
             "\"includePromotedContent\":false,\"withCommunity\":true,"
             "\"withQuickPromoteEligibilityTweetFields\":true,\"withBirdwatchNotes\":true,"
             "\"withVoice\":true}", id, after);
    break;
  }

  if(append(buf, size, &n, "https://x.com/i/api/graphql/", 0) ||
     append(buf, size, &n, feeds[feed].path, 0) ||
     append(buf, size, &n, "?variables=", 0) ||
     append(buf, size, &n, variables, 1) ||
     append(buf, size, &n, "&features=", 0) ||
     append(buf, size, &n, timeline_features, 1) ||
     (feeds[feed].toggles && (append(buf, size, &n, "&fieldToggles=", 0) ||
                              append(buf, size, &n, feeds[feed].toggles, 1)))) {
    return -1;
  }
  return 0;
}


int
x_timeline_url(char *buf, size_t size, int count, int following, const char *cursor) {
  return x_feed_url(buf, size, following ? X_FEED_FOLLOWING : X_FEED_FOR_YOU, NULL, count, cursor);
}


int
x_user_url(char *buf, size_t size, const char *id) {
  char variables[96];
  size_t n = 0;

  if(id && id[0]) {
    if(!post_number(id)) {
      return -1;
    }
    snprintf(variables, sizeof variables, "{\"userId\":\"%s\",\"withSafetyModeUserFields\":true}", id);
    return append(buf, size, &n, "https://x.com/i/api/graphql/" USER_QUERY "/UserByRestId?variables=",
                  0) ||
      append(buf, size, &n, variables, 1) ||
      append(buf, size, &n, "&features=", 0) ||
      append(buf, size, &n, user_features, 1) ? -1 : 0;
  }
  return append(buf, size, &n, "https://x.com/i/api/graphql/" VIEWER_QUERY "/Viewer?variables=", 0) ||
    append(buf, size, &n, "{\"withCommunitiesMemberships\":true}", 1) ||
    append(buf, size, &n, "&features=", 0) ||
    append(buf, size, &n, viewer_features, 1) ||
    append(buf, size, &n, "&fieldToggles=", 0) ||
    append(buf, size, &n, "{\"isDelegate\":false,\"withAuxiliaryUserLabels\":true}", 1) ? -1 : 0;
}


/**
 * how much of s[0..n) is whole characters when the byte after it is next
 **/
static size_t
whole_chars(const char *s, size_t n, char next) {
  if(((unsigned char)next & 0xC0) == 0x80) {
    while(n > 0 && ((unsigned char)s[n - 1] & 0xC0) == 0x80) {
      n--;
    }
    if(n > 0) {
      n--;
    }
  }
  return n;
}


/**
 * copy a post's text, undoing the &amp; &lt; &gt; that X puts in it
 **/
static void
copy_text(char *dst, size_t size, const char *src) {
  static const struct { const char *from; char to; } escapes[] = {
    {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'},
  };
  size_t n = 0, skip;
  char c;

  while(*src && n + 1 < size) {
    c = *src;
    skip = 1;
    for(size_t i=0; i < sizeof escapes / sizeof escapes[0]; i++) {
      if(!strncmp(src, escapes[i].from, strlen(escapes[i].from))) {
        c = escapes[i].to;
        skip = strlen(escapes[i].from);
        break;
      }
    }
    dst[n++] = c;
    src += skip;
  }
  dst[whole_chars(dst, n, *src)] = 0;
}


static const json_node_t *
author(const json_node_t *tweet) {
  return json_path(tweet, "core", "user_results", "result", NULL);
}


/**
 * a field of the author: newer answers keep the names in core, older ones
 * in legacy
 **/
static const char *
user_field(const json_node_t *user, const char *field) {
  const char *value;

  if((value=json_string(json_path(user, "core", field, NULL)))) {
    return value;
  }
  return json_string(json_path(user, "legacy", field, NULL));
}


static const json_node_t *
unwrap(const json_node_t *result) {
  const char *type = json_string(json_get(result, "__typename"));

  if(type && !strcmp(type, "TweetWithVisibilityResults")) {
    return json_get(result, "tweet");
  }
  return result;
}


static const char *
post_text(const json_node_t *tweet) {
  const char *text;

  // long posts are cut in full_text and whole in the note
  if((text=json_string(json_path(tweet, "note_tweet", "note_tweet_results",
                                 "result", "text", NULL)))) {
    return text;
  }
  return json_string(json_path(tweet, "legacy", "full_text", NULL));
}


/**
 * a number of the answer, as X writes counts: a JSON number, or a string
 * for views; -1 when it is missing
 **/
static long long
count(const json_node_t *n) {
  if(!n || (n->type != JSON_NUMBER && n->type != JSON_STRING) || !n->str) {
    return -1;
  }
  return strtoll(n->str, NULL, 10);
}


/**
 * only X's image server is fetched from, and its addresses fit the post
 **/
static int
copy_image_url(char *dst, size_t size, const char *url) {
  if(!url || strncmp(url, "https://pbs.twimg.com/", 22) || strlen(url) >= size) {
    return 0;
  }
  strcpy(dst, url);
  return 1;
}


/**
 * the game a user plays, when their profile's location says it: x.com's
 * new shape keeps it in location.location, the old one in legacy, and the
 * direct messages' users have it as it is
 **/
static void
playing_of(const json_node_t *user, char *game, size_t size) {
  const char *s;

  if(!(s=json_string(json_path(user, "location", "location", NULL))) &&
     !(s=json_string(json_path(user, "legacy", "location", NULL)))) {
    s = json_string(json_get(user, "location"));
  }
  play_game_of(s, game, size);
}


static int
badge(const json_node_t *user) {
  const char *type = json_string(json_path(user, "verification", "verified_type", NULL));
  const json_node_t *blue = json_get(user, "is_blue_verified");

  if(!type) {
    type = json_string(json_path(user, "legacy", "verified_type", NULL));
  }
  if(type && !strcmp(type, "Business")) {
    return X_BADGE_GOLD;
  }
  if(type && !strcmp(type, "Government")) {
    return X_BADGE_GREY;
  }
  return blue && blue->type == JSON_TRUE ? X_BADGE_BLUE : X_BADGE_NONE;
}


/**
 * replace every from in text with to, while it fits in size bytes
 **/
static void
replace(char *text, size_t size, const char *from, const char *to) {
  size_t flen = strlen(from), tlen = strlen(to), len = strlen(text);
  char *at = text;

  if(!flen) {
    return;
  }
  while((at=strstr(at, from))) {
    if(len - flen + tlen + 1 > size) {
      return;
    }
    memmove(at + tlen, at + flen, len - (at - text) - flen + 1);
    memcpy(at, to, tlen);
    len = len - flen + tlen;
    at += tlen;
  }
}


static const json_node_t *
media_list(const json_node_t *tweet) {
  const json_node_t *list = json_path(tweet, "legacy", "extended_entities", "media", NULL);

  if(!list) {
    list = json_path(tweet, "legacy", "entities", "media", NULL);
  }
  return list && list->type == JSON_ARRAY ? list : NULL;
}


/**
 * what X does to a post's text before showing it: t.co links become the
 * address they stand for, and the links of its media and of the post it
 * quotes, which X shows as pictures and as a box, go away
 **/
static void
clean_text(char *text, size_t size, const json_node_t *tweet) {
  const json_node_t *urls, *list;
  const char *quoted;
  size_t n;

  urls = json_path(tweet, "note_tweet", "note_tweet_results", "result", "entity_set",
                   "urls", NULL);
  if(!urls) {
    urls = json_path(tweet, "legacy", "entities", "urls", NULL);
  }
  for(const json_node_t *u=urls ? urls->child : NULL; u; u=u->next) {
    const char *from = json_string(json_get(u, "url"));
    const char *to = json_string(json_get(u, "display_url"));
    if(from && to && !strncmp(from, "https://t.co/", 13)) {
      replace(text, size, from, to);
    }
  }
  if((list=media_list(tweet))) {
    for(const json_node_t *m=list->child; m; m=m->next) {
      const char *url = json_string(json_get(m, "url"));
      if(url && !strncmp(url, "https://t.co/", 13)) {
        replace(text, size, url, "");
      }
    }
  }
  quoted = json_string(json_path(tweet, "legacy", "quoted_status_permalink", "url", NULL));
  if(quoted && !strncmp(quoted, "https://t.co/", 13)) {
    replace(text, size, quoted, "");
  }

  for(n=strlen(text); n && (text[n - 1] == ' ' || text[n - 1] == '\n'); n--) {
    text[n - 1] = 0;
  }
}


/**
 * the pixels of a video's MP4 by the size in its address (".../vid/avc1/
 * 1920x1080/..." on video.twimg.com), 0 when it says none
 **/
static long long
video_pixels(const char *url) {
  const char *p = strstr(url, "/vid/");
  long w, h;
  char *end;

  for(p=p ? p + 5 : NULL; p && *p; p=strchr(p, '/') ? strchr(p, '/') + 1 : NULL) {
    w = strtol(p, &end, 10);
    if(w > 0 && *end == 'x') {
      h = strtol(end + 1, &end, 10);
      if(h > 0 && *end == '/' && w < 10000 && h < 10000) {
        return (long long)w * h;
      }
    }
  }
  return 0;
}


/**
 * a video's MP4s: the sharpest (the most pixels, then the highest bitrate)
 * whose file fits VIDEO_BYTES, the same in the timeline and over the whole
 * screen, so opening it goes on with the file that came; and the smallest,
 * for the data saver. They play while they come, and the player keeps the
 * whole file; only from video.twimg.com
 **/
#define VIDEO_BYTES (512LL << 20) // the player's VIDEO_MAX: 7 minutes of X's 1080p (10368k), 30 of its 720p

static void
pick_video(x_media_t *m, const json_node_t *n) {
  const json_node_t *info = json_get(n, "video_info"), *list = json_get(info, "variants");
  long long rate, px, best_px = -1, best_rate = -1, small_px = -1, small_rate = -1,
            ms = count(json_get(info, "duration_millis"));
  const char *type, *url;

  m->duration_ms = ms > 0 && ms < 24LL * 3600 * 1000 ? (int)ms : 0;
  for(const json_node_t *v=list ? list->child : NULL; v; v=v->next) {
    type = json_string(json_get(v, "content_type"));
    url = json_string(json_get(v, "url"));
    if(!type || strcmp(type, "video/mp4") || !url || strncmp(url, "https://video.twimg.com/", 24) ||
       strlen(url) >= X_VIDEO_URL) {
      continue;
    }
    rate = count(json_get(v, "bitrate"));
    rate = rate < 0 ? 0 : rate;
    px = video_pixels(url);
    if(small_px < 0 || px < small_px || (px == small_px && rate < small_rate)) {
      small_px = px;
      small_rate = rate;
      strcpy(m->video_small, url);
    }
    if((px > best_px || (px == best_px && rate > best_rate)) &&
       (!ms || rate / 8 * (ms / 1000 + 1) <= VIDEO_BYTES)) {
      best_px = px;
      best_rate = rate;
      strcpy(m->video_url, url);
    }
  }
  if(!m->video_url[0]) {
    strcpy(m->video_url, m->video_small);
  }
}


static void
add_media(x_post_t *post, const json_node_t *tweet) {
  const json_node_t *list = media_list(tweet);
  x_media_t *m;
  const char *type;
  long long w, h;

  for(const json_node_t *n=list ? list->child : NULL; n && post->media_count < X_MAX_MEDIA;
      n=n->next) {
    m = &post->media[post->media_count];
    if(!copy_image_url(m->url, sizeof m->url, json_string(json_get(n, "media_url_https")))) {
      continue;
    }
    type = json_string(json_get(n, "type"));
    m->video = type && strcmp(type, "photo");
    pick_video(m, n);
    w = count(json_path(n, "original_info", "width", NULL));
    h = count(json_path(n, "original_info", "height", NULL));
    m->width = w > 0 && w < 100000 ? (int)w : 0;
    m->height = h > 0 && h < 100000 ? (int)h : 0;
    post->media_count++;
  }
}


/**
 * the user's picture on pbs.twimg.com into buf, or an empty buf
 **/
static void
avatar(char *buf, size_t size, const json_node_t *user) {
  if(!copy_image_url(buf, size, json_string(json_path(user, "avatar", "image_url", NULL)))) {
    copy_image_url(buf, size, json_string(json_path(user, "legacy", "profile_image_url_https", NULL)));
  }
}


static int
is_true(const json_node_t *node) {
  return node && node->type == JSON_TRUE;
}


static void
add_quote(x_post_t *post, const json_node_t *tweet) {
  const json_node_t *quoted = json_path(tweet, "quoted_status_result", "result", NULL);
  const char *user, *text, *name;

  if(!quoted) {
    quoted = json_path(tweet, "legacy", "quoted_status_result", "result", NULL);
  }
  quoted = unwrap(quoted);
  if(!(user=user_field(author(quoted), "screen_name")) || !(text=post_text(quoted))) {
    return;
  }
  copy_text(post->quote_user, sizeof post->quote_user, user);
  if((name=user_field(author(quoted), "name"))) {
    copy_text(post->quote_name, sizeof post->quote_name, name);
  }
  avatar(post->quote_avatar, sizeof post->quote_avatar, author(quoted));
  post->quote_badge = badge(author(quoted));
  copy_text(post->quote_text, sizeof post->quote_text, text);
  clean_text(post->quote_text, sizeof post->quote_text, quoted);
}


static void add_poll(x_post_t *post, const json_node_t *tweet);

static void
add_post(collect_t *c, const json_node_t *result) {
  const json_node_t *tweet = unwrap(result);
  const json_node_t *original, *user_node;
  const char *user, *text, *name, *id;
  x_post_t *post;

  // deleted, withheld and unavailable posts have no author or text
  if(!(user=user_field(author(tweet), "screen_name")) || !(text=post_text(tweet))) {
    return;
  }
  c->total++;
  if(c->stored == c->max) {
    return;
  }

  post = &c->posts[c->stored++];
  memset(post, 0, sizeof *post);
  original = unwrap(json_path(tweet, "legacy", "retweeted_status_result", "result", NULL));
  if(original && user_field(author(original), "screen_name") && post_text(original)) {
    copy_text(post->reposter, sizeof post->reposter, user);
    if((name=user_field(author(tweet), "name"))) {
      copy_text(post->reposter_name, sizeof post->reposter_name, name);
    }
    tweet = original;
    user = user_field(author(tweet), "screen_name");
    text = post_text(tweet);
  }
  user_node = author(tweet);
  if((id=json_string(json_get(tweet, "rest_id"))) && post_number(id)) {
    strcpy(post->id, id);
  }
  if((id=json_string(json_get(user_node, "rest_id"))) && post_number(id)) {
    strcpy(post->user_id, id);
  }
  post->pinned = c->pinned > 0;
  copy_text(post->user, sizeof post->user, user);
  if((name=user_field(user_node, "name"))) {
    copy_text(post->name, sizeof post->name, name);
  }
  avatar(post->avatar, sizeof post->avatar, user_node);
  post->badge = badge(user_node);
  playing_of(user_node, post->playing, sizeof post->playing);
  post->time = x_parse_time(json_string(json_path(tweet, "legacy", "created_at", NULL)));
  post->replies = count(json_path(tweet, "legacy", "reply_count", NULL));
  post->reposts = count(json_path(tweet, "legacy", "retweet_count", NULL));
  post->likes = count(json_path(tweet, "legacy", "favorite_count", NULL));
  post->views = count(json_path(tweet, "views", "count", NULL));
  post->bookmarks = count(json_path(tweet, "legacy", "bookmark_count", NULL));
  post->liked = is_true(json_path(tweet, "legacy", "favorited", NULL));
  post->reposted = is_true(json_path(tweet, "legacy", "retweeted", NULL));
  post->bookmarked = is_true(json_path(tweet, "legacy", "bookmarked", NULL));
  post->following = is_true(json_path(user_node, "relationship_perspectives", "following", NULL)) ||
                    is_true(json_path(user_node, "legacy", "following", NULL));
  if(is_true(json_path(tweet, "edit_control", "is_edit_eligible", NULL)) &&
     count(json_path(tweet, "edit_control", "edits_remaining", NULL)) > 0) {
    long long until = count(json_path(tweet, "edit_control", "editable_until_msecs", NULL));
    post->edit_until = until > 0 ? until / 1000 : 0;
  }
  copy_text(post->text, sizeof post->text, text);
  clean_text(post->text, sizeof post->text, tweet);
  add_media(post, tweet);
  add_quote(post, tweet);
  add_poll(post, tweet);
}


/**
 * the first member called key under node, depth first, or NULL
 **/
static const json_node_t *
find(const json_node_t *node, const char *key) {
  const json_node_t *found;

  for(const json_node_t *m=node ? node->child : NULL; m; m=m->next) {
    if(m->key && !strcmp(m->key, key)) {
      return m;
    }
    if((m->type == JSON_OBJECT || m->type == JSON_ARRAY) && (found=find(m, key))) {
      return found;
    }
  }
  return NULL;
}


/**
 * days from 1970-01-01 to a civil date (Howard Hinnant's days_from_civil)
 **/
static long long
days_from_civil(int year, int month, int day) {
  long long y = year - (month <= 2), era, yoe, doy, doe;

  era = (y >= 0 ? y : y - 399) / 400;
  yoe = y - era * 400;
  doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}


/**
 * Unix time of a notice's timestamp_ms: milliseconds as a number or a
 * string, or a time such as "2025-05-05T01:18:21.657Z"; 0 when neither
 **/
static long long
notice_time(const json_node_t *n) {
  int year, month, day, hour, minute, second;
  const char *s;

  if(!n || !(s=n->str) || (n->type != JSON_NUMBER && n->type != JSON_STRING)) {
    return 0;
  }
  if(n->len >= 10 && strspn(s, "0123456789") >= 10) {
    return strtoll(s, NULL, 10) / 1000;
  }
  if(sscanf(s, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6 ||
     month < 1 || month > 12) {
    return 0;
  }
  return days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}


/*
 * Polls: a post's card named poll<N>choice_*, its choices and votes in its
 * binding_values; made on caps.x.com before the post, and voted there.
 */

static int
card_uri_ok(const char *s) {
  return s && !strncmp(s, "card://", 7) && post_number(s + 7);
}


/**
 * a poll card's name: poll2choice_text_only and the like
 **/
static int
poll_name_ok(const char *s) {
  size_t n = s ? strlen(s) : 0;

  if(n < 11 || n >= sizeof ((x_post_t *)0)->poll_name || strncmp(s, "poll", 4) || s[4] < '2' || s[4] > '4' ||
     strncmp(s + 5, "choice", 6)) {
    return 0;
  }
  for(; *s; s++) {
    if(!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) {
      return 0;
    }
  }
  return 1;
}


/**
 * the value of the binding called key among a card's binding_values
 **/
static const json_node_t *
binding(const json_node_t *values, const char *key) {
  for(const json_node_t *b=values ? values->child : NULL; b; b=b->next) {
    const char *k = json_string(json_get(b, "key"));
    if(k && !strcmp(k, key)) {
      return json_get(b, "value");
    }
  }
  return NULL;
}


static void
add_poll(x_post_t *post, const json_node_t *tweet) {
  const json_node_t *card = json_get(tweet, "card");
  const json_node_t *values = json_path(card, "legacy", "binding_values", NULL);
  const char *name = json_string(json_path(card, "legacy", "name", NULL)), *s, *api;
  char key[32];
  int choices;

  if(!poll_name_ok(name)) {
    return;
  }
  choices = name[4] - '0';
  for(int i=0; i < choices; i++) {
    snprintf(key, sizeof key, "choice%d_label", i + 1);
    if(!(s=json_string(json_get(binding(values, key), "string_value"))) || !s[0]) {
      memset(post->poll_label, 0, sizeof post->poll_label);
      return;
    }
    copy_text(post->poll_label[i], sizeof post->poll_label[i], s);
    snprintf(key, sizeof key, "choice%d_count", i + 1);
    s = json_string(json_get(binding(values, key), "string_value"));
    post->poll_votes[i] = s && *s >= '0' && *s <= '9' ? strtoll(s, NULL, 10) : 0;
  }
  post->poll_count = choices;
  post->poll_end = notice_time(json_get(binding(values, "end_datetime_utc"), "string_value"));
  post->poll_final = is_true(json_get(binding(values, "counts_are_final"), "boolean_value"));
  s = json_string(json_get(binding(values, "selected_choice"), "string_value"));
  post->poll_voted = s && s[0] >= '1' && s[0] < '1' + choices && !s[1] ? s[0] - '0' : 0;
  strcpy(post->poll_name, name);
  // the card to vote with, when it votes the way x.com does
  if(!(s=json_string(json_get(card, "rest_id")))) {
    s = json_string(json_path(card, "legacy", "url", NULL));
  }
  api = json_string(json_get(binding(values, "api"), "string_value"));
  if(card_uri_ok(s) && strlen(s) < sizeof post->poll_card && api && !strcmp(api, "capi://passthrough/1")) {
    strcpy(post->poll_card, s);
  }
}


/**
 * s as a form's value: all but letters, digits and -_.~ as %XX
 **/
static int
append_form(char *buf, size_t size, size_t *n, const char *s) {
  static const char hex[] = "0123456789ABCDEF";

  for(const unsigned char *p=(const unsigned char *)s; *p; p++) {
    int plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
                *p == '-' || *p == '_' || *p == '.' || *p == '~';
    if(*n + (plain ? 1 : 3) >= size) {
      return -1;
    }
    if(plain) {
      buf[(*n)++] = (char)*p;
    } else {
      buf[(*n)++] = '%';
      buf[(*n)++] = hex[*p >> 4];
      buf[(*n)++] = hex[*p & 15];
    }
  }
  buf[*n] = 0;
  return 0;
}


int
x_poll_card_request(char *url, size_t usize, char *body, size_t bsize, const char *const *choices,
                    int count, int minutes) {
  char json[1024], part[160];
  size_t n = 0, m = 0;

  if(count < 2 || count > X_POLL_MAX || minutes < 5 || minutes > 10080 ||
     snprintf(url, usize, "https://caps.x.com/v2/cards/create.json") >= (int)usize) {
    return -1;
  }
  // the card_data x.com's composer sends: the card, its endpoint, how long, the choices
  snprintf(part, sizeof part, "{\"twitter:card\":\"poll%dchoice_text_only\",\"twitter:api:api:endpoint\":\"1\","
           "\"twitter:long:duration_minutes\":%d", count, minutes);
  if(append(json, sizeof json, &n, part, 0)) {
    return -1;
  }
  for(int i=0; i < count; i++) {
    if(!choices[i] || !choices[i][0] || strlen(choices[i]) >= X_POLL_LABEL || x_post_length(choices[i]) > 25) {
      return -1;
    }
    snprintf(part, sizeof part, ",\"twitter:string:choice%d_label\":\"", i + 1);
    if(append(json, sizeof json, &n, part, 0) || append_json(json, sizeof json, &n, choices[i]) ||
       append(json, sizeof json, &n, "\"", 0)) {
      return -1;
    }
  }
  if(append(json, sizeof json, &n, "}", 0) || append(body, bsize, &m, "card_data=", 0) ||
     append_form(body, bsize, &m, json)) {
    return -1;
  }
  return 0;
}


int
x_card_uri(const json_node_t *root, char *uri, size_t size) {
  const char *s = json_string(json_get(root, "card_uri"));

  uri[0] = 0;
  if(!card_uri_ok(s) || strlen(s) >= size) {
    return -1;
  }
  strcpy(uri, s);
  return 0;
}


int
x_vote_request(char *url, size_t usize, char *body, size_t bsize, const char *card, const char *id,
               const char *name, int choice) {
  if(!card_uri_ok(card) || !id || !post_number(id) || !poll_name_ok(name) || choice < 1 || choice > X_POLL_MAX ||
     choice > name[4] - '0' ||
     snprintf(url, usize, "https://caps.x.com/v2/capi/passthrough/1") >= (int)usize ||
     snprintf(body, bsize, "twitter%%3Astring%%3Acard_uri=card%%3A%%2F%%2F%s&twitter%%3Along%%3Aoriginal_tweet_id=%s"
              "&twitter%%3Astring%%3Aresponse_card_name=%s&twitter%%3Astring%%3Acards_platform=Web-12"
              "&twitter%%3Astring%%3Aselected_choice=%d", card + 7, id, name, choice) >= (int)bsize) {
    return -1;
  }
  return 0;
}


/**
 * a notification: what it says, its icon, the people in it and the post it
 * is about
 **/
static void
add_notice(collect_t *c, const json_node_t *item) {
  static const struct { const char *name; int icon; } icons[] = {
    {"heart_icon", X_ICON_LIKE}, {"retweet_icon", X_ICON_REPOST}, {"person_icon", X_ICON_FOLLOW},
    {"reply_icon", X_ICON_REPLY},
  };
  const char *text = json_string(json_path(item, "rich_message", "text", NULL));
  const char *icon = json_string(json_get(item, "notification_icon")), *user, *s;
  const json_node_t *people, *person, *tweet;
  x_post_t *post;

  if(!text || !text[0]) {
    return;
  }
  c->total++;
  if(c->stored == c->max) {
    return;
  }
  post = &c->posts[c->stored++];
  memset(post, 0, sizeof *post);
  post->kind = X_KIND_NOTICE;
  for(size_t i=0; icon && i < sizeof icons / sizeof icons[0]; i++) {
    if(!strcmp(icon, icons[i].name)) {
      post->icon = icons[i].icon;
    }
  }
  copy_text(post->text, sizeof post->text, text);

  // the people who did it, else the ones the message names
  people = json_path(item, "template", "from_users", NULL);
  if(!people || !people->child) {
    people = json_path(item, "rich_message", "entities", NULL);
  }
  for(const json_node_t *p=people ? people->child : NULL; p && post->face_count < X_MAX_FACES;
      p=p->next) {
    person = json_get(find(p, "user_results"), "result");
    if(!(user=user_field(person, "screen_name"))) {
      continue;
    }
    if(!post->face_count) {
      copy_text(post->user, sizeof post->user, user);
      if((s=user_field(person, "name"))) {
        copy_text(post->name, sizeof post->name, s);
      }
      if((s=json_string(json_get(person, "rest_id"))) && post_number(s)) {
        strcpy(post->user_id, s);
      }
      avatar(post->avatar, sizeof post->avatar, person);
      post->badge = badge(person);
    }
    avatar(post->faces[post->face_count], sizeof post->faces[0], person);
    post->face_count++;
  }

  tweet = unwrap(json_get(find(json_path(item, "template", "target_objects", NULL), "tweet_results"),
                          "result"));
  if((s=post_text(tweet)) && (user=user_field(author(tweet), "screen_name"))) {
    copy_text(post->quote_user, sizeof post->quote_user, user);
    copy_text(post->quote_text, sizeof post->quote_text, s);
    clean_text(post->quote_text, sizeof post->quote_text, tweet);
    if((s=json_string(json_get(tweet, "rest_id"))) && post_number(s)) {
      strcpy(post->id, s);
    }
    post->liked = is_true(json_path(tweet, "legacy", "favorited", NULL));
    post->reposted = is_true(json_path(tweet, "legacy", "retweeted", NULL));
    post->bookmarked = is_true(json_path(tweet, "legacy", "bookmarked", NULL));
  }
  post->time = notice_time(json_get(item, "timestamp_ms"));
  post->replies = post->reposts = post->likes = post->views = post->bookmarks = -1;
}


/**
 * what an item of a timeline is: TimelineTweet, TimelineNotification,
 * TimelineTrend, TimelineTwitterList..., or NULL for anything else
 **/
static const char *
item_type(const json_node_t *node) {
  const char *type = json_string(json_get(node, "itemType"));

  if(!type) {
    type = json_string(json_get(node, "__typename"));
  }
  return type && !strncmp(type, "Timeline", 8) ? type : NULL;
}


static int
hex_digit(int c) {
  return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1;
}


/**
 * what a trend's link searches for: the query of twitter://search/?query=%23PS5&src=...
 * decoded, which fits buf and has no control characters; returns whether
 * there is one
 **/
static int
trend_query(char *buf, size_t size, const char *url) {
  const char *s = url ? strstr(url, "?query=") : NULL;
  size_t n = 0;
  int hi, lo, c;

  if(!s && url) {
    s = strstr(url, "&query=");
  }
  if(!s) {
    return 0;
  }
  for(s += 7; *s && *s != '&' && *s != '#'; s++) {
    c = (unsigned char)*s;
    if(c == '+') {
      c = ' ';
    } else if(c == '%') {
      if((hi=hex_digit(s[1])) < 0 || (lo=hex_digit(s[2])) < 0) {
        return 0;
      }
      c = hi << 4 | lo;
      s += 2;
    }
    if(c < 0x20 || c == 0x7F || n + 1 >= size || n >= X_QUERY_MAX) {
      return 0;
    }
    buf[n++] = (char)c;
  }
  buf[n] = 0;
  return n > 0;
}


/**
 * a trend of Explore, or a story of its news; not the paid ones
 **/
static void
add_trend(collect_t *c, const json_node_t *item) {
  const char *name = json_string(json_get(item, "name")), *s;
  const json_node_t *n;
  x_post_t *post;

  if(!name || !name[0] || json_get(item, "promoted_metadata")) {
    return;
  }
  c->total++;
  if(c->stored == c->max) {
    return;
  }
  post = &c->posts[c->stored++];
  memset(post, 0, sizeof *post);
  post->kind = X_KIND_TREND;
  copy_text(post->name, sizeof post->name, name);
  if(!trend_query(post->text, sizeof post->text, json_string(json_path(item, "trend_url", "url", NULL)))) {
    copy_text(post->text, sizeof post->text, name);
  }
  if((s=json_string(json_path(item, "trend_metadata", "domain_context", NULL)))) {
    copy_text(post->quote_name, sizeof post->quote_name, s);
  }
  if((s=json_string(json_path(item, "trend_metadata", "meta_description", NULL))) ||
     (s=json_string(json_get(item, "description")))) {
    copy_text(post->quote_text, sizeof post->quote_text, s);
  }
  if(!copy_image_url(post->avatar, sizeof post->avatar,
                     json_string(json_path(item, "thumbnail_image", "original_img_url", NULL)))) {
    n = json_get(item, "images");
    copy_image_url(post->avatar, sizeof post->avatar, json_string(json_get(n ? n->child : NULL, "url")));
  }
  s = json_string(json_get(item, "rank"));
  post->icon = s && s[0] >= '1' && s[0] <= '9' && strlen(s) < 5 ? atoi(s) : 0;
  post->group = is_true(json_get(item, "is_ai_trend"));
  post->replies = post->reposts = post->likes = post->views = post->bookmarks = -1;
}


/**
 * a list of the user's Lists page, once; not the ones it suggests
 **/
static void
add_list(collect_t *c, const json_node_t *item) {
  const json_node_t *list = json_get(item, "list"), *owner;
  const char *id = json_string(json_get(list, "id_str")), *name = json_string(json_get(list, "name"));
  const char *display = json_string(json_get(item, "displayType")), *s;
  x_post_t *post;

  if(!id) {
    id = json_string(json_get(list, "id_string"));
  }
  if(!id || !post_number(id) || !name || !name[0] ||
     (display && !strcmp(display, "ListWithSubscribe") && !is_true(json_get(list, "following")))) {
    return;
  }
  for(int i=0; i < c->stored; i++) {
    if(c->posts[i].kind == X_KIND_LIST && !strcmp(c->posts[i].id, id)) {
      return; // pinned, and among the user's too
    }
  }
  c->total++;
  if(c->stored == c->max) {
    return;
  }
  post = &c->posts[c->stored++];
  memset(post, 0, sizeof *post);
  post->kind = X_KIND_LIST;
  strcpy(post->id, id);
  copy_text(post->name, sizeof post->name, name);
  if((s=json_string(json_get(list, "description")))) {
    copy_text(post->quote_text, sizeof post->quote_text, s);
  }
  if(!copy_image_url(post->avatar, sizeof post->avatar,
                     json_string(json_path(list, "custom_banner_media", "media_info", "original_img_url", NULL))) &&
     !copy_image_url(post->avatar, sizeof post->avatar,
                     json_string(json_path(list, "default_banner_media", "media_info", "original_img_url",
                                           NULL)))) {
    copy_image_url(post->avatar, sizeof post->avatar,
                   json_string(json_path(list, "default_banner_media_results", "result", "media_info",
                                         "original_img_url", NULL)));
  }
  owner = json_path(list, "user_results", "result", NULL);
  if((s=user_field(owner, "screen_name"))) {
    copy_text(post->user, sizeof post->user, s);
    if((s=user_field(owner, "name"))) {
      copy_text(post->quote_name, sizeof post->quote_name, s);
    }
    if((s=json_string(json_get(owner, "rest_id"))) && post_number(s)) {
      strcpy(post->user_id, s);
    }
    avatar(post->quote_avatar, sizeof post->quote_avatar, owner);
    post->badge = badge(owner);
  }
  post->likes = count(json_get(list, "member_count"));
  post->reposts = count(json_get(list, "subscriber_count"));
  post->replies = post->views = post->bookmarks = -1;
  s = json_string(json_get(list, "mode"));
  post->request = s && !strcmp(s, "Private");
  post->following = is_true(json_get(list, "following"));
  post->pinned = is_true(json_get(list, "pinning"));
}


/**
 * a person of a timeline (who to follow): their names, picture, badge, bio
 * and whether the user follows them
 **/
static void
add_user(collect_t *c, const json_node_t *item) {
  const json_node_t *r = json_path(item, "user_results", "result", NULL);
  const char *user = user_field(r, "screen_name"), *id = json_string(json_get(r, "rest_id")), *s;
  x_post_t *post;

  if(!user || !id || !post_number(id) || json_get(item, "promotedMetadata")) {
    return;
  }
  c->total++;
  if(c->stored == c->max) {
    return;
  }
  post = &c->posts[c->stored++];
  memset(post, 0, sizeof *post);
  post->kind = X_KIND_USER;
  strcpy(post->user_id, id);
  copy_text(post->user, sizeof post->user, user);
  if((s=user_field(r, "name"))) {
    copy_text(post->name, sizeof post->name, s);
  }
  avatar(post->avatar, sizeof post->avatar, r);
  post->badge = badge(r);
  playing_of(r, post->playing, sizeof post->playing);
  if((s=json_string(json_path(r, "profile_bio", "description", NULL))) ||
     (s=json_string(json_path(r, "legacy", "description", NULL)))) {
    copy_text(post->text, sizeof post->text, s);
  }
  post->following = is_true(json_path(r, "relationship_perspectives", "following", NULL)) ||
                    is_true(json_path(r, "legacy", "following", NULL));
  post->replies = post->reposts = post->likes = post->views = post->bookmarks = -1;
}


/**
 * every tweet_results in document order, without going into them, so the
 * posts a post quotes are not counted as posts of the timeline; and every
 * notification
 **/
static void
walk(collect_t *c, const json_node_t *node) {
  const char *type = json_string(json_get(node, "type")), *item = item_type(node);
  int pin = type && !strcmp(type, "TimelinePinEntry");

  // an ad, which the timeline may carry even when not asked to
  if(json_get(node, "promotedMetadata")) {
    return;
  }
  if(item && !strcmp(item, "TimelineNotification")) {
    add_notice(c, node);
    return;
  }
  if(item && !strcmp(item, "TimelineTrend")) {
    add_trend(c, node);
    return;
  }
  if(item && !strcmp(item, "TimelineTwitterList")) {
    add_list(c, node);
    return;
  }
  if(item && !strcmp(item, "TimelineUser")) {
    add_user(c, node);
    return;
  }
  c->pinned += pin;
  for(const json_node_t *m=node->child; m; m=m->next) {
    if(m->key && !strcmp(m->key, "tweet_results")) {
      add_post(c, json_get(m, "result"));
    } else if(m->type == JSON_OBJECT || m->type == JSON_ARRAY) {
      walk(c, m);
    }
  }
  c->pinned -= pin;
}


/**
 * the value of the first cursor of type Bottom under node, outside the posts
 **/
static const char *
bottom_cursor(const json_node_t *node) {
  const char *type = json_string(json_get(node, "cursorType")), *found;

  if(type && !strcmp(type, "Bottom")) {
    return json_string(json_get(node, "value"));
  }
  for(const json_node_t *m=node->child; m; m=m->next) {
    if((m->type == JSON_OBJECT || m->type == JSON_ARRAY) &&
       !(m->key && !strcmp(m->key, "tweet_results")) && (found=bottom_cursor(m))) {
      return found;
    }
  }
  return NULL;
}


int
x_timeline_cursor(const json_node_t *root, char *buf, size_t size) {
  const json_node_t *data = json_get(root, "data");
  const char *cursor = data ? bottom_cursor(data) : NULL;

  buf[0] = 0;
  if(!cursor || !cursor[0] || strlen(cursor) >= size || strlen(cursor) >= X_CURSOR_MAX ||
     !plain_cursor(cursor)) {
    return -1;
  }
  strcpy(buf, cursor);
  return 0;
}


int
x_timeline_posts(const json_node_t *root, x_post_t *posts, int max, int *total) {
  collect_t c = {.posts = posts, .max = max};
  const json_node_t *data = json_get(root, "data");

  // the posts are under data.home.home_timeline_urt.instructions; walking
  // all of data keeps working if X moves them around
  if(data) {
    walk(&c, data);
  }
  // a profile's pinned post goes on top, wherever the answer has it
  for(int i=1; i < c.stored; i++) {
    if(posts[i].pinned && !posts[0].pinned) {
      x_post_t pinned = posts[i];
      memmove(&posts[1], &posts[0], i * sizeof *posts);
      posts[0] = pinned;
      break;
    }
  }
  *total = c.total;
  return c.stored;
}


int
x_user_parse(const json_node_t *root, x_user_t *u) {
  const json_node_t *data = json_get(root, "data"), *r, *urls;
  const char *user, *id, *s;

  memset(u, 0, sizeof *u);
  if(!(r=json_path(data, "viewer", "user_results", "result", NULL)) &&
     !(r=json_path(data, "user", "result", NULL))) {
    r = json_path(data, "viewer_v2", "user_results", "result", NULL);
  }
  if(!(user=user_field(r, "screen_name")) || !(id=json_string(json_get(r, "rest_id"))) ||
     !post_number(id)) {
    return -1;
  }
  strcpy(u->id, id);
  copy_text(u->user, sizeof u->user, user);
  if((s=user_field(r, "name"))) {
    copy_text(u->name, sizeof u->name, s);
  }
  avatar(u->avatar, sizeof u->avatar, r);
  // x.com's new shape of a user has the banner beside the avatar
  // (banner.image_url); the old one, in legacy
  if(!copy_image_url(u->banner, sizeof u->banner, json_string(json_path(r, "banner", "image_url", NULL)))) {
    copy_image_url(u->banner, sizeof u->banner, json_string(json_path(r, "legacy", "profile_banner_url", NULL)));
  }
  u->badge = badge(r);
  // x.com's newer shape of a user has the bio in profile_bio; the older, in legacy
  if((s=json_string(json_path(r, "profile_bio", "description", NULL)))) {
    copy_text(u->bio, sizeof u->bio, s);
    urls = json_path(r, "profile_bio", "entities", "description", "urls", NULL);
  } else if((s=json_string(json_path(r, "legacy", "description", NULL)))) {
    copy_text(u->bio, sizeof u->bio, s);
    urls = json_path(r, "legacy", "entities", "description", "urls", NULL);
  } else {
    urls = NULL;
  }
  if(s) {
    for(const json_node_t *n=urls ? urls->child : NULL; n; n=n->next) {
      const char *from = json_string(json_get(n, "url"));
      const char *to = json_string(json_get(n, "display_url"));
      if(from && to && !strncmp(from, "https://t.co/", 13)) {
        replace(u->bio, sizeof u->bio, from, to);
      }
    }
  }
  if((s=json_string(json_path(r, "location", "location", NULL))) ||
     (s=json_string(json_path(r, "legacy", "location", NULL)))) {
    copy_text(u->location, sizeof u->location, s);
  }
  u->following = count(json_path(r, "legacy", "friends_count", NULL));
  u->followers = count(json_path(r, "legacy", "followers_count", NULL));
  u->posts = count(json_path(r, "legacy", "statuses_count", NULL));
  u->joined = x_parse_time(user_field(r, "created_at"));
  // newer answers say it in relationship_perspectives, older ones in legacy
  u->followed = is_true(json_path(r, "relationship_perspectives", "following", NULL)) ||
                is_true(json_path(r, "legacy", "following", NULL));
  u->follows_you = is_true(json_path(r, "relationship_perspectives", "followed_by", NULL)) ||
                   is_true(json_path(r, "legacy", "followed_by", NULL));
  u->muted = is_true(json_path(r, "relationship_perspectives", "muting", NULL)) ||
             is_true(json_path(r, "legacy", "muting", NULL));
  u->blocked = is_true(json_path(r, "relationship_perspectives", "blocking", NULL)) ||
               is_true(json_path(r, "legacy", "blocking", NULL));
  return 0;
}


static int
post_number(const char *id) {
  size_t n = strlen(id);

  if(!n || n >= 24) { // X's numbers have 19 digits; never more than 23
    return 0;
  }
  for(; *id; id++) {
    if(*id < '0' || *id > '9') {
      return 0;
    }
  }
  return 1;
}


int
x_like_request(char *url, size_t usize, char *body, size_t bsize, const char *id, int like) {
  const char *query = like ? LIKE_QUERY : UNLIKE_QUERY;

  if(!post_number(id) ||
     snprintf(url, usize, "https://x.com/i/api/graphql/%s/%s", query,
              like ? "FavoriteTweet" : "UnfavoriteTweet") >= (int)usize ||
     snprintf(body, bsize, "{\"variables\":{\"tweet_id\":\"%s\"},\"queryId\":\"%s\"}", id,
              query) >= (int)bsize) {
    return -1;
  }
  return 0;
}


/**
 * s as the inside of a JSON string, appended at *n: quotes, backslashes and
 * control characters escaped, the rest of the UTF-8 as it is
 **/
static int
append_json(char *buf, size_t size, size_t *n, const char *s) {
  static const char hex[] = "0123456789abcdef";
  unsigned char c;
  char esc[8];
  size_t len;

  for(; *s; s++) {
    c = *s;
    if(c == '"' || c == '\\') {
      esc[0] = '\\';
      esc[1] = c;
      len = 2;
    } else if(c == '\n') {
      memcpy(esc, "\\n", len=2);
    } else if(c < 0x20) {
      memcpy(esc, "\\u00", 4);
      esc[4] = hex[c >> 4];
      esc[5] = hex[c & 15];
      len = 6;
    } else {
      esc[0] = c;
      len = 1;
    }
    if(*n + len >= size) {
      return -1;
    }
    memcpy(buf + *n, esc, len);
    *n += len;
  }
  buf[*n] = 0;
  return 0;
}


int
x_reply_request(char *url, size_t usize, char *body, size_t bsize, const char *id,
                const char *text) {
  return x_post_request(url, usize, body, bsize, id, text, NULL, 0);
}


/**
 * the media_entities of a post: the ids upload.x.com gave
 **/
static int
append_media(char *body, size_t bsize, size_t *n, const char *const *media, int count) {
  if(append(body, bsize, n, "\"media\":{\"media_entities\":[", 0)) {
    return -1;
  }
  for(int i=0; i < count; i++) {
    if(append(body, bsize, n, i ? ",{\"media_id\":\"" : "{\"media_id\":\"", 0) ||
       append(body, bsize, n, media[i], 0) ||
       append(body, bsize, n, "\",\"tagged_users\":[]}", 0)) {
      return -1;
    }
  }
  return append(body, bsize, n, "],\"possibly_sensitive\":false},", 0);
}


int
x_post_request(char *url, size_t usize, char *body, size_t bsize, const char *id,
               const char *text, const char *const *media, int media_count) {
  x_compose_t how = {id && id[0] ? id : NULL, NULL, NULL, NULL, NULL};

  if(id && id[0] && !post_number(id)) {
    return -1;
  }
  return x_compose_request(url, usize, body, bsize, &how, text, media, media_count);
}


int
x_post_length(const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  uint32_t cp;
  int n = 0, more;

  while(*p) {
    // a UTF-8 character, whole or not
    more = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : 0;
    cp = more == 0 ? *p : more == 1 ? *p & 0x1F : more == 2 ? *p & 0x0F : *p & 0x07;
    for(p++; more > 0 && (*p & 0xC0) == 0x80; more--, p++) {
      cp = cp << 6 | (*p & 0x3F);
    }
    n += cp <= 0x10FF || (cp >= 0x2000 && cp <= 0x200D) || (cp >= 0x2010 && cp <= 0x201F) ||
         (cp >= 0x2032 && cp <= 0x2037) ? 1 : 2;
  }
  return n;
}


/**
 * a screen name: letters, digits and _
 **/
static int
screen_name(const char *s) {
  size_t n = s ? strlen(s) : 0;

  if(!n || n > 50) {
    return 0;
  }
  for(; *s; s++) {
    if(!is_word(*s)) {
      return 0;
    }
  }
  return 1;
}


int
x_compose_request(char *url, size_t usize, char *body, size_t bsize, const x_compose_t *how,
                  const char *text, const char *const *media, int media_count) {
  const char *reply = how ? how->reply_to : NULL, *edit = how ? how->edit_of : NULL;
  const char *quote_user = how ? how->quote_user : NULL, *quote_id = how ? how->quote_id : NULL;
  const char *card = how ? how->card_uri : NULL;
  int note = x_post_length(text) > X_POST_CHARS;
  const char *query = note ? NOTE_QUERY : REPLY_QUERY;
  size_t n = 0;

  if(media_count < 0 || media_count > X_MEDIA_IDS) {
    return -1;
  }
  for(int i=0; i < media_count; i++) {
    if(!post_number(media[i])) {
      return -1;
    }
  }
  if((reply && !post_number(reply)) || (edit && !post_number(edit)) || (card && (!card_uri_ok(card) || media_count)) ||
     ((quote_user || quote_id) && !(screen_name(quote_user) && quote_id && post_number(quote_id))) ||
     (!text[0] && !media_count && !quote_id) || strlen(text) > X_REPLY_MAX ||
     snprintf(url, usize, "https://x.com/i/api/graphql/%s/%s", query,
              note ? "CreateNoteTweet" : "CreateTweet") >= (int)usize) {
    return -1;
  }
  // what x.com sends for a reply, or a post, quoting one, or an edit
  if(append(body, bsize, &n, "{\"variables\":{\"tweet_text\":\"", 0) ||
     append_json(body, bsize, &n, text) ||
     append(body, bsize, &n, "\",", 0) ||
     (reply && (append(body, bsize, &n, "\"reply\":{\"in_reply_to_tweet_id\":\"", 0) ||
                append(body, bsize, &n, reply, 0) ||
                append(body, bsize, &n, "\",\"exclude_reply_user_ids\":[]},", 0))) ||
     (quote_id && (append(body, bsize, &n, "\"attachment_url\":\"https://x.com/", 0) ||
                   append(body, bsize, &n, quote_user, 0) ||
                   append(body, bsize, &n, "/status/", 0) ||
                   append(body, bsize, &n, quote_id, 0) ||
                   append(body, bsize, &n, "\",", 0))) ||
     (edit && (append(body, bsize, &n, "\"edit_options\":{\"previous_tweet_id\":\"", 0) ||
               append(body, bsize, &n, edit, 0) ||
               append(body, bsize, &n, "\"},", 0))) ||
     (card && (append(body, bsize, &n, "\"card_uri\":\"", 0) ||
               append(body, bsize, &n, card, 0) ||
               append(body, bsize, &n, "\",", 0))) ||
     append(body, bsize, &n, "\"dark_request\":false,", 0) ||
     append_media(body, bsize, &n, media, media_count) ||
     append(body, bsize, &n, "\"semantic_annotation_ids\":[],", 0) ||
     (note && append(body, bsize, &n, "\"richtext_options\":{\"richtext_tags\":[]},", 0)) ||
     append(body, bsize, &n, "\"disallowed_reply_options\":null},\"features\":", 0) ||
     append(body, bsize, &n, reply_features, 0) ||
     append(body, bsize, &n, ",\"queryId\":\"", 0) ||
     append(body, bsize, &n, query, 0) ||
     append(body, bsize, &n, "\"}", 0)) {
    return -1;
  }
  return 0;
}


int
x_schedule_request(char *url, size_t usize, char *body, size_t bsize, const char *text,
                   const char *const *media, int media_count, long long at) {
  char when[32];
  size_t n = 0;

  if(!text || media_count < 0 || media_count > X_MEDIA_IDS || at <= 0 || (!text[0] && !media_count) ||
     x_post_length(text) > X_POST_CHARS || strlen(text) > X_REPLY_MAX ||
     snprintf(url, usize, "https://x.com/i/api/graphql/%s/CreateScheduledTweet", SCHEDULE_QUERY) >= (int)usize) {
    return -1;
  }
  for(int i=0; i < media_count; i++) {
    if(!post_number(media[i])) {
      return -1;
    }
  }
  snprintf(when, sizeof when, "%lld", at);
  if(append(body, bsize, &n, "{\"variables\":{\"post_tweet_request\":{\"auto_populate_reply_metadata\":false,"
            "\"status\":\"", 0) ||
     append_json(body, bsize, &n, text) ||
     append(body, bsize, &n, "\",\"exclude_reply_user_ids\":[],\"media_ids\":[", 0)) {
    return -1;
  }
  for(int i=0; i < media_count; i++) {
    if(append(body, bsize, &n, i ? ",\"" : "\"", 0) || append(body, bsize, &n, media[i], 0) ||
       append(body, bsize, &n, "\"", 0)) {
      return -1;
    }
  }
  if(append(body, bsize, &n, "]},\"execute_at\":", 0) || append(body, bsize, &n, when, 0) ||
     append(body, bsize, &n, "},\"queryId\":\"" SCHEDULE_QUERY "\"}", 0)) {
    return -1;
  }
  return 0;
}


int
x_pin_request(char *url, size_t usize, char *body, size_t bsize, const char *id, int pin) {
  const char *query = pin ? PIN_QUERY : UNPIN_QUERY;

  if(!post_number(id) ||
     snprintf(url, usize, "https://x.com/i/api/graphql/%s/%s", query, pin ? "PinTweet" : "UnpinTweet") >=
     (int)usize ||
     snprintf(body, bsize, "{\"variables\":{\"tweet_id\":\"%s\"},\"queryId\":\"%s\"}", id, query) >=
     (int)bsize) {
    return -1;
  }
  return 0;
}


int
x_user_request(char *url, size_t usize, char *body, size_t bsize, int what, const char *id) {
  static const char *const paths[] = {
    [X_FOLLOW] = "friendships/create", [X_UNFOLLOW] = "friendships/destroy",
    [X_MUTE] = "mutes/users/create", [X_UNMUTE] = "mutes/users/destroy",
    [X_BLOCK] = "blocks/create", [X_UNBLOCK] = "blocks/destroy",
  };

  if(what < X_FOLLOW || what > X_UNBLOCK || !id || !post_number(id) ||
     snprintf(url, usize, "https://x.com/i/api/1.1/%s.json", paths[what]) >= (int)usize ||
     // the user's state comes back with it, as x.com asks
     snprintf(body, bsize, "include_profile_interstitial_type=1&include_blocking=1&include_blocked_by=1"
              "&include_followed_by=1&include_want_retweets=1&include_mute_edge=1&include_can_dm=1"
              "&include_can_media_tag=1&include_ext_is_blue_verified=1&include_ext_verified_type=1"
              "&include_ext_profile_image_shape=1&skip_status=1&user_id=%s", id) >= (int)bsize) {
    return -1;
  }
  return 0;
}


int
x_repost_request(char *url, size_t usize, char *body, size_t bsize, const char *id, int repost) {
  const char *query = repost ? REPOST_QUERY : UNREPOST_QUERY;

  if(!post_number(id) ||
     snprintf(url, usize, "https://x.com/i/api/graphql/%s/%s", query,
              repost ? "CreateRetweet" : "DeleteRetweet") >= (int)usize ||
     snprintf(body, bsize, "{\"variables\":{\"%s\":\"%s\",\"dark_request\":false},\"queryId\":\"%s\"}",
              repost ? "tweet_id" : "source_tweet_id", id, query) >= (int)bsize) {
    return -1;
  }
  return 0;
}


int
x_delete_request(char *url, size_t usize, char *body, size_t bsize, const char *id) {
  if(!post_number(id) ||
     snprintf(url, usize, "https://x.com/i/api/graphql/" DELETE_QUERY "/DeleteTweet") >= (int)usize ||
     snprintf(body, bsize, "{\"variables\":{\"tweet_id\":\"%s\",\"dark_request\":false},"
              "\"queryId\":\"" DELETE_QUERY "\"}", id) >= (int)bsize) {
    return -1;
  }
  return 0;
}


int
x_scheduled_url(char *buf, size_t size) {
  return snprintf(buf, size, "https://x.com/i/api/graphql/" SCHEDULED_QUERY "/FetchScheduledTweets"
                  "?variables=%%7B%%7D") >= (int)size ? -1 : 0;
}


/**
 * the first array named key under node, depth first, or NULL: the answer's
 * list, wherever X keeps it
 **/
static const json_node_t *
find_array(const json_node_t *node, const char *key, int depth) {
  const json_node_t *found;

  if(!node || depth > 12 || (node->type != JSON_OBJECT && node->type != JSON_ARRAY)) {
    return NULL;
  }
  for(const json_node_t *n=node->child; n; n=n->next) {
    if(node->type == JSON_OBJECT && n->type == JSON_ARRAY && n->key && !strcmp(n->key, key)) {
      return n;
    }
    if((found=find_array(n, key, depth + 1))) {
      return found;
    }
  }
  return NULL;
}


int
x_scheduled_posts(const json_node_t *root, x_post_t *posts, int max, int *total) {
  const json_node_t *list = find_array(json_get(root, "data"), "scheduled_tweet_list", 0);
  int n = 0, all = 0;

  for(const json_node_t *e=list ? list->child : NULL; e; e=e->next) {
    const char *id = json_string(json_get(e, "rest_id")), *state, *text;
    long long at = count(json_path(e, "scheduling_info", "execute_at", NULL));
    x_post_t *p;
    if(!id || !post_number(id) || at <= 0) {
      continue;
    }
    all++;
    if(!posts || n == max) {
      continue;
    }
    p = &posts[n++];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_SCHEDULED;
    snprintf(p->id, sizeof p->id, "%s", id);
    p->time = at / 1000;
    state = json_string(json_path(e, "scheduling_info", "state", NULL));
    p->icon = state && !strcmp(state, "Failed");
    if((text=json_string(json_path(e, "tweet_create_request", "status", NULL)))) {
      copy_text(p->text, sizeof p->text, text);
    }
    p->media_count = 0;
    p->replies = p->reposts = p->likes = p->views = p->bookmarks = -1;
  }
  // soonest first
  for(int i=1; i < n; i++) {
    for(int k=i; k > 0 && posts[k].time < posts[k - 1].time; k--) {
      x_post_t t = posts[k];
      posts[k] = posts[k - 1];
      posts[k - 1] = t;
    }
  }
  if(total) {
    *total = all;
  }
  return n;
}


int
x_unschedule_request(char *url, size_t usize, char *body, size_t bsize, const char *id) {
  if(!post_number(id) ||
     snprintf(url, usize, "https://x.com/i/api/graphql/" UNSCHEDULE_QUERY "/DeleteScheduledTweet") >= (int)usize ||
     snprintf(body, bsize, "{\"variables\":{\"scheduled_tweet_id\":\"%s\"},"
              "\"queryId\":\"" UNSCHEDULE_QUERY "\"}", id) >= (int)bsize) {
    return -1;
  }
  return 0;
}


/*
 * Direct messages, the kind x.com's Messages reads over REST: the inbox, a
 * conversation, sending. Conversations X moved to X Chat (end-to-end
 * encrypted) are not in these answers.
 */

#define DM_EXT "mediaColor%2CaltText%2CbusinessAffiliationsLabel%2CmediaStats%2ChighlightedLabel" \
  "%2CparodyCommentaryFanLabel%2CvoiceInfo%2CbirdwatchPivot%2CsuperFollowMetadata%2CunmentionInfo" \
  "%2CeditControl%2Carticle"
#define DM_QUERY "cards_platform=Web-12&dm_secret_conversations_enabled=false&dm_users=true&ext=" DM_EXT \
  "&filter_low_quality=true&include_blocked_by=1&include_blocking=1&include_can_dm=1" \
  "&include_can_media_tag=1&include_cards=1&include_conversation_info=false&include_ext_alt_text=true" \
  "&include_ext_business_affiliations_label=true&include_ext_edit_control=true" \
  "&include_ext_is_blue_verified=1&include_ext_limited_action_results=true&include_ext_media_color=true" \
  "&include_ext_parody_commentary_fan_label=true&include_ext_profile_image_shape=1" \
  "&include_ext_verified_type=1&include_ext_views=true&include_followed_by=1&include_groups=true" \
  "&include_inbox_timelines=true&include_mute_edge=1&include_profile_interstitial_type=1" \
  "&include_quality=all&include_quote_count=true&include_reply_count=1&include_want_retweets=1" \
  "&krs_registration_enabled=false&nsfw_filtering_enabled=false&skip_status=1&supports_edit=true" \
  "&supports_reactions=true&tweet_mode=extended"
#define DM_SEND_QUERY "ext=mediaColor%2CaltText%2CmediaStats%2ChighlightedLabel%2CvoiceInfo" \
  "%2CbirdwatchPivot%2CsuperFollowMetadata%2CunmentionInfo%2CeditControl%2Carticle" \
  "&include_ext_alt_text=true&include_ext_limited_action_results=true&include_ext_media_color=true" \
  "&include_ext_views=true&include_groups=true&include_inbox_timelines=true&include_reply_count=1" \
  "&supports_edit=true&supports_reactions=true&tweet_mode=extended"


/**
 * whether id is a conversation's number: digits, and a dash between the
 * two people's
 **/
static int
conversation_number(const char *id) {
  size_t n = id ? strlen(id) : 0;

  if(!n || n >= X_ID_MAX || *id == '-' || id[n - 1] == '-') {
    return 0;
  }
  for(; *id; id++) {
    if((*id < '0' || *id > '9') && *id != '-') {
      return 0;
    }
  }
  return 1;
}


int
x_dm_inbox_url(char *buf, size_t size) {
  return snprintf(buf, size, "https://x.com/i/api/1.1/dm/inbox_initial_state.json?%s", DM_QUERY) >= (int)size ?
    -1 : 0;
}


int
x_dm_conversation_url(char *buf, size_t size, const char *conversation) {
  if(!conversation_number(conversation) ||
     snprintf(buf, size, "https://x.com/i/api/1.1/dm/conversation/%s.json?%s"
              "&context=FETCH_DM_CONVERSATION", conversation, DM_QUERY) >= (int)size) {
    return -1;
  }
  return 0;
}


/**
 * the user number id of a DM answer, from its map of users
 **/
static const json_node_t *
dm_user(const json_node_t *data, const char *id) {
  return id && id[0] ? json_get(json_get(data, "users"), id) : NULL;
}


static void
dm_person(x_post_t *p, const json_node_t *u) {
  const char *s;

  if(!u) {
    return;
  }
  if((s=json_string(json_get(u, "id_str")))) {
    snprintf(p->user_id, sizeof p->user_id, "%s", s);
  }
  if((s=json_string(json_get(u, "screen_name")))) {
    snprintf(p->user, sizeof p->user, "%s", s);
  }
  if((s=json_string(json_get(u, "name")))) {
    copy_text(p->name, sizeof p->name, s);
  }
  copy_image_url(p->avatar, sizeof p->avatar, json_string(json_get(u, "profile_image_url_https")));
  p->badge = badge(u);
  playing_of(u, p->playing, sizeof p->playing);
}


/**
 * what a message's text carries, an invite to play being one
 **/
static int
dm_kind(const x_post_t *p, int carries) {
  char game[PLAY_GAME], id[16];

  return carries == X_DM_TEXT && play_invite_parse(p->text, game, sizeof game, id, sizeof id) ? X_DM_INVITE :
    carries;
}


/**
 * a message's text, its links as X shows them and the link of what it
 * carries gone; returns what it carries (X_DM_*)
 **/
static int
dm_text(char *text, size_t size, const json_node_t *data) {
  const json_node_t *urls = json_path(data, "entities", "urls", NULL), *att = json_get(data, "attachment");
  const json_node_t *media = NULL;
  const char *s = json_string(json_get(data, "text"));
  int what = X_DM_TEXT;
  size_t n;

  copy_text(text, size, s ? s : "");
  for(const json_node_t *u=urls ? urls->child : NULL; u; u=u->next) {
    const char *from = json_string(json_get(u, "url"));
    const char *to = json_string(json_get(u, "display_url"));
    if(from && to && !strncmp(from, "https://t.co/", 13)) {
      replace(text, size, from, to);
    }
  }
  if((media=json_get(att, "photo"))) {
    what = X_DM_PHOTO;
  } else if((media=json_get(att, "video"))) {
    what = X_DM_VIDEO;
  } else if((media=json_get(att, "animated_gif"))) {
    what = X_DM_GIF;
  } else if((media=json_get(att, "tweet"))) {
    what = X_DM_POST;
  }
  if(media) {
    const char *url = json_string(json_get(media, "url"));
    if(url && !strncmp(url, "https://t.co/", 13)) {
      replace(text, size, url, "");
    }
  }
  for(n=strlen(text); n && (text[n - 1] == ' ' || text[n - 1] == '\n'); n--) {
    text[n - 1] = 0;
  }
  return what;
}


/**
 * which of two event numbers is later (they grow with time); numbers as
 * text, so longer is later
 **/
static int
later(const char *a, const char *b) {
  size_t na = a ? strlen(a) : 0, nb = b ? strlen(b) : 0;

  return na != nb ? na > nb : na && strcmp(a, b) > 0;
}


static int
newest_first(const void *a, const void *b) {
  const x_post_t *x = a, *y = b;

  return x->time < y->time ? 1 : x->time > y->time ? -1 : 0;
}


static int
oldest_first(const void *a, const void *b) {
  const x_post_t *x = a, *y = b;

  if(strlen(x->id) != strlen(y->id)) {
    return strlen(x->id) < strlen(y->id) ? -1 : 1;
  }
  return strcmp(x->id, y->id);
}


int
x_dm_inbox(const json_node_t *root, const char *me, x_post_t *posts, int max) {
  const json_node_t *data = json_get(root, "inbox_initial_state"), *convs, *entries;
  int n = 0;

  if(!data) {
    data = json_get(root, "inbox_timeline");
  }
  convs = json_get(data, "conversations");
  entries = json_get(data, "entries");
  for(const json_node_t *c=convs ? convs->child : NULL; c && (!posts || n < max); c=c->next) {
    const char *id = json_string(json_get(c, "conversation_id")), *type = json_string(json_get(c, "type"));
    const char *sort = json_string(json_get(c, "sort_event_id")), *mine_read = NULL, *other = NULL;
    const json_node_t *people = json_get(c, "participants");
    x_post_t *p;

    if(!conversation_number(id)) {
      continue;
    }
    if(!posts) {
      n++;
      continue;
    }
    p = &posts[n++];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_CHAT;
    snprintf(p->id, sizeof p->id, "%s", id);
    p->replies = p->reposts = p->likes = p->views = -1;
    p->time = count(json_get(c, "sort_timestamp")) / 1000;
    p->time = p->time < 0 ? 0 : p->time;
    p->group = type && !strcmp(type, "GROUP_DM");
    p->request = json_get(c, "trusted") && json_get(c, "trusted")->type == JSON_FALSE;
    for(const json_node_t *u=people ? people->child : NULL; u; u=u->next) {
      const char *uid = json_string(json_get(u, "user_id"));
      if(uid && me && !strcmp(uid, me)) {
        mine_read = json_string(json_get(u, "last_read_event_id"));
      } else if(uid && !other) {
        other = uid;
      }
      // the faces of a group, up to four
      if(uid && (!me || strcmp(uid, me)) && p->face_count < X_MAX_FACES) {
        const json_node_t *who = dm_user(data, uid);
        if(copy_image_url(p->faces[p->face_count], sizeof p->faces[0],
                          json_string(json_get(who, "profile_image_url_https")))) {
          p->face_count++;
        }
      }
    }
    dm_person(p, dm_user(data, other));
    if(p->group) {
      const char *name = json_string(json_get(c, "name"));
      p->user[0] = 0;
      if(name && name[0]) {
        copy_text(p->name, sizeof p->name, name);
      } else {
        // the names of the others, as X names a group without one
        size_t len = 0;
        int named = 0;
        p->name[0] = 0;
        for(const json_node_t *u=people ? people->child : NULL; u && named < 3; u=u->next) {
          const char *uid = json_string(json_get(u, "user_id"));
          const char *who = json_string(json_get(dm_user(data, uid), "name"));
          if(!uid || !who || (me && !strcmp(uid, me))) {
            continue;
          }
          len += snprintf(p->name + len, len < sizeof p->name ? sizeof p->name - len : 0, "%s%s",
                          named++ ? ", " : "", who);
          len = len < sizeof p->name ? len : sizeof p->name - 1;
        }
      }
      if(!copy_image_url(p->avatar, sizeof p->avatar, json_string(json_get(c, "avatar_image_https")))) {
        p->avatar[0] = 0;
      }
      p->badge = X_BADGE_NONE;
      p->playing[0] = 0;
    }
    p->unread = me && sort && later(sort, mine_read);
  }
  if(!posts) {
    return n;
  }

  // each one's last message: the newest of its entries
  for(const json_node_t *e=entries ? entries->child : NULL; e; e=e->next) {
    const json_node_t *m = json_get(e, "message"), *data_m = json_get(m, "message_data");
    const char *conv = json_string(json_get(m, "conversation_id")), *id = json_string(json_get(m, "id"));
    const char *from = json_string(json_get(data_m, "sender_id"));
    if(!m || !conv || !id) {
      continue;
    }
    for(int i=0; i < n; i++) {
      x_post_t *p = &posts[i];
      if(strcmp(p->id, conv) || !later(id, p->quote_user)) {
        continue;
      }
      // quote_user holds the number of the message shown, while the
      // entries are read
      snprintf(p->quote_user, sizeof p->quote_user, "%s", id);
      p->icon = dm_kind(p, dm_text(p->text, sizeof p->text, data_m));
      p->mine = from && me && !strcmp(from, me);
      if(p->group && from && !p->mine) {
        // a group's message says who wrote it
        const char *who = json_string(json_get(dm_user(data, from), "name"));
        if(who) {
          snprintf(p->quote_name, sizeof p->quote_name, "%s", who);
        }
      }
      break;
    }
  }
  for(int i=0; i < n; i++) {
    posts[i].quote_user[0] = 0;
  }
  qsort(posts, n, sizeof *posts, newest_first);
  return n;
}


int
x_dm_messages(const json_node_t *root, const char *conversation, x_post_t *posts, int max) {
  const json_node_t *data = json_get(root, "conversation_timeline"), *entries;
  int n = 0;

  if(!data) {
    data = json_get(root, "inbox_initial_state");
  }
  entries = json_get(data, "entries");
  for(const json_node_t *e=entries ? entries->child : NULL; e && (!posts || n < max); e=e->next) {
    const json_node_t *m = json_get(e, "message"), *data_m = json_get(m, "message_data");
    const char *conv = json_string(json_get(m, "conversation_id")), *id = json_string(json_get(m, "id"));
    long long ms = count(json_get(m, "time"));
    x_post_t *p;

    if(!m || !id || !post_number(id) || (conv && conversation && strcmp(conv, conversation))) {
      continue;
    }
    if(!posts) {
      n++;
      continue;
    }
    p = &posts[n++];
    memset(p, 0, sizeof *p);
    p->kind = X_KIND_DM;
    snprintf(p->id, sizeof p->id, "%s", id);
    p->replies = p->reposts = p->likes = p->views = -1;
    p->time = ms > 0 ? ms / 1000 : 0;
    dm_person(p, dm_user(data, json_string(json_get(data_m, "sender_id"))));
    if(!p->user_id[0]) {
      const char *from = json_string(json_get(data_m, "sender_id"));
      snprintf(p->user_id, sizeof p->user_id, "%s", from ? from : "");
    }
    p->icon = dm_kind(p, dm_text(p->text, sizeof p->text, data_m));
  }
  if(posts) {
    qsort(posts, n, sizeof *posts, oldest_first);
  }
  return n;
}


int
x_dm_read_request(char *url, size_t usize, char *body, size_t bsize, const char *conversation,
                  const char *last) {
  if(!conversation_number(conversation) || !last || !post_number(last) ||
     snprintf(url, usize, "https://x.com/i/api/1.1/dm/conversation/%s/mark_read.json", conversation) >=
     (int)usize ||
     snprintf(body, bsize, "conversationId=%s&last_read_event_id=%s", conversation, last) >= (int)bsize) {
    return -1;
  }
  return 0;
}


int
x_dm_media_request(char *url, size_t usize, char *body, size_t bsize, const char *conversation,
                   const char *text, const char *media_id, const char *request_id) {
  size_t n = 0;

  for(const char *c=request_id; c && *c; c++) {
    if(!is_word(*c) && *c != '-') {
      return -1;
    }
  }
  if(!text) {
    text = "";
  }
  if(!conversation_number(conversation) || (!text[0] && !(media_id && media_id[0])) || !request_id ||
     !request_id[0] || strlen(text) > X_REPLY_MAX || (media_id && media_id[0] && !post_number(media_id)) ||
     snprintf(url, usize, "https://x.com/i/api/1.1/dm/new2.json?%s", DM_SEND_QUERY) >= (int)usize ||
     (n=snprintf(body, bsize, "{\"conversation_id\":\"%s\",\"recipient_ids\":false,\"request_id\":\"%s\","
                 "\"text\":\"", conversation, request_id)) >= bsize ||
     append_json(body, bsize, &n, text) ||
     append(body, bsize, &n, "\",\"cards_platform\":\"Web-12\",\"include_cards\":1,"
            "\"include_quote_count\":true,\"dm_users\":false", 0) ||
     (media_id && media_id[0] && (append(body, bsize, &n, ",\"media_id\":\"", 0) ||
                                  append(body, bsize, &n, media_id, 0) || append(body, bsize, &n, "\"", 0))) ||
     append(body, bsize, &n, "}", 0)) {
    return -1;
  }
  return 0;
}


int
x_dm_send_request(char *url, size_t usize, char *body, size_t bsize, const char *conversation,
                  const char *text, const char *request_id) {
  if(!text || !text[0]) {
    return -1;
  }
  return x_dm_media_request(url, usize, body, bsize, conversation, text, NULL, request_id);
}


int
x_location_request(char *url, size_t usize, char *body, size_t bsize, const char *location) {
  size_t n = 0;

  if(!location || play_units(location) > PLAY_LOCATION || strpbrk(location, "\r\n") ||
     snprintf(url, usize, "https://x.com/i/api/1.1/account/update_profile.json") >= (int)usize ||
     append(body, bsize, &n, "location=", 0) || append(body, bsize, &n, location, 1)) {
    return -1;
  }
  return 0;
}


#define GROK_CREATE_QUERY "vvC5uy7pWWHXS2aDi1FZeA"


int
x_grok_create_request(char *url, size_t usize, char *body, size_t bsize) {
  if(snprintf(url, usize, "https://x.com/i/api/graphql/%s/CreateGrokConversation", GROK_CREATE_QUERY) >=
     (int)usize ||
     snprintf(body, bsize, "{\"variables\":{},\"queryId\":\"%s\"}", GROK_CREATE_QUERY) >= (int)bsize) {
    return -1;
  }
  return 0;
}


/**
 * whether id can name a conversation of Grok: letters, digits, - and _
 **/
static int
grok_id(const char *id) {
  size_t n = 0;

  for(; id && id[n]; n++) {
    if(!is_word((unsigned char)id[n]) && id[n] != '-') {
      return 0;
    }
  }
  return n > 0 && n < X_GROK_ID_MAX;
}


int
x_grok_conversation(const json_node_t *root, char *id, size_t size) {
  const char *s = json_string(json_path(root, "data", "create_grok_conversation", "conversation_id", NULL));

  if(size) {
    id[0] = 0;
  }
  if(!grok_id(s) || strlen(s) >= size) {
    return -1;
  }
  memcpy(id, s, strlen(s) + 1);
  return 0;
}


int
x_grok_prompt(char *out, size_t size, const char *game, const char *question, const char *language) {
  int len;

  if(!size || !question || !question[0] || !language || !language[0]) {
    return -1;
  }
  len = snprintf(out, size,
                 "I am playing %s%s%s on a PS5 and I want a hint, not a walkthrough: %s\n\n"
                 "Answer in %s, in three short sentences at most, as plain text. Give a nudge first "
                 "rather than the whole solution. Do not spoil anything: no story events, twists, "
                 "endings, characters' fates, or bosses, areas and items I have not mentioned.",
                 game && game[0] ? "\"" : "a game", game && game[0] ? game : "", game && game[0] ? "\"" : "",
                 question, language);
  if(len < 0 || (size_t)len >= size) {
    out[0] = 0;
    return -1;
  }
  return 0;
}


int
x_grok_request(char *url, size_t usize, char *body, size_t bsize, const char *host, const char *id,
               const char *model, const char *message) {
  size_t n = 0;

  if(!grok_id(id) || !message || !message[0] || !host || !host[0] || strchr(host, '/') ||
     snprintf(url, usize, "https://%s/2/grok/add_response.json", host) >= (int)usize ||
     append(body, bsize, &n, "{\"responses\":[{\"message\":\"", 0) ||
     append_json(body, bsize, &n, message) ||
     append(body, bsize, &n, "\",\"sender\":1,\"promptSource\":\"\",\"fileAttachments\":[]}],"
            "\"systemPromptName\":\"\",", 0)) {
    return -1;
  }
  if(model && model[0] && (append(body, bsize, &n, "\"grokModelOptionId\":\"", 0) ||
                           append_json(body, bsize, &n, model) || append(body, bsize, &n, "\",", 0))) {
    return -1;
  }
  if(append(body, bsize, &n, "\"conversationId\":\"", 0) || append(body, bsize, &n, id, 0) ||
     append(body, bsize, &n, "\",\"returnSearchResults\":false,\"returnCitations\":false,"
            "\"promptMetadata\":{\"promptSource\":\"NATURAL\",\"action\":\"INPUT\"},"
            "\"imageGenerationCount\":1,\"requestFeatures\":{\"eagerTweets\":false,\"serverHistory\":true},"
            "\"enableSideBySide\":false,\"toolOverrides\":{},\"isDeepsearch\":false,\"isReasoning\":false}", 0)) {
    return -1;
  }
  return 0;
}


/**
 * s at the end of out (n bytes of size used), without "**", as much as
 * fits at whole characters; 0, or -1 once out is full
 **/
static int
grok_append(char *out, size_t size, size_t *n, const char *s) {
  for(; *s; s++) {
    if(s[0] == '*' && s[1] == '*') {
      s++;
      continue;
    }
    if(*n + 1 >= size) {
      // not in the middle of a character: what was put of it goes
      if(((unsigned char)*s & 0xC0) == 0x80) {
        while(*n && ((unsigned char)out[*n - 1] & 0xC0) == 0x80) {
          (*n)--;
        }
        *n -= *n > 0;
      }
      out[*n] = 0;
      return -1;
    }
    out[(*n)++] = *s;
  }
  out[*n] = 0;
  return 0;
}


int
x_grok_answer(char *body, size_t len, char *out, size_t size, int *x_code) {
  const json_node_t *r, *thinking;
  const char *message, *words, *sender;
  char *line = body, *end = body + len, *eol;
  json_doc_t *doc;
  size_t n = 0, err_off;
  int parts = 0, full = 0, code;

  *x_code = -1;
  if(!size) {
    return 0;
  }
  out[0] = 0;
  while(line < end) {
    for(eol=line; eol < end && *eol != '\n'; eol++) {
    }
    if(eol > line && (doc=json_parse(line, (size_t)(eol - line), &err_off))) {
      r = json_get(json_root(doc), "result");
      words = json_string(json_get(r, "message"));
      if(!words) {
        words = json_string(json_path(r, "response", "token", NULL));
      }
      sender = json_string(json_get(r, "sender"));
      thinking = json_get(r, "isThinking");
      if(words && words[0] && !full && !(sender && (!strcmp(sender, "USER") || !strcmp(sender, "user") ||
                                                     !strcmp(sender, "human"))) &&
         !(thinking && thinking->type == JSON_TRUE)) {
        full = grok_append(out, size, &n, words) != 0;
        parts++;
      } else if(!r && (code=x_first_error(json_root(doc), &message)) > 0 && *x_code < 0) {
        *x_code = code;
      }
      json_free(doc);
    }
    line = eol + 1;
  }
  return parts;
}


int
x_bookmark_request(char *url, size_t usize, char *body, size_t bsize, const char *id, int add) {
  const char *query = add ? BOOKMARK_QUERY : UNBOOKMARK_QUERY;

  if(!post_number(id) ||
     snprintf(url, usize, "https://x.com/i/api/graphql/%s/%s", query,
              add ? "CreateBookmark" : "DeleteBookmark") >= (int)usize ||
     snprintf(body, bsize, "{\"variables\":{\"tweet_id\":\"%s\"},\"queryId\":\"%s\"}", id,
              query) >= (int)bsize) {
    return -1;
  }
  return 0;
}


int
x_action_done(const json_node_t *root, const char **message) {
  const json_node_t *data = json_get(root, "data");
  const char *done;
  int code;

  *message = NULL;
  if((done=json_string(json_get(data, "favorite_tweet"))) ||
     (done=json_string(json_get(data, "unfavorite_tweet"))) ||
     (done=json_string(json_get(data, "tweet_bookmark_put"))) ||
     (done=json_string(json_get(data, "tweet_bookmark_delete")))) {
    if(!strcmp(done, "Done")) {
      return 0;
    }
  }
  if(json_string(json_path(data, "create_tweet", "tweet_results", "result", "rest_id", NULL)) ||
     json_string(json_path(data, "tweet", "rest_id", NULL)) || // scheduled
     json_string(json_path(data, "notetweet_create", "tweet_results", "result", "rest_id", NULL)) ||
     ((json_get(data, "pin_tweet") || json_get(data, "unpin_tweet")) && !json_get(root, "errors")) ||
     json_string(json_path(data, "create_retweet", "retweet_results", "result", "rest_id", NULL)) ||
     json_path(data, "unretweet", "source_tweet_results", "result", NULL) ||
     (json_path(data, "delete_tweet", "tweet_results", NULL) && !json_get(root, "errors"))) {
    return 0;
  }
  // a direct message sent: the answer is its event; followed, muted or
  // blocked: the user
  if(!data && (json_get(root, "entries") || json_string(json_get(root, "id_str"))) &&
     !json_get(root, "errors")) {
    return 0;
  }
  code = x_first_error(root, message);
  return code > 0 ? code : -1;
}


int
x_first_error(const json_node_t *root, const char **message) {
  const json_node_t *errors = json_get(root, "errors");
  const json_node_t *code;

  *message = NULL;
  if(!errors || errors->type != JSON_ARRAY || !errors->child) {
    return -1;
  }
  *message = json_string(json_get(errors->child, "message"));
  code = json_get(errors->child, "code");
  return code && code->type == JSON_NUMBER ? atoi(code->str) : 0;
}


void
x_post_line(const x_post_t *post, char *buf, size_t size) {
  const char *s = post->text;
  size_t n;

  if(post->reposter[0]) {
    n = snprintf(buf, size, "@%s reposted @%s: ", post->reposter, post->user);
  } else {
    n = snprintf(buf, size, "@%s: ", post->user);
  }
  if(n >= size) {
    return; // screen names are ASCII, so snprintf cut at a whole character
  }

  for(; *s && n + 1 < size; s++) {
    buf[n++] = (*s == '\n' || *s == '\r' || *s == '\t') ? ' ' : *s;
  }
  if(*s) {
    // no room for the rest: end with "..." after a whole character
    n = whole_chars(buf, size - 4, buf[size - 4]);
    memcpy(buf + n, "...", 3);
    n += 3;
  }
  buf[n] = 0;
}


long long
x_parse_time(const char *created_at) {
  static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int day, hour, minute, second, year, month;
  char name[4];
  const char *at;

  if(!created_at || sscanf(created_at, "%*3s %3s %d %d:%d:%d %*s %d", name, &day,
                           &hour, &minute, &second, &year) != 6 ||
     strlen(name) != 3 || !(at=strstr(months, name)) || (at - months) % 3) {
    return 0;
  }
  month = (at - months) / 3 + 1;
  return days_from_civil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}


int
x_upload_id(const json_node_t *root, char *id, size_t size) {
  const char *s = json_string(json_get(root, "media_id_string"));

  if(!s || !post_number(s) || strlen(s) >= size) {
    return -1;
  }
  strcpy(id, s);
  return 0;
}


int
x_upload_url(char *buf, size_t size, int step, const char *media_id, long long total,
             const char *type, int segment) {
  static const char base[] = "https://upload.x.com/i/media/upload.json";
  int n;

  if(step == X_UPLOAD_INIT) {
    if(!type || total <= 0 || (strcmp(type, "image/jpeg") && strcmp(type, "image/png") &&
                               strcmp(type, "video/mp4"))) {
      return -1;
    }
    // a direct message's media are of their own kind, as x.com's Messages sends them
    n = snprintf(buf, size, "%s?command=INIT&total_bytes=%lld&media_type=%s%%2F%s&media_category=%s",
                 base, total, strncmp(type, "video", 5) ? "image" : "video", strchr(type, '/') + 1,
                 segment == X_UPLOAD_FOR_DM ? (strncmp(type, "video", 5) ? "dm_image" : "dm_video") :
                 strncmp(type, "video", 5) ? "tweet_image" : "tweet_video");
  } else if(!media_id || !post_number(media_id)) {
    return -1;
  } else if(step == X_UPLOAD_APPEND && segment >= 0 && segment < 1000) {
    n = snprintf(buf, size, "%s?command=APPEND&media_id=%s&segment_index=%d", base, media_id,
                 segment);
  } else if(step == X_UPLOAD_FINALIZE) {
    n = snprintf(buf, size, "%s?command=FINALIZE&media_id=%s&allow_async=true", base, media_id);
  } else if(step == X_UPLOAD_STATUS) {
    n = snprintf(buf, size, "%s?command=STATUS&media_id=%s", base, media_id);
  } else {
    return -1;
  }
  return n < 0 || (size_t)n >= size ? -1 : 0;
}


int
x_upload_form(const char *boundary, char *head, size_t hsize, char *tail, size_t tsize,
              char *type, size_t ysize) {
  int a, b, c;

  for(const char *p=boundary; *p; p++) {
    if(!is_word(*p) && *p != '-') {
      return -1;
    }
  }
  if(!*boundary || strlen(boundary) > 70) {
    return -1;
  }
  a = snprintf(head, hsize, "--%s\r\nContent-Disposition: form-data; name=\"media\"; filename=\"blob\"\r\n"
               "Content-Type: application/octet-stream\r\n\r\n", boundary);
  b = snprintf(tail, tsize, "\r\n--%s--\r\n", boundary);
  c = snprintf(type, ysize, "multipart/form-data; boundary=%s", boundary);
  return a < 0 || b < 0 || c < 0 || (size_t)a >= hsize || (size_t)b >= tsize || (size_t)c >= ysize ?
         -1 : 0;
}


int
x_upload_state(const json_node_t *root, int *wait_s) {
  const json_node_t *info = json_get(root, "processing_info");
  const char *state = json_string(json_get(info, "state"));
  long long after = count(json_get(info, "check_after_secs"));

  *wait_s = after > 0 && after < 60 ? (int)after : 1;
  if(!info) {
    return json_string(json_get(root, "media_id_string")) ? X_UPLOAD_READY : X_UPLOAD_FAILED;
  }
  if(state && !strcmp(state, "succeeded")) {
    return X_UPLOAD_READY;
  }
  if(state && (!strcmp(state, "pending") || !strcmp(state, "in_progress"))) {
    return X_UPLOAD_WAIT;
  }
  return X_UPLOAD_FAILED;
}
