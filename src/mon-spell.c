/**
 * \file mon-spell.c
 * \brief Monster spell casting and selection
 *
 * Copyright (c) 2010-14 Chris Carr and Nick McConnell
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
#include "effects.h"
#include "init.h"
#include "mon-attack.h"
#include "mon-desc.h"
#include "mon-lore.h"
#include "mon-make.h"
#include "mon-predicate.h"
#include "mon-spell.h"
#include "mon-timed.h"
#include "mon-util.h"
#include "obj-knowledge.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"

/**
 * ------------------------------------------------------------------------
 * Spell casting
 * ------------------------------------------------------------------------ */
typedef enum {
	SPELL_TAG_NONE,
	SPELL_TAG_NAME,
	SPELL_TAG_PRONOUN,
	SPELL_TAG_TARGET,
	SPELL_TAG_TYPE,
	SPELL_TAG_OF_TYPE
} spell_tag_t;

static spell_tag_t spell_tag_lookup(const char *tag)
{
	if (strncmp(tag, "name", 4) == 0)
		return SPELL_TAG_NAME;
	else if (strncmp(tag, "pronoun", 7) == 0)
		return SPELL_TAG_PRONOUN;
	else if (strncmp(tag, "target", 6) == 0)
		return SPELL_TAG_TARGET;
	else if (strncmp(tag, "type", 4) == 0)
		return SPELL_TAG_TYPE;
	else if (strncmp(tag, "oftype", 6) == 0)
		return SPELL_TAG_OF_TYPE;
	else
		return SPELL_TAG_NONE;
}

/**
 * Lookup a race-specific message for a spell.
 *
 * \param r is the race.
 * \param s_idx is the spell index.
 * \param msg_type is the type of message.
 * \return the text of the message if there's a race-specific one or NULL if
 * there is not.
 */
static const char *find_alternate_spell_message(const struct monster_race *r,
		int s_idx, enum monster_altmsg_type msg_type)
{
	const struct monster_altmsg *am = r->spell_msgs;

	while (1) {
		if (!am) {
			return NULL;
		}
		if (am->index == s_idx && am->msg_type == msg_type) {
			 return am->message;
		}
		am = am->next;
	}
}

/**
 * Print a monster spell message.
 *
 * We fill in the monster name and/or pronoun where necessary in
 * the message to replace instances of {name} or {pronoun}.
 */
