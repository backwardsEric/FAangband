/**
 * \file wiz-spoil.c
 * \brief Spoiler generation
 *
 * Copyright (c) 1997 Ben Harrison, and others
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "buildid.h"
#include "cmds.h"
#include "game-world.h"
#include "init.h"
#include "mon-lore.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-design.h"
#include "obj-info.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-properties.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-quest.h"
#include "ui-knowledge.h"
#include "ui-mon-lore.h"
#include "wizard.h"
#include "z-file.h"


/**
 * ------------------------------------------------------------------------
 * Item Spoilers by Ben Harrison (benh@phial.com)
 * ------------------------------------------------------------------------ */


/**
 * The spoiler file being created
 */
static ang_file *fh = NULL;


/**
 * Write out `n' of the character `c' to the spoiler file
 */
static void spoiler_out_n_chars(int n, char c)
{
	while (--n >= 0) file_writec(fh, c);
}

/**
 * Write out `n' blank lines to the spoiler file
 */
static void spoiler_blanklines(int n)
{
	spoiler_out_n_chars(n, '\n');
}

/**
 * Write a line to the spoiler file and then "underline" it with hypens
 */
static void spoiler_underline(const char *str, char c)
{
	text_out("%s", str);
	text_out("\n");
	spoiler_out_n_chars(strlen(str), c);
	text_out("\n");
}



/**
 * The basic items categorized by type
 */
static const grouper group_item[] =
{
	{ TV_SHOT,		"Ammo" },
	{ TV_ARROW,		  NULL },
	{ TV_BOLT,		  NULL },

	{ TV_BOW,		"Bows" },

	{ TV_SWORD,		"Weapons" },
	{ TV_POLEARM,	  NULL },
	{ TV_HAFTED,	  NULL },
	{ TV_DIGGING,	  NULL },

	{ TV_SOFT_ARMOR,	"Armour (Body)" },
	{ TV_HARD_ARMOR,	  NULL },
	{ TV_DRAG_ARMOR,	  NULL },

	{ TV_CLOAK,		"Armour (Misc)" },
	{ TV_SHIELD,	  NULL },
	{ TV_HELM,		  NULL },
	{ TV_CROWN,		  NULL },
	{ TV_GLOVES,	  NULL },
	{ TV_BOOTS,		  NULL },

	{ TV_AMULET,	"Amulets" },
	{ TV_RING,		"Rings" },

	{ TV_SCROLL,	"Scrolls" },
	{ TV_POTION,	"Potions" },
	{ TV_FOOD,		"Food" },
	{ TV_MUSHROOM,	"Mushrooms" },

	{ TV_ROD,		"Rods" },
	{ TV_WAND,		"Wands" },
	{ TV_STAFF,		"Staffs" },

	{ TV_MAGIC_BOOK,	"Magic Books" },
	{ TV_PRAYER_BOOK,	"Holy Books" },
	{ TV_NATURE_BOOK,	"Nature Books" },
	{ TV_SHADOW_BOOK,	"Shadow Books" },
	{ TV_OTHER_BOOK,	"Mystery Books" },

	{ TV_CHEST,		"Chests" },

	{ TV_LIGHT,		  "Lights and fuel" },
	{ TV_FLASK,		  NULL },

	{ 0, "" }
};





/**
 * Describe the kind
 */
static void kind_info(char *buf, size_t buf_len, char *dam, size_t dam_len,
		char *wgt, size_t wgt_len, int *lev, int32_t *val, int k)
{
	struct object_kind *kind = &k_info[k];
	struct object *obj = object_new(), *known_obj = object_new();
	int i;

	/* Prepare a fake item */
	object_prep(obj, kind, 0, MAXIMISE);

	/* Cancel bonuses */
	for (i = 0; i < OBJ_MOD_MAX; i++)
		obj->modifiers[i] = 0;
	obj->to_a = 0;
	obj->to_h = 0;
	obj->to_d = 0;

	/* Level */
	(*lev) = kind->level;

	/* Make known */
	object_copy(known_obj, obj);
	obj->known = known_obj;

	/* Value */
	(*val) = object_value(obj, 1);

	/* Description (too brief) */
	if (buf) {
		object_desc(buf, buf_len, obj, ODESC_BASE | ODESC_SPOIL, NULL);
	}

	/* Weight */
	if (wgt) {
		int16_t obj_weight = object_weight_one(obj);

		strnfmt(wgt, wgt_len, "%3d.%d", obj_weight / 10,
			obj_weight % 10);
	}

	/* Hack */
	if (!dam) {
		object_delete(NULL, NULL, &known_obj);
		object_delete(NULL, NULL, &obj);
		return;
	}

	/* Misc info */
	dam[0] = '\0';

	/* Damage */
	if (tval_is_ammo(obj) || tval_is_melee_weapon(obj))
		strnfmt(dam, dam_len, "%dd%d", obj->dd, obj->ds);
	else if (tval_is_armor(obj))
		strnfmt(dam, dam_len, "%d", obj->ac);

	object_delete(NULL, NULL, &known_obj);
	object_delete(NULL, NULL, &obj);
}


