/*  This file is part of mrim-prpl.
 *
 *  mrim-prpl is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  mrim-prpl is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with mrim-prpl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mrim.h"
#include "package.h"
#include "statuses.h"
#include "cl.h"
#include "util.h"
#include "message.h"

/* Contact list */

void mrim_cl_skip(MrimPackage *pack, gchar *mask) {
	while (*mask) {
		switch (*mask) {
			case 's':
				g_free(mrim_package_read_LPSA(pack));
				break;
			case 'z':
			case 'u':
				mrim_package_read_UL(pack);
				break;
		}
		mask++;
	}
}

static MrimBuddy *mrim_cl_load_buddy(MrimData *mrim, MrimPackage *pack, gchar *mask) {
	MrimBuddy *mb	= g_new0(MrimBuddy, 1);
	mb->mrim		= mrim;
	mb->flags		= mrim_package_read_UL(pack);
	mb->group_id		= mrim_package_read_UL(pack);
	mb->email		= mrim_package_read_LPSA(pack);
	mb->alias		= mrim_package_read_LPSW(pack);
	mb->s_flags		= mrim_package_read_UL(pack);
	mb->phones		= g_new0(gchar*, 4);
	{
		guint32 status_id = mrim_package_read_UL(pack);
		{
			gchar *phones = mrim_package_read_LPSA(pack);
			if (phones) {
				gchar **phones_splitted = g_strsplit(phones, ",", 3);
				guint i = 0;
				while (phones_splitted[i]) {
					gchar *phone;
					if ((!phones_splitted[i][0]) || (phones_splitted[i][0] == '+')) {
						phone = g_strdup(phones_splitted[i]);
					} else {
						phone = g_strdup_printf("+%s", phones_splitted[i]);
					}
					mb->phones[i] = phone;
					i++;
				}
				g_strfreev(phones_splitted);
			}
		}
		{
			gchar *status_uri = mrim_package_read_LPSA(pack);
			gchar *tmp = mrim_package_read_LPSW(pack);
			gchar *status_title = purple_markup_escape_text(tmp, -1);
			g_free(tmp);
			tmp = mrim_package_read_LPSW(pack);
			gchar *status_desc = purple_markup_escape_text(tmp, -1);
			g_free(tmp);
			mb->status = make_mrim_status(status_id, status_uri, status_title, status_desc);
		}
	}
	mb->com_support = mrim_package_read_UL(pack);
	mb->user_agent = mrim_package_read_LPSA(pack);
	mrim_package_read_UL(pack);
	mrim_package_read_UL(pack);
	mrim_package_read_UL(pack);
	{
		gchar *tmp = mrim_package_read_LPSW(pack);
		mb->microblog = purple_markup_escape_text(tmp, -1);
		g_free(tmp);
	}
	mrim_cl_skip(pack, mask + 16);
	mb->authorized = !(mb->s_flags & CONTACT_INTFLAG_NOT_AUTHORIZED);
	return mb;
}

void mrim_avatar_cb(PurpleUtilFetchUrlData *url_data, gpointer user_data, const gchar *url_text, gsize len, const gchar *error_message) {
	purple_debug_info("mrim-prpl", "[%s]\n", __func__);
	PurpleBuddy* buddy = user_data;
	if(url_text && len) {
		purple_buddy_icons_set_for_user(purple_buddy_get_account(buddy), purple_buddy_get_name(buddy), g_memdup(url_text, len), len, NULL);
	}
}

void mrim_fetch_avatar(PurpleBuddy *buddy) {
	g_return_if_fail(buddy != NULL);
	g_return_if_fail(buddy->name != NULL);
	g_return_if_fail(is_myworld_able(buddy->name) == TRUE);
	purple_debug_info("mrim-prpl", "[%s] Fetch avatar for buddy '%s'\n", __func__, buddy->name);
	//if (!is_valid_email(buddy->name)) return;
	if ((!buddy->icon) && buddy->name) {
		gchar** split_1 = g_strsplit(buddy->name,"@",2);
		gchar* email_name=split_1[0];
		gchar* domain;
		gchar** split_2;
		if (split_1[1]) {
			split_2 = g_strsplit(split_1[1],".ru\0",2);
			domain = split_2[0];
			if (g_strcmp0(domain, "corp.mail") == 0) {
				domain = g_strdup("corp");
			}
		} else {
			g_strfreev(split_1);
			return;
		}
		gchar* url = g_strdup_printf("http://obraz.foto.mail.ru/%s/%s/_mrimavatar", domain, email_name);
		g_strfreev(split_2);
		g_strfreev(split_1);
		purple_util_fetch_url(url, TRUE, NULL, TRUE, mrim_avatar_cb, buddy);
		g_free(url);
	}
}

void mrim_cl_load(MrimPackage *pack, MrimData *mrim) {
	guint32 group_count = mrim_package_read_UL(pack);
	gchar *group_mask = mrim_package_read_LPSA(pack);
	gchar *buddy_mask = mrim_package_read_LPSA(pack);
	/* GROUPS */
	purple_debug_info("mrim-prpl", "[%s] Group count = %i, group mask = '%s', contact mask = '%s'\n", __func__, group_count, group_mask, buddy_mask);
	{
		guint32 i;
		for (i = 0; i < group_count; i++) {
			guint32 flags = mrim_package_read_UL(pack);
			gchar *name = mrim_package_read_LPSW(pack);
			purple_debug_info("mrim-prpl", "[%s] New group: name = '%s', flags = 0x%x\n",  __func__, name, flags);
			new_mrim_group(mrim, i, name, flags);
			mrim_cl_skip(pack, group_mask + 2);
			g_free(name);
		}
	}
	g_free(group_mask);
	{
		guint32 id = 20;
		while (pack->cur < pack->data_size) {
			MrimBuddy *mb = mrim_cl_load_buddy(mrim, pack, buddy_mask);
			if (!mb)
				break;

			mb->id = id++;

			if (mb->flags & CONTACT_FLAG_REMOVED) {
				purple_debug_info("mrim-prpl", "[%s] Buddy '%s' removed\n", __func__, mb->email);
				free_mrim_buddy(mb);
				continue;
			}
			/* CHATS */
			if (mb->flags & CONTACT_FLAG_MULTICHAT) {
				PurpleGroup *group = get_mrim_group(mrim, mb->group_id)->group;
				PurpleChat *pc = NULL;
				PurpleChat *old_pc = purple_blist_find_chat(mrim->account, mb->email);
				if (old_pc) {
					pc = old_pc;
					purple_debug_info("mrim-prpl", "[%s] update chat: %s \n", __func__, mb->email);
				} else {
					purple_debug_info("mrim-prpl", "[%s] New chat: %s \n", __func__, mb->email);
					GHashTable *defaults = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
					g_hash_table_insert(defaults, "room", g_strdup(mb->email));
					pc = purple_chat_new(mrim->account, mb->email, defaults);

					purple_blist_add_chat(pc, group, NULL);
				}
				purple_blist_alias_chat(pc, mb->alias);
			} else {
				/* BUDDIES */
				purple_debug_info("mrim-prpl", "[%s] New buddy: email = '%s', nick = '%s', flags = 0x%x, status = '%s', UA = '%s', microblog = '%s'\n",
						__func__, mb->email, mb->alias, mb->flags, mb->status->purple_id, mb->user_agent, mb->microblog);
				PurpleGroup *group = get_mrim_group(mrim, mb->group_id)->group;
				PurpleBuddy *buddy = purple_find_buddy(mrim->account, mb->email);
				if (buddy) {
					purple_blist_alias_buddy(buddy, mb->alias);
				} else {
					buddy = purple_buddy_new(mrim->account, mb->email, mb->alias);
					purple_blist_add_buddy(buddy, NULL, group, NULL);
				}
				purple_buddy_set_protocol_data(buddy, mb);
				mb->buddy = buddy;
				update_buddy_status(buddy);
				if (purple_account_get_bool(mrim->gc->account, "fetch_avatars", TRUE)) {
					if (!(mb->flags & CONTACT_FLAG_PHONE)) {
						mrim_fetch_avatar(buddy);
					}
				}
			}
		} /* while */
	}
	g_free(buddy_mask);
	/* Purge all obsolete buddies. */
	{
		GSList *buddies = purple_find_buddies(mrim->gc->account, NULL);
		GSList *first = buddies;
		while (buddies) {
			PurpleBuddy *buddy = (PurpleBuddy*)buddies->data;
			if (buddy) {
				if (!(buddy->proto_data)) {
					purple_blist_remove_buddy(buddy);
				}
			}
			buddies = g_slist_next(buddies);
		}
		g_slist_free(first);
	}
	/* TODO: Purge all obsolete chats. */
	purple_blist_show();
}

