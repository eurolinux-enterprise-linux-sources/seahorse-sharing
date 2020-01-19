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

#include <config.h>

#include <glib/gi18n.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-glib/glib-watch.h>
#include <avahi-glib/glib-malloc.h>

#include "config.h"
#include "seahorse-daemon.h"

#include <gtk/gtk.h>

#define HKP_SERVICE_TYPE "_pgpkey-hkp._tcp."

static gboolean    start_publishing        (void);

/**
 * SECTION:seahorse-sharing
 * @short_description: Starts the HKP service and offers it using Avahi
 *
 **/

/* TODO: Need to be able to advertize in real DNS domains */

static void start_sharing ();
static void stop_sharing ();

/* Helpers ------------------------------------------------------------------ */

static gchar*
string_up_first (const gchar *orig)
{
	gchar *t, *t2, *ret;
	if (g_utf8_validate (orig, -1, NULL)) {
		t = g_utf8_find_next_char (orig, NULL);
		if (t != NULL) {
			t2 = g_utf8_strup (orig, t - orig);
			ret = g_strdup_printf ("%s%s", t2, t);
			g_free (t2);

			/* Can't find first UTF8 char */
		} else {
			ret = g_strdup (orig);
		}

	/* Just use ASCII functions when not UTF8 */
	} else {
		ret = g_strdup (orig);
		ret[0] = g_ascii_toupper (ret[0]);
	}

	return ret;
}

