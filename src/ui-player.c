/**
 * \file ui-player.c
 * \brief character screens and dumps
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
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
#include "game-world.h"
#include "init.h"
#include "obj-curse.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-util.h"
#include "player.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "player-properties.h"
#include "player-timed.h"
#include "player-util.h"
#include "store.h"
#include "ui-birth.h"
#include "ui-display.h"
#include "ui-entry.h"
#include "ui-entry-renderers.h"
#include "ui-history.h"
#include "ui-input.h"
#include "ui-menu.h"
#include "ui-object.h"
#include "ui-output.h"
#include "ui-player.h"


/**
 * ------------------------------------------------------------------------
 * Panel utilities
 * ------------------------------------------------------------------------ */

/**
 * Panel line type
 */
struct panel_line {
	uint8_t attr;
	const char *label;
	char value[20];
};

/**
 * Panel holder type
 */
struct panel {
	size_t len;
	size_t max;
	struct panel_line *lines;
};

/**
 * Allocate some panel lines
 */
static struct panel *panel_allocate(int n) {
	struct panel *p = mem_zalloc(sizeof *p);

	p->len = 0;
	p->max = n;
	p->lines = mem_zalloc(p->max * sizeof *p->lines);

	return p;
}

/**
 * Free up panel lines
 */
static void panel_free(struct panel *p) {
	assert(p);
	mem_free(p->lines);
	mem_free(p);
}

/**
 * Add a new line to the panel
 */
static void panel_line(struct panel *p, uint8_t attr, const char *label,
		const char *fmt, ...) {
	va_list vp;

	struct panel_line *pl;

	/* Get the next panel line */
	assert(p);
	assert(p->len != p->max);
	pl = &p->lines[p->len++];

	/* Set the basics */
	pl->attr = attr;
	pl->label = label;

	/* Set the value */
	va_start(vp, fmt);
	vstrnfmt(pl->value, sizeof pl->value, fmt, vp);
	va_end(vp);
}

/**
 * Cache the layout of the character sheet, currently only for the resistance
 * panel, since it is no longer hardwired.
 */
struct char_sheet_resist {
	struct ui_entry* entry;
	wchar_t label[6];
};
struct char_sheet_config {
	struct ui_entry** stat_mod_entries;
	region res_regions[4];
	struct char_sheet_resist *resists_by_region[4];
	int n_resist_by_region[4];
	int n_stat_mod_entries;
	int res_cols;
	int res_rows;
	int res_nlabel;
};
static struct char_sheet_config *cached_config = NULL;
static void display_resistance_panel(int ipart, struct char_sheet_config* config);


static bool have_valid_char_sheet_config(void)
{
	if (!cached_config) {
		return false;
	}
	if (cached_config->res_cols !=
		cached_config->res_nlabel + 1 + player->body.count) {
		return false;
	}
	return true;
}


static void release_char_sheet_config(void)
{
	int i;

	if (!cached_config) {
		return;
	}
	for (i = 0; i < 4; ++i) {
		mem_free(cached_config->resists_by_region[i]);
	}
	mem_free(cached_config->stat_mod_entries);
	mem_free(cached_config);
	cached_config = 0;
}


static bool check_for_two_categories(const struct ui_entry* entry,
	void *closure)
{
	const char **categories = closure;

	return ui_entry_has_category(entry, categories[0]) &&
		ui_entry_has_category(entry, categories[1]);
}


static void configure_char_sheet(void)
{
	const char* region_categories[] = {
		"resistances",
		"abilities",
		"modifiers",
		"hindrances"
	};
	const char* test_categories[2];
	struct ui_entry_iterator* ui_iter;
	int i, n, next_col;

	release_char_sheet_config();

	cached_config = mem_alloc(sizeof(*cached_config));

	test_categories[0] = "CHAR_SCREEN1";
	test_categories[1] = "stat_modifiers";
	ui_iter = initialize_ui_entry_iterator(check_for_two_categories,
		test_categories, test_categories[1]);
	n = count_ui_entry_iterator(ui_iter);
	/*
	 * Linked to hardcoded stats display with STAT_MAX entries so only use
	 * that many.
	 */
	if (n > STAT_MAX) {
	    n = STAT_MAX;
	}
	cached_config->n_stat_mod_entries = n;
	cached_config->stat_mod_entries = mem_alloc(n *
		sizeof(*cached_config->stat_mod_entries));
	for (i = 0; i < n; ++i) {
		cached_config->stat_mod_entries[i] =
			advance_ui_entry_iterator(ui_iter);
	}
	release_ui_entry_iterator(ui_iter);

	cached_config->res_nlabel = 6;
	cached_config->res_cols =
		cached_config->res_nlabel + 1 + player->body.count;
	cached_config->res_rows = 0;
	next_col = 0;
	for (i = 0; i < 4; ++i) {
		int j;

		//cached_config->res_regions[i].col =
		//	i * (cached_config->res_cols + 1);
		cached_config->res_regions[i].col = next_col;
		cached_config->res_regions[i].row = 2 + STAT_MAX;
		cached_config->res_regions[i].width = cached_config->res_cols
			+ ((i % 2 == 0) ? 5 : 0);
		next_col += cached_config->res_regions[i].width + 1;

		test_categories[1] = region_categories[i];
		ui_iter = initialize_ui_entry_iterator(check_for_two_categories, test_categories, region_categories[i]);
		n = count_ui_entry_iterator(ui_iter);
		/*
		 * Fit in 24 row display; leave at least one row blank before
		 * prompt on last row.
		 */
		if (n + 2 + cached_config->res_regions[i].row > 22) {
		    n = 20 - cached_config->res_regions[i].row;
		}
		cached_config->n_resist_by_region[i] = n;
		cached_config->resists_by_region[i] = mem_alloc(n * sizeof(*cached_config->resists_by_region[i]));
		for (j = 0; j < n; ++j) {
			struct ui_entry *entry = advance_ui_entry_iterator(ui_iter);

			cached_config->resists_by_region[i][j].entry = entry;
			get_ui_entry_label(entry, cached_config->res_nlabel, true, cached_config->resists_by_region[i][j].label);
			(void) text_mbstowcs(cached_config->resists_by_region[i][j].label + 5, ":", 1);
		}
		release_ui_entry_iterator(ui_iter);

		if (cached_config->res_rows <
			cached_config->n_resist_by_region[i]) {
			cached_config->res_rows =
				cached_config->n_resist_by_region[i];
		}
	}
	for (i = 0; i < 4; ++i) {
		cached_config->res_regions[i].page_rows =
			cached_config->res_rows + 2;
	}
}


