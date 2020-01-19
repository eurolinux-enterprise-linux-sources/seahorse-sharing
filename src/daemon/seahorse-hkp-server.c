/*
 * Seahorse
 *
 * Copyright (C) 2005 Stefan Walter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <libsoup/soup.h>

#include "config.h"
#include "errno.h"
#include "string.h"

#include "seahorse-daemon.h"

#include <gpgme.h>

/**
 * SECTION:seahorse-hkp-server
 * @short_description: Implements the HKP Server ( HTTP Keyserver Protocol) Server
 *
 * http://tools.ietf.org/html/draft-shaw-openpgp-hkp-00
 *
 **/

/*
 * DEBUG: Set to number other than zero, in order to run HKP
 * server on a specific port
 */
#define HKP_FIXED_PORT  0       /* 11371 */

/*
 * The HKP format is deeeesgusting HTML, and pretty brain dead as far as error
 * codes, formatting and the like. But in the interest of interoperability, this
 * HKP server strives to generate the same stuff that PKS does.
 */

#define HKP_ERROR_RESPONSE      "<title>Public Key Server -- Error</title><p>\r\n" \
                                "<h1>Public Key Server -- Error</h1><p>\r\n" \
                                "%s"

#define HKP_VINDEX_PREFIX       "<title>Public Key Server -- Verbose Index ``%s''</title><p>" \
                                "<h1>Public Key Server -- Verbose Index ``%s''</h1><p>" \
                                "<pre>"

#define HKP_INDEX_PREFIX        "<title>Public Key Server -- Verbose Index ``%s''</title><p>" \
                                "<h1>Public Key Server -- Verbose Index ``%s''</h1><p>" \
                                "<pre>Type bits /keyID    Date       User ID\r\n"

#define HKP_INDEX_SUFFIX        "</pre>"

#define HKP_INDEX_REVOKED       "*** KEY REVOKED ***"
#define HKP_INDEX_UIDNAME       "%s "
#define HKP_INDEX_UIDEMAIL      "%s &lt;<a href=\"/pks/lookup?op=index&search=0x%s\">%s</a>&gt;"
#define HKP_INDEX_KEY_LINE      "pub % 5d%s/<a href=\"/pks/lookup?op=get&search=0x%s\">%s</a> %s %s\r\n"
#define HKP_INDEX_FPR_LINE      "     Key fingerprint = %s\r\n"
#define HKP_INDEX_UID_LINE      "                               %s\r\n"
#define HKP_INDEX_SIG_LINE      "sig        <a href=\"/pks/lookup?op=get&search=0x%s\">%s</a>             %s\r\n"

#define HKP_GET_PREFIX          "<title>Public Key Server -- Get ``%s''</title><p>\r\n" \
                                "<h1>Public Key Server -- Get ``%s''</h1><p>\r\n" \
                                "<pre>\r\n"

#define HKP_GET_SUFFIX          "\r\n" \
                                "</pre>"

#define HKP_ADD_RESPONSE        "<title>Public Key Server -- Error</title><p>\r\n" \
                                "<h1>Public Key Server -- Error</h1><p>\r\n" \
                                "Adding of keys not allowed"

#define HKP_NOTFOUND_RESPONSE   "<HEAD><TITLE>404 Not Found</TITLE></HEAD><BODY>unknown uri in pks request</BODY>\r\n"

/* Our seahorse HKP server */
static SoupServer *soup_server = NULL;
static gpgme_ctx_t gpgme_ctx = NULL;

/**
* str: the string to return parts of
* len: the length of the returned part
*
* returns the last "len" bytes of "str"
*
**/
static const gchar*
last_x (const gchar *str, guint len)
{
	guint l = strlen (str);
	if (l > len)
		return str + (l - len);
	return str;
}

