/**
 * \file save.c
 * \brief Individual saving functions
 *
 * Copyright (c) 1997 Ben Harrison
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
#include "cave.h"
#include "game-world.h"
#include "init.h"
#include "mon-group.h"
#include "mon-lore.h"
#include "mon-make.h"
#include "monster.h"
#include "object.h"
#include "obj-desc.h"
#include "obj-design.h"
#include "obj-knowledge.h"
#include "obj-pile.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "option.h"
#include "player-quest.h"
#include "player.h"
#include "savefile.h"
#include "store.h"
#include "obj-util.h"
#include "player-history.h"
#include "player-timed.h"
#include "trap.h"
#include "ui-term.h"


/**
 * Write a description of the character
 */
void wr_description(void)
{
	char buf[1024];

	if (player->is_dead)
		strnfmt(buf, sizeof buf, "%s, dead (%s)",
				player->full_name,
				player->died_from);
	else
		strnfmt(buf, sizeof buf, "%s, L%d %s %s, at DL%d",
				player->full_name,
				player->lev,
				player->race->name,
				player->class->name,
				player->depth);

	wr_string(buf);
}


/**
 * Write an "item" record
 */
static void wr_item(const struct object *obj)
{
	size_t i;

	wr_u16b(0xffff);
	wr_byte(ITEM_VERSION);

	wr_u16b(obj->oidx);

	/* Location */
	wr_byte(obj->grid.y);
	wr_byte(obj->grid.x);

	/* Names of object base and object */
	wr_string(tval_find_name(obj->tval));
	if (obj->sval) {
		char name[1024];
		obj_desc_name_format(name, sizeof name, 0,
							 lookup_kind(obj->tval, obj->sval)->name, 0, false);
		wr_string(name);
	} else {
		wr_string("");
	}

	wr_s16b(obj->pval);

	wr_byte(obj->number);
	wr_s16b(obj->weight);

	if (obj->artifact) {
		wr_string(obj->artifact->name);
	} else {
		wr_string("");
	}

	if (obj->ego) {
		wr_string(obj->ego->name);
	} else {
		wr_string("");
	}

	if (obj->effect)
		wr_byte(1);
	else
		wr_byte(0);

	wr_s16b(obj->timeout);

	wr_s16b(obj->to_h);
	wr_s16b(obj->to_d);
	wr_s16b(obj->to_a);
	wr_s16b(obj->ac);
	wr_byte(obj->dd);
	wr_byte(obj->ds);

	wr_byte(obj->origin);
	wr_byte(obj->origin_depth);
	wr_s16b(obj->origin_place);
	if (obj->origin_race) {
		wr_string(obj->origin_race->name);
	} else {
		wr_string("");
	}
	wr_byte(obj->notice);

	for (i = 0; i < OF_SIZE; i++)
		wr_byte(obj->flags[i]);

	for (i = 0; i < OBJ_MOD_MAX; i++) {
		wr_s16b(obj->modifiers[i]);
	}

	/* Write brands if any */
	if (obj->brands) {
		wr_byte(1);
		for (i = 0; i < z_info->brand_max; i++) {
			wr_byte(obj->brands[i] ? 1 : 0);
		}
	} else {
		wr_byte(0);
	}

	/* Write slays if any */
	if (obj->slays) {
		wr_byte(1);
		for (i = 0; i < z_info->slay_max; i++) {
			wr_byte(obj->slays[i] ? 1 : 0);
		}
	} else {
		wr_byte(0);
	}

	/* Write curses if any */
	if (obj->curses) {
		wr_byte(1);
		for (i = 0; i < z_info->curse_max; i++) {
			wr_byte(obj->curses[i].power);
			wr_u16b(obj->curses[i].timeout);
		}
	} else {
		wr_byte(0);
	}

	for (i = 0; i < ELEM_MAX; i++) {
		wr_s16b(obj->el_info[i].res_level);
		wr_byte(obj->el_info[i].flags);
	}

	/* Held by monster index */
	wr_s16b(obj->held_m_idx);
	
	wr_s16b(obj->mimicking_m_idx);

	/* Activation and effects*/
	if (obj->activation)
		wr_u16b(obj->activation->index);
	else
		wr_u16b(0);
	wr_u16b(obj->time.base);
	wr_u16b(obj->time.dice);
	wr_u16b(obj->time.sides);

	/* Save the inscription (if any) */
	if (obj->note)
		wr_string(quark_str(obj->note));
	else
		wr_string("");
}