/* Groups */

void mrim_modify_group_ack(MrimData *mrim, gpointer user_data, MrimPackage *pack) {
	guint32 status = mrim_package_read_UL(pack);
	purple_debug_info("mrim-prpl", "[%s] Status is %i\n", __func__, status);
	g_return_if_fail(status == CONTACT_OPER_SUCCESS);
}

void mrim_rename_group(PurpleConnection *gc, const char *old_name, PurpleGroup *group, GList *moved_buddies) {
	MrimData *mrim = gc->proto_data;
	g_return_if_fail(mrim != NULL);
	MrimGroup *gr = get_mrim_group_by_name(mrim, group->name);
	g_free(gr->name);
	gr->name = g_strdup(group->name);
	MrimPackage *pack = mrim_package_new(mrim->seq++, MRIM_CS_MODIFY_CONTACT);
	mrim_package_add_UL(pack, gr->id);
	mrim_package_add_UL(pack, gr->flags);
	mrim_package_add_UL(pack, 0);
	mrim_package_add_LPSA(pack, NULL);
	mrim_package_add_LPSW(pack, gr->name);
	mrim_package_add_LPSA(pack, NULL);
	mrim_add_ack_cb(mrim, pack->header->seq, mrim_modify_group_ack, NULL);
	mrim_package_send(pack, mrim);
}

void mrim_remove_group(PurpleConnection *gc, PurpleGroup *group) {
	MrimData *mrim = gc->proto_data;
	g_return_if_fail(mrim != NULL);
	MrimGroup *gr = get_mrim_group_by_name(mrim, group->name);
	g_return_if_fail(gr != NULL);
	MrimPackage *pack = mrim_package_new(mrim->seq++, MRIM_CS_MODIFY_CONTACT);
	mrim_package_add_UL(pack, gr->id);
	mrim_package_add_UL(pack, gr->flags & CONTACT_FLAG_REMOVED);
	mrim_package_add_UL(pack, 0);
	mrim_package_add_LPSA(pack, NULL);
	mrim_package_add_LPSW(pack, gr->name);
	mrim_package_add_LPSA(pack, NULL);
	mrim_add_ack_cb(mrim, pack->header->seq, mrim_modify_group_ack, NULL);
	mrim_package_send(pack, mrim);
}

MrimGroup *new_mrim_group(MrimData *mrim, guint32 id, gchar *name, guint32 flags) {
	MrimGroup *group = g_new0(MrimGroup, 1);
	group->id = id;
	group->name = g_strdup(name);
	group->flags = flags;
	group->group = purple_find_group(name);
	if (!group->group) {
		group->group = purple_group_new(name);
		purple_blist_add_group(group->group, NULL);
	}
	g_hash_table_insert(mrim->groups, GUINT_TO_POINTER(id), group);
	return group;
}

void free_mrim_group(MrimGroup *group) {
	if (group) {
		if (group->name) {
			g_free(group->name);
		}
		g_free(group);
	}
}

MrimGroup *get_mrim_group(MrimData *mrim, guint32 id) {
	MrimGroup *group =  g_hash_table_lookup(mrim->groups, GUINT_TO_POINTER(id));
	g_return_val_if_fail(group != NULL, g_hash_table_lookup(mrim->groups, GUINT_TO_POINTER(0)));
	return group;
}

MrimGroup *get_mrim_group_by_name(MrimData *mrim, gchar *name) {
	if (!name) {
		purple_debug_info("mrim-prpl", "[%s] name = NULL!\n", __func__);
		return NULL;
	} else {
		purple_debug_info("mrim-prpl", "[%s] name = '%s'\n", __func__, name);
	};
	GList *g = g_list_first(g_hash_table_get_values(mrim->groups));
	MrimGroup *group;
	while (g) {
		group = g->data;
		if (!group) {
			purple_debug_info("mrim-prpl", "[%s] g->data FAIL!\n", __func__);
		} else if (!group->name) {
			purple_debug_info("mrim-prpl", "[%s] NONAME group (id, flags) = (%u,%u)\n", __func__, group->id, group->flags);
		} else {
			purple_debug_info("mrim-prpl", "[%s] group info: (id, flags) = (%u,%u)\n", __func__, group->id, group->flags);
			purple_debug_info("mrim-prpl", "[%s] group->name = '%s'\n", __func__, group->name);
			if (g_strcmp0(group->name, name) == 0) {
				g_list_free(g);
				return group;
			};
		};
		g = g_list_next(g);
	};
	g_list_free(g);
	return NULL;
}

void mrim_add_group_ack(MrimData *mrim, gpointer user_data, MrimPackage *pack) {
	guint32 status = mrim_package_read_UL(pack);
	purple_debug_info("mrim-prpl", "[%s] Status = %i\n", __func__, status);
	g_return_if_fail(status == CONTACT_OPER_SUCCESS);
	guint32 id = mrim_package_read_UL(pack);
	AddContactInfo *info = user_data;
	new_mrim_group(mrim, id, info->group->name, 0);
	if (info->buddy) {
		if (info->move) {
			mrim_add_buddy(mrim->gc, info->buddy, info->group);
		} else {
			mrim_move_buddy(mrim->gc, info->buddy->name, NULL, info->group->name);
		}
	}
}

void cl_add_group(MrimData *mrim, gchar *name, AddContactInfo *info) {
	purple_debug_info("mrim-prpl", "[%s] Add group with name '%s'\n", __func__, name);
	guint32 groups_count = g_hash_table_size(mrim->groups);
	MrimPackage *pack = mrim_package_new(mrim->seq++, MRIM_CS_ADD_CONTACT);
	mrim_package_add_UL(pack, CONTACT_FLAG_GROUP | (groups_count << 24));
	mrim_package_add_UL(pack, 0);
	mrim_package_add_LPSA(pack, NULL);
	mrim_package_add_LPSW(pack, name);
	mrim_package_add_LPSA(pack, NULL);
	mrim_package_add_UL(pack, 0);
	mrim_package_add_UL(pack, 0);
	if (!info) {
		info = g_new0(AddContactInfo, 1);
		info->group = purple_find_group(name);
	}
	mrim_add_ack_cb(mrim, pack->header->seq, mrim_add_group_ack, info);
	mrim_package_send(pack, mrim);
}

/* Buddies */

void mrim_add_contact_ack(MrimData *mrim, gpointer user_data, MrimPackage *pack) {
	guint32 status = mrim_package_read_UL(pack);
	purple_debug_info("mrim-prpl", "[%s] Status is %i\n", __func__, status);
	g_return_if_fail(status == CONTACT_OPER_SUCCESS);
	guint32 id = mrim_package_read_UL(pack);
	BuddyAddInfo *info = user_data;
	PurpleBuddy *buddy = info->buddy;
	MrimBuddy *mb = buddy->proto_data;
	mb->id = id;
}

