/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-confirmation-dialog.h
 *
 * Copyright (C) 2009 David Zeuthen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#if !defined (__GDU_GTK_INSIDE_GDU_GTK_H) && !defined (GDU_GTK_COMPILATION)
#confirmation "Only <gdu-gtk/gdu-gtk.h> can be included directly, this file may disappear or change contents."
#endif

#ifndef __GDU_CONFIRMATION_DIALOG_H
#define __GDU_CONFIRMATION_DIALOG_H

#include <gdu-gtk/gdu-gtk-types.h>
#include <gdu-gtk/gdu-dialog.h>

G_BEGIN_DECLS

#define GDU_TYPE_CONFIRMATION_DIALOG            gdu_confirmation_dialog_get_type()
#define GDU_CONFIRMATION_DIALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDU_TYPE_CONFIRMATION_DIALOG, GduConfirmationDialog))
#define GDU_CONFIRMATION_DIALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GDU_TYPE_CONFIRMATION_DIALOG, GduConfirmationDialogClass))
#define GDU_IS_CONFIRMATION_DIALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDU_TYPE_CONFIRMATION_DIALOG))
#define GDU_IS_CONFIRMATION_DIALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDU_TYPE_CONFIRMATION_DIALOG))
#define GDU_CONFIRMATION_DIALOG_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GDU_TYPE_CONFIRMATION_DIALOG, GduConfirmationDialogClass))

typedef struct GduConfirmationDialogClass   GduConfirmationDialogClass;
typedef struct GduConfirmationDialogPrivate GduConfirmationDialogPrivate;

struct GduConfirmationDialog
{
        GduDialog parent;

        /*< private >*/
        GduConfirmationDialogPrivate *priv;
};

struct GduConfirmationDialogClass
{
        GduDialogClass parent_class;
};

GType       gdu_confirmation_dialog_get_type       (void) G_GNUC_CONST;
GtkWidget  *gdu_confirmation_dialog_new            (GtkWindow      *parent,
                                                    GduPresentable *presentable,
                                                    const gchar    *message,
                                                    const gchar    *button_text);
GtkWidget  *gdu_confirmation_dialog_new_for_drive  (GtkWindow      *parent,
                                                    GduDevice      *device,
                                                    const gchar    *message,
                                                    const gchar    *button_text);
GtkWidget  *gdu_confirmation_dialog_new_for_volume (GtkWindow      *parent,
                                                    GduDevice      *device,
                                                    const gchar    *message,
                                                    const gchar    *button_text);

G_END_DECLS

#endif /* __GDU_CONFIRMATION_DIALOG_H */