/**
 * Write a monster record (including held or mimicked objects)
 */
static void wr_monster(const struct monster *mon)
{
	size_t j;
	struct object *obj = mon->held_obj; 
	struct object *dummy = object_new();

	wr_u16b(mon->midx);
	wr_string(mon->race->name);
	if (mon->original_race) {
		wr_string(mon->original_race->name);
	} else {
		wr_string("none");
	}
	if (mon->player_race) {
		wr_string(mon->player_race->name);
	} else {
		wr_string("none");
	}
	if (mon->original_player_race) {
		wr_string(mon->original_player_race->name);
	} else {
		wr_string("none");
	}
	wr_byte(mon->grid.y);
	wr_byte(mon->grid.x);
	wr_s16b(mon->hp);
	wr_s16b(mon->maxhp);
	wr_byte(mon->mspeed);
	wr_byte(mon->energy);
	wr_byte(mon->target.grid.y);
	wr_byte(mon->target.grid.x);
	wr_s16b(mon->target.midx);
	wr_byte(mon->home.y);
	wr_byte(mon->home.x);
	wr_byte(MON_TMD_MAX);

	for (j = 0; j < MON_TMD_MAX; j++)
		wr_s16b(mon->m_timed[j]);

	for (j = 0; j < MFLAG_SIZE; j++)
		wr_byte(mon->mflag[j]);

	for (j = 0; j < OF_SIZE; j++)
		wr_byte(mon->known_pstate.flags[j]);

	for (j = 0; j < ELEM_MAX; j++)
		wr_s16b(mon->known_pstate.el_info[j].res_level);

	/* Write mimicked object marker, if any */
	if (mon->mimicked_obj) {
		wr_u16b(mon->midx);
	} else
		wr_u16b(0);

	/* Write all held objects, followed by a dummy as a marker */
	while (obj) {
		wr_item(obj);
		obj = obj->next;
	}
	wr_item(dummy);
	object_delete(NULL, NULL, &dummy);

	/* Write group info */
	wr_u16b(mon->group_info[PRIMARY_GROUP].index);
	wr_byte(mon->group_info[PRIMARY_GROUP].role);
	wr_u16b(mon->group_info[SUMMON_GROUP].index);
	wr_byte(mon->group_info[SUMMON_GROUP].role);
}

/**
 * Write a trap record
 */
static void wr_trap(struct trap *trap)
{
	size_t i;

	if (trap->t_idx) {
		wr_string(trap_info[trap->t_idx].desc);
	} else {
		wr_string("");
	}
	wr_byte(trap->grid.y);
	wr_byte(trap->grid.x);
	wr_byte(trap->power);
	wr_byte(trap->timeout);

	for (i = 0; i < TRF_SIZE; i++)
		wr_byte(trap->flags[i]);
}

/**
 * Write RNG state
 *
 * There were originally 64 bytes of randomizer saved. Now we only need
 * 32 + 5 bytes saved, so we'll write an extra 27 bytes at the end which won't
 * be used.
 */
void wr_randomizer(void)
{
	int i;

	/* current value for the simple RNG */
	wr_u32b(Rand_value);

	/* state index */
	wr_u32b(state_i);

	/* RNG variables */
	wr_u32b(z0);
	wr_u32b(z1);
	wr_u32b(z2);

	/* RNG state */
	for (i = 0; i < RAND_DEG; i++)
		wr_u32b(STATE[i]);

	/* NULL padding */
	for (i = 0; i < 59 - RAND_DEG; i++)
		wr_u32b(0);
}


/**
 * Write the "options"
 */
void wr_options(void)
{
	int i;

	/* Special Options */
	wr_byte(player->opts.delay_factor);
	wr_byte(player->opts.hitpoint_warn);
	wr_byte(player->opts.lazymove_delay);
	/* Fix for tests - only write if angband_term exists, ie in a real game */
	wr_byte(angband_term[0] ? SIDEBAR_MODE : 0);

	/* Normal options */
	for (i = 0; i < OPT_MAX; i++) {
		const char *name = option_name(i);
		if (name) {
			wr_string(name);
			wr_byte(player->opts.opt[i]);
		}
   }

	/* Sentinel */
	wr_byte(0);
}