/**
 * Returns a "rating" of x depending on y, and sets "attr" to the
 * corresponding "attribute".
 */
static const char *likert(int x, int y, uint8_t *attr)
{
	/* Paranoia */
	if (y <= 0) y = 1;

	/* Negative value */
	if (x < 0) {
		*attr = COLOUR_RED;
		return ("Very Bad");
	}

	/* Analyze the value */
	switch ((x / y))
	{
		case 0:
		case 1:
		{
			*attr = COLOUR_RED;
			return ("Bad");
		}
		case 2:
		{
			*attr = COLOUR_RED;
			return ("Poor");
		}
		case 3:
		case 4:
		{
			*attr = COLOUR_YELLOW;
			return ("Fair");
		}
		case 5:
		{
			*attr = COLOUR_YELLOW;
			return ("Good");
		}
		case 6:
		{
			*attr = COLOUR_YELLOW;
			return ("Very Good");
		}
		case 7:
		case 8:
		{
			*attr = COLOUR_L_GREEN;
			return ("Excellent");
		}
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		{
			*attr = COLOUR_L_GREEN;
			return ("Superb");
		}
		case 14:
		case 15:
		case 16:
		case 17:
		{
			*attr = COLOUR_L_GREEN;
			return ("Heroic");
		}
		default:
		{
			*attr = COLOUR_L_GREEN;
			return ("Legendary");
		}
	}
}


/**
 * Calculate average unarmed damage
 */
static int average_unarmed_damage(struct player *p)
{
	int bonus = deadliness_conversion[MAX(p->state.to_d, 0)];
	int chances = 2 + (bonus / 100) + (randint0(100) < (bonus % 100) ? 1 : 0);
	int min_blow = 1 + (p->lev / 10);
	int max_blow = MAX(2 * p->lev / 5, min_blow);
	int n;
	int *num_events = mem_zalloc((max_blow - min_blow + 1) * sizeof(int));
	int *powers = mem_zalloc((max_blow - min_blow + 1) * sizeof(int));
	int total, sum = 0, big_sum = 0;

	/* Record powers */
	for (n = 0; n <= max_blow - min_blow; n++) {
		int k;
		powers[n] = 1;
		for (k = 0; k < chances; k++) {
			powers[n] *= n + min_blow;
		}
	}

	/* Calculate number of events with largest choice n + min_blow */
	if (min_blow == max_blow) {
		/* Trivial case */
		num_events[0] = 1;
	} else {
		/* Total number of events */
		total = powers[max_blow - min_blow];

		/* Events where the max is reached is the total minus the number where
		 * max_blow-1 is reached */
		num_events[max_blow - min_blow] = powers[max_blow - min_blow]
			- powers[max_blow - min_blow - 1];
		total -= num_events[max_blow - min_blow];

		/* Now keep applying this until we get to the minimum */
		for (n = max_blow - min_blow - 1; n > 0; n--) {
			num_events[n] = powers[max_blow - min_blow] - powers[n - 1];
			num_events[n] -= num_events[n + 1];
			total -= num_events[n];
		}

		/* NUmber of events with the minimum is the number left over */
		num_events[0] = total;
	}

	/* Get the weighted damage sums */
	for (n = min_blow; n <= max_blow; n++) {
		struct unarmed_blow blow = unarmed_blows[n - 1];
		int max, average = damcalc(blow.dd, blow.ds, AVERAGE);

		/* Get the average damage from regular strikes */
		sum += average * num_events[n - min_blow];

		/* Get the damage from power strikes */
		if (n < num_unarmed_blows - 1) {
			blow = unarmed_blows[n];
		}
		max = damcalc(blow.dd, blow.ds, MAXIMISE);
		big_sum += max * num_events[n - min_blow];
	}

	/* Adjust for Martial Arts and Power Strike abilities */
	if (player_has(p, PF_MARTIAL_ARTS)) {
		sum = (sum * 5 + big_sum) / 6;
	} else if (player_has(p, PF_POWER_STRIKE)) {
		sum = (sum * 7 + big_sum) / 8;
	}

	/* Turn into an average */
	sum /= powers[max_blow - min_blow];

	mem_free(powers);
	mem_free(num_events);

	return sum;
}

/**
 * Equippy chars
 */
