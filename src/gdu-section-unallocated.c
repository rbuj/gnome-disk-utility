/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/* gdu-section-unallocated.c
 *
 * Copyright (C) 2007 David Zeuthen
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

#include <config.h>
#include <string.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <stdlib.h>
#include <math.h>
#include <polkit-gnome/polkit-gnome.h>

#include "gdu-pool.h"
#include "gdu-util.h"
#include "gdu-section-unallocated.h"

struct _GduSectionUnallocatedPrivate
{
        gboolean init_done;

        GtkWidget *sensitive_vbox;

        GtkWidget *size_hscale;
        GtkWidget *size_spin_button;
        GtkWidget *fstype_combo_box;
        GtkWidget *fslabel_entry;
        GtkWidget *secure_erase_combo_box;
        GtkWidget *warning_hbox;
        GtkWidget *warning_label;
        GtkWidget *encrypt_check_button;

        PolKitAction *pk_create_partition_action;
        PolKitGnomeAction *create_partition_action;
};

static GObjectClass *parent_class = NULL;

G_DEFINE_TYPE (GduSectionUnallocated, gdu_section_unallocated, GDU_TYPE_SECTION)

/* ---------------------------------------------------------------------------------------------------- */


typedef struct {
        GduSectionUnallocated *section;
        GduPresentable *presentable;
        char *encrypt_passphrase;
        gboolean save_in_keyring;
        gboolean save_in_keyring_session;
} CreatePartitionData;

static void
create_partition_data_free (CreatePartitionData *data)
{
        if (data->section != NULL)
                g_object_unref (data->section);
        if (data->presentable != NULL)
                g_object_unref (data->presentable);
        if (data->encrypt_passphrase != NULL) {
                memset (data->encrypt_passphrase, '\0', strlen (data->encrypt_passphrase));
                g_free (data->encrypt_passphrase);
        }
        g_free (data);
}

static void
create_partition_completed (GduDevice  *device,
                            const char *created_device_object_path,
                            GError     *error,
                            gpointer    user_data)
{
        CreatePartitionData *data = user_data;

        if (error != NULL) {
                gdu_device_job_set_failed (device, error);
                g_error_free (error);
        } else if (data != NULL && created_device_object_path != NULL &&
                   (data->save_in_keyring || data->save_in_keyring_session)) {
                GduPool *pool;
                GduDevice *cleartext_device;
                GduDevice *crypto_device;
                const char *cleartext_objpath;

                pool = gdu_device_get_pool (device);

                cleartext_device = gdu_pool_get_by_object_path (pool,
                                                                created_device_object_path);
                if (cleartext_device != NULL) {
                        cleartext_objpath = gdu_device_crypto_cleartext_get_slave (cleartext_device);
                        if (cleartext_objpath != NULL &&
                            (crypto_device = gdu_pool_get_by_object_path (pool, cleartext_objpath)) != NULL) {

                                gdu_util_save_secret (crypto_device,
                                                      data->encrypt_passphrase,
                                                      data->save_in_keyring_session);
                                /* make sure the tab for the encrypted device is updated (it displays whether
                                 * the passphrase is in the keyring or now)
                                 */
                                gdu_shell_update (gdu_section_get_shell (GDU_SECTION (data->section)));

                                g_object_unref (crypto_device);
                        }
                        g_object_unref (cleartext_device);
                }
                g_object_unref (pool);

        }

        if (data != NULL)
                create_partition_data_free (data);
}

