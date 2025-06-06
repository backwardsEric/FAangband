/**
 * \file gen-monster.c
 * \brief Dungeon monster generation
 *
 * Copyright (c) 2013
 * Nick McConnell, Leon Marrick, Ben Harrison, James E. Wilson, 
 * Robert A. Koeneke
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
 *
 * Code for selecting appropriate monsters for levels when generated.  The
 * intent is to enable easy theming of monsters in sections of the dungeon 
 * level, or even whole levels.
 */

#include "angband.h"
#include "cave.h"
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "monster.h"
#include "mon-make.h"
#include "mon-spell.h"

/**
 * Restrictions on monsters, used in pits, vaults, and chambers.  Used in
 * mon_select().
 */
static bool allow_unique;
static wchar_t base_d_chars[3]; /* always terminated with 0 */
static int select_current_level;

/**
 * Return true if the given glyph matches the restrictions by glyph.
 */
static bool matches_glyph_restrictions(wchar_t x)
{
	int i = 0;

	while (base_d_chars[i]) {
		/*
		 * If the glyph matches one of the allowed glyphs, it passes
		 * the restrictions.
		 */
		if (x == base_d_chars[i]) {
			return true;
		}
		++i;
		assert(i < (int)N_ELEMENTS(base_d_chars));
	}
	/* If there were no restrictions, then the glyph is always accepted. */
	return (i == 0);
}

/**
 * Return the pit profile matching the given name.
 * \param name the pit profile name
 * \return the pit profile
 */
static struct pit_profile *lookup_pit_profile(const char *name)
{
	struct pit_profile *profile;

	/* Look for it */
	for (profile = pit_info; profile; profile = profile->next) {
		if (profile->name && streq(name, profile->name))
			return profile;
	}

	return NULL;
}

/**
 * Hack - select beings of the four basic elements.  Used in the elemental 
 * war themed level.
 */
static bool vault_aux_elemental(struct monster_race *race)
{
	/* Demons are always welcome. */
	if (my_stristr(race->base->name, "demon"))
		return true;

	/* Certain names are a givaway. */
	if (strstr(race->name, "Fire")
		|| strstr(race->name, "Hell")
		|| strstr(race->name, "Frost")
		|| strstr(race->name, "Cold")
		|| strstr(race->name, "Acid")
		|| strstr(race->name, "Water")
		|| strstr(race->name, "Energy"))
		return true;

	/* Otherwise, try selecting by breath attacks. */
	if (rf_has(race->spell_flags, RSF_BR_ACID)
		|| rf_has(race->spell_flags, RSF_BR_ELEC)
		|| rf_has(race->spell_flags, RSF_BR_FIRE)
		|| rf_has(race->spell_flags, RSF_BR_COLD))
		return true;

	/* Nope */
	return false;
}

/**
 * This function selects monsters by monster base symbol 
 * (may be any of the characters allowed)
 * \param race the monster race being tested for suitability
 * \return true if the race is accepted
 *
 * Uniques may be forbidden, or allowed on rare occasions.
 *
 * This is a hook called as an argument to get_mon_num_prep()
 */
static bool mon_select(struct monster_race *race)
{
	/* Special case: Elemental war themed level. */
	if (player->themed_level == themed_level_index("Elemental")) {
		return (vault_aux_elemental(race));
	}

	/* Special case: Estolad themed level. */
	if (player->themed_level == themed_level_index("Estolad")) {
		if (!rf_has(race->flags, RF_PLAYER))
			return false;
	}

	/* Require that the monster symbol be correct. */
	if (!matches_glyph_restrictions(race->base->d_char))
		return (false);

	/* No invisible undead until deep. */
	if (select_current_level < 40 && rf_has(race->flags, RF_UNDEAD)
			&& rf_has(race->flags, RF_INVISIBLE))
		return (false);

	/* Usually decline unique monsters. */
	if (rf_has(race->flags, RF_UNIQUE)) {
		if (!allow_unique)
			return (false);
		else if (randint0(5) != 0)
			return (false);
	}

	/* Okay */
	return (true);
}