static void display_player_equippy(int y, int x)
{
	int i;

	uint8_t a;
	wchar_t c;

	struct object *obj;

	/* Dump equippy chars */
	for (i = 0; i < player->body.count; ++i) {
		/* Object */
		obj = slot_object(player, i);

		/* Get attr/char for display; clear if big tiles or no object */
		if (obj && tile_width == 1 && tile_height == 1) {
			a = object_attr(obj);
			c = object_char(obj);
		} else {
			a = COLOUR_WHITE;
			c = L' ';
		}

		/* Dump */
		Term_putch(x + i, y, a, c);
	}
}


static void display_resistance_panel(int ipart, struct char_sheet_config *config)
{
	int *vals = mem_alloc((player->body.count + 1) * sizeof(*vals));
	int *auxs = mem_alloc((player->body.count + 1) * sizeof(*auxs));
	struct object **equipment =
		mem_alloc(player->body.count * sizeof(*equipment));
	struct cached_object_data **ocaches =
		mem_zalloc(player->body.count * sizeof(*ocaches));
	struct cached_player_data *pcache = NULL;
	struct ui_entry_details render_details;
	int i;
	int j;
	int col = config->res_regions[ipart].col;
	int row = config->res_regions[ipart].row;

	for (i = 0; i < player->body.count; i++) {
		equipment[i] = slot_object(player, i);
	}

	/* Equippy */
	display_player_equippy(row++, col + config->res_nlabel);

	Term_putstr(col, row++, config->res_cols, COLOUR_WHITE, "      abcdefgimnop@");
	render_details.label_position.x = col;
	render_details.value_position.x = col + config->res_nlabel;
	render_details.position_step = loc(1, 0);
	render_details.combined_position.x = col + config->res_nlabel +
		player->body.count + 1;
	render_details.vertical_label = false;
	render_details.alternate_color_first = false;
	render_details.show_combined = ipart % 2 ? false : true;
	for (i = 0; i < config->n_resist_by_region[ipart]; i++, row++) {
		const struct ui_entry *entry = config->resists_by_region[ipart][i].entry;

		for (j = 0; j < player->body.count; j++) {
			compute_ui_entry_values_for_object(entry, equipment[j], player, ocaches + j, vals + j, auxs + j);
		}
		compute_ui_entry_values_for_player(entry, player, &pcache, vals + player->body.count, auxs + player->body.count);

		render_details.label_position.y = row;
		render_details.value_position.y = row;
		render_details.combined_position.y = row;
		render_details.known_rune = is_ui_entry_for_known_rune(entry, player);
		ui_entry_renderer_apply(get_ui_entry_renderer_index(entry), config->resists_by_region[ipart][i].label, config->res_nlabel, vals, auxs, player->body.count + 1, &render_details);
	}

	if (pcache) {
		release_cached_player_data(pcache);
	}
	for (i = 0; i < player->body.count; ++i) {
		if (ocaches[i]) {
			release_cached_object_data(ocaches[i]);
		}
	}
	mem_free(ocaches);
	mem_free(equipment);
	mem_free(auxs);
	mem_free(vals);
}

static void display_player_flag_info(void)
{
	int i;

	for (i = 0; i < 4; i++)
		display_resistance_panel(i, cached_config);
}


/**
 * Special display, part 2b
 */
void display_player_stat_info(void)
{
	int i, row, col;

	char buf[80];


	/* Row */
	row = 2;

	/* Column */
	col = 42;

	/* Print out the labels for the columns */
	c_put_str(COLOUR_WHITE, "  Self", row-1, col+5);
	c_put_str(COLOUR_WHITE, " RB", row-1, col+12);
	c_put_str(COLOUR_WHITE, " CB", row-1, col+16);
	c_put_str(COLOUR_WHITE, " EB", row-1, col+20);
	c_put_str(COLOUR_WHITE, "  Best", row-1, col+24);

	/* Display the stats */
	for (i = 0; i < STAT_MAX; i++) {
		/* Reduced or normal */
		if (player->stat_cur[i] < player->stat_max[i])
			/* Use lowercase stat name */
			put_str(stat_names_reduced[i], row+i, col);
		else
			/* Assume uppercase stat name */
			put_str(stat_names[i], row+i, col);

		/* Indicate natural maximum */
		if (player->stat_max[i] == 18+100)
			put_str("!", row+i, col+3);

		/* Internal "natural" maximum value */
		cnv_stat(player->stat_max[i], buf, sizeof(buf));
		c_put_str(COLOUR_L_GREEN, buf, row+i, col+5);

		/* Race Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->race->r_adj[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+12);

		/* Class Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->class->c_adj[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+16);

		/* Equipment Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->state.stat_add[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+20);

		/* Resulting "modified" maximum value */
		cnv_stat(player->state.stat_top[i], buf, sizeof(buf));
		c_put_str(COLOUR_L_GREEN, buf, row+i, col+24);

		/* Only display stat_use if there has been draining */
		if (player->stat_cur[i] < player->stat_max[i]) {
			cnv_stat(player->state.stat_use[i], buf, sizeof(buf));
			c_put_str(COLOUR_YELLOW, buf, row+i, col+31);
		}
	}
}


/**
 * Special display, part 2c
 *
 * Display stat modifiers from equipment and sustains.  Colors and symbols
 * are configured from ui_entry.txt and ui_entry_renderers.txt.  Other
 * configuration that's possible there (extra characters for each number
 * for instance) aren't well handled here - the assumption is just one digit
 * for each equipment slot.
 */