void wr_messages(void)
{
	int16_t i;
	uint16_t num;

	num = messages_num();
	if (num > 80) num = 80;
	wr_u16b(num);

	/* Dump the messages (oldest first!) */
	for (i = num - 1; i >= 0; i--) {
		wr_string(message_str(i));
		wr_u16b(message_type(i));
	}
}


void wr_monster_memory(void)
{
	int r_idx;

	wr_byte(MFLAG_SIZE);

	for (r_idx = 0; r_idx < z_info->r_max; r_idx++) {
		struct monster_race *race = &r_info[r_idx];
		struct monster_lore *lore = &l_list[r_idx];

		/* Names and kill counts */
		if (!race->name || !(lore->pkills || lore->thefts)) continue;
		wr_string(race->name);
		wr_u16b(lore->pkills);
		wr_u16b(lore->thefts);
	}
	wr_string("No more monsters");
}



void wr_object_memory(void)
{
	int k_idx;

	wr_u16b(z_info->k_max);
	wr_byte(OF_SIZE);
	wr_byte(OBJ_MOD_MAX);
	wr_byte(ELEM_MAX);
	wr_byte(z_info->brand_max);
	wr_byte(z_info->slay_max);
	wr_byte(z_info->curse_max);

	/* Kind knowledge */
	for (k_idx = 0; k_idx < z_info->k_max; k_idx++) {
		uint8_t tmp8u = 0;
		struct object_kind *kind = &k_info[k_idx];

		if (kind->aware) tmp8u |= 0x01;
		if (kind->tried) tmp8u |= 0x02;
		if (kind_is_ignored_aware(kind)) tmp8u |= 0x04;
		if (kind->everseen) tmp8u |= 0x08;
		if (kind_is_ignored_unaware(kind)) tmp8u |= 0x10;

		wr_byte(tmp8u);
	}
}


void wr_quests(void)
{
	struct quest *quest = quests;
	bool extra = false;

	/* Dump the quest results */
	while (quest) {
		struct quest_place *place = quest->place;
		wr_string(quest->name);
		wr_u16b(quest->complete ? 1 : 0);
		wr_u16b(quest->cur_num);

		/* Note final quest */
		if (quest->type == QUEST_FINAL) {
			extra = true;
			quest = quest->next;
			continue;
		}
		if (!extra) {
			quest = quest->next;
			continue;
		}

		/* Extra quests */
		wr_u16b(quest->type);
		while (place) {
			wr_string(place->map->name);
			wr_u16b(place->place);
			wr_u16b(place->block ? 1 : 0);
			place = place->next;
		}
		wr_string("No more places");
		if (quest->race->name) {
			wr_string(quest->race->name);
		} else {
			wr_string("none");
		}
		wr_u16b(quest->max_num);
		quest = quest->next;
	}
	wr_string("No more quests");
}