/**
* str: the str to replace characters in
*
* replaces critical characters '<>&"'
*
* returns a gchar with the escaped characters
**/
static gchar*
escape_html (const gchar *str)
{
	GString *html;
	size_t l;

	html = g_string_sized_new (strlen (str));

	while (*str) {
		l = strcspn (str, "<>&\"");
		g_string_append_len (html, str, l);

		str += l;
		switch(str[0]) {
		case '<':
			g_string_append (html, "&lt;");
			break;
		case '>':
			g_string_append (html, "&gt;");
			break;
		case '&':
			g_string_append (html, "&amp;");
			break;
		case '\"':
			g_string_append (html, "&quot;");
			break;
		};

		if(str[0])
			str++;
	}

	return g_string_free (html, FALSE);
}

/**
* key: the gpgme key
*
* formats a fingerprint by adding a space every 4 characters
*
* returns a gchar* containing the formated fingerprint
**/
static gchar*
format_key_fingerprint (gpgme_key_t key)
{
	GString *string;
	const gchar *raw;
	guint len, i;

	g_return_val_if_fail (key->subkeys != NULL, NULL);
	g_return_val_if_fail (key->subkeys->fpr != NULL, NULL);

	raw = key->subkeys->fpr;
	len = strlen (raw);
	string = g_string_new ("");

	for (i = 0; i < len; i++) {
		if (i > 0 && i % 4 == 0)
			g_string_append (string, " ");
		g_string_append_c (string, raw[i]);
	}

	return g_string_free (string, FALSE);
}

/**
* name: optional. The user name
* keyid: the keyid
* email: optional. The user's email
*
* uses name, email and keyid to created formated output
*
* returns the gchar * containing the formated key data
**/
static gchar*
format_key_uid (const gchar *name, const gchar *keyid, const gchar *email)
{
	if (name && !name[0])
		name = NULL;
	if (email && !email[0])
		email = NULL;
	if (name && email)
		return g_strdup_printf (HKP_INDEX_UIDEMAIL, name, last_x (keyid, 8), email);
	if (name)
		return g_strdup_printf (HKP_INDEX_UIDNAME, name);
	return g_strdup("");
}

/**
* key: the gpgme key
*
* returns the algorithm letter depending on the pubkey of "key"
**/
static const gchar*
get_key_algo_letter (gpgme_key_t key)
{
	g_return_val_if_fail (key->subkeys != NULL, "?");

	switch(key->subkeys->pubkey_algo) {
	case GPGME_PK_RSA:
	case GPGME_PK_RSA_E:
	case GPGME_PK_RSA_S:
		return "R";
	case GPGME_PK_ELG:
	case GPGME_PK_ELG_E:
		return "E";
	case GPGME_PK_DSA:
		return "D";
	default:
		return "?";
	}
}


/**
* response: the first part of the response. Will be extended
* key: the gpgme key to add
* verbose: TRUE if signatures should be added
* fingerprints: TRUE if fingerprints should be added
*
* Adds key-info lines to the reponse
*
**/
static void
append_key_info (GString *response, gpgme_key_t key, gboolean verbose,
                 gboolean fingerprints)
{
	gboolean first = TRUE;
	gpgme_user_id_t uid;
	gpgme_key_sig_t sig;
	gchar *uidpart;
	struct tm dtm;
	time_t dt;
	gchar *t;

	g_return_if_fail (key->subkeys != NULL);
	g_return_if_fail (key->uids != NULL);

	for(uid = key->uids; uid != NULL; ) {

		/* Prepare the UID. REVOKED is treated as UID. Wierd */
		if (key->revoked)
			uidpart = g_strdup (HKP_INDEX_REVOKED);
		else
			uidpart = format_key_uid (uid->name, key->subkeys->fpr, uid->email);

		/* The first line */
		if(first) {

			/* Dates need to be formatted a specific way */
			dt = key->subkeys->timestamp;
			gmtime_r (&dt, &dtm);
			t = g_new0 (gchar, 16);
			strftime (t, 16, "%Y/%m/%d", &dtm);

			g_string_append_printf (response, HKP_INDEX_KEY_LINE,
			                        key->subkeys->length,          /* bits */
			                        get_key_algo_letter (key),     /* algorithm */
			                        last_x (key->subkeys->fpr, 8), /* fingerprint link */
			                        last_x (key->subkeys->fpr, 8), /* fingerprint */
			                        t,                             /* Creation date */
			                        uidpart);                      /* UID */

			g_free (t);

			/* Add a fingerprint line if necessary */
			if (fingerprints) {
				t = format_key_fingerprint (key);
				g_string_append_printf (response, HKP_INDEX_FPR_LINE, t);
				g_free (t);
			}
		} else {
			g_string_append_printf (response, HKP_INDEX_UID_LINE, uidpart);
		}

		g_free (uidpart);

		if (!first || !key->revoked) {

			/* Add signatures if necessary */
			if (verbose) {
				for (sig = uid->signatures; sig; sig = sig->next) {
					t = format_key_uid (sig->name, sig->keyid, sig->email);
					g_string_append_printf (response, HKP_INDEX_SIG_LINE,
					                        last_x (sig->keyid, 8),
					                        last_x (sig->keyid, 8), t);
					g_free (t);
				}
			}

			/* Next UID */
			uid = uid->next;
		}

		first = FALSE;
	}
}