static void display_player_sust_info(struct char_sheet_config *config)
{
	int *vals = mem_alloc((player->body.count + 1) * sizeof(*vals));
	int *auxs = mem_alloc((player->body.count + 1) * sizeof(*auxs));
	struct object **equipment =
		mem_alloc(player->body.count * sizeof(*equipment));
	struct cached_object_data **ocaches =
		mem_zalloc(player->body.count * sizeof(*ocaches));
	struct cached_player_data *pcache = NULL;
	struct ui_entry_details render_details;
	int i, row, col;

	for (i = 0; i < player->body.count; i++) {
		equipment[i] = slot_object(player, i);
	}

	/* Row */
	row = 2;

	/* Column */
	col = 26;

	/* Header */
	c_put_str(COLOUR_WHITE, "abcdefgimnop@", row - 1, col);

	render_details.label_position.x = col + player->body.count + 5;
	render_details.value_position.x = col;
	render_details.position_step = loc(1, 0);
	render_details.combined_position = loc(0, 0);
	render_details.vertical_label = false;
	render_details.alternate_color_first = false;
	render_details.known_rune = true;
	render_details.show_combined = false;
	for (i = 0; i < config->n_stat_mod_entries; i++) {
		const struct ui_entry *entry = config->stat_mod_entries[i];
		int j;

		for (j = 0; j < player->body.count; j++) {
			compute_ui_entry_values_for_object(entry, equipment[j], player, ocaches + j, vals + j, auxs + j);
		}
		compute_ui_entry_values_for_player(entry, player, &pcache, vals + player->body.count, auxs + player->body.count);
		/* Just use the sustain information for the player column. */
		vals[player->body.count] = 0;

		render_details.label_position.y = row + i;
		render_details.value_position.y = row + i;
		ui_entry_renderer_apply(get_ui_entry_renderer_index(entry), NULL, 0, vals, auxs, player->body.count + 1, &render_details);
	}

	if (pcache) {
		release_cached_player_data(pcache);
	}
	for (i = 0; i < player->body.count; ++i) {
		if (ocaches[i]) {
			release_cached_object_data(ocaches[i]);
		}
	}
	mem_free(ocaches);
	mem_free(equipment);
	mem_free(auxs);
	mem_free(vals);
}



static void display_panel(const struct panel *p, bool left_adj,
		const region *bounds)
{
	size_t i;
	int col = bounds->col;
	int row = bounds->row;
	int w = bounds->width;
	int offset = 0;

	region_erase(bounds);

	if (left_adj) {
		for (i = 0; i < p->len; i++) {
			struct panel_line *pl = &p->lines[i];

			int len = pl->label ? strlen(pl->label) : 0;
			if (offset < len) offset = len;
		}
		offset += 2;
	}

	for (i = 0; i < p->len; i++, row++) {
		int len;
		struct panel_line *pl = &p->lines[i];

		if (!pl->label)
			continue;

		Term_putstr(col, row, strlen(pl->label), COLOUR_WHITE, pl->label);

		len = strlen(pl->value);
		len = len < w - offset ? len : w - offset - 1;

		if (left_adj)
			Term_putstr(col+offset, row, len, pl->attr, pl->value);
		else
			Term_putstr(col+w-len, row, len, pl->attr, pl->value);
	}
}

static const char *show_title(void)
{
	if (player->wizard)
		return "[=-WIZARD-=]";
	else if (player->total_winner || player->lev > PY_MAX_LEVEL)
		return "***WINNER***";
	else
		return player->class->title[(player->lev - 1) / 5];
}

static const char *show_adv_exp(void)
{
	if (player->lev < PY_MAX_LEVEL) {
		static char buffer[30];
		int32_t advance = (player_exp[player->lev - 1]);
		strnfmt(buffer, sizeof(buffer), "%ld", (long)advance);
		return buffer;
	}
	else {
		return "********";
	}
}

static const char *show_depth(void)
{
	static char buffer[13];

	if (player->max_depth == 0) return "Town";

	strnfmt(buffer, sizeof(buffer), "%d' (L%d)",
	        player->max_depth * 50, player->max_depth);
	return buffer;
}

static const char *show_speed(void)
{
	static char buffer[10];
	int tmp = player->state.speed;
	if (player->timed[TMD_FAST])
		tmp -= (player_has(player, PF_ENHANCE_MAGIC)) ? 13 : 10;
	if (player->timed[TMD_SLOW]) tmp += 10;
	if (tmp == 110) return "Normal";
	int multiplier = 10 * extract_energy[tmp] / extract_energy[110];
	int int_mul = multiplier / 10;
	int dec_mul = multiplier % 10;
	if (OPT(player, effective_speed))
		strnfmt(buffer, sizeof(buffer), "%d.%dx (%d)", int_mul, dec_mul, tmp - 110);
	else
		strnfmt(buffer, sizeof(buffer), "%d (%d.%dx)", tmp - 110, int_mul, dec_mul);
	return buffer;
}

static uint8_t max_color(int val, int max)
{
	return val < max ? COLOUR_YELLOW : COLOUR_L_GREEN;
}

/**
 * Colours for table items
 */
static const uint8_t colour_table[] =
{
	COLOUR_RED, COLOUR_RED, COLOUR_RED, COLOUR_L_RED, COLOUR_ORANGE,
	COLOUR_YELLOW, COLOUR_YELLOW, COLOUR_GREEN, COLOUR_GREEN, COLOUR_L_GREEN,
	COLOUR_L_BLUE, COLOUR_L_BLUE
};