static void
create_partition_callback (GtkAction *action, gpointer user_data)
{
        GduSectionUnallocated *section = GDU_SECTION_UNALLOCATED (user_data);
        GduPresentable *presentable;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        GduDevice *device;
        guint64 offset;
        guint64 size;
        char *type;
        char *label;
        char **flags;
        char *fstype;
        char *fslabel;
        char *fserase;
        char *encrypt_passphrase;
        const char *scheme;
        CreatePartitionData *data;

        type = NULL;
        label = NULL;
        flags = NULL;
        device = NULL;
        fstype = NULL;
        fslabel = NULL;
        fserase = NULL;
        encrypt_passphrase = NULL;
        toplevel_presentable = NULL;
        toplevel_device = NULL;
        data = NULL;

        presentable = gdu_section_get_presentable (GDU_SECTION (section));
        g_assert (presentable != NULL);

        device = gdu_presentable_get_device (presentable);
        if (device != NULL) {
                g_warning ("%s: device is supposed to be NULL",  __FUNCTION__);
                goto out;
        }

        toplevel_presentable = gdu_util_find_toplevel_presentable (presentable);
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }
        if (!gdu_device_is_partition_table (toplevel_device)) {
                g_warning ("%s: device for toplevel presentable is not a partition table", __FUNCTION__);
                goto out;
        }

        offset = gdu_presentable_get_offset (presentable);
        size = (guint64) (((double) gtk_range_get_value (GTK_RANGE (section->priv->size_hscale))) * 1024.0 * 1024.0);
        fstype = gdu_util_fstype_combo_box_get_selected (section->priv->fstype_combo_box);
        fslabel = g_strdup (gtk_entry_get_text (GTK_ENTRY (section->priv->fslabel_entry)));
        fserase = gdu_util_secure_erase_combo_box_get_selected (section->priv->secure_erase_combo_box);

        /* TODO: set flags */
        flags = NULL;

        /* default the partition type according to the kind of file system */
        scheme = gdu_device_partition_table_get_scheme (toplevel_device);

        /* see gdu_util.c:gdu_util_fstype_combo_box_create_store() */
        if (strcmp (fstype, "msdos_extended_partition") == 0) {
                type = g_strdup ("0x05");
                g_free (fstype);
                g_free (fslabel);
                g_free (fserase);
                fstype = g_strdup ("");
                fslabel = g_strdup ("");
                fserase = g_strdup ("");
        } else {
                type = gdu_util_get_default_part_type_for_scheme_and_fstype (scheme, fstype, size);
                if (type == NULL) {
                        g_warning ("Cannot determine default part type for scheme '%s' and fstype '%s'",
                                   scheme, fstype);
                        goto out;
                }
        }

        /* set partition label to the file system label (TODO: handle max len) */
        if (strcmp (scheme, "gpt") == 0 || strcmp (scheme, "apm") == 0) {
                label = g_strdup (label);
        } else {
                label = g_strdup ("");
        }

        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (section->priv->encrypt_check_button))) {

                data = g_new (CreatePartitionData, 1);
                data->section = g_object_ref (section);
                data->presentable = g_object_ref (presentable);

                data->encrypt_passphrase = gdu_util_dialog_ask_for_new_secret (
                        gdu_shell_get_toplevel (gdu_section_get_shell (GDU_SECTION (section))),
                        &data->save_in_keyring,
                        &data->save_in_keyring_session);
                if (data->encrypt_passphrase == NULL) {
                        create_partition_data_free (data);
                        data = NULL;
                        goto out;
                }
        }

        gdu_device_op_create_partition (toplevel_device,
                                        offset,
                                        size,
                                        type,
                                        label,
                                        flags,
                                        fstype,
                                        fslabel,
                                        fserase,
                                        data != NULL ? data->encrypt_passphrase : NULL,
                                        create_partition_completed,
                                        data);

        /* go to toplevel */
        gdu_shell_select_presentable (gdu_section_get_shell (GDU_SECTION (section)), toplevel_presentable);

out:
        if (encrypt_passphrase != NULL) {
                memset (encrypt_passphrase, '\0', strlen (encrypt_passphrase));
                g_free (encrypt_passphrase);
        }
        g_free (type);
        g_free (label);
        g_strfreev (flags);
        g_free (fstype);
        g_free (fslabel);
        g_free (fserase);
        if (device != NULL)
                g_object_unref (device);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);
}