/**
* msg: a soup message
* details: The details to add to the server response
* gerr: used to print a warning
*
* Adds an error resonse body to the message
*
* returns SOUP_STATUS_OK
**/
static guint
lookup_handle_error (SoupMessage *msg, const gchar *details, gpgme_error_t gerr)
{
	gchar *t = g_strdup_printf (HKP_ERROR_RESPONSE, details);
	soup_message_set_response (msg, "text/html", SOUP_MEMORY_TAKE, t, strlen(t));

	if (gerr)
		g_warning ("HKP Server GPG error: %s", gpgme_strerror (gerr));

	// Yes, that's right. HKP returns 200 even when errors occur
	return SOUP_STATUS_OK;
}

/**
* msg: the message to add to
* args: the arguments to parse
* verbose: TRUE to get more information (vindex)
*
* Parses the arguments and creates a hkp index response
*
*
* returns SOUP_STATUS_OK on success
**/
static guint
lookup_handle_index (SoupMessage *msg, GHashTable *args, gboolean verbose)
{
	gpgme_keylist_mode_t mode = GPGME_KEYLIST_MODE_LOCAL;
	gboolean fingerprints = FALSE;
	gboolean found = FALSE;
	gpgme_error_t gerr;
	gpgme_key_t key;
	GString *response;
	const gchar *search;
	gchar *t, *resp;

	if (verbose)
		mode |= GPGME_KEYLIST_MODE_SIGS;
	gerr = gpgme_set_keylist_mode (gpgme_ctx, mode);

	/* Get the various necessary arguments */
	t = (gchar*)g_hash_table_lookup(args, "fingerprint");
	if (t && g_ascii_strcasecmp(t, "on") == 0)
		fingerprints = TRUE;

	search = (const gchar*)g_hash_table_lookup(args, "search");
	if (!search || !search[0])
		return lookup_handle_error (msg, "pks request did not include a <b>search</b> property", 0);

	response = g_string_sized_new (512);

	/* List keys and append to output */
	gerr = gpgme_op_keylist_start (gpgme_ctx, search, 0);
	if (gerr)
		return lookup_handle_error (msg, "Error retrieving key(s)", gerr);

	while (gpgme_op_keylist_next (gpgme_ctx, &key) == 0) {

		if (!found) {
			/* The header */
			t = escape_html (search);
			g_string_printf (response, verbose ? HKP_VINDEX_PREFIX : HKP_INDEX_PREFIX, t, t);
			g_free (t);
		}

		found = TRUE;
		append_key_info (response, key, verbose, fingerprints);
		gpgme_key_release (key);
	}

	gpgme_op_keylist_end (gpgme_ctx);

	if (found) {
		g_string_append (response, HKP_INDEX_SUFFIX);

	} else {
		/* Not finding any keys is an error condition */
		g_string_free (response, TRUE);
		return lookup_handle_error (msg, "No matching keys in database", 0);
	}

	resp = g_string_free (response, FALSE);
	soup_message_set_response (msg, "text/html", SOUP_MEMORY_TAKE,
	                           resp, strlen (resp));
	return SOUP_STATUS_OK;
}