/**
 * Accept characters representing a race or group of monsters and 
 * an (adjusted) depth, and use these to set values for required
 * monster base symbol.
 *
 * \param monster_type the monster type to be selected, as described below
 * \param depth the native depth to choose monsters
 * \param current_depth is the depth at which the monsters will be placed
 * \param unique_ok whether to allow uniques to be chosen
 * \return success if the monster allocation table has been rebuilt
 *
 * This code has been adapted from Oangband code to make use of monster bases.
 *
 * This function is called to set restrictions, point the monster 
 * allocation function to mon_select() or mon_pit_hook(), and remake monster 
 * allocation.
 * It undoes all of this things when called with monster_type NULL.
 * If called with a pit profile name, it will get monsters from that profile.
 * If called with monster_type "random", it will get a random monster base and 
 * describe the monsters by its name (for use by cheat_room).
 */
bool mon_restrict(const char *monster_type, int depth, int current_depth,
		bool unique_ok)
{
	int i, j = 0;

	/* Clear global monster restriction variables. */
	allow_unique = unique_ok;
	base_d_chars[0] = 0;
	select_current_level = current_depth;

	/* No monster type specified, no restrictions. */
	if (monster_type == NULL) {
		get_mon_num_prep(NULL);
		return true;
	} else if (streq(monster_type, "random")) {
		/* Handle random */
		for (i = 0; i < 2500; i++) {
			/* Get a random monster. */
			j = randint1(z_info->r_max - 1);

			/* Must be a real monster */
			if (!r_info[j].rarity)
				continue;

			/* Try for close to depth, accept in-depth if necessary */
			if (i < 200) {
				if ((!rf_has(r_info[j].flags, RF_UNIQUE))
					&& (r_info[j].level != 0) && (r_info[j].level <= depth)
					&& (ABS(r_info[j].level - current_depth) <
						1 + (current_depth / 4)))
					break;
			} else {
				if ((!rf_has(r_info[j].flags, RF_UNIQUE))
					&& (r_info[j].level != 0) && (r_info[j].level <= depth))
					break;
			}
		}

		/* We've found a monster. */
		if (i < 2499) {
			/* Use that monster's base type for all monsters. */
			base_d_chars[0] = r_info[j].base->d_char;
			base_d_chars[1] = 0;

			/* Prepare allocation table */
			get_mon_num_prep(mon_select);
			return true;
		} else
			/* Paranoia - area stays empty if no monster is found */
			return false;
	} else {
		/* Use a pit profile */
		struct pit_profile *profile = lookup_pit_profile(monster_type);

		/* Accept the profile or leave area empty if none found */
		if (profile)
			dun->pit_type = profile;
		else
			return false;

		/* Prepare allocation table */
		get_mon_num_prep(mon_pit_hook);
		return true;
	}
}


/**
 * Apply any general restrictions on monsters in (vaults and) themed levels.
 */
void general_monster_restrictions(void)
{
	/* Clear global monster restriction variables. */
	allow_unique = true;
	base_d_chars[0] = 0;

	/* Most themed levels have monster restrictions. */
	if (player->themed_level == themed_level_index("Elemental")) {
		get_mon_num_prep(mon_select);
	} else if (player->themed_level == themed_level_index("Dragon")) {
		base_d_chars[0] = L'd';
		base_d_chars[1] = L'D';
		base_d_chars[2] = 0;
		get_mon_num_prep(mon_select);
	} else if (player->themed_level == themed_level_index("Wilderness")) {
		get_mon_num_prep(mon_select);
	} else if (player->themed_level == themed_level_index("Demon")) {
		get_mon_num_prep(mon_select);
	} else if (player->themed_level == themed_level_index("Mines")) {
		get_mon_num_prep(mon_select);
	} else if (player->themed_level == themed_level_index("Estolad")) {
		get_mon_num_prep(mon_select);
	}
}