void wr_player(void)
{
	size_t i;

	wr_string(player->full_name);

	wr_string(player->died_from);

	wr_string(player->history);

	/* Race/Class/Gender/Spells */
	wr_string(player->race->name);
	wr_string(player->shape->name);
	wr_string(player->class->name);
	wr_byte(player->opts.name_suffix);

	wr_byte(player->hitdie);

	wr_s16b(player->age);
	wr_s16b(player->ht);
	wr_s16b(player->wt);

	/* Dump the stats (maximum and current and birth and swap-mapping) */
	wr_byte(STAT_MAX);
	for (i = 0; i < STAT_MAX; ++i) wr_s16b(player->stat_max[i]);
	for (i = 0; i < STAT_MAX; ++i) wr_s16b(player->stat_cur[i]);
	for (i = 0; i < STAT_MAX; ++i) wr_s16b(player->stat_map[i]);
	for (i = 0; i < STAT_MAX; ++i) wr_s16b(player->stat_birth[i]);

	wr_s16b(player->ht_birth);
	wr_s16b(player->wt_birth);
	wr_byte(player->old_grid.y);
	wr_byte(player->old_grid.x);
	wr_u32b(player->au_birth);

	/* Player body */
	wr_string(player->body.name);
	wr_u16b(player->body.count);
	for (i = 0; i < player->body.count; i++) {
		wr_u16b(player->body.slots[i].type);
		wr_string(player->body.slots[i].name);
	}

	wr_s16b(player->themed_level_appeared);
	wr_byte(player->themed_level);
	wr_byte(player->num_traps);

	wr_u32b(player->au);


	wr_u32b(player->max_exp);
	wr_u32b(player->exp);
	wr_u16b(player->exp_frac);
	wr_s16b(player->lev);

	wr_s16b(player->mhp);
	wr_s16b(player->chp);
	wr_u16b(player->chp_frac);

	wr_s16b(player->msp);
	wr_s16b(player->csp);
	wr_u16b(player->csp_frac);

	/* Max Player and Dungeon Levels */
	wr_s16b(player->max_lev);
	wr_s16b(player->max_depth);
	wr_s16b(player->home);

	/* Location info */
	for (i = 0; i < 4; i++) {
		wr_s16b(player->recall[i]);
	}
	wr_s16b(player->recall_pt);
	wr_s16b(player->place);
	wr_s16b(player->last_place);

	/* More info */
	wr_byte(player->unignoring);
	wr_s16b(player->deep_descent);

	wr_s16b(player->energy);
	wr_s16b(player->word_recall);

	/* Find the number of timed effects */
	wr_byte(TMD_MAX);

	/* Read all the effects, in a loop */
	for (i = 0; i < TMD_MAX; i++)
		wr_s16b(player->timed[i]);

	/* Total energy used so far */
	wr_u32b(player->total_energy);
	/* # of turns spent resting */
	wr_u32b(player->resting_turn);

	/* Learned specialties (assumes < 128 entries in list-player-flags.h) */
	for (i = 0; i < 16; i++) {
		if (i < PF_SIZE) {
			wr_byte(player->specialties[i]);
		} else {
			wr_byte(0L);
		}
	}

	/* More specialty info */
	wr_s16b(player->speed_boost);		/* Specialty Fury, Phasewalk */
	wr_s16b(player->heighten_power);	/* Specialty Heighten Magic */
	wr_byte(player->skip_cmd_coercion);
	wr_byte(0);
	wr_byte(0);
	wr_byte(0);

	/* Future use */
	for (i = 0; i < 2; i++) wr_u32b(0L);
}


void wr_ignore(void)
{
	size_t i;
	uint16_t j, k, n;
	int nrune;

	/* Write number of ignore bytes */
	assert(ignore_size <= 255);
	wr_byte((uint8_t)ignore_size);
	for (i = 0; i < ignore_size; i++)
		wr_byte(ignore_level[i]);

	/* Write ego-item ignore bits */
	wr_u16b(z_info->e_max);
	wr_u16b(ITYPE_SIZE);
	for (i = 0; i < z_info->e_max; i++) {
		bitflag everseen = 0, itypes[ITYPE_SIZE];

		/* Figure out and write the everseen flag */
		if (e_info[i].everseen)
			everseen |= 0x02;
		wr_byte(everseen);

		/* Figure out and write the ignore flags */
		itype_wipe(itypes);
		for (j = ITYPE_NONE; j < ITYPE_MAX; j++)
			if (ego_is_ignored(i, j))
				itype_on(itypes, j);

		for (j = 0; j < ITYPE_SIZE; j++)
			wr_byte(itypes[j]);
	}

	/* Write the current number of aware object auto-inscriptions */
	n = 0;
	for (i = 0; i < z_info->k_max; i++)
		if (k_info[i].note_aware)
			n++;

	wr_u16b(n);

	/* Write the aware object autoinscriptions array */
	for (i = 0; i < z_info->k_max; i++) {
		if (k_info[i].note_aware) {
			char name[1024];
			wr_string(tval_find_name(k_info[i].tval));
			obj_desc_name_format(name, sizeof name, 0, k_info[i].name, 0,
								 false);
			wr_string(name);
			wr_string(quark_str(k_info[i].note_aware));
		}
	}

	/* Write the current number of unaware object auto-inscriptions */
	n = 0;
	for (i = 0; i < z_info->k_max; i++)
		if (k_info[i].note_unaware)
			n++;

	wr_u16b(n);

	/* Write the unaware object autoinscriptions array */
	for (i = 0; i < z_info->k_max; i++) {
		if (k_info[i].note_unaware) {
			char name[1024];
			wr_string(tval_find_name(k_info[i].tval));
			obj_desc_name_format(name, sizeof name, 0, k_info[i].name, 0,
								 false);
			wr_string(name);
			wr_string(quark_str(k_info[i].note_unaware));
		}
	}

	/* Write the current number of rune auto-inscriptions */
	j = 0;
	nrune = max_runes();
	assert(nrune >= 0 && nrune <= 65535);
	n = (uint16_t) MAX(0, MIN(65535, nrune));
	for (k = 0; k < n; k++)
		if (rune_note(k))
			j++;

	wr_u16b(j);

	/* Write the rune autoinscriptions array */
	for (k = 0; k < n; k++) {
		if (rune_note(k)) {
			wr_s16b(k);
			wr_string(quark_str(rune_note(k)));
		}
	}

	return;
}