void mrim_add_buddy(PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group) {
	purple_debug_info("mrim-prpl", "[%s]\n", __func__);
	g_return_if_fail(buddy != NULL);
	g_return_if_fail(group != NULL);
	g_return_if_fail(gc != NULL);
	g_return_if_fail(gc->state == PURPLE_CONNECTED);
	purple_debug_info("mrim-prpl", "[%s] Add buddy '%s' to group '%s'\n", __func__, buddy->name, group->name);
	{
		const gchar *normalized_name = mrim_normalize(gc->account, (const gchar*)buddy->name);
		g_free(buddy->name);
		buddy->name = (gchar*)normalized_name;
	}
	PurpleBuddy *old_buddy = purple_find_buddy(gc->account, buddy->name);
	MrimData *mrim = gc->proto_data;
	MrimBuddy *mb;
	if (old_buddy != NULL  && old_buddy != buddy) {
		purple_blist_remove_buddy(buddy);
		buddy = old_buddy;
		mb = (MrimBuddy*)(buddy->proto_data);
		if (mb) {
			mb->buddy = buddy;
			purple_blist_alias_buddy(buddy, mb->alias);
			update_buddy_status(buddy);
		}
	} else if (is_valid_email(buddy->name) || is_valid_phone(buddy->name)) {
		purple_debug_info("mrim-prpl", "[%s] Buddy has a valid email or phone '%s'\n", __func__, buddy->name);
		MrimGroup *gr = get_mrim_group_by_name(mrim, group->name);
		gint group_id = gr ? gr->id : -1;
		if (group_id == -1) {
			purple_debug_info("mrim-prpl", "[%s] Group '%s' not exists - creating\n", __func__, group->name);
			AddContactInfo *info = g_new(AddContactInfo, 1);
			info->buddy = buddy;
			info->group = group;
			info->move = FALSE;
			cl_add_group(mrim, group->name, info);
		} else {
			mb = g_new0(MrimBuddy, 1);
			mb->email = g_strdup(buddy->name);
			mb->alias = g_strdup(buddy->alias ? buddy->alias : buddy->name);
			buddy->proto_data = mb;
			mb->group_id = group_id;
			mb->phones = g_new0(gchar*, 4);
			if (is_valid_phone(buddy->name)) {
				mb->flags |= CONTACT_FLAG_PHONE;
				mb->authorized = TRUE;
				mb->status = make_mrim_status(STATUS_ONLINE, NULL, NULL, NULL);
			} else {
				mb->authorized = FALSE;
				mb->status = make_mrim_status(STATUS_OFFLINE, NULL, NULL, NULL);
			}
			purple_debug_info("mrim-prpl", "[%s] Adding buddy with email = '%s' alias = '%s', flags = 0x%x\n", __func__,
				mb->email, mb->alias, mb->flags);
			MrimPackage *pack = mrim_package_new(mrim->seq++, MRIM_CS_ADD_CONTACT);
			mrim_package_add_UL(pack, mb->flags);
			mrim_package_add_UL(pack, mb->group_id);
			mrim_package_add_LPSA(pack, mb->email);
			mrim_package_add_LPSW(pack, mb->alias);
			{
				gchar *str = g_strjoinv(",", mb->phones);
				mrim_package_add_LPSA(pack, str);
				g_free(str);
			}
			mrim_package_add_LPSA(pack, " ");
			mrim_package_add_UL(pack, 0);
			{
				BuddyAddInfo *info = g_new(BuddyAddInfo, 1);
				info->buddy = buddy;
				mrim_add_ack_cb(mrim, pack->header->seq, mrim_add_contact_ack, info);
			}
			mrim_package_send(pack, mrim);
			if (!(mb->flags & CONTACT_FLAG_PHONE)) {
				mrim_fetch_avatar(buddy);
			}
		}
	} else {
		purple_debug_info("mrim-prpl", "[%s] '%s' is not valid email or phone number!\n", __func__, buddy->name);
		gchar *msg = g_strdup_printf(_("Unable to add the buddy \"%s\" because the username is invalid.  Usernames must be a valid email address(in mail.ru bk.ru list.ru corp.mail.ru inbox.ru domains), valid ICQ UIN in NNNN@uin.icq format or valid phone number (start with + and contain only numbers, spaces and \'-\'."), buddy->name);
		purple_notify_error(gc, NULL, _("Unable to Add"), msg);
		g_free(msg);
		purple_blist_remove_buddy(buddy);
	}
	purple_blist_show();
}

void mrim_modify_buddy_ack(MrimData *mrim, gpointer user_data, MrimPackage *pack) {
	guint32 status = mrim_package_read_UL(pack);
	purple_debug_info("mrim-prpl", "[%s] Status is %i\n", __func__, status);
	g_return_if_fail(status == CONTACT_OPER_SUCCESS);
}

void mrim_modify_buddy(MrimData *mrim, PurpleBuddy *buddy) {
	MrimBuddy *mb = buddy->proto_data;
	MrimPackage *pack = mrim_package_new(mrim->seq++, MRIM_CS_MODIFY_CONTACT);
	mrim_package_add_UL(pack, mb->id);
	mrim_package_add_UL(pack, mb->flags);
	mrim_package_add_UL(pack, mb->group_id);
	mrim_package_add_LPSA(pack, mb->flags & CONTACT_FLAG_PHONE ? "phone" : mb->email);
	mrim_package_add_LPSW(pack, mb->alias);
	{
		gchar *str = g_strjoinv(",", mb->phones);
		mrim_package_add_LPSA(pack, str);
		g_free(str);
	}
	mrim_add_ack_cb(mrim, pack->header->seq, mrim_modify_buddy_ack, NULL);
	mrim_package_send(pack, mrim);
}

void mrim_remove_buddy(PurpleConnection *gc, PurpleBuddy *buddy, PurpleGroup *group) {
	MrimData *mrim = gc->proto_data;
	MrimBuddy *mb = buddy->proto_data;
	purple_debug_info("mrim-prpl", "[%s] Removing buddy '%s' from buddy list\n", __func__, buddy->name);
	mb->flags |= CONTACT_FLAG_REMOVED;
	mrim_modify_buddy(mrim, buddy);
}

void free_mrim_buddy(MrimBuddy *mb) {
	if (mb) {
		g_free(mb->email);
		g_free(mb->alias);
		g_strfreev(mb->phones);
		g_free(mb->user_agent);
		g_free(mb->microblog);
		free_mrim_status(mb->status);
		g_free(mb);
	}
}

void mrim_free_buddy(PurpleBuddy *buddy) {
	if (buddy->proto_data) {
		MrimBuddy *mb = buddy->proto_data;
		free_mrim_buddy(mb);
	}
}

void mrim_alias_buddy(PurpleConnection *gc, const char *who, const char *alias) {
	PurpleBuddy *buddy = purple_find_buddy(gc->account, (gchar*)who);
	g_return_if_fail(buddy != NULL);
	MrimData *mrim = gc->proto_data;
	MrimBuddy *mb = buddy->proto_data;
	g_return_if_fail(mb != NULL);
	g_free(mb->alias);
	mb->alias = g_strdup(alias);
	mrim_modify_buddy(mrim, buddy);
}

void mrim_move_buddy(PurpleConnection *gc, const char *who, const char *old_group, const char *new_group) {
	purple_debug_info("mrim-prpl", "[%s] Moving '%s' to group '%s'\n", __func__, who, new_group);
	PurpleBuddy *buddy = purple_find_buddy(gc->account, (gchar*)who);
	g_return_if_fail(buddy != NULL);
	MrimData *mrim = gc->proto_data;
	MrimBuddy *mb = buddy->proto_data;
	g_return_if_fail(mb != NULL);
	MrimGroup *group_found = get_mrim_group_by_name(mrim, (gchar*)new_group);
	gint group_id = group_found ? group_found->id : -1;
	if (!group_found) {
		purple_debug_info("mrim-prpl", "[%s] Group '%s' not exists - creating\n", __func__, new_group);
		AddContactInfo *info = g_new(AddContactInfo, 1);
		info->buddy = buddy;
		info->group = purple_find_group((gchar*)new_group);
		info->move = TRUE;
		cl_add_group(mrim, (gchar*)new_group, info);
	} else {
		mb->group_id = group_id;
		mrim_modify_buddy(mrim, buddy);
	}
	g_free(group_found);
}

const char *mrim_normalize(const PurpleAccount *account, const char *who) {
	return g_ascii_strdown((gchar*)who, -1);
}