static void spell_message(struct monster *mon,
						  const struct monster_spell *spell,
						  bool seen, bool hits)
{
	const char punct[] = ".!?;:,'";
	char buf[1024] = "\0";
	const char *next;
	const char *s;
	const char *tag;
	const char *in_cursor;
	size_t end = 0;
	struct monster_spell_level *level = spell->level;
	struct monster *t_mon = NULL;
	bool is_leading;

	/* Get the right level of message */
	while (level->next && mon->race->spell_power >= level->next->power) {
		level = level->next;
	}

	/* Get the target monster, if any */
	if (mon->target.midx > 0) {
		t_mon = cave_monster(cave, mon->target.midx);
	}

	/* Get the message */
	if (!seen) {
		if (t_mon) {
			return;
		} else {
			in_cursor = find_alternate_spell_message(mon->race,
				spell->index, MON_ALTMSG_UNSEEN);
			if (in_cursor == NULL) {
				in_cursor = level->blind_message;
				if (in_cursor == NULL) {
					msg("No message-invis for monster "
						"spell %d cast by %s.  "
						"Please report this bug.",
						(int)spell->index,
						mon->race->name);
					return;
				}
			} else if (in_cursor[0] == '\0') {
				return;
			}
		}
	} else if (!hits) {
		in_cursor = find_alternate_spell_message(mon->race,
			spell->index, MON_ALTMSG_MISS);
		if (in_cursor == NULL) {
			in_cursor = level->miss_message;
			if (in_cursor == NULL) {
				msg("No message-miss for monster spell %d "
					"cast by %s.  Please report this bug.",
					(int)spell->index, mon->race->name);
				return;
			}
		} else if (in_cursor[0] == '\0') {
			return;
		}
	} else {
		in_cursor = find_alternate_spell_message(mon->race,
			spell->index, MON_ALTMSG_SEEN);
		if (in_cursor == NULL) {
			in_cursor = level->message;
			if (in_cursor == NULL) {
				msg("No message-vis for monster spell %d "
					"cast by %s.  Please report this bug.",
					(int)spell->index, mon->race->name);
				return;
			}
		} else if (in_cursor[0] == '\0') {
			return;
		}
	}

	next = strchr(in_cursor, '{');
	is_leading = (next == in_cursor);
	while (next) {
		/* Copy the text leading up to this { */
		strnfcat(buf, 1024, &end, "%.*s", (int) (next - in_cursor),
			in_cursor);

		s = next + 1;
		while (*s && isalpha((unsigned char) *s)) s++;

		/* Valid tag */
		if (*s == '}') {
			/* Start the tag after the { */
			tag = next + 1;
			in_cursor = s + 1;

			switch (spell_tag_lookup(tag)) {
				case SPELL_TAG_NAME: {
					char m_name[80];
					int mdesc_mode = (MDESC_IND_HID |
						MDESC_PRO_HID);

					if (is_leading) {
						mdesc_mode |= MDESC_CAPITAL;
					}
					if (!strchr(punct, *in_cursor)) {
						mdesc_mode |= MDESC_COMMA;
					}
					monster_desc(m_name, sizeof(m_name),
						mon, mdesc_mode);

					strnfcat(buf, sizeof(buf), &end, "%s",
						m_name);
					break;
				}

				case SPELL_TAG_PRONOUN: {
					char m_poss[80];

					/* Get the monster possessive ("his"/"her"/"its") */
					monster_desc(m_poss, sizeof(m_poss), mon, MDESC_PRO_VIS | MDESC_POSS);

					strnfcat(buf, sizeof(buf), &end, "%s",
						m_poss);
					break;
				}

				case SPELL_TAG_TARGET: {
					char m_name[80];
					if (mon->target.midx > 0) {
						int mdesc_mode = MDESC_TARG;

						if (!strchr(punct, *in_cursor)) {
							mdesc_mode |= MDESC_COMMA;
						}
						monster_desc(m_name,
							sizeof(m_name), t_mon,
							mdesc_mode);
						strnfcat(buf, sizeof(buf), &end,
							"%s", m_name);
					} else {
						strnfcat(buf, sizeof(buf), &end, "you");
					}
					break;
				}

				case SPELL_TAG_TYPE: {
					/* Get the attack type (assuming lash) */
					int type = mon->race->blow[0].effect->lash_type;
					char *type_name = projections[type].lash_desc;

					strnfcat(buf, sizeof(buf), &end, "%s",
						type_name);
					break;
				}

				case SPELL_TAG_OF_TYPE: {
					/* Get the attack type (assuming lash) */
					int type = mon->race->blow[0].effect->lash_type;
					char *type_name = projections[type].lash_desc;

					if (type_name) {
						strnfcat(buf, sizeof(buf), &end, " of ");
						strnfcat(buf, sizeof(buf), &end,
							"%s", type_name);
					}
					break;
				}

				default: {
					break;
				}
			}
		} else {
			/* An invalid tag, skip it */
			in_cursor = next + 1;
		}

		next = strchr(in_cursor, '{');
		is_leading = false;
	}
	strnfcat(buf, 1024, &end, "%s", in_cursor);

	msgt(spell->msgt, "%s", buf);
}

const struct monster_spell *monster_spell_by_index(int index)
{
	const struct monster_spell *spell = monster_spells;
	while (spell) {
		if (spell->index == index)
			break;
		spell = spell->next;
	}
	return spell;
}

/**
 * Check if a spell effect which has been saved against would also have
 * been prevented by an object property, and learn the appropriate rune
 */
static void spell_check_for_fail_rune(const struct monster_spell *spell)
{
	struct effect *effect = spell->effect;
	while (effect) {
		if (effect->index == EF_TELEPORT_LEVEL) {
			/* Special case - teleport level */
			equip_learn_element(player, ELEM_NEXUS);
		} else if (effect->index == EF_TIMED_INC) {
			/* Timed effects */
			(void) player_inc_check(player, effect->subtype, false);
		}
		effect = effect->next;
	}
}