static struct panel *get_panel_topleft(void) {
	struct panel *p = panel_allocate(7);

	panel_line(p, COLOUR_L_BLUE, "Name", "%s", player->full_name);
	panel_line(p, COLOUR_L_BLUE, "Race",	"%s", player->race->name);
	panel_line(p, COLOUR_L_BLUE, "Class", "%s", player->class->name);
	panel_line(p, COLOUR_L_BLUE, "Title", "%s", show_title());
	panel_line(p, COLOUR_L_BLUE, "HP", "%d/%d", player->chp, player->mhp);
	panel_line(p, COLOUR_L_BLUE, "SP", "%d/%d", player->csp, player->msp);
	panel_line(p, COLOUR_L_BLUE, "Gold", "%d", player->au);

	return p;
}

static struct panel *get_panel_midleft(void) {
	struct panel *p = panel_allocate(7);
	int diff = weight_remaining(player);
	uint8_t attr = diff < 0 ? COLOUR_L_RED : COLOUR_L_GREEN;

	panel_line(p, max_color(player->lev, player->max_lev),
			"Level", "%d", player->lev);
	panel_line(p, max_color(player->exp, player->max_exp),
			"Cur Exp", "%d", player->exp);
	panel_line(p, COLOUR_L_GREEN, "Max Exp", "%d", player->max_exp);
	panel_line(p, COLOUR_L_GREEN, "Adv Exp", "%s", show_adv_exp());
	panel_line(p, attr, "Burden", "%.1f lb",
			   player->upkeep->total_weight / 10.0F);
	panel_line(p, attr, "Overweight", "%d.%d lb", -diff / 10, abs(diff) % 10);
	panel_line(p, COLOUR_L_GREEN, "Max Depth", "%s", show_depth());

	return p;
}

static struct panel *get_panel_combat(void) {
	struct panel *p = panel_allocate(7);
	struct object *obj;
	int bth, dam, hit;
	int melee_dice = 1, melee_sides = 1;

	/* AC */
	panel_line(p, COLOUR_L_BLUE, "Armor", "[%d,%+d]",
			player->known_state.ac, player->known_state.to_a);

	/* Melee */
	obj = equipped_item_by_slot_name(player, "weapon");
	bth = (player->state.skills[SKILL_TO_HIT_MELEE] * 10) / BTH_PLUS_ADJ;
	dam = player->known_state.to_d;
	hit = player->known_state.to_h;
	if (obj && obj->known) {
		melee_dice = obj->known->dd;
		melee_sides = obj->known->ds;
		dam += object_to_dam(obj->known);
		hit += object_to_hit(obj->known);
	}
	if (player->known_state.bless_wield) {
		hit += 2;
	}

	if (!obj && (player_has(player, PF_UNARMED_COMBAT) ||
				 player_has(player, PF_MARTIAL_ARTS))) {
		panel_line(p, COLOUR_L_BLUE, "Melee", "Av.%d,%+d%%",
				   average_unarmed_damage(player),
				   deadliness_conversion[MAX(dam, 0)]);
	} else {
		panel_line(p, COLOUR_L_BLUE, "Melee", "%dd%d,%c%d%%", melee_dice,
				   melee_sides,
				   (dam >= 0) ? '+' : '-',
				   deadliness_conversion[MIN(ABS(dam), 150)]);
	}
	panel_line(p, COLOUR_L_BLUE, "To-hit", "%d,%+d", bth / 10, hit);
	panel_line(p, COLOUR_L_BLUE, "Blows", "%d.%d/turn",
			player->state.num_blows / 100, (player->state.num_blows / 10 % 10));

	/* Ranged */
	obj = equipped_item_by_slot_name(player, "shooting");
	bth = (player->state.skills[SKILL_TO_HIT_BOW] * 10) / BTH_PLUS_ADJ;
	dam = player->known_state.to_d;
	hit = player->known_state.to_h;
	if (obj && obj->known) {
		dam += object_to_dam(obj->known);
		hit += object_to_hit(obj->known);
	}

	panel_line(p, COLOUR_L_BLUE, "Shoot to-dam", "%c%d%%",
			   (dam >= 0) ? '+' : '-',
			   deadliness_conversion[MIN(ABS(dam), 150)]);
	panel_line(p, COLOUR_L_BLUE, "To-hit", "%d,%+d", bth / 10, hit);
	panel_line(p, COLOUR_L_BLUE, "Shots", "%d.%d/turn",
			   player->state.num_shots / 10, player->state.num_shots % 10);

	return p;
}

static struct panel *get_panel_skills(void) {
	struct panel *p = panel_allocate(8);

	int skill;
	uint8_t attr;
	const char *desc;
	int depth = cave ? cave->depth : 0;

#define BOUND(x, min, max)		MIN(max, MAX(min, x))

	/* Saving throw */
	skill = BOUND(player->state.skills[SKILL_SAVE], 0, 100);
	panel_line(p, colour_table[skill / 10], "Saving Throw", "%d%%", skill);

	/* Stealth */
	desc = likert(player->state.skills[SKILL_STEALTH], 1, &attr);
	panel_line(p, attr, "Stealth", "%s", desc);

	/* Physical disarming: assume we're disarming a dungeon trap */
	skill = BOUND(player->state.skills[SKILL_DISARM_PHYS] - depth / 5, 2, 100);
	panel_line(p, colour_table[skill / 10], "Disarm - phys.", "%d%%", skill);