/* User actions */

void blist_authorize_menu_item(PurpleBlistNode *node, gpointer userdata) { /* Request auth message */
	PurpleBuddy *buddy = (PurpleBuddy*)node;
	g_return_if_fail(buddy != NULL);
	MrimBuddy *mb = buddy->proto_data;
	g_return_if_fail(mb != NULL);
	MrimData *mrim = (MrimData*)userdata;
	g_return_if_fail(mrim != NULL);
	purple_debug_info("mrim", "[%s] Asking authorization of '%s'\n", __func__, mb->email);
	mrim_send_authorize(mrim, mb->email, NULL);
}

#ifdef ENABLE_GTK

void update_sms_char_counter(GObject *object, gpointer user_data) {
	SmsDialogParams *params = user_data;
	gchar *original_text, *new_text;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(params->message_text);
	{
		GtkTextIter start, end;
		gtk_text_buffer_get_start_iter(buffer, &start);
		gtk_text_buffer_get_end_iter(buffer, &end);
		original_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
	}
	if (gtk_toggle_button_get_active((GtkToggleButton*)params->translit)) {
		new_text = transliterate_text(original_text);
	} else {
		new_text = g_strdup(original_text);
	}
	g_free(original_text);
	g_free(params->sms_text);
	params->sms_text = new_text;
	gint count = g_utf8_strlen(new_text, -1);
	gchar *buf = g_strdup_printf(_("Symbols: %d"), count);
	gtk_label_set_text(params->char_counter, buf);
	g_free(buf);
}

void sms_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
	SmsDialogParams *params = user_data;
	switch (response_id) {
		case GTK_RESPONSE_ACCEPT:
			{
				MrimBuddy *mb = params->mb;
				MrimData *mrim = params->mrim;
				update_sms_char_counter(NULL, params); //На случай, если после включения транслитерации пользователь уже не изменял текст
				gchar *text = params->sms_text;
				gint phone_index = gtk_combo_box_get_active(params->phone);
				if (phone_index > -1) {
					gchar *phone = mb->phones[phone_index];
					mrim_send_sms(mrim, phone, text);
				}
				break;
			}
		case GTK_RESPONSE_REJECT:
			break;
	}
	gtk_widget_destroy((GtkWidget*)dialog);
}

void sms_dialog_destroy(GtkDialog *dialog, gpointer user_data) {
	SmsDialogParams *params = user_data;
	g_free(params->sms_text);
	g_free(params);
}

void sms_dialog_edit_phones(GtkButton *button, gpointer user_data) {
	SmsDialogParams *params = user_data;
	blist_edit_phones_menu_item((PurpleBlistNode*)params->buddy, params->mrim);
	gtk_combo_box_remove_text(params->phone, 2);
	gtk_combo_box_remove_text(params->phone, 1);
	gtk_combo_box_remove_text(params->phone, 0);
	gtk_combo_box_append_text(params->phone, params->mb->phones[0]);
	gtk_combo_box_append_text(params->phone, params->mb->phones[1]);
	gtk_combo_box_append_text(params->phone, params->mb->phones[2]);
	gtk_combo_box_set_active(params->phone, 0);
}

void blist_gtk_sms_menu_item(PurpleBlistNode *node, gpointer userdata) {
	PurpleBuddy *buddy = (PurpleBuddy *) node;
	MrimData *mrim = userdata;
	g_return_if_fail(buddy != NULL);
	g_return_if_fail(mrim != NULL);
	MrimBuddy *mb = buddy->proto_data;
	g_return_if_fail(mb != NULL);

	/* Диалог */
	GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Send SMS"), NULL, GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT, GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, NULL);
	gtk_window_set_default_size((GtkWindow*)dialog, 320, 240);
	GtkWidget *content_area = gtk_dialog_get_content_area((GtkDialog*)dialog);
	GtkWidget *hbox;
	//gtk_container_set_border_width(content_area, 8); // Не понимаю почему не работает. Если когда-нибудь заработает - убрать следующую строчку
	gtk_container_set_border_width((GtkContainer*)dialog, 6);
	gtk_box_set_spacing((GtkBox*)content_area, 6);
	/* Псевдоним */
	GtkWidget *buddy_name = gtk_label_new(mb->alias);
	gtk_box_pack_start((GtkBox*)content_area, buddy_name, FALSE, TRUE, 0);
	/* Телефон */
	hbox = gtk_hbox_new(FALSE, 6);
	gtk_box_pack_start((GtkBox*)content_area, hbox, FALSE, TRUE, 0);
	GtkWidget *phone_combo_box = gtk_combo_box_new_text();
	gtk_combo_box_append_text((GtkComboBox*)phone_combo_box, mb->phones[0]);
	gtk_combo_box_append_text((GtkComboBox*)phone_combo_box, mb->phones[1]);
	gtk_combo_box_append_text((GtkComboBox*)phone_combo_box, mb->phones[2]);
	gtk_combo_box_set_active((GtkComboBox*)phone_combo_box, 0);
	gtk_box_pack_start((GtkBox*)hbox, gtk_label_new(_("Phone:")), FALSE, TRUE, 0);
	gtk_box_pack_start((GtkBox*)hbox, phone_combo_box, TRUE, TRUE, 0);
	GtkWidget *edit_phones_button = gtk_button_new_from_stock(GTK_STOCK_EDIT);
	gtk_box_pack_end((GtkBox*)hbox, edit_phones_button, FALSE, TRUE, 0);
	/* Текст сообщения */
	GtkWidget *scrolled_wnd = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy((GtkScrolledWindow*)scrolled_wnd, GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	GtkWidget *message_text = gtk_text_view_new();
	gtk_container_add((GtkContainer*)scrolled_wnd, message_text);
	gtk_box_pack_start((GtkBox*)content_area, scrolled_wnd, TRUE, TRUE, 0);
	gtk_text_view_set_wrap_mode((GtkTextView*)message_text, GTK_WRAP_WORD);
	/* Флажок транслитерации и счётчик символов */
	hbox = gtk_hbutton_box_new();
	gtk_button_box_set_spacing((GtkBox*)hbox, 6);
	gtk_button_box_set_layout((GtkButtonBox*)hbox, GTK_BUTTONBOX_EDGE);
	GtkWidget *translit = gtk_check_button_new_with_label(_("Translit"));
	gtk_container_add((GtkContainer*)hbox, translit);
	GtkWidget *char_counter = gtk_label_new("");
	gtk_container_add((GtkContainer*)hbox, char_counter);
	gtk_box_pack_end((GtkBox*)content_area, hbox, FALSE, TRUE, 0);
	/* Сохраним адреса нужных объектов */
	SmsDialogParams *params = g_new0(SmsDialogParams, 1);
	params->buddy = buddy;
	params->mrim = mrim;
	params->mb = mb;
	params->message_text = (GtkTextView*)message_text;
	params->translit = (GtkCheckButton*)translit;
	params->char_counter = (GtkLabel*)char_counter;
	params->phone = (GtkComboBox*)phone_combo_box;
	params->sms_text = NULL;
	/* Подключим обработчики сигналов */
	g_signal_connect(G_OBJECT(dialog), "destroy", G_CALLBACK(sms_dialog_destroy), params);
	{
		GtkTextBuffer *buffer = gtk_text_view_get_buffer((GtkTextView*)message_text);
		g_signal_connect(G_OBJECT(buffer), "changed", G_CALLBACK(update_sms_char_counter), params);
		update_sms_char_counter(G_OBJECT(buffer), params);
	}
	g_signal_connect(G_OBJECT(translit), "toggled", G_CALLBACK(update_sms_char_counter), params);
	g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(sms_dialog_response), params);
	g_signal_connect(G_OBJECT(edit_phones_button), "clicked", G_CALLBACK(sms_dialog_edit_phones), params);
	/* Отображаем диалог */
	gtk_widget_show_all(dialog);
	/* Если текущая локаль не поддерживает транслитерацию - отключим эту функцию */
	if (g_strcmp0("translit-table", _("translit-table")) == 0) {
		gtk_widget_hide(translit);
	}
	/* Делаем активным окном окно ввода сообщения */
	gtk_widget_grab_focus(message_text);
}