/**
* msg: the message to add data to
* args: the arguments to parse
*
* Adds the requested key to the message
*
* returns SOUP_STATUS_OK on success
**/
static guint
lookup_handle_get (SoupMessage *msg, GHashTable *args)
{
	gpgme_error_t gerr;
	gpgme_data_t data;
	const gchar *search;
	gchar *key, *t;
	GString *response;
	size_t len;

	search = (const gchar*)g_hash_table_lookup (args, "search");
	if(!search || !search[0])
		return lookup_handle_error (msg, "pks request did not include a <b>search</b> property", 0);

	/* Retrieve the key */
	gerr = gpgme_data_new (&data);
	if (gerr)
		return lookup_handle_error (msg, "Internal error", gerr);

	gpgme_set_armor (gpgme_ctx, 1);
	gerr = gpgme_op_export (gpgme_ctx, search, 0, data);
	if(gerr)
		return lookup_handle_error (msg, "Error retrieving key(s)", gerr);

	key = gpgme_data_release_and_get_mem (data, &len);
	if(!key || len == 0) {
		g_free (key);
		return lookup_handle_error (msg, "No matching key in database", 0);
	}

	/* And return our response */
	t = escape_html (search);
	response = g_string_sized_new (len + 128);
	g_string_printf (response, HKP_GET_PREFIX, t, t);
	g_string_append_len (response, key, len);
	g_string_append (response, HKP_GET_SUFFIX);
	g_free (t);
	g_free (key);

	t = g_string_free (response, FALSE);
	soup_message_set_response (msg, "text/html", SOUP_MEMORY_TAKE,
	                           t, strlen(t));
	return SOUP_STATUS_OK;
}

/**
* server: SoupServer
* msg: Soup message
* path: part of the request
* args: part of the request
* context: additional client information
* data: User data
*
* Handles index and get requests
*
**/
static void
lookup_callback (SoupServer *server, SoupMessage *msg,
                 const gchar *path, GHashTable *args,
                 SoupClientContext *context, gpointer data)
{
	const gchar *t;
	guint code = SOUP_STATUS_INTERNAL_SERVER_ERROR;

	soup_message_headers_append (msg->response_headers, "Connection", "close");
	if(msg->method != SOUP_METHOD_GET) {
		soup_message_set_status (msg, SOUP_STATUS_METHOD_NOT_ALLOWED);
		return;
	}

	/* Parse the arguments */
	if (!args || !g_hash_table_size (args)) {
		code = lookup_handle_error (msg, "pks request had no query string", 0);
		soup_message_set_status (msg, SOUP_STATUS_METHOD_NOT_ALLOWED);
		return;
	}

	/* Figure out the operation */
	t = (const gchar*)g_hash_table_lookup (args, "op");
	if(!t || !t[0])
		code = lookup_handle_error(msg, "pks request did not include an <b>op</b> property", 0);

	else if(g_ascii_strcasecmp(t, "index") == 0)
		code = lookup_handle_index(msg, args, FALSE);

	else if(g_ascii_strcasecmp(t, "vindex") == 0)
		code = lookup_handle_index(msg, args, TRUE);

	else if(g_ascii_strcasecmp(t, "get") == 0)
		code = lookup_handle_get(msg, args);

	else
		code = lookup_handle_error(msg, "pks request had an invalid <b>op</b> property", 0);

	soup_message_set_status (msg, code);
}

/**
* server: SoupServer
* msg: Soup message
* path: part of the request
* args: part of the request
* context: additional client information
* data: User data
*
* Sends and error (adding keys is not allowed)
*
**/
static void
add_callback (SoupServer *server, SoupMessage *msg,
              const gchar *path, GHashTable *args,
              SoupClientContext *context, gpointer data)
{
	    soup_message_set_response (msg, "text/html", SOUP_MEMORY_STATIC,
	                               HKP_ADD_RESPONSE, strlen(HKP_ADD_RESPONSE));
	    soup_message_set_status (msg, SOUP_STATUS_METHOD_NOT_ALLOWED);
	    soup_message_headers_append (msg->response_headers, "Connection", "close");
}