static void
show_error (GtkWidget *parent, const gchar *heading, const gchar *message)
{
	GtkWidget *dialog;

	g_return_if_fail (message || heading);
	if (!message)
		message = "";

	if (parent) {
		if (!GTK_IS_WIDGET (parent)) {
			g_warn_if_reached ();
			parent = NULL;
		} else {
			if (!GTK_IS_WINDOW (parent))
				parent = gtk_widget_get_toplevel (parent);
			if (!GTK_IS_WINDOW (parent) && gtk_widget_is_toplevel (parent))
				parent = NULL;
		}
	}

	dialog = gtk_message_dialog_new (GTK_WINDOW (parent),
	                                 GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
	                                 GTK_BUTTONS_CLOSE, NULL);
	if (heading)
		g_object_set (G_OBJECT (dialog),
		              "text", heading,
		              "secondary-text", message,
		              NULL);
	else
		g_object_set (G_OBJECT (dialog),
		              "text", message,
		              NULL);

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static void
handle_error (GError* err, const char* desc, ...)
{
	gchar *t = NULL;
	va_list ap;

	if(!err)
		return;

	va_start(ap, desc);
	if (desc)
		t = g_strdup_vprintf (desc, ap);
	va_end(ap);

	show_error (NULL, t, err->message ? err->message : "");
	g_free(t);
}

/* DNS-SD publishing -------------------------------------------------------- */

static AvahiEntryGroup *avahi_group = NULL;
static AvahiClient *avahi_client = NULL;
static AvahiGLibPoll *avahi_poll = NULL;

/* Name and 'alternate' integer (for collisions). */
static gchar *share_name = NULL;
static int share_alternate = 0;

/**
* errmsg: if TRUE an error message will be shown
*
* Stops the Avahi service
*
**/
static void
stop_publishing (gboolean errmsg)
{
	if (share_name)
		g_free (share_name);
	share_name = NULL;

	if (avahi_group)
		avahi_entry_group_free (avahi_group);
	avahi_group = NULL;

	if (avahi_client)
		avahi_client_free (avahi_client);
	avahi_client = NULL;

	if (errmsg) {
		show_error (NULL, _("Couldn't share keys"),
		            _("Can't publish discovery information on the network."));
	}
}

/**
*
* Calcs a share name and stores it in share_name
*
* TODO: The share name must be < 63 characters. Or avahi_entry_group_add_service
* will fail. (happens only in EXTREME circumstances or never at all)
**/
static void
calc_share_name ()
{
	const gchar *user_name;
	gchar *t = NULL;

	user_name = g_get_real_name ();
	if (!user_name || g_str_equal (user_name, "Unknown"))
		user_name = t = string_up_first (g_get_user_name ());

	/*
	 * Translators: The %s will get filled in with the user name
	 * of the user, to form a genitive. If this is difficult to
	 * translate correctly so that it will work correctly in your
	 * language, you may use something equivalent to
	 * "Shared keys of %s".
	 */
	share_name = g_strdup_printf (_("%s's encryption keys"), user_name);
	g_free (t);

	if (share_alternate) {
		t = g_strdup_printf ("%s #%d", share_name, share_alternate);
		g_free (share_name);
		share_name = t;
	}
}

/**
*
* Adds the HKP service to the avahi_group the port advertised is the one of
* the hkp server
*
**/
static void
add_service ()
{
	int r;

	r = avahi_entry_group_add_service (avahi_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
	                                   0, share_name, HKP_SERVICE_TYPE, NULL, NULL,
	                                   seahorse_hkp_server_get_port (), NULL);
	if (r >= 0)
		r = avahi_entry_group_commit (avahi_group);

	if (r < 0) {
		g_warning ("failed to register _pgpkey-hkp._tcp service: %s", avahi_strerror (r));
		stop_publishing (TRUE);
		return;
	}
}

/**
* group: the group
* state: the new state the group is in
* userdata: ignored
*
* Callback called whenever the state of the group changes
*
* Acts on collisions and failure
**/
static void
services_callback(AvahiEntryGroup *group, AvahiEntryGroupState state,
                  AVAHI_GCC_UNUSED void *userdata)
{
	g_assert (!avahi_group || group == avahi_group);

	switch (state) {
	case AVAHI_ENTRY_GROUP_COLLISION:
		/* Someone else has our registered name */
		share_alternate++;
		calc_share_name ();
		g_warning ("naming collision trying new name: %s", share_name);
		add_service ();
		break;

	case AVAHI_ENTRY_GROUP_FAILURE:
		g_warning ("avahi entry group failure: %s",
		           avahi_strerror (avahi_client_errno (avahi_client)));
		stop_publishing (TRUE);
		break;

	default:
		break;
	};
}

/**
* client: the Avahi client
* state: the state in which the client is now
* userdata: ignored
*
* Callback called whenever the state of the Avahi client changes
*
* Adds a group on startup and a service, acts on collisions and failures
**/
static void
client_callback (AvahiClient *client, AvahiClientState state,
                 AVAHI_GCC_UNUSED void * userdata)
{
	gboolean errmsg;

	g_assert (!avahi_client || client == avahi_client);

	switch (state) {
	case AVAHI_CLIENT_S_RUNNING:
		/* Create a new entry group if necessary */
		if (!avahi_group) {
			avahi_group = avahi_entry_group_new (client, services_callback, NULL);
			if (!avahi_group) {
				g_warning ("couldn't create avahi services group: %s",
				           avahi_strerror (avahi_client_errno (client)));
				stop_publishing (TRUE);
				return;
			}
		}

		/* Add add the good stuff */
		add_service ();
		break;

	case AVAHI_CLIENT_S_COLLISION:
		/* Drop our published services */
		if (avahi_group)
			avahi_entry_group_reset (avahi_group);
		break;

	case AVAHI_CLIENT_FAILURE:
		errmsg = (avahi_client_errno (client) != AVAHI_ERR_DISCONNECTED);
		if (errmsg)
			g_warning ("failure talking with avahi: %s",
			           avahi_strerror (avahi_client_errno (client)));
		stop_publishing (errmsg);

		/* Try to restart */
		sleep (1);
		start_publishing ();
		break;

	default:
		break;
	};
}

static void
free_avahi ()
{
	if (avahi_poll)
		avahi_glib_poll_free (avahi_poll);
	avahi_poll = NULL;
}

const AvahiPoll*
dns_sd_get_poll ()
{
	if (!avahi_poll) {
		avahi_set_allocator (avahi_glib_allocator ());
		avahi_poll = avahi_glib_poll_new (NULL, G_PRIORITY_DEFAULT);
		if (!avahi_poll) {
			g_warning ("couldn't initialize avahi glib poll integration");
			return NULL;
		}

		g_atexit (free_avahi);
	}
	return avahi_glib_poll_get (avahi_poll);
}

/**
*
* Calcs a new share name and starts a new Avahi client
*
* Returns FALSE on error
**/
static gboolean
start_publishing ()
{
	int aerr;

	/* The share name is important */
	share_alternate = 0;
	calc_share_name ();

	avahi_client = avahi_client_new (dns_sd_get_poll (), 0, client_callback, NULL, &aerr);
	if (!avahi_client)
		return FALSE;

	return TRUE;
}

/* -------------------------------------------------------------------------- */

/**
*
* starts the hkp server and the Avahi client
*
**/
static void
start_sharing ()
{
	GError *err = NULL;

	if (!seahorse_hkp_server_is_running ()) {

		if (!seahorse_hkp_server_start (&err)) {
			handle_error (err, _("Couldn't share keys"));
			g_clear_error (&err);
			return;
		}

		if (!start_publishing ()) {
			seahorse_hkp_server_stop ();

			stop_publishing (TRUE);
			return;
		}
	}
}

/**
*
* Stops the hkp server and the Avahi publishing of the service
*
**/
static void
stop_sharing ()
{
	stop_publishing (FALSE);

	if (seahorse_hkp_server_is_running ())
		seahorse_hkp_server_stop ();
}

/**
 * seahorse_sharing_init:
 *
 * Starts Avahi depending on the GConf settings. Also adds a callback to listen
 * for GConf changes. Also starts the HKP server.
 *
 */
void
seahorse_sharing_init ()
{
	start_sharing();
}

/**
 * seahorse_sharing_cleanup:
 *
 * Removes the gconf callback and stops Avahi
 *
 */
void
seahorse_sharing_cleanup ()
{
	stop_sharing ();
}