/**
 * Create a spoiler file for items
 */
void spoil_obj_desc(const char *fname)
{
	int i, k, s, t, n = 0;
	uint16_t *who;
	char buf[1024];
	char wgt[80];
	char dam[80];
	const char *format = "%-51s  %7s%6s%4s%9s\n";

	/* Open the file */
	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, fname);
	fh = file_open(buf, MODE_WRITE, FTYPE_TEXT);

	/* Oops */
	if (!fh) {
		msg("Cannot create spoiler file.");
		return;
	}

	/* Allocate the "who" array */
	who = mem_zalloc(z_info->r_max * sizeof(uint16_t));

	/* Header */
	file_putf(fh, "Spoiler File -- Basic Items (%s)\n\n\n", buildid);

	/* More Header */
	file_putf(fh, format, "Description", "Dam/AC", "Wgt", "Lev", "Cost");
	file_putf(fh, format, "----------------------------------------",
	        "------", "---", "---", "----");

	/* List the groups */
	for (i = 0; true; i++) {
		/* Write out the group title */
		if (group_item[i].name) {
			/* Bubble-sort by cost and then level */
			for (s = 0; s < n - 1; s++) {
				for (t = 0; t < n - 1; t++) {
					int i1 = t;
					int i2 = t + 1;

					int e1;
					int e2;

					int32_t t1;
					int32_t t2;

					kind_info(NULL, 0, NULL, 0, NULL, 0, &e1, &t1, who[i1]);
					kind_info(NULL, 0, NULL, 0, NULL, 0, &e2, &t2, who[i2]);

					if ((t1 > t2) || ((t1 == t2) && (e1 > e2))) {
						int tmp = who[i1];
						who[i1] = who[i2];
						who[i2] = tmp;
					}
				}
			}

			/* Spoil each item */
			for (s = 0; s < n; s++) {
				int e;
				int32_t v;
				size_t u8len;

				/* Describe the kind */
				kind_info(buf, sizeof(buf), dam, sizeof(dam), wgt, sizeof(wgt),
						  &e, &v, who[s]);

				/* Dump it */
				/*
				 * Per C99, width specifications to %s measure
				 * bytes.  To align the columns if the
				 * description has characters that take
				 * multiple bytes, handle the first column
				 * separately.  If the description has
				 * decomposed characters (ones where multiple
				 * Unicode code points combine to form one
				 * printed character), the following columns
				 * will still be out of alignment, but they'll
				 * be closer to aligned than what the standard
				 * library functions would do.
				 */
				u8len = utf8_strlen(buf);
				if (u8len < 51) {
					file_putf(fh, "  %s%*s", buf,
						(int) (51 - u8len), " ");
				} else {
					if (u8len > 51) {
						utf8_clipto(buf, 51);
					}
					file_putf(fh, "  %s", buf);
				}
				file_putf(fh, "%7s%6s%4d%9ld\n", dam, wgt, e,
						  (long)(v));
			}

			/* Start a new set */
			n = 0;

			/* Notice the end */
			if (!group_item[i].tval) break;

			/* Start a new set */
			file_putf(fh, "\n\n%s\n\n", group_item[i].name);
		}

		/* Get legal item types */
		for (k = 1; k < z_info->k_max; k++) {
			struct object_kind *kind = &k_info[k];

			/* Skip wrong tvals */
			if (kind->tval != group_item[i].tval) continue;

			/* Skip instant-artifacts */
			if (kf_has(kind->kind_flags, KF_INSTA_ART)) continue;

			/* Save the index */
			who[n++] = k;
		}
	}

	/* Free the "who" array */
	mem_free(who);

	/* Check for errors */
	if (!file_close(fh)) {
		msg("Cannot close spoiler file.");
		return;
	}

	/* Message */
	msg("Successfully created a spoiler file.");
}