static void
create_part_fstype_combo_box_changed (GtkWidget *combo_box, gpointer user_data)
{
        GduSectionUnallocated *section = GDU_SECTION_UNALLOCATED (user_data);
        char *fstype;
        GduCreatableFilesystem *creatable_fs;
        gboolean label_entry_sensitive;
        int max_label_len;

        label_entry_sensitive = FALSE;
        max_label_len = 0;

        fstype = gdu_util_fstype_combo_box_get_selected (combo_box);
        if (fstype != NULL) {
                creatable_fs = gdu_util_find_creatable_filesystem_for_fstype (fstype);
                /* Note: there may not have a creatable file system... e.g. the user could
                 *       select "Extended" on mbr partition tables.
                 */
                if (creatable_fs != NULL) {
                        max_label_len = creatable_fs->max_label_len;
                }
        }

        if (max_label_len > 0)
                label_entry_sensitive = TRUE;

        gtk_entry_set_max_length (GTK_ENTRY (section->priv->fslabel_entry), max_label_len);
        gtk_widget_set_sensitive (section->priv->fslabel_entry, label_entry_sensitive);

        g_free (fstype);
}

static char*
create_part_size_format_value_callback (GtkScale *scale, gdouble value)
{
        return gdu_util_get_size_for_display ((guint64) value, FALSE);
}

static gboolean
has_extended_partition (GduSectionUnallocated *section, GduPresentable *presentable)
{
        GList *l;
        GList *enclosed_presentables;
        gboolean ret;
        GduPool *pool;

        ret = FALSE;

        pool = gdu_presentable_get_pool (presentable);
        enclosed_presentables = gdu_pool_get_enclosed_presentables (pool, presentable);
        g_object_unref (pool);

        for (l = enclosed_presentables; l != NULL; l = l->next) {
                GduPresentable *p = l->data;
                GduDevice *d;

                d = gdu_presentable_get_device (p);
                if (d == NULL)
                        continue;

                if (gdu_device_is_partition (d)) {
                        int partition_type;
                        partition_type = strtol (gdu_device_partition_get_type (d), NULL, 0);
                        if (partition_type == 0x05 ||
                            partition_type == 0x0f ||
                            partition_type == 0x85) {
                                ret = TRUE;
                                break;
                        }
                }

        }
        g_list_foreach (enclosed_presentables, (GFunc) g_object_unref, NULL);
        g_list_free (enclosed_presentables);
        return ret;
}


/**
 * update_warning:
 * @section: A #GduSectionUnallocated object.
 *
 * Update the warning widgets to tell the user how much MBR sucks if
 * that is what he is using.
 *
 * Returns: #TRUE if no more partitions can be created
 **/
static gboolean
update_warning (GduSectionUnallocated *section)
{
        GduPresentable *presentable;
        gboolean show_warning;
        GduPresentable *toplevel_presentable;
        GduDevice *toplevel_device;
        char *warning_markup;
        gboolean at_max_partitions;

        show_warning = FALSE;
        warning_markup = NULL;
        at_max_partitions = FALSE;

        presentable = gdu_section_get_presentable (GDU_SECTION (section));

        toplevel_presentable = gdu_util_find_toplevel_presentable (presentable);
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }

        if (gdu_device_is_partition_table (toplevel_device)) {
                const char *scheme;

                scheme = gdu_device_partition_table_get_scheme (toplevel_device);

                if (scheme != NULL &&
                    strcmp (scheme, "mbr") == 0 &&
                    !has_extended_partition (section, toplevel_presentable)) {
                        int num_partitions;

                        num_partitions = gdu_device_partition_table_get_count (toplevel_device);

                        if (num_partitions == 3) {
                                show_warning = TRUE;
                                warning_markup = g_strdup (
                                        _("<small><b>This is the last primary partition that can be created. "
                                          "If you need more partitions, you can create an Extended "
                                          "Partition.</b></small>"));
                        } else if (num_partitions == 4) {
                                show_warning = TRUE;
                                at_max_partitions = TRUE;
                                warning_markup = g_strdup (
                                        _("<small><b>No more partitions can be created. You may want to delete "
                                          "an existing partition and then create an Extended Partition.</b></small>"));
                        }
                }
        }

        if (show_warning) {
                gtk_widget_show (section->priv->warning_hbox);
                gtk_label_set_markup (GTK_LABEL (section->priv->warning_label), warning_markup);
        } else {
                gtk_widget_hide (section->priv->warning_hbox);
        }