/**
 * Place monsters, up to the number asked for, in a rectangle centered on 
 * y0, x0.  Accept values for monster depth, symbol, and maximum vertical 
 * and horizontal displacement.  Call monster restriction functions if 
 * needed.
 * \param c the current chunk being generated
 * \param type the type of monster (see comments to mon_restrict())
 * \param depth selection depth
 * \param num the number of monsters to try and place - inexact due to groups
 * \param y0 the centre of the rectangle for monster placement
 * \param x0 the centre of the rectangle for monster placement
 * \param dy the dimensions of the rectangle
 * \param dx the dimensions of the rectangle
 * \param origin the origin for monster drops
 *
 * Return prematurely if the code starts looping too much (this may happen 
 * if y0 or x0 are out of bounds, or the area is already occupied).
 */
void spread_monsters(struct chunk *c, const char *type, int depth, int num, 
		int y0, int x0, int dy, int dx, uint8_t origin)
{
	int i, j;
	int count;
	int y = y0, x = x0;
	int start_mon_num = c->mon_max;

	/* Restrict monsters.  Allow uniques. Leave area empty if none found. */
	if (!mon_restrict(type, depth, c->depth, true))
		return;

	/* Build the monster probability table. */
	if (!get_mon_num(depth, c->depth))
		return;


	/* Try to summon monsters within our rectangle of effect. */
	for (count = 0, i = 0; ((count < num) && (i < 50)); i++) {
		/* Get a location */
		if ((dy == 0) && (dx == 0)) {
			y = y0;
			x = x0;
			if (!square_in_bounds(c, loc(x, y))) {
				(void) mon_restrict(NULL, depth,
					c->depth, true);
				return;
			}
		} else {
			for (j = 0; j < 10; j++) {
				y = rand_spread(y0, dy);
				x = rand_spread(x0, dx);
				if (!square_in_bounds(c, loc(x, y))) {
					if (j < 9) {
						continue;
					} else {
						(void) mon_restrict(NULL, depth,
							c->depth, true);
						return;
					}
				}
				break;
			}
		}

		/* Require "empty" floor grids */
		if (!square_isempty(c, loc(x, y))) continue;

		/* Place the monster (sleeping, allow groups) */
		pick_and_place_monster(c, loc(x, y), depth, true, true, origin);

		/* Rein in monster groups and escorts a little. */
		if (c->mon_max - start_mon_num > num * 2)
			break;

		/* Count the monster(s), reset the loop count */
		count++;
		i = 0;
	}

	/* Remove monster restrictions. */
	(void) mon_restrict(NULL, depth, c->depth, true);
}


/**
 * To avoid rebuilding the monster list too often (which can quickly 
 * get expensive), we handle monsters of a specified race separately.
 *
 * \param c the current chunk being generated
 * \param racial_symbol the allowable monster_base symbols
 * \param vault_type the type of vault, which affects monster selection depth
 * \param data the vault text description, which contains the racial symbol
 * \param y1 the limits of the vault
 * \param y2 the limits of the vault
 * \param x1 the limits of the vault
 * \param x2 the limits of the vault
 */