/**
 * ------------------------------------------------------------------------
 * Artifact Spoilers by: randy@PICARD.tamu.edu (Randy Hutson)
 *
 * (Mostly) rewritten in 2002 by Andi Sidwell and Robert Ruehlmann.
 * ------------------------------------------------------------------------ */


/**
 * The artifacts categorized by type
 */
static const grouper group_artifact[] =
{
	{ TV_SWORD,         "Edged Weapons" },
	{ TV_POLEARM,       "Polearms" },
	{ TV_HAFTED,        "Hafted Weapons" },
	{ TV_BOW,           "Bows" },
	{ TV_DIGGING,       "Diggers" },

	{ TV_SOFT_ARMOR,    "Body Armor" },
	{ TV_HARD_ARMOR,    NULL },
	{ TV_DRAG_ARMOR,    NULL },

	{ TV_CLOAK,         "Cloaks" },
	{ TV_SHIELD,        "Shields" },
	{ TV_HELM,          "Helms/Crowns" },
	{ TV_CROWN,         NULL },
	{ TV_GLOVES,        "Gloves" },
	{ TV_BOOTS,         "Boots" },

	{ TV_LIGHT,         "Light Sources" },
	{ TV_AMULET,        "Amulets" },
	{ TV_RING,          "Rings" },

	{ 0, NULL }
};


/**
 * Create a spoiler file for artifacts
 */
void spoil_artifact(const char *fname)
{
	int i, j;
	char buf[1024];

	/* Build the filename */
	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, fname);
	fh = file_open(buf, MODE_WRITE, FTYPE_TEXT);

	/* Oops */
	if (!fh) {
		msg("Cannot create spoiler file.");
		return;
	}

	/* Dump to the spoiler file */
	text_out_hook = text_out_to_file;
	text_out_file = fh;

	/* Dump the header */
	spoiler_underline(format("Artifact Spoilers for %s", buildid), '=');

	text_out("\n Randart seed is %lu\n", (unsigned long)seed_randart);

	/* List the artifacts by tval */
	for (i = 0; group_artifact[i].tval; i++) {
		/* Write out the group title */
		if (group_artifact[i].name) {
			spoiler_blanklines(2);
			spoiler_underline(group_artifact[i].name, '=');
			spoiler_blanklines(1);
		}

		/* Now search through all of the artifacts */
		for (j = 1; j < z_info->a_max; ++j) {
			const struct artifact *art = &a_info[j];
			struct artifact artc;
			char buf2[80];
			struct object *obj, *known_obj;
			int16_t art_weight;

			/* We only want objects in the current group */
			if (art->tval != group_artifact[i].tval) continue;

			/* Get local object */
			obj = object_new();
			known_obj = object_new();

			/*
			 * Make a copy of the artifact state; hide the
			 * flavour text:  spoilers spoil the mechanics, not
			 * the atmosphere.
			 */
			memcpy(&artc, art, sizeof(artc));
			artc.text = NULL;

			/* Attempt to "forge" the artifact */
			if (!make_fake_artifact(obj, &artc)) {
				object_delete(NULL, NULL, &known_obj);
				object_delete(NULL, NULL, &obj);
				continue;
			}

			/* Grab artifact name */
			object_copy(known_obj, obj);
			obj->known = known_obj;
			object_desc(buf2, sizeof(buf2), obj, ODESC_PREFIX |
				ODESC_COMBAT | ODESC_EXTRA | ODESC_SPOIL, NULL);

			/* Print name and underline */
			spoiler_underline(buf2, '-');

			/* Write out the artifact description to the spoiler file */
			object_info_spoil(fh, obj, 80);

			/*
			 * Determine the minimum and maximum depths an
			 * artifact can appear, its rarity, its weight, and
			 * its power rating.
			 */
			art_weight = object_weight_one(obj);
			text_out("\nMin Level %u, Max Level %u, Generation chance %u, %d.%d lbs\n",
				art->alloc_min, art->alloc_max, art->alloc_prob,
				art_weight / 10, art_weight % 10);

			/* Include description for randarts, which gives generation info */
			if (j >= z_info->a_max - ART_NUM_RANDOM) {
				text_out("%s.\n", art->text);
			}

			/* Terminate the entry */
			spoiler_blanklines(2);
			object_delete(NULL, NULL, &known_obj);
			object_delete(NULL, NULL, &obj);
		}
	}

	/* Check for errors */
	if (!file_close(fh)) {
		msg("Cannot close spoiler file.");
		return;
	}

	/* Message */
	msg("Successfully created a spoiler file.");
}