void wr_misc(void)
{
	size_t i;
	int j;

	/* Map */
	wr_string(world->name);

	/* Where we've been */
	for (j = 0; j < world->num_levels; j++) {
		wr_byte(world->levels[j].visited ? 1 : 0);
	}

	/* Random artifact seed */
	wr_u32b(seed_randart);

	/* Write the "object seeds" */
	wr_u32b(seed_flavor);

	/* Special stuff */
	wr_u16b(player->total_winner);
	wr_u16b(player->noscore);

	/* Write death */
	wr_byte(player->is_dead);

	/* Current turn */
	wr_s32b(turn);

	/* Property knowledge */
	/* Flags */
	for (i = 0; i < OF_SIZE; i++)
		wr_byte(player->obj_k->flags[i]);

	/* Modifiers */
	for (i = 0; i < OBJ_MOD_MAX; i++) {
		wr_s16b(player->obj_k->modifiers[i]);
	}

	/* Elements */
	for (i = 0; i < ELEM_MAX; i++) {
		wr_s16b(player->obj_k->el_info[i].res_level);
		wr_byte(player->obj_k->el_info[i].flags);
	}

	/* Brands */
	for (i = 0; i < z_info->brand_max; i++) {
		wr_byte(player->obj_k->brands[i] ? 1 : 0);
	}

	/* Slays */
	for (i = 0; i < z_info->slay_max; i++) {
		wr_byte(player->obj_k->slays[i] ? 1 : 0);
	}

	/* Curses */
	for (i = 0; i < z_info->curse_max; i++) {
		wr_byte(player->obj_k->curses[i].power ? 1 : 0);
	}

	/* Combat data */
	wr_s16b(player->obj_k->ac);
	wr_s16b(player->obj_k->to_a);
	wr_s16b(player->obj_k->to_h);
	wr_s16b(player->obj_k->to_d);
	wr_byte(player->obj_k->dd);
	wr_byte(player->obj_k->ds);
}


void wr_artifacts(void)
{
	int i;
	uint16_t tmp16u;

	/* Dump the artifacts */
	tmp16u = z_info->a_max;
	wr_u16b(tmp16u);
	for (i = 0; i < tmp16u; i++) {
		const struct artifact_upkeep *au = &aup_info[i];
		wr_byte(au->created ? 1 : 0);
		wr_byte(au->seen ? 1 : 0);
		wr_byte(au->everseen ? 1 : 0);
		wr_byte(0);
	}
}


void wr_player_hp(void)
{
	int i;

	wr_u16b(PY_MAX_LEVEL);
	for (i = 0; i < PY_MAX_LEVEL; i++)
		wr_s16b(player->player_hp[i]);
}


void wr_player_spells(void)
{
	int i;

	wr_u16b(player->class->magic.total_spells);

	for (i = 0; i < player->class->magic.total_spells; i++)
		wr_byte(player->spell_flags[i]);

	for (i = 0; i < player->class->magic.total_spells; i++)
		wr_byte(player->spell_order[i]);
}

static void wr_gear_aux(struct object *gear)
{
	struct object *obj;

	/* Write the inventory */
	for (obj = gear; obj; obj = obj->next) {
		/* Skip non-objects */
		assert(obj->kind);

		/* Write code for equipment or other gear */
		wr_byte(object_slot(player->body, obj));

		/* Dump object */
		wr_item(obj);

	}

	/* Write finished code */
	wr_byte(FINISHED_CODE);
}