/**
* server: SoupServer
* msg: Soup message
* path: part of the request
* args: part of the request
* context: additional client information
* data: User data
*
* Sends the default error message
*
**/
static void
default_callback (SoupServer *server, SoupMessage *msg,
                  const char *path, GHashTable *args,
                  SoupClientContext *context, gpointer data)
{
	soup_message_set_response (msg, "text/html", SOUP_MEMORY_STATIC,
	                           HKP_NOTFOUND_RESPONSE, strlen(HKP_NOTFOUND_RESPONSE));
	soup_message_set_status (msg, SOUP_STATUS_NOT_FOUND);
	soup_message_headers_append (msg->response_headers, "Connection", "close");
}

/**
 * seahorse_hkp_server_error_domain:
 *
 *
 * Returns: the error domain for the hkp server
 */
GQuark
seahorse_hkp_server_error_domain ()
{
	static GQuark q = 0;
	if(q == 0)
		q = g_quark_from_static_string ("seahorse-gpgme-error");
	return q;
}

/**
 * seahorse_hkp_server_start:
 * @err: the error to return
 *
 * Starts an async soup server and adds callbacks
 *
 * Returns: FALSE on error
 */
gboolean
seahorse_hkp_server_start(GError **err)
{
	guint port = HKP_FIXED_PORT ? HKP_FIXED_PORT : SOUP_ADDRESS_ANY_PORT;

	g_assert (!err || !*err);

	if (HKP_FIXED_PORT > 0)
		port = HKP_FIXED_PORT;

	/* Initialize GPGME context */
	if (gpgme_ctx == NULL) {
		gpgme_protocol_t proto = GPGME_PROTOCOL_OpenPGP;
		gpgme_error_t gerr;

		if (!gpgme_check_version (NULL))
			g_return_val_if_reached (FALSE);

		gerr = gpgme_engine_check_version (proto);
		g_return_val_if_fail (gerr == 0, FALSE);

		gerr = gpgme_new (&gpgme_ctx);
		if (gerr != 0) {
			g_warning ("couldn't initialize gpgme: %s", gpgme_strerror (gerr));
			return FALSE;
		}

		gerr = gpgme_set_protocol (gpgme_ctx, proto);
		g_return_val_if_fail (gerr == 0, FALSE);
	}

	/* Now start the HTTP server */
	g_return_val_if_fail (soup_server == NULL, FALSE);

	soup_server = soup_server_new (SOUP_SERVER_PORT, port, NULL);
	if (!soup_server) {
		g_set_error (err, HKP_SERVER_ERROR, errno, "%s", g_strerror (errno));
		return FALSE;
	}

	soup_server_add_handler (soup_server, "/pks/lookup", lookup_callback, NULL, NULL);
	soup_server_add_handler (soup_server, "/pks/add", add_callback, NULL, NULL);
	soup_server_add_handler (soup_server, NULL, default_callback, NULL, NULL);

	soup_server_run_async (soup_server);

	return TRUE;
}

/**
 * seahorse_hkp_server_stop:
 *
 * Stops the hkp server
 *
 */
void
seahorse_hkp_server_stop()
{
	if (soup_server) {
		g_object_unref (soup_server);
		soup_server = NULL;
	}

	if (gpgme_ctx) {
		gpgme_release (gpgme_ctx);
		gpgme_ctx = NULL;
	}
}

/**
 * seahorse_hkp_server_is_running:
 *
 *
 * Returns: TRUE if the hkp server is running
 */
gboolean
seahorse_hkp_server_is_running()
{
	return soup_server != NULL;
}

/**
 * seahorse_hkp_server_get_port:
 *
 *
 * Returns: The port of the hkp server (0 on error)
 */
guint
seahorse_hkp_server_get_port()
{
	g_return_val_if_fail (soup_server != NULL, 0);
	return soup_server_get_port (soup_server);
}