/**
 * ------------------------------------------------------------------------
 * Brief monster spoilers
 * ------------------------------------------------------------------------ */
/**
 * Create a brief spoiler file for monsters
 */
void spoil_mon_desc(const char *fname)
{
	int i, n = 0;

	char buf[1024];

	char nam[80];
	char lev[80];
	char rar[80];
	char spd[80];
	char ac[80];
	char hp[80];
	char exp[80];
	char *mbbuf;

	uint16_t *who;

	/* Build the filename */
	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, fname);
	fh = file_open(buf, MODE_WRITE, FTYPE_TEXT);

	/* Oops */
	if (!fh) {
		msg("Cannot create spoiler file.");
		return;
	}

	/* Dump the header */
	file_putf(fh, "Monster Spoilers for %s\n", buildid);
	file_putf(fh, "------------------------------------------\n\n");

	/* Dump the header */
	file_putf(fh, "%-40.40s%4s%4s%6s%8s%4s  %11.11s\n",
	        "Name", "Lev", "Rar", "Spd", "Hp", "Ac", "Visual Info");
	file_putf(fh, "%-40.40s%4s%4s%6s%8s%4s  %11.11s\n",
	        "----", "---", "---", "---", "--", "--", "-----------");

	/* Allocate the "who" array */
	who = mem_zalloc(z_info->r_max * sizeof(uint16_t));

	/* Scan the monsters (except the ghost) */
	for (i = 1; i < z_info->r_max - 1; i++) {
		struct monster_race *race = &r_info[i];

		/* Use that monster */
		if (race->name) who[n++] = (uint16_t)i;
	}

	/* Sort the array by dungeon depth of monsters */
	sort(who, n, sizeof(*who), cmp_monsters);

	mbbuf = mem_alloc(text_wcsz() + 1);

	/* Scan again */
	for (i = 0; i < n; i++) {
		struct monster_race *race = &r_info[who[i]];
		const char *name = race->name;
		size_t u8len;
		int n_mbbuf;

		/* Get the "name" */
		if (quest_unique_monster_check(race))
			strnfmt(nam, sizeof(nam), "[Q] %s", name);
		else if (rf_has(race->flags, RF_UNIQUE))
			strnfmt(nam, sizeof(nam), "[U] %s", name);
		else
			strnfmt(nam, sizeof(nam), "The %s", name);

		/* Level */
		strnfmt(lev, sizeof(lev), "%d", race->level);

		/* Rarity */
		strnfmt(rar, sizeof(rar), "%d", race->rarity);

		/* Speed */
		if (race->speed >= 110)
			strnfmt(spd, sizeof(spd), "+%d", (race->speed - 110));
		else
			strnfmt(spd, sizeof(spd), "-%d", (110 - race->speed));

		/* Armor Class */
		strnfmt(ac, sizeof(ac), "%d", race->ac);

		/* Hitpoints */
		strnfmt(hp, sizeof(hp), "%d", race->avg_hp);

		/* Experience */
		strnfmt(exp, sizeof(exp), "%ld", (long)(race->mexp));

		/* Use visual instead */
		n_mbbuf = text_wctomb(mbbuf, race->d_char);
		if (n_mbbuf > 0) {
			mbbuf[n_mbbuf] = '\0';
			strnfmt(exp, sizeof(exp), "%s '%s'",
				attr_to_text(race->d_attr), mbbuf);
		} else {
			strnfmt(exp, sizeof(exp), "%s (invalid character)",
				attr_to_text(race->d_attr));
		}

		/*
		 * Dump the info.  The rationale for handling the first column
		 * separately is the same as in spoil_obj_desc():  better
		 * alignment if there are multibyte characters in the name.
		 */
		u8len = utf8_strlen(nam);
		if (u8len < 40) {
			file_putf(fh, "%s%*s", nam, (int) (40 - u8len), " ");
		} else {
			if (u8len > 40) {
				utf8_clipto(nam, 40);
			}
			file_putf(fh, "%s", nam);
		}
		file_putf(fh, "%4s%4s%6s%8s%4s  %11.11s\n",
		        lev, rar, spd, hp, ac, exp);
	}

	/* End it */
	file_putf(fh, "\n");

	mem_free(mbbuf);

	/* Free the "who" array */
	mem_free(who);

	/* Check for errors */
	if (!file_close(fh)) {
		msg("Cannot close spoiler file.");
		return;
	}

	/* Worked */
	msg("Successfully created a spoiler file.");
}