void wr_gear(void)
{
	wr_gear_aux(player->gear);
	wr_gear_aux(player->gear_k);
}


void wr_stores(void)
{
	int i;

	wr_u16b(z_info->store_max);
	for (i = 0; i < world->num_towns; i++) {
		struct town *town = &world->towns[i];
		struct store *store = town->stores;
		while (store) {
			struct object *obj;

			/* Save the current owner */
			if (store_is_home(store))
				wr_byte(-1);
			else
				wr_byte(store->owner->oidx);

			/* Save the stock size */
			wr_byte(store->stock_num);

			/* Save the stock */
			for (obj = store->stock; obj; obj = obj->next) {
				wr_item(obj->known);
				wr_item(obj);
			}
			store = store->next;
		}
	}
}



/**
 * Write the current dungeon terrain features and info flags
 *
 * Note that the cost and when fields of c->squares[y][x] are not saved
 */
static void wr_dungeon_aux(struct chunk *c)
{
	int y, x;
	size_t i;

	uint8_t tmp8u;

	uint8_t count;
	uint8_t prev_char;

	/* Dungeon specific info follows */
	wr_string(c->name ? c->name : "Blank");
	wr_u16b(c->height);
	wr_u16b(c->width);

	/* Run length encoding of c->squares[y][x].info */
	for (i = 0; i < SQUARE_SIZE; i++) {
		count = 0;
		prev_char = 0;

		/* Dump for each grid */
		for (y = 0; y < c->height; y++) {
			for (x = 0; x < c->width; x++) {
				/* Extract the important c->squares[y][x].info flags */
				tmp8u = square(c, loc(x, y))->info[i];

				/* If the run is broken, or too full, flush it */
				if ((tmp8u != prev_char) || (count == UCHAR_MAX)) {
					wr_byte(count);
					wr_byte(prev_char);
					prev_char = tmp8u;
					count = 1;
				} else /* Continue the run */
					count++;
			}
		}

		/* Flush the data (if any) */
		if (count) {
			wr_byte(count);
			wr_byte(prev_char);
		}
	}

	/* Now the terrain */
	count = 0;
	prev_char = 0;

	/* Dump for each grid */
	for (y = 0; y < c->height; y++) {
		for (x = 0; x < c->width; x++) {
			/* Extract a byte */
			tmp8u = square(c, loc(x, y))->feat;

			/* If the run is broken, or too full, flush it */
			if ((tmp8u != prev_char) || (count == UCHAR_MAX)) {
				wr_byte(count);
				wr_byte(prev_char);
				prev_char = tmp8u;
				count = 1;
			} else /* Continue the run */
				count++;
		}
	}

	/* Flush the data (if any) */
	if (count) {
		wr_byte(count);
		wr_byte(prev_char);
	}

	/* Write feeling */
	wr_byte(c->feeling);
	wr_u16b(c->feeling_squares);
	wr_s32b(c->turn);

	/* Write bones selector */
	wr_byte(c->ghost->bones_selector);

	/* Write connector info */
	if (OPT(player, birth_levels_persist)) {
		if (c->join) {
			struct connector *current = c->join;
			while (current) {
				wr_byte(current->grid.x);
				wr_byte(current->grid.y);
				wr_byte(current->feat);
				for (i = 0; i < SQUARE_SIZE; i++) {
					wr_byte(current->info[i]);
				}
				current = current->next;
			}
		}

		/* Write a sentinel byte */
		wr_byte(0xff);
	}
}

/**
 * Write the dungeon floor objects
 */
static void wr_objects_aux(struct chunk *c)
{
	int y, x, i;
	struct object *dummy;

	if (player->is_dead)
		return;
	
	/* Write the objects */
	wr_u16b(c->obj_max);
	for (y = 0; y < c->height; y++) {
		for (x = 0; x < c->width; x++) {
			struct object *obj = square(c, loc(x, y))->obj;
			while (obj) {
				wr_item(obj);
				obj = obj->next;
			}
		}
	}

	/* Write known objects we don't know the location of, and imagined versions
	 * of known objects */
	for (i = 1; i < c->obj_max; i++) {
		struct object *obj = c->objects[i];
		if (!obj) continue;
		if (square_in_bounds_fully(c, obj->grid)) continue;
		if (obj->held_m_idx) continue;
		if (obj->mimicking_m_idx) continue;
		if (obj->known && !(obj->known->notice & OBJ_NOTICE_IMAGINED)) continue;
		assert(obj->oidx == i);
		wr_item(obj);
	}

	/* Write a dummy record as a marker */
	dummy = mem_zalloc(sizeof(*dummy));
	wr_item(dummy);
	mem_free(dummy);
}