	/* Magical disarming */
	skill = BOUND(player->state.skills[SKILL_DISARM_MAGIC] - depth / 5, 2, 100);
	panel_line(p, colour_table[skill / 10], "Disarm - magic", "%d%%", skill);

	/* Magic devices */
	skill = BOUND(player->state.skills[SKILL_DEVICE], 10, 150);
	panel_line(p, colour_table[skill / 13], "Magic Devices", "%d", skill);

	/* Searching ability */
	skill = BOUND(player->state.skills[SKILL_SEARCH], 0, 100);
	panel_line(p, colour_table[skill / 10], "Searching", "%d%%", skill);

	/* Infravision */
	panel_line(p, COLOUR_L_GREEN, "Infravision", "%d ft",
			player->state.see_infra * 10);

	/* Speed */
	skill = player->state.speed;
	if (player->timed[TMD_FAST])
		skill -= (player_has(player, PF_ENHANCE_MAGIC)) ? 13 : 10;
	if (player->timed[TMD_SLOW]) skill += 10;
	attr = skill < 110 ? COLOUR_L_UMBER : COLOUR_L_GREEN;
	panel_line(p, attr, "Speed", "%s", show_speed());

	return p;
}

static struct panel *get_panel_misc(void) {
	struct panel *p = panel_allocate(7);
	uint8_t attr = COLOUR_L_BLUE;

	panel_line(p, attr, "Age", "%d", player->age);
	panel_line(p, attr, "Height", "%d'%d\"", player->ht / 12, player->ht % 12);
	panel_line(p, attr, "Weight", "%dst %dlb", player->wt / 14, player->wt % 14);
	panel_line(p, attr, "Turns used:", "");
	panel_line(p, attr, "Game", "%d", turn);
	panel_line(p, attr, "Standard", "%d", player->total_energy / 100);
	panel_line(p, attr, "Resting", "%d", player->resting_turn);

	return p;
}


/**
 * Panels for main character screen
 */
static const struct {
	region bounds;
	bool align_left;
	struct panel *(*panel)(void);
} panels[] =
{
	/*   x  y wid rows */
	{ {  1, 1, 40, 7 }, true,  get_panel_topleft },	/* Name, Class, ... */
	{ { 21, 1, 18, 3 }, false, get_panel_misc },	/* Age, ht, wt, ... */
	{ {  1, 9, 24, 7 }, false, get_panel_midleft },/* Cur Exp, Max Exp, ... */
	{ { 29, 9, 19, 7 }, false, get_panel_combat },
	{ { 52, 9, 20, 8 }, false, get_panel_skills },
};

void display_player_xtra_info(int mode)
{
	size_t i;
	char points[65];
	size_t free_space = Term->hgt - 24;
	for (i = 0; i < N_ELEMENTS(panels); i++) {
		struct panel *p = panels[i].panel();
		display_panel(p, panels[i].align_left, &panels[i].bounds);
		panel_free(p);
	}

	/* Indent output by 1 character, and wrap at column 72 */
	text_out_wrap = 72;
	text_out_indent = 1;

	/* Recall points */
	if (player->recall[0]) {
		int j;

		my_strcpy(points, level_name(&world->levels[player->recall[0]]),
				  sizeof(points));
		for (j = 1; j < 4; j++) {
			if (player->recall[j]) {
				my_strcat(points, ", ", sizeof(points));
				my_strcat(points, level_name(&world->levels[player->recall[j]]),
						  sizeof(points));
			} else {
				break;
			}
		}
	} else {
		my_strcpy(points, "None", sizeof(points));
	}
	Term_gotoxy(text_out_indent, free_space ? 18 : 17);
	text_out_to_screen(COLOUR_WHITE, "Recall pts: ");
	text_out_indent = 13;
	text_out_to_screen(COLOUR_L_BLUE, points);
	text_out_indent = 1;

	/* History */
	Term_gotoxy(text_out_indent, (free_space > 1) ? 21 : 20);
	text_out_to_screen(COLOUR_WHITE, player->history);

	/* Reset text_out() vars */
	text_out_wrap = 0;
	text_out_indent = 0;

	return;
}

/**
 * Display the character on the screen (two different modes)
 *
 * The top two lines, and the bottom line (or two) are left blank.
 *
 * Mode 0 = standard display with skills/history
 * Mode 1 = special display with equipment flags
 * Mode 2 = standard display with skills/history, extra space enforced
 */
void display_player(int mode)
{
	if (!have_valid_char_sheet_config()) {
		configure_char_sheet();
	}

	/* Erase screen */
	clear_from(0);

	/* When not playing, do not display in subwindows */
	if (Term != angband_term[0] && !player->upkeep->playing) return;

	/* Stat info */
	display_player_stat_info();

	if (mode == 1) {
		struct panel *p = panels[0].panel();
		display_panel(p, panels[0].align_left, &panels[0].bounds);
		panel_free(p);

		/* Stat/Sustain flags */
		display_player_sust_info(cached_config);

		/* Other flags */
		display_player_flag_info();
	} else {
		/* Extra info */
		display_player_xtra_info(mode);
	}
}


/**
 * Write a character dump
 */
