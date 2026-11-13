/* Small JSON reader for the payloads: parses a document into a tree of
nodes. Strings are decoded in place (escapes become UTF-8), so the text buffer
must stay alive and unchanged while the tree is in use. */

#pragma once

#include <stddef.h>


typedef enum json_type {
  JSON_NULL,
  JSON_FALSE,
  JSON_TRUE,
  JSON_NUMBER,
  JSON_STRING,
  JSON_ARRAY,
  JSON_OBJECT,
} json_type_t;

typedef struct json_node json_node_t;
struct json_node {
  json_type_t  type;
  const char  *key;   // member name inside an object, else NULL
  const char  *str;   // decoded text of a string, raw text of a number
  size_t       len;
  json_node_t *child; // first element or member
  json_node_t *next;  // next element or member
};

typedef struct json_doc json_doc_t;


/**
 * parse text (len bytes, modified in place); on error returns NULL and sets
 * *err_off to where it stopped
 **/
json_doc_t *json_parse(char *text, size_t len, size_t *err_off);

// why json_parse_why stopped
enum { JSON_ERR_SYNTAX, JSON_ERR_MEMORY, JSON_ERR_DEPTH };

/**
 * json_parse that also sets *why to a JSON_ERR_* on error
 **/
json_doc_t *json_parse_why(char *text, size_t len, size_t *err_off, int *why);
const json_node_t *json_root(const json_doc_t *doc);
void json_free(json_doc_t *doc);

/**
 * member of an object, or NULL
 **/
const json_node_t *json_get(const json_node_t *obj, const char *key);

/**
 * follow a NULL-terminated list of member names, e.g.
 * json_path(user, "core", "screen_name", NULL)
 **/
const json_node_t *json_path(const json_node_t *node, ...);

/**
 * text of a string node, or NULL for anything else
 **/
const char *json_string(const json_node_t *node);