#endif

void blist_send_sms(PurpleConnection *gc, PurpleRequestFields *fields) {
	g_return_if_fail(gc);
	PurpleRequestField *RadioBoxField = purple_request_fields_get_field(fields, "combobox");
	int index  = RadioBoxField->u.choice.value;
	GList *list = RadioBoxField->u.choice.labels;
	while (index-- && list)
		list = list->next;
	gchar *message = (gchar*)purple_request_fields_get_string(fields, "message_box");
	mrim_send_sms((MrimData*)gc->proto_data, list->data, message);
}

void  blist_sms_menu_item(PurpleBlistNode *node, gpointer userdata) {
	PurpleBuddy *buddy = (PurpleBuddy *) node;
	MrimData *mrim = userdata;
	g_return_if_fail(buddy != NULL);
	g_return_if_fail(mrim != NULL);
	MrimBuddy *mb = buddy->proto_data;
	g_return_if_fail(mb != NULL);
	PurpleRequestFields *fields;
	PurpleRequestFieldGroup *group;
	PurpleRequestField *field;
	fields = purple_request_fields_new();
	group = purple_request_field_group_new(NULL);
	purple_request_fields_add_group(fields, group);
	field = purple_request_field_choice_new("combobox", _("Choose phone number"), 0);
	purple_request_field_choice_add(field, mb->phones[0]);
	purple_request_field_choice_add(field, mb->phones[1]);
	purple_request_field_choice_add(field, mb->phones[2]);
	purple_request_field_group_add_field(group, field);
	field = purple_request_field_string_new("message_box",_("SMS message text"),"",TRUE);
	purple_request_field_group_add_field(group, field);
	purple_request_fields(mrim->gc, _("Send SMS"), NULL, _("SMS message should contain not\nmore than 135 symbols in latin\nor 35 in cyrillic."),
		fields, _("_Send"), G_CALLBACK(blist_send_sms), _("_Cancel"), NULL, mrim->account, buddy->name, NULL, mrim->gc);
}

void blist_edit_phones(PurpleBuddy *buddy, PurpleRequestFields *fields) {
	g_return_if_fail(buddy);
	MrimBuddy *mb = buddy->proto_data;
	g_return_if_fail(mb);
	PurpleAccount *account = purple_buddy_get_account(buddy);
	PurpleConnection *gc = purple_account_get_connection(account);
	MrimData *mrim = purple_connection_get_protocol_data(gc);
	PurpleRequestFieldGroup *group = fields->groups->data;
	mb->phones[0] = g_strdup(purple_request_fields_get_string(fields, "phone1"));
	mb->phones[1] = g_strdup(purple_request_fields_get_string(fields, "phone2"));
	mb->phones[2] = g_strdup(purple_request_fields_get_string(fields, "phone3"));
	guint i = 0;
	while (mb->phones[i]) {
		if (mb->phones[i][0] && (mb->phones[i][0] != '+')) {
			gchar *phone = g_strdup_printf("+%s", mb->phones[i]);
			g_free(mb->phones[i]);
			mb->phones[i] = phone;
		}
		i++;
	}
	mrim_modify_buddy(mrim, buddy);
}

void blist_edit_phones_menu_item(PurpleBlistNode *node, gpointer userdata) {
	PurpleBuddy *buddy = (PurpleBuddy*)node;
	MrimData *mrim = userdata;
	g_return_if_fail(buddy != NULL);
	g_return_if_fail(mrim != NULL);
	MrimBuddy *mb = buddy->proto_data;
	g_return_if_fail(mb != NULL);
	if (!mb->phones) { /* Так не должно быть */
		mb->phones = g_new0(char *, 4);
	}
	PurpleRequestFields *fields;
	PurpleRequestFieldGroup *group;
	PurpleRequestField *field;
	fields = purple_request_fields_new();
	group = purple_request_field_group_new(mb->email);
	purple_request_fields_add_group(fields, group);
	field = purple_request_field_string_new("phone1", _("_Main number"), mb->phones[0], FALSE);
	purple_request_field_group_add_field(group, field);
	field = purple_request_field_string_new("phone2", _("S_econd number"), mb->phones[1], FALSE);
	purple_request_field_group_add_field(group, field);
	field = purple_request_field_string_new("phone3", _("_Third number"), mb->phones[2], FALSE);
	purple_request_field_group_add_field(group, field);
	purple_request_fields(mrim->gc, _("Phone numbers"), _("Phone numbers"), _("Specify numbers as shown: +71234567890"),  fields,
			_("_OK"), G_CALLBACK(blist_edit_phones),
			_("_Cancel"), NULL,
			mrim->account, buddy->name, NULL, buddy);
}

void mrim_url_menu_action(PurpleBlistNode *node, gpointer userdata) {
	PurpleBuddy *buddy = (PurpleBuddy*)node;
	PurpleAccount *account = purple_buddy_get_account(buddy);
	MrimData *mrim = account->gc->proto_data;
	g_return_if_fail(mrim != NULL);
	mrim_open_myworld_url(mrim, buddy->name, userdata);
}

void blist_toggle_visible(PurpleBlistNode *node, gpointer userdata) {
	PurpleBuddy *buddy = (PurpleBuddy*)node;
	MrimData *mrim = userdata;
	g_return_if_fail(buddy != NULL);
	g_return_if_fail(mrim != NULL);
	MrimBuddy *mb = buddy->proto_data;
	g_return_if_fail(mb != NULL);
	mb->flags ^= CONTACT_FLAG_VISIBLE;
	mrim_modify_buddy(mrim, buddy);
}

void blist_toggle_invisible(PurpleBlistNode *node, gpointer userdata) {
	PurpleBuddy *buddy = (PurpleBuddy*)node;
	MrimData *mrim = userdata;
	g_return_if_fail(buddy != NULL);
	g_return_if_fail(mrim != NULL);
	MrimBuddy *mb = buddy->proto_data;
	g_return_if_fail(mb != NULL);
	mb->flags ^= CONTACT_FLAG_INVISIBLE;
	mrim_modify_buddy(mrim, buddy);
}

GList *mrim_user_actions(PurpleBlistNode *node) {
	purple_debug_info("mrim-prpl", "[%s]\n", __func__);
	if (!PURPLE_BLIST_NODE_IS_BUDDY(node)) return NULL;
	PurpleBuddy *buddy = (PurpleBuddy*)node;
	MrimBuddy *mb = buddy->proto_data;
	MrimData *mrim = purple_buddy_get_account(buddy)->gc->proto_data;
	if (mb) {
		PurpleMenuAction *action;
		GList *list = NULL;//g_list_append(NULL, NULL);
		if (!mb->authorized) {
			action = purple_menu_action_new(_("Request authorization"), PURPLE_CALLBACK(blist_authorize_menu_item), mrim, NULL);
			list = g_list_append(list, action);
		}
		if (mb->phones && mb->phones[0]) {
#ifdef ENABLE_GTK
			if (mrim->use_gtk) {
				action = purple_menu_action_new(_("Send an SMS..."), PURPLE_CALLBACK(blist_gtk_sms_menu_item), mrim, NULL);
			} else {			
#endif
				action = purple_menu_action_new(_("Send an SMS..."), PURPLE_CALLBACK(blist_sms_menu_item), mrim, NULL);
#ifdef ENABLE_GTK
			}
#endif
			list = g_list_append(list, action);
		}
		action = purple_menu_action_new(_("Edit phone numbers..."), PURPLE_CALLBACK(blist_edit_phones_menu_item), mrim, NULL);
		list = g_list_append(list, action);
		if (is_valid_email(mb->email)) {
			list = g_list_append(list, NULL);
			action = purple_menu_action_new(_("MyWorld@Mail.ru"), PURPLE_CALLBACK(mrim_url_menu_action),
				"http://r.mail.ru/cln3587/my.mail.ru/%s/%s", NULL);
			list = g_list_append(list, action);
			action = purple_menu_action_new(_("Photo@Mail.ru"), PURPLE_CALLBACK(mrim_url_menu_action),
				"http://r.mail.ru/cln3565/foto.mail.ru/%s/%s", NULL);
			list = g_list_append(list, action);
			action = purple_menu_action_new(_("Video@Mail.ru"), PURPLE_CALLBACK(mrim_url_menu_action),
				"http://r.mail.ru/cln3567/video.mail.ru/%s/%s", NULL);
			list = g_list_append(list, action);
			action = purple_menu_action_new(_("Blogs@Mail.ru"), PURPLE_CALLBACK(mrim_url_menu_action),
				"http://r.mail.ru/cln3566/blogs.mail.ru/%s/%s", NULL);
			list = g_list_append(list, action);
			list = g_list_append(list, NULL);
		}
		{
			GList *submenu = NULL;
			action = purple_menu_action_new((mb->flags & CONTACT_FLAG_VISIBLE) ? _("Remove from 'Visible to' list") :
				_("Add to 'Visible to' list"), PURPLE_CALLBACK(blist_toggle_visible), mrim, NULL);
			submenu = g_list_append(submenu, action);
			action = purple_menu_action_new((mb->flags & CONTACT_FLAG_INVISIBLE) ? _("Remove from 'Invisible to' list") : 
				_("Add to 'Invisible to' list"), PURPLE_CALLBACK(blist_toggle_invisible), mrim, NULL);
			submenu = g_list_append(submenu, action);
			action = purple_menu_action_new(_("Visibility settings"), NULL, mrim, submenu);
			list = g_list_append(list, action);
		}
		return list;
	} else { //Так быть вообще то не должно
		return NULL;
	}
}