/**
 * ------------------------------------------------------------------------
 * Monster spoilers originally by: smchorse@ringer.cs.utsa.edu (Shawn McHorse)
 * ------------------------------------------------------------------------ */


/**
 * Create a spoiler file for monsters (-SHAWN-)
 */
void spoil_mon_info(const char *fname)
{
	char buf[1024];
	int i, n;
	uint16_t *who;
	int count = 0;
	textblock *tb = NULL;
	char *mbbuf;

	/* Open the file */
	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, fname);
	fh = file_open(buf, MODE_WRITE, FTYPE_TEXT);

	if (!fh) {
		msg("Cannot create spoiler file.");
		return;
	}

	/* Dump the header */
	tb = textblock_new();
	textblock_append(tb, "Monster Spoilers for %s\n", buildid);
	textblock_append(tb, "------------------------------------------\n\n");
	textblock_to_file(tb, fh, 0, 75);
	textblock_free(tb);
	tb = NULL;

	/* Allocate the "who" array */
	who = mem_zalloc(z_info->r_max * sizeof(uint16_t));

	/* Scan the monsters */
	for (i = 1; i < z_info->r_max; i++) {
		struct monster_race *race = &r_info[i];

		/* Use that monster */
		if (race->name) who[count++] = (uint16_t)i;
	}

	sort(who, count, sizeof(*who), cmp_monsters);

	mbbuf = mem_alloc(text_wcsz() + 1);

	/* List all monsters in order. */
	for (n = 0; n < count; n++) {
		int r_idx = who[n];
		const struct monster_race *race = &r_info[r_idx];
		const struct monster_lore *lore = &l_list[r_idx];
		int n_mbbuf;

		tb = textblock_new();

		/* Line 1: prefix, name, color, and symbol */
		if (quest_unique_monster_check(race))
			textblock_append(tb, "[Q] ");
		else if (rf_has(race->flags, RF_UNIQUE))
			textblock_append(tb, "[U] ");
		else
			textblock_append(tb, "The ");

		/* As of 3.5, race->name and race->text are stored as UTF-8 strings;
		 * there is no conversion from the source edit files. */
		textblock_append(tb, "%s", race->name);
		textblock_append(tb, "  (");	/* ---)--- */
		textblock_append(tb, "%s", attr_to_text(race->d_attr));
		n_mbbuf = text_wctomb(mbbuf, race->d_char);
		if (n_mbbuf > 0) {
			mbbuf[n_mbbuf] = '\0';
			textblock_append(tb, " '%s')\n", mbbuf);
		} else {
			textblock_append(tb, " (invalid character))\n");
		}

		/* Line 2: number, level, rarity, speed, HP, AC, exp */
		textblock_append(tb, "=== ");
		textblock_append(tb, "Num:%d  ", r_idx);
		textblock_append(tb, "Lev:%d  ", race->level);
		textblock_append(tb, "Rar:%d  ", race->rarity);

		if (race->speed >= 110)
			textblock_append(tb, "Spd:+%d  ", (race->speed - 110));
		else
			textblock_append(tb, "Spd:-%d  ", (110 - race->speed));

		textblock_append(tb, "Hp:%d  ", race->avg_hp);
		textblock_append(tb, "Ac:%d  ", race->ac);
		textblock_append(tb, "Exp:%ld\n", (long)(race->mexp));

		/* Normal description (with automatic line breaks) */
		lore_description(tb, race, lore, true);
		textblock_append(tb, "\n");

		textblock_to_file(tb, fh, 0, 75);
		textblock_free(tb);
		tb = NULL;
	}

	mem_free(mbbuf);

	/* Free the "who" array */
	mem_free(who);

	/* Check for errors */
	if (!file_close(fh)) {
		msg("Cannot close spoiler file.");
		return;
	}

	msg("Successfully created a spoiler file.");
}