void get_vault_monsters(struct chunk *c, char racial_symbol[], char *vault_type,
						const char *data, int y1, int y2, int x1, int x2)
{
	int i, y, x, depth;
	char stmp[2] = { '\0', '\0' };
	wchar_t wtmp[2];
	const char *t;

	for (i = 0; racial_symbol[i] != '\0'; i++) {
		/* Require correct race, allow uniques. */
		allow_unique = true;
		stmp[0] = racial_symbol[i];
		if (text_mbstowcs(wtmp, stmp, N_ELEMENTS(wtmp)) != 1) {
			/*
			 * Skip if could not convert the character to one
			 * wide character.
			 */
			continue;
		}
		base_d_chars[0] = wtmp[0];
		base_d_chars[1] = 0;
		select_current_level = c->depth;

		/* Determine level of monster */
		if (strstr(vault_type, "Lesser vault"))
			depth = c->depth + 2;
		else if (strstr(vault_type, "Medium vault"))
			depth = c->depth + 4;
		else if (strstr(vault_type, "Greater vault"))
			depth = c->depth + 6;
		else
			depth = c->depth;

		/* Prepare allocation table */
		get_mon_num_prep(mon_select);

		/* Build the monster probability table. */
		if (!get_mon_num(depth, c->depth))
			continue;


		/* Place the monsters */
		for (t = data, y = y1; y <= y2; y++) {
			for (x = x1; x <= x2; x++, t++) {
				if (*t == racial_symbol[i]) {
					/* Place a monster */
					pick_and_place_monster(c, loc(x, y), depth, false,
																false, ORIGIN_DROP_SPECIAL);
				}
			}
		}
	}

	/* Clear any current monster restrictions. */
	get_mon_num_prep(NULL);
}

/**
 * Function for placing appropriate monsters in a room of chambers
 *
 * \param c the current chunk being generated
 * \param y1 the limits of the vault
 * \param x1 the limits of the vault
 * \param y2 the limits of the vault
 * \param x2 the limits of the vault
 * \param name the name of the monster type for use in mon_restrict()
 * \param area the total room area, used for scaling monster quantity
 */
void get_chamber_monsters(struct chunk *c, int y1, int x1, int y2, int x2, 
	char *name, int area)
{
	int i, y, x;
	int16_t monsters_left, depth;
	bool random = one_in_(20);

	/* Get a legal depth. */
	depth = c->depth + randint0(11) - 5;

	/* Choose a pit profile, using that depth. */
	if (!random) {
		while (true) {
			/* Choose a pit profile */
			set_pit_type(depth, 0);

			/* Check if the pit was set correctly
			   Done currently by checking if a name was saved */
			if (dun->pit_type->name)
				break;
		}
	}

	/* Allow (slightly) tougher monsters. */
	depth = c->depth + (c->depth < 60 ? c->depth / 12 : 5);

	/* Set monster generation restrictions. Occasionally random. */
	if (random) {
		if (!mon_restrict("random", depth, c->depth, true))
			return;
		my_strcpy(name, "random", sizeof(name));
	} else {
		if (!mon_restrict(dun->pit_type->name, depth, c->depth, true))
			return;
		my_strcpy(name, dun->pit_type->name, sizeof(name));
	}

	/* Build the monster probability table. */
	if (!get_mon_num(depth, c->depth)) {
		(void) mon_restrict(NULL, depth, c->depth, false);
		name = NULL;
		return;
	}

	/* No normal monsters. */
	generate_mark(c, y1, x1, y2, x2, SQUARE_MON_RESTRICT);

	/* Allow about a monster every 20-30 grids. */
	monsters_left = area / (30 - c->depth / 10);

	/* Place the monsters. */
	for (i = 0; i < 300; i++) {
		/* Check for early completion. */
		if (!monsters_left)
			break;

		/* Pick a random in-room square. */
		y = y1 + randint0(1 + ABS(y2 - y1));
		x = x1 + randint0(1 + ABS(x2 - x1));

		/* Require a passable square with no monster in it already. */
		if (!square_isempty(c, loc(x, y)))
			continue;

		/* Place a single monster.  Sleeping 2/3rds of the time. */
		pick_and_place_monster(c, loc(x, y), c->depth, (randint0(3) != 0),
			false, ORIGIN_DROP_SPECIAL);

		/* One less monster to place. */
		monsters_left--;
	}

	/* Remove our restrictions. */
	(void) mon_restrict(NULL, depth, c->depth, false);
}