/* User info */

MrimSearchResult *mrim_parse_search_result(MrimPackage *pack) {
	guint32 status = mrim_package_read_UL(pack);
	purple_debug_info("mrim-prpl", "[%s] Status is %i\n", __func__, status);
	if (status != MRIM_ANKETA_INFO_STATUS_OK) {
		switch (status) {
			case MRIM_ANKETA_INFO_STATUS_NOUSER:
				purple_notify_warning(mrim_plugin, _("Encountered an error while working on user details!"),
					_("Encountered an error while working on user details!"), _("User not found."));
				break;
			case MRIM_ANKETA_INFO_STATUS_DBERR:
				purple_notify_warning(mrim_plugin, _("Encountered an error while working on user details!"),
					_("Encountered an error while working on user details!"), _("DBERR error. Please try later."));
				break;
			case MRIM_ANKETA_INFO_STATUS_RATELIMERR:
				purple_notify_warning(mrim_plugin, _("Encountered an error while working on user details!"),
					_("Encountered an error while working on user details!"), _("MRIM_ANKETA_INFO_STATUS_RATELIMERR"));
				break;
			default:
				purple_notify_warning(mrim_plugin, _("Encountered an error while working on user details!"),
					_("Encountered an error while working on user details!"), _("unknown error"));
				break;
		}
		return NULL;
	}
	MrimSearchResult *result = g_new0(MrimSearchResult, 1);
	result->column_count = mrim_package_read_UL(pack);
	result->row_count = mrim_package_read_UL(pack);
	guint32 date = mrim_package_read_UL(pack);
	purple_debug_info("mrim-prpl", "[%s] Column count is %i, row count is %i\n", __func__, result->column_count, result->row_count);
	result->columns = g_new0(MrimSearchResultColumn, result->column_count);
	result->rows = g_new0(gchar**, result->row_count);
	guint i;
	for (i = 0; i < result->column_count; i++) {
		result->columns[i].title = mrim_package_read_LPSA(pack);
		if (g_strcmp0(result->columns[i].title, "Username") == 0) {
			result->username_index = i;
		} else if (g_strcmp0(result->columns[i].title, "Domain") == 0) {
			result->domain_index = i;
		}
		if ((g_strcmp0(result->columns[i].title, "Username") == 0) || (g_strcmp0(result->columns[i].title, "Domain") == 0) ||
			(g_strcmp0(result->columns[i].title, "City_id") == 0) || (g_strcmp0(result->columns[i].title, "Country_id") == 0) ||
			(g_strcmp0(result->columns[i].title, "BMonth") == 0) || (g_strcmp0(result->columns[i].title, "BDay") == 0) /*|| 
			(g_strcmp0(result->columns[i].title, "mrim_status") == 0)*/) {
			result->columns[i].skip = TRUE;
		} else {
			result->columns[i].skip = FALSE;
		}
		if ((g_strcmp0(result->columns[i].title, "Nickname") == 0) || (g_strcmp0(result->columns[i].title, "FirstName") == 0) ||
			(g_strcmp0(result->columns[i].title, "LastName") == 0) || (g_strcmp0(result->columns[i].title, "Location") == 0) ||
			(g_strcmp0(result->columns[i].title, "status_title") == 0) || (g_strcmp0(result->columns[i].title, "status_desc") == 0)) {
			result->columns[i].unicode = TRUE;
		} else {
			result->columns[i].unicode = FALSE;
		}
	}
	
	for (guint i = 0; i < result->row_count; i++) {
		if (pack->cur >= pack->data_size) break;
		result->rows[i] = g_new0(gchar*, result->column_count);
		for (guint j = 0; j < result->column_count; j++) {
			if (result->columns[j].unicode) {
				result->rows[i][j] = mrim_package_read_LPSW(pack);
			} else {
				result->rows[i][j] = mrim_package_read_LPSA(pack);
			}
			if ((!result->rows[i][j]) || (!result->rows[i][j][0])) {
				g_free(result->rows[i][j]);
				result->rows[i][j] = g_strdup(" ");
			}
			if (g_strcmp0(result->columns[j].title, "Sex") == 0) {
				gchar *value = (atoi(result->rows[i][j]) == 1) ? g_strdup(_("Male")) : g_strdup(_("Female"));
				g_free(result->rows[i][j]);
				result->rows[i][j] = value;
			} else if (g_strcmp0(result->columns[j].title, "Zodiac") == 0) {
				guint index = atoi(result->rows[i][j]) - 1;
				if (index < ARRAY_SIZE(zodiac)) {
					g_free(result->rows[i][j]);
					result->rows[i][j] = g_strdup(_(zodiac[index]));
				}
			}
		}
	};
	// Detecting if search results contain birthday column so we can determine age then:
	purple_debug_info("mrim-prpl", "[%s] Looking for BDay...\n", __func__);
	guint bday_col_index = result->column_count;
	for (guint i = 0; i < result->column_count; i++) {
		if (g_strcmp0(result->columns[i].title, "Birthday") == 0) {
			bday_col_index = i;
			break;
		};
	};
	if (bday_col_index < result->column_count) {
		guint age_col_index = result->column_count++;
		result->columns = g_renew(MrimSearchResultColumn, result->columns, result->column_count);
		result->columns[age_col_index].title		= "Age";
		result->columns[age_col_index].skip			= FALSE;
		result->columns[age_col_index].unicode	= FALSE;
		
		for (guint row_id = 0; row_id < result->row_count; row_id++) {
			gchar **old_row = result->rows[row_id];
			if (!old_row) {
				break;
			} else {
				result->rows[row_id] = g_new0(gchar*, result->column_count);
				for (guint col_id = 0; col_id < age_col_index; col_id++) {
					result->rows[row_id][col_id] = g_strdup(old_row[col_id]);
					g_free(old_row[col_id]);
				}
				gchar *BDay_Str = g_strdup(result->rows[row_id][bday_col_index]);
				gchar *buddy_age = g_strdup("0");
				if ( g_strcmp0(BDay_Str, " ") == 0 ) {
					g_free(buddy_age);
					buddy_age = g_strdup(_("Not specified"));
				} else {
					int bd_year=0, bd_mon=0, bd_day=0;
					int ret = sscanf(BDay_Str, "%u-%u-%u", &bd_year, &bd_mon, &bd_day);
					purple_debug_info("mrim-prpl", "[%s] Birthday parsed (ret=%i) is %i-%i-%i.\n", __func__, ret, bd_year, bd_mon, bd_day);
					
#if GLIB_MINOR_VERSION < 26
			// Age calculation for GLib <= 2.26
					GTimeVal *gTimeReal = g_new0(GTimeVal, 1);
					GDate *gCurDate			= g_new0(GDate, 1);
					g_get_current_time (gTimeReal);
					g_date_set_time_val (gCurDate, gTimeReal);
					g_date_subtract_years (gCurDate, bd_year);
					g_date_subtract_months (gCurDate, bd_mon % 12);
					g_date_subtract_days (gCurDate, bd_day);
					g_free(buddy_age);
					int full_years	= g_date_get_year(gCurDate);
					int full_months = g_date_get_month(gCurDate) % 12;
					
					g_free(gTimeReal);
					g_free(gCurDate);
			// End GLib < 2.26
#else
			// Used for GLib >= 2.26
					GDateTime *TimeNow	= g_date_time_new_now_local();
					GDateTime *LifeTime;
					LifeTime	= g_date_time_add_full(TimeNow, -bd_year, -bd_mon, -bd_day, 0, 0, 0);
					g_free(buddy_age);
					int full_years	= g_date_time_get_year(LifeTime);
					int full_months = g_date_time_get_month(LifeTime) % 12;
					g_date_time_unref(TimeNow);
					g_date_time_unref(LifeTime);
			// End GLib >= 2.26
#endif
					if (!full_months) {
						buddy_age	= g_strdup_printf(_("%i full years"), full_years);
					} else {
						buddy_age	= g_strdup_printf(_("%i years, %i months"), full_years, full_months);
					}
				}
				result->rows[row_id][age_col_index] = buddy_age;
			}
		}
	};
	// Age guessing end. 
	purple_debug_info("mrim-prpl", "[%s] Search result parsed OK (%i rows)\n", __func__, i);
	return result;
}