void write_character_dump(ang_file *fff)
{
	int i, x, y, ylim;

	int a;
	wchar_t c;

	struct store *home = store_home(player);
	struct object **home_list = mem_zalloc(sizeof(struct object *) *
										   z_info->store_inven_max);
	char o_name[80];

	int n;
	char *buf, *p;

	if (!have_valid_char_sheet_config()) {
		configure_char_sheet();
	}

	n = 80;
	if (n < 2 * cached_config->res_cols + 1) {
		n = 2 * cached_config->res_cols + 1;
	}
	buf = mem_alloc(text_wcsz() * n + 1);

	/* Begin dump */
	file_putf(fff, "  [%s Character Dump]\n\n", buildid);

	/* Display player basics */
	display_player(2);

	/* Dump part of the screen */
	for (y = 1; y < 23; y++) {
		p = buf;
		/* Dump each row */
		for (x = 0; x < 79; x++) {
			/* Get the attr/char */
			(void)(Term_what(x, y, &a, &c));

			/* Dump it */
			n = text_wctomb(p, c);
			if (n > 0) {
				p += n;
			} else {
				*p++ = ' ';
			}
		}

		/* Back up over spaces */
		while ((p > buf) && (p[-1] == ' ')) --p;

		/* Terminate */
		*p = '\0';

		/* End the row */
		file_putf(fff, "%s\n", buf);
	}

	/* Dump specialties if any */
	file_putf(fff, "\n  [Specialty Abilities]\n\n");
	for (i = 0; i < PF_MAX; i++) {
		if (!pf_has(player->specialties, i)) continue;
		file_putf(fff, "%s\n", lookup_ability("player", i, 0)->name);
	}
	file_putf(fff, "\n");

	display_player(1);

	/* Print a header */
	file_putf(fff, "%-25s%s\n", "Resistances", "Abilities");

	/* Dump part of the screen */
	ylim = ((cached_config->n_resist_by_region[0] >
		cached_config->n_resist_by_region[1]) ?
		cached_config->n_resist_by_region[0] :
		cached_config->n_resist_by_region[1]) +
		cached_config->res_regions[0].row + 2;
	for (y = cached_config->res_regions[0].row + 2; y < ylim; y++) {
		p = buf;
		/* Dump each row */
		for (x = 0; x < 2 * cached_config->res_cols + 6; x++) {
			/* Get the attr/char */
			(void)(Term_what(x, y, &a, &c));

			/* Dump it */
			n = text_wctomb(p, c);
			if (n > 0) {
				p += n;
			} else {
				*p++ = ' ';
			}
		}

		/* Back up over spaces */
		while ((p > buf) && (p[-1] == ' ')) --p;

		/* Terminate */
		*p = '\0';

		/* End the row */
		file_putf(fff, "%s\n", buf);
	}

	/* Skip a line */
	file_putf(fff, "\n");

	/* Print a header */
	file_putf(fff, "%-25s%s\n", "Hindrances", "Modifiers");

	/* Dump part of the screen */
	ylim = ((cached_config->n_resist_by_region[2] >
		cached_config->n_resist_by_region[3]) ?
		cached_config->n_resist_by_region[2] :
		cached_config->n_resist_by_region[3]) +
		cached_config->res_regions[0].row + 2;
	for (y = cached_config->res_regions[0].row + 2; y < ylim; y++) {
		p = buf;
		/* Dump each row */
		for (x = 0; x < 2 * cached_config->res_cols + 6; x++) {
			/* Get the attr/char */
			(void)(Term_what(x + 2 * cached_config->res_cols + 7, y, &a, &c));

			/* Dump it */
			n = text_wctomb(p, c);
			if (n > 0) {
				p += n;
			} else {
				*p++ = ' ';
			}
		}

		/* Back up over spaces */
		while ((p > buf) && (p[-1] == ' ')) --p;

		/* Terminate */
		*p = '\0';

		/* End the row */
		file_putf(fff, "%s\n", buf);
	}

	/* Skip some lines */
	file_putf(fff, "\n\n");


	/* If dead, dump last messages -- Prfnoff */
	if (player->is_dead) {
		i = messages_num();
		if (i > 15) i = 15;
		file_putf(fff, "  [Last Messages]\n\n");
		while (i-- > 0)
		{
			file_putf(fff, "> %s\n", message_str((int16_t)i));
		}
		if (streq(player->died_from, "Retiring")) {
			file_putf(fff, "\nRetired.\n\n");
		} else {
			file_putf(fff, "\nKilled by %s.\n\n",
				player->died_from);
		}
	}


	/* Dump the equipment */
	file_putf(fff, "  [Character Equipment]\n\n");
	for (i = 0; i < player->body.count; i++) {
		struct object *obj = slot_object(player, i);
		if (!obj) continue;

		object_desc(o_name, sizeof(o_name), obj,
			ODESC_PREFIX | ODESC_FULL, player);
		file_putf(fff, "%c) %s\n", gear_to_label(player, obj), o_name);
		object_info_chardump(fff, obj, 5, 72);
	}
	file_putf(fff, "\n\n");

	/* Dump the inventory */
	file_putf(fff, "\n\n  [Character Inventory]\n\n");
	for (i = 0; i < z_info->pack_size; i++) {
		struct object *obj = player->upkeep->inven[i];
		if (!obj) break;

		object_desc(o_name, sizeof(o_name), obj,
			ODESC_PREFIX | ODESC_FULL, player);
		file_putf(fff, "%c) %s\n", gear_to_label(player, obj), o_name);
		object_info_chardump(fff, obj, 5, 72);
	}
	file_putf(fff, "\n\n");

	/* Dump the quiver */
	file_putf(fff, "\n\n  [Character Quiver]\n\n");
	for (i = 0; i < z_info->quiver_size; i++) {
		struct object *obj = player->upkeep->quiver[i];
		if (!obj) continue;

		object_desc(o_name, sizeof(o_name), obj,
			ODESC_PREFIX | ODESC_FULL, player);
		file_putf(fff, "%c) %s\n", gear_to_label(player, obj), o_name);
		object_info_chardump(fff, obj, 5, 72);
	}
	file_putf(fff, "\n\n");

	/* Dump the Home -- if anything there */
	if (home) {
		store_stock_list(home, home_list, z_info->store_inven_max);
		if (home->stock_num) {
			/* Header */
			file_putf(fff, "  [Home Inventory]\n\n");

			/* Dump all available items */
			for (i = 0; i < z_info->store_inven_max; i++) {
				struct object *obj = home_list[i];
				if (!obj) break;
				object_desc(o_name, sizeof(o_name), obj,
							ODESC_PREFIX | ODESC_FULL, player);
				file_putf(fff, "%c) %s\n", I2A(i), o_name);

				object_info_chardump(fff, obj, 5, 72);
			}

			/* Add an empty line */
			file_putf(fff, "\n\n");
		}
	}

	/* Dump character history */
	dump_history(fff);
	file_putf(fff, "\n\n");

	/* Dump options */
	file_putf(fff, "  [Options]\n\n");

	/* Dump options */
	for (i = 0; i < OP_MAX; i++) {
		int opt;
		const char *title = "";
		switch (i) {
			case OP_INTERFACE: title = "User interface"; break;
			case OP_BIRTH: title = "Birth"; break;
		    default: continue;
		}

		file_putf(fff, "  [%s]\n\n", title);
		for (opt = 0; opt < OPT_MAX; opt++) {
			const char *desc;
			size_t u8len;

			if (option_type(opt) != i) continue;
			desc = option_desc(opt);
			u8len = utf8_strlen(desc);
			if (u8len < 45) {
				file_putf(fff, "%s%*s", desc,
					(int)(45 - u8len), " ");
			} else {
				file_putf(fff, "%s", desc);
			}
			file_putf(fff, ": %s (%s)\n",
			        player->opts.opt[opt] ? "yes" : "no ",
			        option_name(opt));
		}

		/* Skip some lines */
		file_putf(fff, "\n");
	}

	mem_free(home_list);
	mem_free(buf);
}

