/* gn-memo-provider.c
 *
 * Copyright 2018 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define G_LOG_DOMAIN "gn-memo-provider"

#include "config.h"

#include "gn-item.h"
#include "gn-plain-note.h"
#include "gn-memo-provider.h"
#include "gn-trace.h"

/**
 * SECTION: gn-memo-provider
 * @title: GnMemoProvider
 * @short_description: Evolution memo provider
 * @include: "gn-memo-provider.h"
 */

/*
 * Evolution memos are usually saved as iCalendar VJOURNAL.
 * See https://tools.ietf.org/html/rfc5545
 *
 * The SUMMARY field of VJOURNAL is considered as note title
 * and the DESCRIPTION field as the note content.  Currently
 * we concatinate multiple DESCRIPTION fields into one (if
 * there is more than one) and show.
 */

struct _GnMemoProvider
{
  GnProvider parent_instance;

  gchar *uid;
  gchar *name;
  gchar *icon;

  ESource *source;
  ECalClient *client;
  ECalClientView *client_view;

  GList *notes;
};

G_DEFINE_TYPE (GnMemoProvider, gn_memo_provider, GN_TYPE_PROVIDER)

static void
gn_memo_provider_finalize (GObject *object)
{
  GnMemoProvider *self = (GnMemoProvider *)object;

  GN_ENTRY;

  g_clear_object (&self->client_view);
  g_clear_object (&self->client);
  g_clear_object (&self->source);

  g_clear_pointer (&self->uid, g_free);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (gn_memo_provider_parent_class)->finalize (object);

  GN_EXIT;
}

static gchar *
gn_memo_provider_get_uid (GnProvider *provider)
{
  return g_strdup (GN_MEMO_PROVIDER (provider)->uid);
}

static gchar *
gn_memo_provider_get_name (GnProvider *provider)
{
  return g_strdup (GN_MEMO_PROVIDER (provider)->name);
}

static GList *
gn_memo_provider_get_notes (GnProvider *provider)
{
  return GN_MEMO_PROVIDER (provider)->notes;
}

static void
gn_memo_provider_objects_added_cb (ECalClientView *client_view,
                                   const GSList   *objects,
                                   gpointer        user_data)
{
  GnMemoProvider *self = user_data;

  GN_ENTRY;

  g_assert (E_IS_CAL_CLIENT_VIEW (client_view));
  g_assert (GN_IS_MEMO_PROVIDER (self));

  for (GSList *node = (GSList *)objects; node != NULL; node = node->next)
    {
      g_autoptr (ECalComponent) component = NULL;
      g_autofree gchar *content = NULL;
      ECalComponentText component_text;
      icalcomponent *icomponent;
      GnNote *note;
      GSList *text_list;
      const gchar *uid;

      icomponent = icalcomponent_new_clone (node->data);
      component = e_cal_component_new_from_icalcomponent (icomponent);

      e_cal_component_get_uid (component, &uid);
      e_cal_component_get_summary (component, &component_text);
      e_cal_component_get_description_list (component, &text_list);

      for (GSList *node = text_list; node != NULL; node = node->next)
        {
          ECalComponentText *text = node->data;
          g_autofree gchar *old_content = content;

          if (content != NULL)
            content = g_strconcat (old_content, "\n", text->value, NULL);
          else
            content = g_strdup (text->value);
        }

      e_cal_component_free_text_list (text_list);

      note = g_object_new (GN_TYPE_PLAIN_NOTE,
                           "uid", uid,
                           "title", component_text.value,
                           NULL);
      gn_note_set_text_content (note, content);
      g_object_set_data (G_OBJECT (note), "provider", GN_PROVIDER (self));
      g_object_set_data_full (G_OBJECT (note), "component",
                              g_object_ref (component),
                              g_object_unref);

      self->notes = g_list_prepend (self->notes, note);
    }

  GN_EXIT;
}

static void
gn_memo_provider_loaded_cb (ECalClientView *client_view,
                            const GError   *error,
                            gpointer        user_data)
{
  g_autoptr(GTask) task = user_data;

  g_assert (E_IS_CAL_CLIENT_VIEW (client_view));
  g_assert (G_IS_TASK (task));

  if (error)
    g_task_return_error (task, g_error_copy (error));
  else
    g_task_return_boolean (task, TRUE);
}

static void
gn_memo_provider_view_ready_cb (GObject      *object,
                                GAsyncResult *result,
                                gpointer      user_data)
{
  ECalClient *client = (ECalClient *)object;
  GnMemoProvider *self;
  g_autoptr(GTask) task = user_data;
  g_autoptr (GError) error = NULL;

  GN_ENTRY;

  g_assert (E_IS_CAL_CLIENT (client));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  g_assert (GN_IS_MEMO_PROVIDER (self));

  if (!e_cal_client_get_view_finish (client, result,
                                     &self->client_view,
                                     &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      GN_EXIT;
    }

  g_signal_connect_object (self->client_view, "objects-added",
                           G_CALLBACK (gn_memo_provider_objects_added_cb),
                           self, G_CONNECT_AFTER);
  g_signal_connect_object (self->client_view, "complete",
                           G_CALLBACK (gn_memo_provider_loaded_cb),
                           g_object_ref (task), G_CONNECT_AFTER);


  e_cal_client_view_start (self->client_view, &error);

  if (error)
    {
      g_task_return_error (task, g_steal_pointer (&error));
      g_object_unref (task);
    }

  GN_EXIT;
}

static void
gn_memo_provider_load_items_async (GnProvider          *provider,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  GnMemoProvider *self = (GnMemoProvider *)provider;
  g_autoptr(GTask) task = NULL;

  g_assert (GN_IS_MEMO_PROVIDER (self));
  g_assert (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, gn_memo_provider_load_items_async);

  e_cal_client_get_view (self->client,
                         "#t",
                         cancellable,
                         gn_memo_provider_view_ready_cb,
                         g_steal_pointer (&task));
}

static void
gn_memo_provider_class_init (GnMemoProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GnProviderClass *provider_class = GN_PROVIDER_CLASS (klass);

  object_class->finalize = gn_memo_provider_finalize;

  provider_class->get_uid = gn_memo_provider_get_uid;
  provider_class->get_name = gn_memo_provider_get_name;
  provider_class->get_notes = gn_memo_provider_get_notes;

  provider_class->load_items_async = gn_memo_provider_load_items_async;
}

static void
gn_memo_provider_init (GnMemoProvider *self)
{
}

GnMemoProvider *
gn_memo_provider_new (ESource    *source,
                      ECalClient *client)
{
  GnMemoProvider *self;

  g_return_val_if_fail (E_IS_SOURCE (source), NULL);
  g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);

  self = g_object_new (GN_TYPE_MEMO_PROVIDER, NULL);
  self->source = g_object_ref (source);
  self->client = g_object_ref (client);
  self->uid = g_strdup (e_source_get_uid (source));
  self->name = g_strconcat ("Memo: ",
                            e_source_get_display_name (source),
                            NULL);

  return self;
}