void mrim_get_info_ack(MrimData *mrim, gpointer user_data, MrimPackage *pack) {
	gchar *user_name = user_data;
	MrimSearchResult *result = mrim_parse_search_result(pack);
	if (result) {
		guint i;
		PurpleNotifyUserInfo *info = purple_notify_user_info_new();
		purple_notify_user_info_add_pair(info, _("E-mail"), user_name);
		for (i = 0; i < result->column_count; i++) {
			if (!result->columns[i].skip) {
				purple_notify_user_info_add_pair(info, _(result->columns[i].title), result->rows[0][i]);
			}
		}
		PurpleBuddy *buddy = purple_find_buddy(mrim->account, user_name);
		if (buddy) {
			MrimBuddy *mb = buddy->proto_data;
			if (mb) {
				if (mb->user_agent) {
					gchar *tmp = mrim_get_ua_alias(mrim, mb->user_agent);
					purple_notify_user_info_add_pair(info, _("User agent"), tmp);
					g_free(tmp);
				}
				if (mb->microblog) {
					purple_notify_user_info_add_pair(info, _("Microblog"), mb->microblog);
				}
			}
		}
		purple_notify_userinfo(mrim->gc, user_name, info, NULL, NULL);
	}
}

void mrim_get_info(PurpleConnection *gc, const char *username) {
	purple_debug_info("mrim-prpl", "[%s]\n", __func__);
	g_return_if_fail(username);
	g_return_if_fail(gc);
	MrimData *mrim = gc->proto_data;
	g_return_if_fail(mrim != NULL);
	purple_debug_info("mrim-prpl", "[%s] Fetching info for user '%s'\n", __func__, username);
	if (!is_valid_email((gchar*)username)) {
		PurpleNotifyUserInfo *info = purple_notify_user_info_new();
		purple_notify_user_info_add_pair(info, _("UserInfo is not available for conferences and phones"), "");
		purple_notify_userinfo(gc, username, info, NULL, NULL);
	} else {
		gchar** split = g_strsplit(username, "@", 2);
		gchar *user = split[0];
		gchar *domain = split[1];
		MrimPackage *pack = mrim_package_new(mrim->seq++, MRIM_CS_WP_REQUEST);
		mrim_package_add_UL(pack, MRIM_CS_WP_REQUEST_PARAM_USER);
		mrim_package_add_LPSA(pack, user);
		mrim_package_add_UL(pack, MRIM_CS_WP_REQUEST_PARAM_DOMAIN);
		mrim_package_add_LPSA(pack, domain);
		g_strfreev(split);
		mrim_add_ack_cb(mrim, pack->header->seq, mrim_get_info_ack, g_strdup(username));
		mrim_package_send(pack, mrim);
	}
}

void mrim_searchresults_add_buddy(PurpleConnection *gc, GList *row, void *user_data) {
	MrimData *mrim = user_data;
	purple_debug_info("mrim","%s", mrim->account->username);
	if (!purple_find_buddy(mrim->account, g_list_nth_data(row, 0))) {
			purple_blist_request_add_buddy(mrim->account,  g_list_nth_data(row, 0), NULL, NULL); // TODO Propose alias automatically.
		}
}

void mrim_search_ack(MrimData *mrim, gpointer user_data, MrimPackage *pack) {
	MrimSearchResult *result = mrim_parse_search_result(pack);
	if (result) {
		purple_debug_info("mrim-prpl", "[%s]\n", __func__);
		PurpleNotifySearchResults *results = purple_notify_searchresults_new();
		PurpleNotifySearchColumn *column = purple_notify_searchresults_column_new(_("E-mail"));
		purple_notify_searchresults_column_add(results, column);
		guint32 i;
		for (i = 0; i < result->column_count; i++) {
			if (!result->columns[i].skip) {
				column = purple_notify_searchresults_column_new(g_strdup(_(result->columns[i].title)));
				purple_notify_searchresults_column_add(results, column);
			}
		}
		purple_notify_searchresults_button_add(results, PURPLE_NOTIFY_BUTTON_ADD,
			mrim_searchresults_add_buddy);
		for (i = 0; i < result->row_count; i++) {
			if (result->rows[i]) {
				guint32 j;
				GList *row = g_list_append(NULL, g_strdup_printf("%s@%s",
					result->rows[i][result->username_index],
						result->rows[i][result->domain_index]));
				for (j = 0; j < result->column_count; j++) {
					if (!result->columns[j].skip) {
						row = g_list_append(row, result->rows[i][j]);
					}
				}
				purple_notify_searchresults_row_add(results, row);
			} else {
				break;
			}
		}
		purple_notify_searchresults(mrim->gc, NULL, _("Search results"), NULL, results, NULL, NULL);
	}
}

/* Tooltip */

void mrim_tooltip_text(PurpleBuddy *buddy, PurpleNotifyUserInfo *info, gboolean full) {
	purple_debug_info("mrim-prpl","[%s]\n",__func__);
	g_return_if_fail(buddy);
	if (buddy->alias) {
		purple_notify_user_info_add_pair(info, _("Name"), buddy->alias);
	}
	MrimBuddy *mb = buddy->proto_data;
	if (mb) {
		MrimData *mrim = mb->mrim;
		if (mb->flags & CONTACT_FLAG_MULTICHAT) {
			purple_notify_user_info_add_pair(info, _("Account"), buddy->account->username);
			purple_notify_user_info_add_pair(info, _("Room"), mb->email);
			purple_notify_user_info_add_pair(info, _("Alias"), mb->alias);
			return;
		}
		if (mb->status->id != STATUS_OFFLINE) {
			//if (mb->listening) {
				purple_notify_user_info_add_pair(info, _("Status"), mb->status->title);
			//} else {
			//	purple_notify_user_info_add_pair(info, _("Status"), mb->status->display_str);
			//}
		}
		if (mb->listening) {
			purple_notify_user_info_add_pair(info, _("Listening"), mb->listening);
		} else if (mb->status && mb->status->desc) {
			purple_notify_user_info_add_pair(info, _("Comment"), mb->status->desc);
		}
		if (mb->user_agent) {
			gchar *tmp = mrim_get_ua_alias(mrim, mb->user_agent);
			purple_notify_user_info_add_pair(info, _("User agent"), tmp);
			g_free(tmp);
		}
		if (mb->microblog) {
			purple_notify_user_info_add_pair(info, _("Microblog"), mb->microblog);
		}
	}
	purple_debug_info("mrim-prpl","[%s] end.\n",__func__);
}