/**
 * Calculate the base to-hit value for a monster spell based on race only
 * See also: chance_of_monster_hit_base
 *
 * \param race The monster race
 * \param spell The spell
 */
static int chance_of_spell_hit_base(const struct monster_race *race,
	const struct monster_spell *spell)
{
	return MAX(race->level, 1) * 3 + spell->hit;
}

/**
 * Calculate the to-hit value of a monster spell for a specific monster
 *
 * \param mon The monster
 * \param spell The spell
 */
static int chance_of_spell_hit(const struct monster *mon,
	const struct monster_spell *spell)
{
	int to_hit = chance_of_spell_hit_base(mon->race, spell);

	/* Apply confusion hit reduction for each level of confusion */
	for (int i = 0; i < monster_effect_level(mon, MON_TMD_CONF); i++) {
		to_hit = to_hit * (100 - CONF_HIT_REDUCTION) / 100;
	}

	return to_hit;
}

/**
 * Process a monster spell 
 *
 * \param index is the monster spell flag (RSF_FOO)
 * \param mon is the attacking monster
 * \param seen is whether the player can see the monster at this moment
 */
void do_mon_spell(int index, struct monster *mon, bool seen)
{
	const struct monster_spell *spell = monster_spell_by_index(index);

	bool ident = false;
	bool hits;
	int target_midx = mon->target.midx;

	/* See if it hits */
	if (spell->hit == 100) {
		hits = true;
	} else if (spell->hit == 0) {
		hits = false;
	} else {
		if (target_midx > 0) {
			hits = test_hit(chance_of_spell_hit(mon, spell),
				cave_monster(cave, target_midx)->race->ac);
		} else {
			hits = check_hit(player, chance_of_spell_hit(mon, spell));
		}
	}

	/* Tell the player what's going on */
	disturb(player);
	spell_message(mon, spell, seen, hits);

	if (hits) {
		struct monster_spell_level *level = spell->level;

		/* Get the right level of save message */
		while (level->next && mon->race->spell_power >= level->next->power) {
			level = level->next;
		}

		/* Try a saving throw if available */
		if (level->save_message && (target_midx <= 0) &&
				randint0(100) < player->state.skills[SKILL_SAVE]) {
			msg("%s", level->save_message);
			spell_check_for_fail_rune(spell);
		} else {
			effect_do(spell->effect, source_monster(mon->midx), NULL, &ident, true, 0, 0, 0, NULL);
		}
	}
}

/**
 * ------------------------------------------------------------------------
 * Spell selection
 * ------------------------------------------------------------------------ */
/**
 * Types of monster spells used for spell selection.
 */
static const struct mon_spell_info {
	uint16_t index;				/* Numerical index (RSF_FOO) */
	int type;				/* Type bitflag */
} mon_spell_types[] = {
    #define RSF(a, b)	{ RSF_##a, b },
    #include "list-mon-spells.h"
    #undef RSF
};


static bool mon_spell_is_valid(int index)
{
	return index > RSF_NONE && index < RSF_MAX;
}

static bool monster_spell_is_breath(int index)
{
	return (mon_spell_types[index].type & RST_BREATH) ? true : false;
}

static bool mon_spell_has_damage(int index)
{
	return (mon_spell_types[index].type & RST_DAMAGE) ? true : false;
}

bool mon_spell_is_innate(int index)
{
	return mon_spell_types[index].type & (RST_INNATE);
}

/**
 * Test a spell bitflag for a type of spell.
 * Returns true if any desired type is among the flagset
 *
 * \param f is the set of spell flags we're testing
 * \param types is the spell type(s) we're looking for
 */
bool test_spells(bitflag *f, int types)
{
	const struct mon_spell_info *info;

	for (info = mon_spell_types; info->index < RSF_MAX; info++)
		if (rsf_has(f, info->index) && (info->type & types))
			return true;

	return false;
}