/**
 * Save the lore to a file in the user directory.
 *
 * \param path is the path to the filename
 *
 * \returns true on success, false otherwise.
 */
bool dump_save(const char *path)
{
	if (text_lines_to_file(path, write_character_dump)) {
		msg("Failed to create file %s.new", path);
		return false;
	}

	return true;
}



#define INFO_SCREENS 2 /* Number of screens in character info mode */


/**
 * Change name
 */
void do_cmd_change_name(void)
{
	ui_event ke;
	int mode = 0;
	int prompt_line = 23 + MIN(Term->hgt - 24, 2);
	const char *p;

	bool more = true;

	/* Prompt */
	p = "['c' to change name, 'f' to file, 'h' to change mode, or ESC]";

	/* Save screen */
	screen_save();

	/* Forever */
	while (more) {
		/* Display the player */
		display_player(mode);

		/* Prompt */
		Term_putstr(2, prompt_line, -1, COLOUR_WHITE, p);

		/* Query */
		ke = inkey_ex();

		if ((ke.type == EVT_KBRD)||(ke.type == EVT_BUTTON)) {
			switch (ke.key.code) {
				case ESCAPE: more = false; break;
				case 'c': {
					if(arg_force_name)
						msg("You are not allowed to change your name!");
					else {
					char namebuf[32] = "";

					/* Set player name */
					if (get_character_name(namebuf, sizeof namebuf))
						my_strcpy(player->full_name, namebuf,
								  sizeof(player->full_name));
					}

					break;
				}

				case 'f': {
					char buf[1024];
					char fname[80];

					/* Get the filesystem-safe name and append .txt */
					player_safe_name(fname, sizeof(fname), player->full_name, false);
					my_strcat(fname, ".txt", sizeof(fname));

					if (get_file(fname, buf, sizeof buf)) {
						if (dump_save(buf))
							msg("Character dump successful.");
						else
							msg("Character dump failed!");
					}
					break;
				}
				
				case 'h':
				case ARROW_LEFT:
				case ' ':
					mode = (mode + 1) % INFO_SCREENS;
					break;

				case 'l':
				case ARROW_RIGHT:
					mode = (mode - 1) % INFO_SCREENS;
					break;
			}
		} else if (ke.type == EVT_MOUSE) {
			if (ke.mouse.button == 1) {
				/* Flip through the screens */			
				mode = (mode + 1) % INFO_SCREENS;
			} else if (ke.mouse.button == 2) {
				/* exit the screen */
				more = false;
			} else {
				/* Flip backwards through the screens */			
				mode = (mode - 1) % INFO_SCREENS;
			}
		}

		/* Flush messages */
		event_signal(EVENT_MESSAGE_FLUSH);
	}

	/* Load screen */
	screen_load();
}


static void init_ui_player(void)
{
	/* Nothing to do; lazy initialization. */
}


static void cleanup_ui_player(void)
{
	release_char_sheet_config();
}


struct init_module ui_player_module = {
	.name = "ui-player",
	.init = init_ui_player,
	.cleanup = cleanup_ui_player
};