/* Authorization */

void mrim_authorization_yes(gpointer va_data) {
	MrimAuthData *data = va_data;
	MrimData *mrim = data->mrim;
	purple_debug_info("mrim-prpl","[%s] Authorization request from '%s' acepted\n", __func__, data->from);
	MrimPackage *pack = mrim_package_new(data->seq, MRIM_CS_AUTHORIZE);
	mrim_package_add_LPSA(pack, data->from);
	mrim_package_send(pack, mrim);
	PurpleBuddy *buddy = purple_find_buddy(mrim->account, data->from);
	if (buddy && buddy->proto_data)	{
		MrimBuddy *mb = buddy->proto_data;
		if (!mb->authorized) {
			mrim_send_authorize(mrim, data->from, NULL); /* TODO: Request auth message */
		}
	}
	g_free(data->from);
	g_free(data);
}

void mrim_authorization_no(gpointer va_data) {
	purple_debug_info("mrim-prpl", "[%s]\n", __func__);
	MrimAuthData *data = va_data;
	g_free(data->from);
	g_free(data);
}

void mrim_send_authorize(MrimData *mrim, gchar *email, gchar *message) { /* TODO: Auth message */
	purple_debug_info("mrim-prpl", "[%s] Send auhtorization request to '%s' with message '%s'\n", __func__, email, message);
	MrimPackage *pack = mrim_package_new(mrim->seq++, MRIM_CS_MESSAGE);
	mrim_package_add_UL(pack, MESSAGE_FLAG_AUTHORIZE | MESSAGE_FLAG_NORECV);
	mrim_package_add_LPSA(pack, email);
	mrim_package_add_base64(pack, "uss", 2, mrim->nick, message ? message : " ");
	mrim_package_add_UL(pack, 0);
	mrim_package_send(pack, mrim);
}

/*
* CHATS
*/

// handle MULTICHAT_GET_MEMBERS
void mrim_chat_blist(MrimData *mrim, gpointer data, MrimPackage *pack)
{
	purple_debug_info("mrim-prpl", "[%s] room=<%s>\n", __func__, (gchar*) data);
	PurpleConversation *conv =  purple_find_chat(mrim->gc, get_chat_id(data));
	PurpleConvChat *chat = PURPLE_CONV_CHAT(conv);

	mrim_package_read_UL(pack); // todo: wtf?? 0x62
	mrim_package_read_UL(pack); // todo: wtf?? 0x02 == MULTICHAT_MEMBERS
	gchar *topic = mrim_package_read_LPSW(pack);
	mrim_package_read_UL(pack); // todo: wtf?? 0x4c
	// Set Topic
	purple_conv_chat_set_topic(chat, NULL, topic);
	///
	/// Add users
	int n = mrim_package_read_UL(pack);
	for (int i = 0; i<n ; i++)
	{
		gchar *username = mrim_package_read_LPSA(pack);
		purple_conv_chat_add_user(chat, username, NULL, PURPLE_CBFLAGS_NONE, TRUE);
	}
}


void mrim_chat_join(PurpleConnection *gc, GHashTable *components)
{
	gchar *room = g_hash_table_lookup(components, "room");
	if (! is_valid_chat(room))
	{
		char *buf = g_strdup_printf(_("%s is not a valid room name"), room);
		purple_notify_error(gc, _("Invalid Room Name"), _("Invalid Room Name"), buf);
		purple_serv_got_join_chat_failed(gc, components);
		g_free(buf);
		return;
	}

	const char *username = gc->account->username;
	MrimData *mrim = gc->proto_data;
	PurpleChat *pchat = purple_blist_find_chat(gc->account, room);
	if (pchat == NULL) // TODO
	{
		// add chat
		purple_debug_info("mrim-prpl", "[%s] New chat: %s \n", __func__, room);
		GHashTable *defaults = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
		g_hash_table_insert(defaults, "room", g_strdup(room));
		PurpleChat *pc = purple_chat_new(mrim->account, room, defaults);

		purple_blist_add_chat(pc, get_mrim_group(mrim, 0)->group, NULL);  // add to "Остальные" // TODO // TODO move to ack?

		MrimPackage *pack = mrim_package_new(mrim->seq, MRIM_CS_ADD_CONTACT);
		mrim_package_add_UL(pack, CONTACT_FLAG_MULTICHAT);
		mrim_package_add_UL(pack, 0);
		mrim_package_add_UL(pack, 0);
		mrim_package_add_LPSW(pack, "THIS IS TOPIC"); // TODO
		mrim_package_add_UL(pack, 0);
		mrim_package_add_UL(pack, 0);
		mrim_package_add_UL(pack, 0);

		mrim_package_add_UL(pack, 0); // UL 34h 50h 21h
		mrim_package_add_UL(pack, 0); // UL 30h 1fh
		// UL count
		// count штук LPS email
		mrim_package_send(pack, mrim);

	}

	if (!purple_find_chat(gc, get_chat_id(room))) {
		purple_debug_info("mrim-prpl", "[%s] %s is joining chat room %s\n", __func__, username, room);

		serv_got_joined_chat(gc, get_chat_id(room), room);
		// Get users list
		mrim_add_ack_cb(mrim, mrim->seq, mrim_chat_blist, g_strdup(room));

		MrimPackage *pack = mrim_package_new(mrim->seq++, MRIM_CS_MESSAGE);
		mrim_package_add_UL(pack, MESSAGE_FLAG_MULTICHAT );
		mrim_package_add_LPSA(pack, room);
		mrim_package_add_UL(pack, 0);
		mrim_package_add_UL(pack, 0);
		mrim_package_add_UL(pack, 4);
		mrim_package_add_UL(pack, MULTICHAT_GET_MEMBERS);
		mrim_package_send(pack, mrim);
	}
}

void mrim_reject_chat(PurpleConnection *gc, GHashTable *components)
{
	const char *room = g_hash_table_lookup(components, "room");
	purple_debug_info("mrim-prpl", "[%s] room = %s\n", __func__, room);
}

char *mrim_get_chat_name(GHashTable *components)
{
	purple_debug_info("mrim", "%s\n", __func__);
	const char *str = g_hash_table_lookup(components, "room");
	return (char*)str;
}


void mrim_chat_invite(PurpleConnection *gc, int id, const char *message, const char *who)
{
	purple_debug_info("mrim-prpl", "[%s]\n", __func__);
	MrimData *mrim = gc->proto_data;
	PurpleConversation *conv = purple_find_chat(gc, id);
	const char *room = conv->name;

	purple_debug_info("mrim-prpl", "%s is invited to join chat room %s\n", who, room);

	MrimPackage *pack = mrim_package_new(mrim->seq++, MRIM_CS_MESSAGE);
	mrim_package_add_UL(pack, CONTACT_FLAG_MULTICHAT );
	mrim_package_add_LPSA(pack, room);
	mrim_package_add_UL(pack, 0); // message
	mrim_package_add_UL(pack, 0); // rtf
	mrim_package_add_UL(pack, 0); // 0x27 ???
	mrim_package_add_UL(pack, MULTICHAT_ADD_MEMBERS);
	mrim_package_add_UL(pack, 0); // 0x1f
	mrim_package_add_UL(pack, 1); // CLPS?
	mrim_package_add_LPSA(pack, who);
	mrim_package_send(pack, mrim);
}

void mrim_chat_leave(PurpleConnection *gc, int id)
{
	PurpleConversation *conv = purple_find_chat(gc, id);
	purple_debug_info("mrim-prpl", "[%s] %s is leaving chat room %s\n", __func__, gc->account->username, conv->name);
}