out:
        g_free (warning_markup);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);

        return at_max_partitions;
}

static void
update (GduSectionUnallocated *section)
{
        GduPresentable *presentable;
        GduDevice *device;
        guint64 size;
        GduDevice *toplevel_device;
        GduPresentable *toplevel_presentable;
        const char *scheme;
        gboolean at_max_partitions;

        toplevel_presentable = NULL;
        toplevel_device = NULL;

        presentable = gdu_section_get_presentable (GDU_SECTION (section));
        device = gdu_presentable_get_device (presentable);

        toplevel_presentable = gdu_util_find_toplevel_presentable (presentable);
        toplevel_device = gdu_presentable_get_device (toplevel_presentable);
        if (toplevel_device == NULL) {
                g_warning ("%s: no device for toplevel presentable",  __FUNCTION__);
                goto out;
        }

        if (device != NULL) {
                g_warning ("%s: device is not NULL for presentable",  __FUNCTION__);
                goto out;
        }


        scheme = gdu_device_partition_table_get_scheme (toplevel_device);

        at_max_partitions = update_warning (section);
        gtk_widget_set_sensitive (section->priv->sensitive_vbox, !at_max_partitions);

        gtk_widget_set_sensitive (GTK_WIDGET (section), !gdu_device_is_read_only (toplevel_device));

        size = gdu_presentable_get_size (presentable);

        if (!section->priv->init_done) {
                section->priv->init_done = TRUE;

                gtk_range_set_range (GTK_RANGE (section->priv->size_hscale), 0, size / 1024.0 / 1024.0);
                gtk_range_set_value (GTK_RANGE (section->priv->size_hscale), size / 1024.0 / 1024.0);

                gtk_spin_button_set_range (GTK_SPIN_BUTTON (section->priv->size_spin_button), 0, size / 1024.0 / 1024.0);
                gtk_spin_button_set_value (GTK_SPIN_BUTTON (section->priv->size_spin_button), size / 1024.0 / 1024.0);

                gtk_combo_box_set_active (GTK_COMBO_BOX (section->priv->secure_erase_combo_box), 0);
                gtk_combo_box_set_active (GTK_COMBO_BOX (section->priv->fstype_combo_box), 0);
                gtk_entry_set_text (GTK_ENTRY (section->priv->fslabel_entry), "");
                gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (section->priv->encrypt_check_button), FALSE);

                /* only allow creation of extended partitions if there currently are none */
                if (has_extended_partition (section, toplevel_presentable)) {
                        gdu_util_fstype_combo_box_rebuild (section->priv->fstype_combo_box, NULL);
                } else {
                        gdu_util_fstype_combo_box_rebuild (section->priv->fstype_combo_box, scheme);
                }
        }

out:
        if (device != NULL)
                g_object_unref (device);
        if (toplevel_presentable != NULL)
                g_object_unref (toplevel_presentable);
        if (toplevel_device != NULL)
                g_object_unref (toplevel_device);
}

static gboolean
size_hscale_change_value_callback (GtkRange     *range,
                                   GtkScrollType scroll,
                                   gdouble       value,
                                   gpointer      user_data)
{
        GduSectionUnallocated *section = GDU_SECTION_UNALLOCATED (user_data);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON (section->priv->size_spin_button), value);
        return FALSE;
}

static void
size_spin_button_value_changed_callback (GtkSpinButton *spin_button,
                                         gpointer       user_data)
{
        GduSectionUnallocated *section = GDU_SECTION_UNALLOCATED (user_data);
        double value;

        value = gtk_spin_button_get_value (spin_button);
        gtk_range_set_value (GTK_RANGE (section->priv->size_hscale), value);
}

/* ---------------------------------------------------------------------------------------------------- */

static void
gdu_section_unallocated_finalize (GduSectionUnallocated *section)
{
        polkit_action_unref (section->priv->pk_create_partition_action);
        g_object_unref (section->priv->create_partition_action);

        if (G_OBJECT_CLASS (parent_class)->finalize)
                (* G_OBJECT_CLASS (parent_class)->finalize) (G_OBJECT (section));
}