/**
 * Set a spell bitflag to ignore a specific set of spell types.
 *
 * \param f is the set of spell flags we're pruning
 * \param types is the spell type(s) we're ignoring
 */
void ignore_spells(bitflag *f, int types)
{
	const struct mon_spell_info *info;

	for (info = mon_spell_types; info->index < RSF_MAX; info++)
		if (rsf_has(f, info->index) && (info->type & types))
			rsf_off(f, info->index);
}

/**
 * Turn off spells with a side effect or a proj_type that is resisted by
 * something in flags, subject to intelligence and chance.
 *
 * \param spells is the set of spells we're pruning
 * \param flags is the set of object flags we're testing
 * \param pflags is the set of player flags we're testing
 * \param el is what we know about the player's elemental resists
 * \param mon is the monster whose spells we are considering
 */
void unset_spells(bitflag *spells, bitflag *flags, bitflag *pflags,
				  struct element_info *el, const struct monster *mon)
{
	const struct mon_spell_info *info;
	bool smart = monster_is_smart(mon);
	int lowest_resist = RES_LEVEL_BASE;
	bitflag backup[RSF_SIZE];
	int restore_chance = 0;

	rsf_wipe(backup);

	for (info = mon_spell_types; info->index < RSF_MAX; info++) {
		const struct monster_spell *spell = monster_spell_by_index(info->index);
		const struct effect *effect;

		/* Ignore missing spells */
		if (!spell) continue;
		if (!rsf_has(spells, info->index)) continue;

		/* Get the effect */
		effect = spell->effect;

		/* First we test the elemental spells */
		if (info->type & (RST_BOLT | RST_BALL | RST_BREATH)) {
			int element = effect->subtype;
			int raw_resist = RES_LEVEL_BASE - el[element].res_level;

			/* Smart monsters keep a backup */
			if (smart && (raw_resist <= lowest_resist)) {
				lowest_resist = raw_resist;
				rsf_on(backup, info->index);
			}

			/* High resist means more likely to drop the spell */
			if (randint0(100) < raw_resist) {
				rsf_off(spells, info->index);
			}
		} else {
			/* Now others with resisted effects */
			while (effect) {
				/* Timed effects */
				if ((smart || !one_in_(3))
						&& effect->index == EF_TIMED_INC) {
					const struct timed_failure *f;
					bool resisted = false;

					assert(effect->subtype >= 0
						&& effect->subtype < TMD_MAX);
					for (f = timed_effects[effect->subtype].fail;
							f && !resisted;
							f = f->next) {
						switch (f->code) {
						case TMD_FAIL_FLAG_OBJECT:
							if (of_has(flags, f->idx)) {
								resisted = true;
							}
							break;

						case TMD_FAIL_FLAG_RESIST:
							if (el[f->idx].res_level <= RES_LEVEL_EFFECT) {
								resisted = true;
							}
							break;

						case TMD_FAIL_FLAG_VULN:
							if (el[f->idx].res_level > RES_LEVEL_BASE) {
								resisted = true;
							}
							break;

						case TMD_FAIL_FLAG_PLAYER:
							if (pf_has(pflags, f->idx)) {
								resisted = true;
							}
							break;

						/*
						 * The monster doesn't track
						 * the timed effects present
						 * on the player so do
						 * nothing with resistances
						 * due to those.
						 */
						case TMD_FAIL_FLAG_TIMED_EFFECT:
							break;
						}
					}
					if (resisted) {
						break;
					}
				}

				/* Mana drain */
				if ((smart || one_in_(2)) &&
						effect->index == EF_DRAIN_MANA &&
						pf_has(pflags, PF_NO_MANA))
					break;

				effect = effect->next;
			}
			if (effect)
				rsf_off(spells, info->index);
		}
	}

	/* Smart monsters re-assess dropped elemental spells */
	restore_chance = 1 + rsf_count(spells);
	if (smart && one_in_(restore_chance)) {
		for (info = mon_spell_types; info->index < RSF_MAX; info++) {
			const struct monster_spell *spell =
				monster_spell_by_index(info->index);
			int element;
			if (!spell) continue;
			if (!rsf_has(backup, info->index)) continue;
			element = spell->effect->subtype;
			if (RES_LEVEL_BASE - el[element].res_level == lowest_resist) {
				rsf_on(spells, info->index);
			}
		}
	}
}