/**
 * Write the monster list
 */
static void wr_monsters_aux(struct chunk *c)
{
	int i;

	if (player->is_dead)
		return;

	/* Total monsters */
	wr_u16b(cave_monster_max(c));

	/* Dump the monsters */
	for (i = 1; i < cave_monster_max(c); i++) {
		const struct monster *mon = cave_monster(c, i);

		wr_monster(mon);
	}
}

static void wr_traps_aux(struct chunk *c)
{
    int x, y;
	struct trap *dummy;

    if (player->is_dead)
		return;

    wr_byte(TRF_SIZE);

	for (y = 0; y < c->height; y++) {
		for (x = 0; x < c->width; x++) {
			struct trap *trap = square(c, loc(x, y))->trap;
			while (trap) {
				wr_trap(trap);
				trap = trap->next;
			}
		}
	}

	/* Write a dummy record as a marker */
	dummy = mem_zalloc(sizeof(*dummy));
	wr_trap(dummy);
	mem_free(dummy);
}

void wr_dungeon(void)
{
	/* Dungeon specific info follows */
	wr_u16b(player->depth);
	wr_u16b(daycount);
	wr_u16b(player->grid.y);
	wr_u16b(player->grid.x);
	wr_byte(SQUARE_SIZE);

	if (player->is_dead)
		return;

	/* Write caves */
	wr_dungeon_aux(cave);
	wr_dungeon_aux(player->cave);

	/* Compact the monsters */
	compact_monsters(cave, 0);
}


void wr_objects(void)
{
	wr_objects_aux(cave);
	wr_objects_aux(player->cave);
}

void wr_monsters(void)
{
	wr_monsters_aux(cave);
	wr_monsters_aux(player->cave);
}

void wr_traps(void)
{
	wr_traps_aux(cave);
	wr_traps_aux(player->cave);
}

/*
 * Write the chunk list
 */
void wr_chunks(void)
{
	int j;

	if (player->is_dead)
		return;

	wr_u16b(chunk_list_max);

	/* Now write each chunk */
	for (j = 0; j < chunk_list_max; j++) {
		struct chunk *c = chunk_list[j];

		/* Write the terrain and info */
		wr_dungeon_aux(c);

		/* Write the objects */
		wr_objects_aux(c);

		/* Write the monsters */
		wr_monsters_aux(c);

		/* Write the traps */
		wr_traps_aux(c);

		/* Write other chunk info */
		if (OPT(player, birth_levels_persist)) {
			int i;

			wr_string(c->name);
			wr_s32b(c->turn);
			wr_u16b(c->depth);
			wr_byte(c->feeling);
			wr_u32b(c->obj_rating);
			wr_u32b(c->mon_rating);
			wr_byte(c->good_item ? 1 : 0);
			wr_u16b(c->height);
			wr_u16b(c->width);
			wr_u16b(c->feeling_squares);
			for (i = 0; i < z_info->f_max + 1; i++) {
				wr_u16b(c->feat_count[i]);
			}
			wr_byte(c->ghost->bones_selector);
		}
	}
}


void wr_history(void)
{
	size_t i, j;

	struct history_info *history_list;
	uint32_t length = history_get_list(player, &history_list);

	wr_byte(HIST_SIZE);
	wr_u32b(length);
	for (i = 0; i < length; i++) {
		for (j = 0; j < HIST_SIZE; j++)
			wr_byte(history_list[i].type[j]);
		wr_s32b(history_list[i].turn);
		wr_s16b(history_list[i].place);
		wr_s16b(history_list[i].clev);
		if (history_list[i].a_idx) {
			wr_string(a_info[history_list[i].a_idx].name);
		} else {
			wr_string("");
		}
		wr_string(history_list[i].event);
	}
}
