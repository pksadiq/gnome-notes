/* gn-tag-store.c
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

#define G_LOG_DOMAIN "gn-tag-store"

#include "config.h"

#include <gtk/gtk.h>

#include "gn-tag-store.h"
#include "gn-trace.h"

/**
 * SECTION: gn-tag-store
 * @title: GnTagStore
 * @short_description:
 * @include: "gn-tag-store.h"
 */

struct _GnTagStore
{
  GListStore *store;
};

/* Tag */
struct _GnTag
{
  GObject parent_instance;

  gchar       *name;
  const gchar *intern_name; /* A casefold intern string */
  GdkRGBA     *rgba;
};

G_DEFINE_TYPE (GnTag, gn_tag, G_TYPE_OBJECT)


static gboolean
gn_tag_store_find (GnTagStore  *self,
                   const gchar *name, /* interned */
                   guint       *position)
{
  gpointer object;
  guint i = 0;

  g_assert (self != NULL);

  while ((object = g_list_model_get_item (G_LIST_MODEL (self->store), i)))
    {
      if (name == GN_TAG (object)->intern_name)
        {
          *position = i;
          g_object_unref (object);

          return TRUE;
        }
      g_object_unref (object);
      i++;
    }

  return FALSE;
}

/**
 * gn_tag_store_new:
 *
 * Create an empty store of tags/labels.
 *
 * Returns: (transfer full): a new #GnTagStore.
 * Free with gn_tag_store_free().
 */
GnTagStore *
gn_tag_store_new (void)
{
  GnTagStore *self;

  self = g_slice_new (GnTagStore);
  self->store = g_list_store_new (GN_TYPE_TAG);

  return self;
}

/**
 * gn_tag_store_free:
 * @self: A #GnListStore
 *
 * Free the store @self and all containing tags.
 */
void
gn_tag_store_free (GnTagStore *self)
{
  g_return_if_fail (self != NULL);

  g_object_unref (self->store);
  g_slice_free (GnTagStore, self);
}

/**
 * gn_tag_store_get_model:
 * @self: A #GnListStore
 *
 * The associated tag store of @self.
 *
 * Returns: (transfer none): A #GListModel
 */
GListModel *
gn_tag_store_get_model (GnTagStore *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return G_LIST_MODEL (self->store);
}

/**
 * gn_tag_store_insert:
 * @self: A #GnListStore
 * @name: A non-empty string
 * @rgba: (nullable): A #GdkRGBA
 *
 * Insert A tag with name @name and color @rgba.
 *
 * Returns: (transfer none): The @GnTag with name @name.
 */
GnTag *
gn_tag_store_insert (GnTagStore  *self,
                     const gchar *name,
                     GdkRGBA     *rgba)
{
  GnTag *tag;
  g_autofree gchar *casefold = NULL;
  const gchar *str_intern;
  guint position;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (*name != '\0', NULL);

  casefold = g_utf8_casefold (name, -1);
  str_intern = g_intern_string (casefold);

  if (gn_tag_store_find (self, str_intern, &position))
    return g_list_model_get_item (G_LIST_MODEL (self->store), position);

  tag = g_object_new (GN_TYPE_TAG, NULL);
  tag->name = g_strdup (name);
  tag->intern_name = str_intern;

  if (rgba)
    tag->rgba = gdk_rgba_copy (rgba);

  g_list_store_append (self->store, tag);
  g_object_unref (tag);

  return tag;
}

/* Tag */
static void
gn_tag_finalize (GObject *object)
{
  GnTag *tag = (GnTag *)object;

  gdk_rgba_free (tag->rgba);
  g_free (tag->name);

  G_OBJECT_CLASS (gn_tag_parent_class)->finalize (object);
}

static void
gn_tag_class_init (GnTagClass *tag_klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (tag_klass);

  object_class->finalize = gn_tag_finalize;
}

static void
gn_tag_init (GnTag *tag)
{
}

const gchar *
gn_tag_get_name (GnTag *tag)
{
  g_return_val_if_fail (GN_IS_TAG (tag), NULL);

  return tag->name;
}

gboolean
gn_tag_get_rgba (GnTag   *tag,
                 GdkRGBA *rgba)
{
  g_return_val_if_fail (GN_IS_TAG (tag), FALSE);

  if (tag->rgba == NULL)
    return FALSE;

  if (rgba != NULL)
    *rgba = *tag->rgba;

  return TRUE;
}

gint
gn_tag_compare (gconstpointer a,
                gconstpointer b,
                gpointer      user_data)
{
  GnTag *tag_a = (GnTag *)a;
  GnTag *tag_b = (GnTag *)b;

  return strcmp (tag_a->intern_name, tag_b->intern_name);
}