/**
 * Determine the damage of a spell attack which ignores monster hp
 * (i.e. bolts and balls, including arrows/boulders/storms/etc.)
 *
 * \param spell is the attack type
 * \param race is the monster race of the attacker
 * \param dam_aspect is the damage calc required (min, avg, max, random)
 */
static int nonhp_dam(const struct monster_spell *spell,
					 const struct monster_race *race, aspect dam_aspect)
{
	int dam = 0;
	struct effect *effect = spell->effect;

	/* Set the reference race for calculations */
	ref_race = race;

	/* Now add the damage for each effect */
	while (effect) {
		random_value rand;
		/* Lash needs special treatment bacause it depends on monster blows */
		if (effect->index == EF_LASH) {
			int i;

			/* Scan through all blows for damage */
			for (i = 0; i < z_info->mon_blows_max; i++) {
				/* Extract the attack infomation */
				random_value dice = race->blow[i].dice;

				/* Full damage of first blow, plus half damage of others */
				dam += randcalc(dice, race->level, dam_aspect) / (i ? 2 : 1);
			}
		} else if (effect->dice && (effect->index != EF_TIMED_INC)) {
			/* Timed effects increases don't count as damage in lore */
			dice_roll(effect->dice, &rand);
			dam += randcalc(rand, 0, dam_aspect);
		}
		effect = effect->next;
	}

	ref_race = NULL;

	return dam;
}

/**
 * Determine the damage of a monster breath attack
 *
 * \param type is the attack element type
 * \param hp is the monster's hp
 */
int breath_dam(int type, int hp)
{
	struct projection *element = &projections[type];
	int dam;

	/* Damage is based on monster's current hp */
	dam = hp / element->divisor;

	/* Check for maximum damage */
	if (dam > element->damage_cap)
		dam = element->damage_cap;

	return dam;
}

/**
 * Calculate the damage of a monster spell.
 *
 * \param index is the index of the spell in question.
 * \param hp is the hp of the casting monster.
 * \param race is the race of the casting monster.
 * \param dam_aspect is the damage calc we want (min, max, avg, random).
 */
static int mon_spell_dam(int index, int hp, const struct monster_race *race,
						 aspect dam_aspect)
{
	const struct monster_spell *spell = monster_spell_by_index(index);

	if (monster_spell_is_breath(index))
		return breath_dam(spell->effect->subtype, hp);
	else
		return nonhp_dam(spell, race, dam_aspect);
}


/**
 * Create a mask of monster spell flags of a specific type.
 *
 * \param f is the flag array we're filling
 * \param ... is the list of flags we're looking for
 *
 * N.B. RST_NONE must be the last item in the ... list
 */
void create_mon_spell_mask(bitflag *f, ...)
{
	const struct mon_spell_info *rs;
	int i;
	va_list args;

	rsf_wipe(f);

	va_start(args, f);

	/* Process each type in the va_args */
    for (i = va_arg(args, int); i != RST_NONE; i = va_arg(args, int)) {
		for (rs = mon_spell_types; rs->index < RSF_MAX; rs++) {
			if (rs->type & i) {
				rsf_on(f, rs->index);
			}
		}
	}

	va_end(args);

	return;
}

const char *mon_spell_lore_description(int index,
									   const struct monster_race *race)
{
	if (mon_spell_is_valid(index)) {
		const struct monster_spell *spell = monster_spell_by_index(index);

		/* Get the right level of description */
		struct monster_spell_level *level = spell->level;
		while (level->next && race->spell_power >= level->next->power) {
			level = level->next;
		}
		return level->lore_desc;
	} else {
		return "";
	}
}

int mon_spell_lore_damage(int index, const struct monster_race *race,
		bool know_hp)
{
	if (mon_spell_is_valid(index) && mon_spell_has_damage(index)) {
		int hp = know_hp ? race->avg_hp : 0;
		return mon_spell_dam(index, hp, race, MAXIMISE);
	} else {
		return 0;
	}
}