static void
gdu_section_unallocated_class_init (GduSectionUnallocatedClass *klass)
{
        GObjectClass *obj_class = (GObjectClass *) klass;
        GduSectionClass *section_class = (GduSectionClass *) klass;

        parent_class = g_type_class_peek_parent (klass);

        obj_class->finalize = (GObjectFinalizeFunc) gdu_section_unallocated_finalize;
        section_class->update = (gpointer) update;
}

static void
gdu_section_unallocated_init (GduSectionUnallocated *section)
{
        GtkWidget *hbox;
        GtkWidget *vbox;
        GtkWidget *vbox2;
        GtkWidget *vbox3;
        GtkWidget *button;
        GtkWidget *label;
        GtkWidget *align;
        GtkWidget *table;
        GtkWidget *entry;
        GtkWidget *combo_box;
        GtkWidget *hscale;
        GtkWidget *spin_button;
        GtkWidget *button_box;
        GtkWidget *image;
        GtkWidget *check_button;
        int row;

        section->priv = g_new0 (GduSectionUnallocatedPrivate, 1);

        section->priv->pk_create_partition_action = polkit_action_new ();
        polkit_action_set_action_id (section->priv->pk_create_partition_action, "org.freedesktop.devicekit.disks.erase");
        section->priv->create_partition_action = polkit_gnome_action_new_default (
                "create-partition",
                section->priv->pk_create_partition_action,
                _("C_reate"),
                _("Create"));
        g_object_set (section->priv->create_partition_action,
                      "auth-label", _("C_reate..."),
                      "yes-icon-name", GTK_STOCK_ADD,
                      "no-icon-name", GTK_STOCK_ADD,
                      "auth-icon-name", GTK_STOCK_ADD,
                      "self-blocked-icon-name", GTK_STOCK_ADD,
                      NULL);
        g_signal_connect (section->priv->create_partition_action, "activate",
                          G_CALLBACK (create_partition_callback), section);

        vbox = gtk_vbox_new (FALSE, 10);

        /* ---------------- */
        /* Create partition */
        /* ---------------- */

        vbox3 = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), vbox3, FALSE, TRUE, 0);

        section->priv->sensitive_vbox = gtk_vbox_new (FALSE, 5);
        gtk_box_pack_start (GTK_BOX (vbox3), section->priv->sensitive_vbox, FALSE, TRUE, 0);

        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("<b>Create Partition</b>"));
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (section->priv->sensitive_vbox), label, FALSE, FALSE, 0);
        vbox2 = gtk_vbox_new (FALSE, 5);
        align = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
        gtk_alignment_set_padding (GTK_ALIGNMENT (align), 0, 0, 24, 0);
        gtk_container_add (GTK_CONTAINER (align), vbox2);
        gtk_box_pack_start (GTK_BOX (section->priv->sensitive_vbox), align, FALSE, TRUE, 0);

        /* explanatory text */
        label = gtk_label_new (NULL);
        gtk_label_set_markup (GTK_LABEL (label), _("To create a new partition, select the size and whether to create "
                                                   "a file system. The partition type, label and flags can be changed "
                                                   "after creation."));
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_box_pack_start (GTK_BOX (vbox2), label, FALSE, TRUE, 0);

        table = gtk_table_new (4, 2, FALSE);
        gtk_box_pack_start (GTK_BOX (vbox2), table, FALSE, FALSE, 0);

        row = 0;

        /* create size */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Size:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        hbox = gtk_hbox_new (FALSE, 5);

        hscale = gtk_hscale_new_with_range (0, 10000, 1000);
        gtk_scale_set_draw_value (GTK_SCALE (hscale), FALSE);
        //g_signal_connect (hscale, "format-value", (GCallback) create_part_size_format_value_callback, section);
        g_signal_connect (hscale, "change-value", (GCallback) size_hscale_change_value_callback, section);
        gtk_box_pack_start (GTK_BOX (hbox), hscale, TRUE, TRUE, 0);
        section->priv->size_hscale = hscale;

        spin_button = gtk_spin_button_new_with_range (0, 10000, 1);
        g_signal_connect (spin_button, "value-changed", (GCallback) size_spin_button_value_changed_callback, section);
        gtk_box_pack_start (GTK_BOX (hbox), spin_button, FALSE, TRUE, 0);
        section->priv->size_spin_button = spin_button;
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), spin_button);

        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_label_set_markup (GTK_LABEL (label), _("MB"));
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);

        gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        row++;

        /* secure erase */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("Er_ase:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        combo_box = gdu_util_secure_erase_combo_box_create ();
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        section->priv->secure_erase_combo_box = combo_box;

        row++;

        /* secure erase desc */
        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 40);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gdu_util_secure_erase_combo_box_set_desc_label (combo_box, label);

        row++;

        /* _file system_ label */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Label:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        entry = gtk_entry_new ();
        gtk_table_attach (GTK_TABLE (table), entry, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
        section->priv->fslabel_entry = entry;

        row++;

        /* _file system_ type */
        label = gtk_label_new (NULL);
        gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
        gtk_label_set_markup_with_mnemonic (GTK_LABEL (label), _("_Type:"));
        gtk_table_attach (GTK_TABLE (table), label, 0, 1, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);

        combo_box = gdu_util_fstype_combo_box_create (NULL);
        gtk_table_attach (GTK_TABLE (table), combo_box, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo_box);
        section->priv->fstype_combo_box = combo_box;

        row++;

        /* fstype desc */
        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_label_set_width_chars (GTK_LABEL (label), 40);
        gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
        gtk_table_attach (GTK_TABLE (table), label, 1, 2, row, row + 1,
                          GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        gdu_util_fstype_combo_box_set_desc_label (combo_box, label);

        row++;

        /* whether to encrypt underlying device */
        check_button = gtk_check_button_new_with_mnemonic (_("E_ncrypt underlying device"));
        gtk_widget_set_tooltip_text (check_button,
                                     _("Encryption protects your data, requiring a "
                                       "passphrase to be enterered before the file system can be "
                                       "used. May decrease performance and may not be compatible if "
                                       "you use the media on other operating systems."));
        if (gdu_util_can_create_encrypted_device ()) {
                gtk_table_attach (GTK_TABLE (table), check_button, 1, 2, row, row + 1,
                                  GTK_FILL, GTK_EXPAND | GTK_FILL, 2, 2);
        }
        section->priv->encrypt_check_button = check_button;

        row++;

        /* create button */
        button_box = gtk_hbutton_box_new ();
        gtk_button_box_set_layout (GTK_BUTTON_BOX (button_box), GTK_BUTTONBOX_START);
        gtk_box_set_spacing (GTK_BOX (button_box), 6);
        gtk_box_pack_start (GTK_BOX (vbox2), button_box, TRUE, TRUE, 0);
        button = polkit_gnome_action_create_button (section->priv->create_partition_action);
        gtk_container_add (GTK_CONTAINER (button_box), button);

        /* update sensivity and length of fs label and ensure it's set initially */
        g_signal_connect (section->priv->fstype_combo_box, "changed",
                          G_CALLBACK (create_part_fstype_combo_box_changed), section);
        create_part_fstype_combo_box_changed (section->priv->fstype_combo_box, section);

        /* Warning used for
         * - telling the user he won't be able to add more partitions if he adds this one
         * - telling the user that we can't add any partitions because the four primary ones
         *   are used already and there is no extended partition
         */
        hbox = gtk_hbox_new (FALSE, 5);
        image = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_BUTTON);
        label = gtk_label_new (NULL);
        gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, TRUE, 0);
        gtk_box_pack_start (GTK_BOX (vbox3), hbox, TRUE, TRUE, 10);
        section->priv->warning_hbox = hbox;
        section->priv->warning_label = label;

        gtk_box_pack_start (GTK_BOX (section), vbox, TRUE, TRUE, 0);
}

GtkWidget *
gdu_section_unallocated_new (GduShell       *shell,
                             GduPresentable *presentable)
{
        return GTK_WIDGET (g_object_new (GDU_TYPE_SECTION_UNALLOCATED,
                                         "shell", shell,
                                         "presentable", presentable,
                                         NULL));
}
