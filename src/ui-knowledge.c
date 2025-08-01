/**
 * \file ui-knowledge.c
 * \brief Player knowledge functions
 *
 * Copyright (c) 2000-2007 Eytan Zweig, Andrew Doull, Pete Mack.
 * Copyright (c) 2010 Peter Denison, Chris Carr.
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
#include "cmds.h"
#include "effects.h"
#include "effects-info.h"
#include "game-input.h"
#include "game-world.h"
#include "grafmode.h"
#include "init.h"
#include "mon-init.h"
#include "mon-lore.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-ignore.h"
#include "obj-knowledge.h"
#include "obj-info.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-calcs.h"
#include "player-history.h"
#include "player-util.h"
#include "project.h"
#include "store.h"
#include "target.h"
#include "trap.h"
#include "ui-context.h"
#include "ui-equip-cmp.h"
#include "ui-history.h"
#include "ui-knowledge.h"
#include "ui-menu.h"
#include "ui-mon-list.h"
#include "ui-mon-lore.h"
#include "ui-object.h"
#include "ui-obj-list.h"
#include "ui-options.h"
#include "ui-output.h"
#include "ui-prefs.h"
#include "ui-score.h"
#include "ui-store.h"
#include "ui-target.h"
#include "wizard.h"
#include "z-util.h"

/**
 * The first part of this file contains the knowledge menus.  Generic display
 * routines are followed  by sections which implement "subclasses" of the
 * abstract classes represented by member_funcs and group_funcs.
 *
 * After the knowledge menus are various knowledge functions - message review;
 * inventory, equipment, monster and object lists; symbol lookup; and the 
 * "locate" command which scrolls the screen around the current dungeon level.
 */

typedef struct {
	/* Name of this group */
	const char *(*name)(int gid);

	/* Compares gids of two oids */
	int (*gcomp)(const void *, const void *);

	/* Returns gid for an oid */
	int (*group)(int oid);

	/* Summary function for the "object" information. */
	void (*summary)(int gid, const int *item_list, int n, int top, int row,
					int col);

	/* Maximum possible item count for this class */
	int maxnum;

	/* Items don't need to be IDed to recognize membership */
	bool easy_know;

} group_funcs;

typedef struct {
	/* Displays an entry at given location, including kill-count and graphics */
	void (*display_member)(int col, int row, bool cursor, int oid);

	/* Displays lore for an oid */
	void (*lore)(int oid);


	/* Required only for objects with modifiable display attributes
	 * Unknown 'flavors' return flavor attributes */

	/* Get character attr for OID (by address) */
	wchar_t *(*xchar)(int oid);

	/* Get color attr for OID (by address) */
	uint8_t *(*xattr)(int oid);

	/* Returns optional extra prompt */
	const char *(*xtra_prompt)(int oid);

	/* Handles optional extra actions */
	void (*xtra_act)(struct keypress ch, int oid);

	/* Does this kind have visual editing? */
	bool is_visual;

} member_funcs;


/**
 * Helper class for generating joins
 */
typedef struct join {
		int oid;
		int gid;
} join_t;

static struct parser *init_ui_knowledge_parser(void);
static errr run_ui_knowledge_parser(struct parser *p);
static errr finish_ui_knowledge_parser(struct parser *p);
static void cleanup_ui_knowledge_parsed_data(void);

struct file_parser ui_knowledge_parser = {
	"ui_knowledge",
	init_ui_knowledge_parser,
	run_ui_knowledge_parser,
	finish_ui_knowledge_parser,
	cleanup_ui_knowledge_parsed_data
};

/**
 * A default group-by
 */
static join_t *default_join;

/**
 * Clipboard variables for copy & paste in visual mode
 */
static uint8_t attr_idx = 0;
static wchar_t char_idx = 0;

/**
 * ------------------------------------------------------------------------
 * Knowledge menu utilities
 * ------------------------------------------------------------------------ */

static int default_item_id(int oid)
{
	return default_join[oid].oid;
}

static int default_group_id(int oid)
{
	return default_join[oid].gid;
}

/**
 * Return a specific ordering for the features
 */
static int feat_order(int feat)
{
	struct feature *f = &f_info[feat];

	switch (f->d_char)
	{
		case L'.': 				return 0;
		case L'\'': case L'+': 	return 1;
		case L'<': case L'>':	return 2;
		case L'#':				return 3;
		case L'*': case L'%' :	return 4;
		case L';': case L':' :	return 5;
		case L' ':				return 7;
		default:
		{
			return 6;
		}
	}
}


/**
 * Return the actual width of a symbol
 */
static int actual_width(int width)
{
	return width * tile_width;
}

/**
 * Return the actual height of a symbol
 */
static int actual_height(int height)
{
	return height * tile_height;
}


/**
 * From an actual width, return the logical width
 */
static int logical_width(int width)
{
	return width / tile_width;
}

/**
 * From an actual height, return the logical height
 */
static int logical_height(int height)
{
	return height / tile_height;
}


/**
 * Display tiles.
 */
static void display_tiles(int col, int row, int height, int width,
		uint8_t attr_top, wchar_t char_left)
{
	int i, j;

	/* Clear the display lines */
	for (i = 0; i < height; i++)
			Term_erase(col, row + i, width);

	width = logical_width(width);
	height = logical_height(height);

	/* Display lines until done */
	for (i = 0; i < height; i++) {
		/* Display columns until done */
		for (j = 0; j < width; j++) {
			uint8_t a;
			wchar_t c;
			int x = col + actual_width(j);
			int y = row + actual_height(i);
			int ia, ic;

			ia = attr_top + i;
			ic = char_left + j;

			a = (uint8_t)ia;
			c = (wchar_t)ic;

			/* Display symbol */
			big_pad(x, y, a, c);
		}
	}
}


/**
 * Place the cursor at the correct position for tile picking
 */
static void place_tile_cursor(int col, int row, uint8_t a, wchar_t c,
		uint8_t attr_top, wchar_t char_left)
{
	int i = a - attr_top;
	int j = c - char_left;

	int x = col + actual_width(j);
	int y = row + actual_height(i);

	/* Place the cursor */
	Term_gotoxy(x, y);
}


/**
 * Remove the tile display and clear the screen 
 */
static void remove_tiles(int col, int row, bool *picker_ptr, int width,
						 int height)
{
	int i;

	/* No more big cursor */
	bigcurs = false;

	/* Cancel visual list */
	*picker_ptr = false;

	/* Clear the display lines */
	for (i = 0; i < height; i++)
		Term_erase(col, row + i, width);

}

/**
 *  Do tile picker command -- Change tiles
 */
static bool tile_picker_command(ui_event ke, bool *tile_picker_ptr,
		int height, int width, uint8_t *attr_top_ptr,
		wchar_t *char_left_ptr, uint8_t *cur_attr_ptr,
		wchar_t *cur_char_ptr, int col, int row, int *delay)
{
	static uint8_t attr_old = 0;
	static wchar_t char_old = 0;

	/* These are the distance we want to maintain between the
	 * cursor and borders. */
	int frame_left = logical_width(10);
	int frame_right = logical_width(10);
	int frame_top = logical_height(4);
	int frame_bottom = logical_height(4);


	/* Get mouse movement */
	if (*tile_picker_ptr &&  (ke.type == EVT_MOUSE)) {
		int eff_width = actual_width(width);
		int eff_height = actual_height(height);
		uint8_t a = *cur_attr_ptr;
		wchar_t c = *cur_char_ptr;

		int my = logical_height(ke.mouse.y - row);
		int mx = logical_width(ke.mouse.x - col);

		if ((my >= 0) && (my < eff_height) && (mx >= 0) && (mx < eff_width)
			&& ((ke.mouse.button == 1) || (a != *attr_top_ptr + my)
				|| (c != *char_left_ptr + mx))) {
			/* Set the visual */
			*cur_attr_ptr = a = *attr_top_ptr + my;
			*cur_char_ptr = c = *char_left_ptr + mx;

			/* Move the frame */
			if (*char_left_ptr > MAX(0, (int)c - frame_left))
				(*char_left_ptr)--;
			if (*char_left_ptr + eff_width <= MIN(255, (int)c + frame_right))
				(*char_left_ptr)++;
			if (*attr_top_ptr > MAX(0, (int)a - frame_top))
				(*attr_top_ptr)--;
			if (*attr_top_ptr + eff_height <= MIN(255, (int)a + frame_bottom))
				(*attr_top_ptr)++;

			/* Delay */
			*delay = 100;

			/* Accept change */
			if (ke.mouse.button)
			  remove_tiles(col, row, tile_picker_ptr, width, height);

			return true;
		} else if (ke.mouse.button == 2) {
			/* Cancel change */
			*cur_attr_ptr = attr_old;
			*cur_char_ptr = char_old;
			remove_tiles(col, row, tile_picker_ptr, width, height);

			return true;
		} else {
			return false;
		}
	}

	if (ke.type != EVT_KBRD)
		return false;


	switch (ke.key.code)
	{
		case ESCAPE:
		{
			if (*tile_picker_ptr) {
				/* Cancel change */
				*cur_attr_ptr = attr_old;
				*cur_char_ptr = char_old;
				remove_tiles(col, row, tile_picker_ptr, width, height);

				return true;
			}

			break;
		}

		case KC_ENTER:
		{
			if (*tile_picker_ptr) {
				/* Accept change */
				remove_tiles(col, row, tile_picker_ptr, width, height);
				return true;
			}

			break;
		}

		case 'V':
		case 'v':
		{
			/* No visual mode without graphics, for now - NRM */
			if (current_graphics_mode != NULL)
				if (current_graphics_mode->grafID == 0)
					break;

			if (!*tile_picker_ptr) {
				*tile_picker_ptr = true;
				bigcurs = true;

				*attr_top_ptr = (uint8_t)MAX(0, (int)*cur_attr_ptr - frame_top);
				*char_left_ptr = (wchar_t)MAX(0, (int)*cur_char_ptr - frame_left);

				attr_old = *cur_attr_ptr;
				char_old = *cur_char_ptr;
			} else {
				/* Cancel change */
				*cur_attr_ptr = attr_old;
				*cur_char_ptr = char_old;
				remove_tiles(col, row, tile_picker_ptr, width, height);
			}

			return true;
		}

		case 'C':
		case 'c':
		{
			/* Set the tile */
			attr_idx = *cur_attr_ptr;
			char_idx = *cur_char_ptr;

			return true;
		}

		case 'P':
		case 'p':
		{
			if (attr_idx) {
				/* Set the char */
				*cur_attr_ptr = attr_idx;
				*attr_top_ptr = (uint8_t)MAX(0, (int)*cur_attr_ptr - frame_top);
			}

			if (char_idx) {
				/* Set the char */
				*cur_char_ptr = char_idx;
				*char_left_ptr = (wchar_t)MAX(0, (int)*cur_char_ptr - frame_left);
			}

			return true;
		}

		default:
		{
			int d = target_dir(ke.key);
			uint8_t a = *cur_attr_ptr;
			wchar_t c = *cur_char_ptr;

			if (!*tile_picker_ptr)
				break;

			bigcurs = true;

			/* Restrict direction */
			if ((a == 0) && (ddy[d] < 0)) d = 0;
			if ((c == 0) && (ddx[d] < 0)) d = 0;
			if ((a == 255) && (ddy[d] > 0)) d = 0;
			if ((c == 255) && (ddx[d] > 0)) d = 0;

			a += ddy[d];
			c += ddx[d];

			/* Set the tile */
			*cur_attr_ptr = a;
			*cur_char_ptr = c;

			/* Move the frame */
			if (ddx[d] < 0 &&
					*char_left_ptr > MAX(0, (int)c - frame_left))
				(*char_left_ptr)--;
			if ((ddx[d] > 0) &&
					*char_left_ptr + (width / tile_width) <=
							MIN(255, (int)c + frame_right))
			(*char_left_ptr)++;

			if (ddy[d] < 0 &&
					*attr_top_ptr > MAX(0, (int)a - frame_top))
				(*attr_top_ptr)--;
			if (ddy[d] > 0 &&
					*attr_top_ptr + (height / tile_height) <=
							MIN(255, (int)a + frame_bottom))
				(*attr_top_ptr)++;

			/* We need to always eat the input even if it is clipped,
			 * otherwise it will be interpreted as a change object
			 * selection command with messy results.
			 */
			return true;
		}
	}

	/* Tile picker command is not used */
	return false;
}


/**
 * Display glyph and colours
 */
static void display_glyphs(int col, int row, int height, int width, uint8_t a,
			   wchar_t c)
{
	int i;
	int x, y;

	/* Clear the display lines */
	for (i = 0; i < height; i++)
	        Term_erase(col, row + i, width);

	/* Prompt */
	prt("Choose colour:", row + height/2, col);
	Term_locate(&x, &y);
	for (i = 0; i < MAX_COLORS; i++) big_pad(x + i, y, i, c);
	
	/* Place the cursor */
	Term_gotoxy(x + a, y);
}

/**
 * Do glyph picker command -- Change glyphs
 */
static bool glyph_command(ui_event ke, bool *glyph_picker_ptr,
			  int height, int width, uint8_t *cur_attr_ptr,
			  wchar_t *cur_char_ptr, int col, int row)
{
	static uint8_t attr_old = 0;
	static wchar_t char_old = 0;
	
	/* Get mouse movement */
	if (*glyph_picker_ptr && (ke.type == EVT_MOUSE)) {
		int mx = logical_width(ke.mouse.x - col);
		
		if (ke.mouse.y != row + height / 2) return false;
		
		if ((mx >= 0) && (mx < MAX_COLORS) && (ke.mouse.button == 1)) {
			/* Set the visual */
			*cur_attr_ptr = mx - 14;

			/* Accept change */
			remove_tiles(col, row, glyph_picker_ptr, width, height);
			
			return true;
		} else {
		        return false;
		}
	}

	if (ke.type != EVT_KBRD)
	        return false;


	switch (ke.key.code)
	{
	        case ESCAPE:
		{
			if (*glyph_picker_ptr) {
				/* Cancel change */
				*cur_attr_ptr = attr_old;
				*cur_char_ptr = char_old;
				remove_tiles(col, row, glyph_picker_ptr, width, height);
				
				return true;
			}

			break;
		}

	    case KC_ENTER:
	    {
		    if (*glyph_picker_ptr) {
			    /* Accept change */
			    remove_tiles(col, row, glyph_picker_ptr, width, height);
			    return true;
		    }
		    
		    break;
	    }

	    case 'V':
	    case 'v':
	    {
		    if (!*glyph_picker_ptr) {
			    *glyph_picker_ptr = true;

			    attr_old = *cur_attr_ptr;
			    char_old = *cur_char_ptr;
		    } else {
			    /* Cancel change */
			    *cur_attr_ptr = attr_old;
			    *cur_char_ptr = char_old;
			    remove_tiles(col, row, glyph_picker_ptr, width, height);
		    }

		    return true;
	    }

		case 'C':
		case 'c':
		{
			/* Set the tile */
			attr_idx = *cur_attr_ptr;
			char_idx = *cur_char_ptr;

			return true;
		}

		case 'P':
		case 'p':
		{
			if (attr_idx) {
				/* Set the char */
				*cur_attr_ptr = attr_idx;
			}

			if (char_idx) {
				/* Set the char */
				*cur_char_ptr = char_idx;
			}

			return true;
		}

	    case 'i':
	    case 'I':
	    {
		    if (*glyph_picker_ptr) {
			    char code_point[6];
			    bool res = false;
	
			    /* Ask the user for a code point */
			    Term_gotoxy(col, row + height/2 + 2);
			    res = get_string("(up to 5 hex digits):", code_point, 5);
	
			    /* Process input */
			    if (res) {
				    unsigned long int point = strtoul(code_point,
													  (char **)NULL, 16);
				    *cur_char_ptr = (wchar_t) point;
				    return true;
			    }
		    }
		    
		    break;
		    
		    
	    }
	    
	    default:
	    {
		    int d = target_dir(ke.key);
		    uint8_t a = *cur_attr_ptr;
		    
		    if (!*glyph_picker_ptr)
				break;

		    /* Horizontal only */
		    if (ddy[d] != 0) break;
		    
		    /* Horizontal movement */
		    if (ddx[d] != 0) {
				a += ddx[d] + BASIC_COLORS;
				a = a % BASIC_COLORS;
				*cur_attr_ptr = a;
		    }
    
	
		    /* We need to always eat the input even if it is clipped,
		     * otherwise it will be interpreted as a change object
		     * selection command with messy results.
		     */
		    return true;
	    }
	}

	/* Glyph picker command is not used */
	return false;
}

static void display_group_member(struct menu *menu, int oid,
						bool cursor, int row, int col, int wid)
{
	const member_funcs *o_funcs = menu->menu_data;
	uint8_t attr = curs_attrs[CURS_KNOWN][cursor == oid];

	(void)wid;

	/* Print the interesting part */
	o_funcs->display_member(col, row, cursor, oid);

#ifdef KNOWLEDGE_MENU_DEBUG
	c_put_str(attr, format("%d", oid), row, 60);
#endif

	/* Do visual mode */
	if (o_funcs->is_visual && o_funcs->xattr) {
		wchar_t c = *o_funcs->xchar(oid);
		uint8_t a = *o_funcs->xattr(oid);
		char buf[12];

		strnfmt(buf, sizeof(buf), "%d/%ld", a, (long int)c);
		c_put_str(attr, buf, row, 64 - (int) strlen(buf));
	}
}

static const char *recall_prompt(int oid)
{
	(void)oid;
	return ", 'r' to recall";
}

#define swap(a, b) (swapspace = (void*)(a)), ((a) = (b)), ((b) = swapspace)

/* Flag value for missing array entry */
#define MISSING -17

/**
 * Interactive group by.
 * Recognises inscriptions, graphical symbols, lore
 */
static void display_knowledge(const char *title, int *obj_list, int o_count,
				group_funcs g_funcs, member_funcs o_funcs,
				const char *otherfields)
{
	/* Maximum number of groups to display */
	int max_group = g_funcs.maxnum < o_count ? g_funcs.maxnum : o_count ;

	/* This could (should?) be (void **) */
	int *g_list, *g_offset;

	const char **g_names;

	int g_name_len = 8;  /* group name length, minumum is 8 */

	int grp_cnt = 0; /* total number groups */

	int g_cur = 0, grp_old = -1; /* group list positions */
	int o_cur = 0;					/* object list positions */
	int g_o_count = 0;				 /* object count for group */
	int oid;  				/* object identifiers */

	region title_area = { 0, 0, 0, 4 };
	region group_region = { 0, 6, MISSING, -2 };
	region object_region = { MISSING, 6, 0, -2 };

	/* display state variables */
	bool tiles = (current_graphics_mode != NULL);
	bool tile_picker = false;
	bool glyph_picker = false;
	uint8_t attr_top = 0;
	wchar_t char_left = 0;

	int delay = 0;

	struct menu group_menu;
	struct menu object_menu;
	menu_iter object_iter = { NULL, NULL, display_group_member, NULL, NULL };

	/* Panel state */
	/* These are swapped in parallel whenever the actively browsing " */
	/* changes */
	int *active_cursor = &g_cur, *inactive_cursor = &o_cur;
	struct menu *active_menu = &group_menu, *inactive_menu = &object_menu;
	int panel = 0;

	void *swapspace;
	bool do_swap = false;

	bool flag = false;
	bool redraw = true;

	int browser_rows;
	int wid, hgt;
	int i;
	int prev_g = -1;
	ui_event ke;

	/* Get size */
	Term_get_size(&wid, &hgt);
	browser_rows = hgt - 8;

	/* Determine if using tiles or not */
	if (tiles) tiles = (current_graphics_mode->grafID != 0);

	if (g_funcs.gcomp)
		sort(obj_list, o_count, sizeof(*obj_list), g_funcs.gcomp);

	/* Sort everything into group order */
	g_list = mem_zalloc((max_group + 1) * sizeof(int));
	g_offset = mem_zalloc((max_group + 1) * sizeof(int));

	for (i = 0; i < o_count; i++) {
		if (prev_g != g_funcs.group(obj_list[i])) {
			prev_g = g_funcs.group(obj_list[i]);
			g_offset[grp_cnt] = i;
			g_list[grp_cnt++] = prev_g;
		}
	}

	g_offset[grp_cnt] = o_count;
	g_list[grp_cnt] = -1;


	/* The compact set of group names, in display order */
	g_names = mem_zalloc(grp_cnt * sizeof(char*));

	for (i = 0; i < grp_cnt; i++) {
		int len;
		g_names[i] = g_funcs.name(g_list[i]);
		len = strlen(g_names[i]);
		if (len > g_name_len) g_name_len = len;
	}

	/* Reasonable max group name len */
	if (g_name_len >= 20) g_name_len = 20;

	object_region.col = g_name_len + 3;
	group_region.width = g_name_len;


	/* Leave room for the group summary information */
	if (g_funcs.summary) object_region.page_rows = -3;


	/* Set up the two menus */
	menu_init(&group_menu, MN_SKIN_SCROLL, menu_find_iter(MN_ITER_STRINGS));
	menu_setpriv(&group_menu, grp_cnt, g_names);
	menu_layout(&group_menu, &group_region);
	group_menu.flags |= MN_DBL_TAP;

	menu_init(&object_menu, MN_SKIN_SCROLL, &object_iter);
	menu_setpriv(&object_menu, 0, &o_funcs);
	menu_layout(&object_menu, &object_region);
	object_menu.flags |= MN_DBL_TAP;

	o_funcs.is_visual = false;

	/* Save screen */
	screen_save();
	clear_from(0);

	/* This is the event loop for a multi-region panel */
	/* Panels are -- text panels, two menus, and visual browser */
	/* with "pop-up menu" for lore */
	while ((!flag) && (grp_cnt)) {
		bool recall = false;

		if (redraw) {
			/* Print the title bits */
			region_erase(&title_area);
			prt(format("Knowledge - %s", title), 2, 0);
			prt("Group", 4, 0);
			prt("Name", 4, g_name_len + 3);

			if (otherfields)
				prt(otherfields, 4, 46);


			/* Print dividers: horizontal and vertical */
			for (i = 0; i < 79; i++)
				Term_putch(i, 5, COLOUR_WHITE, L'=');

			for (i = 0; i < browser_rows; i++)
				Term_putch(g_name_len + 1, 6 + i, COLOUR_WHITE, L'|');


			/* Reset redraw flag */
			redraw = false;
		}

		if (g_cur != grp_old) {
			grp_old = g_cur;
			o_cur = 0;
			g_o_count = g_offset[g_cur+1] - g_offset[g_cur];
			menu_set_filter(&object_menu, obj_list + g_offset[g_cur],
							g_o_count);
			group_menu.cursor = g_cur;
			object_menu.cursor = 0;
		}

		/* HACK ... */
		if (!(tile_picker || glyph_picker)) {
			/* ... The object menu may be browsing the entire group... */
			o_funcs.is_visual = false;
			menu_set_filter(&object_menu, obj_list + g_offset[g_cur],
							g_o_count);
			object_menu.cursor = o_cur;
		} else {
			/* ... or just a single element in the group. */
			o_funcs.is_visual = true;
			menu_set_filter(&object_menu, obj_list + o_cur + g_offset[g_cur],
							1);
			object_menu.cursor = 0;
		}

		oid = obj_list[g_offset[g_cur]+o_cur];

		/* Print prompt */
		{
			const char *pedit = (!o_funcs.xattr) ? "" :
					(!(attr_idx|char_idx) ?
					 ", 'c' to copy" : ", 'c', 'p' to paste");
			const char *xtra = o_funcs.xtra_prompt ?
				o_funcs.xtra_prompt(oid) : "";
			const char *pvs = "";

			if (tile_picker) pvs = ", ENTER to accept";
			else if (glyph_picker) pvs = ", 'i' to insert, ENTER to accept";
			else if (o_funcs.xattr) pvs = ", 'v' for visuals";

			prt(format("<dir>%s%s%s, ESC", pvs, pedit, xtra), hgt - 1, 0);
		}

		if (do_swap) {
			do_swap = false;
			swap(active_menu, inactive_menu);
			swap(active_cursor, inactive_cursor);
			panel = 1 - panel;
		}

		if (g_funcs.summary && !tile_picker && !glyph_picker) {
			g_funcs.summary(g_cur, obj_list, g_o_count, g_offset[g_cur],
			                object_menu.active.row +
							object_menu.active.page_rows,
			                object_region.col);
		}

		menu_refresh(inactive_menu, false);
		menu_refresh(active_menu, false);

		handle_stuff(player);

		if (tile_picker) {
		        bigcurs = true;
			display_tiles(g_name_len + 3, 7, browser_rows - 1,
				      wid - (g_name_len + 3), attr_top,
				      char_left);
			place_tile_cursor(g_name_len + 3, 7, 
					  *o_funcs.xattr(oid),
					  *o_funcs.xchar(oid),
					  attr_top, char_left);
		}

		if (glyph_picker) {
		        display_glyphs(g_name_len + 3, 7, browser_rows - 1,
				       wid - (g_name_len + 3),
				       *o_funcs.xattr(oid),
				       *o_funcs.xchar(oid));
		}

		if (delay) {
			/* Force screen update */
			Term_fresh();

			/* Delay */
			Term_xtra(TERM_XTRA_DELAY, delay);

			delay = 0;
		}

		ke = inkey_ex();
		if (!tile_picker && !glyph_picker) {
			ui_event ke0 = EVENT_EMPTY;

			if (ke.type == EVT_MOUSE)
				menu_handle_mouse(active_menu, &ke, &ke0);
			else if (ke.type == EVT_KBRD)
				menu_handle_keypress(active_menu, &ke, &ke0);

			if (ke0.type != EVT_NONE)
				ke = ke0;
		}

		/* XXX Do visual mode command if needed */
		if (o_funcs.xattr && o_funcs.xchar) {
			if (tiles) {
				if (tile_picker_command(ke, &tile_picker, 
						browser_rows - 1,
						wid - (g_name_len + 3),
						&attr_top, &char_left,
						o_funcs.xattr(oid),
						o_funcs.xchar(oid),
						g_name_len + 3, 7, &delay))
					continue;
			} else {
				if (glyph_command(ke, &glyph_picker, 
						browser_rows - 1, wid - (g_name_len + 3),
						o_funcs.xattr(oid),
						o_funcs.xchar(oid),
						g_name_len + 3, 7))
					continue;
			}
		}

		switch (ke.type)
		{
			case EVT_KBRD:
			{
				if (ke.key.code == 'r' || ke.key.code == 'R')
					recall = true;
				else if (o_funcs.xtra_act)
					o_funcs.xtra_act(ke.key, oid);

				break;
			}

			case EVT_MOUSE:
			{
				/* Change active panels */
				if (region_inside(&inactive_menu->active, &ke)) {
					swap(active_menu, inactive_menu);
					swap(active_cursor, inactive_cursor);
					panel = 1-panel;
				}

				continue;
			}

			case EVT_ESCAPE:
			{
				if (panel == 1)
					do_swap = true;
				else
					flag = true;

				break;
			}

			case EVT_SELECT:
			{
				if (panel == 0)
					do_swap = true;
				else if (panel == 1 && oid >= 0 && o_cur == active_menu->cursor)
					recall = true;
				break;
			}

			case EVT_MOVE:
			{
				*active_cursor = active_menu->cursor;
				break;
			}

			default:
			{
				break;
			}
		}

		/* Recall on screen */
		if (recall) {
			if (oid >= 0)
				o_funcs.lore(oid);

			redraw = true;
		}
	}

	/* Prompt */
	if (!grp_cnt)
		prt(format("No %s known.", title), 15, 0);

	mem_free(g_names);
	mem_free(g_offset);
	mem_free(g_list);

	screen_load();
}

/**
 * ------------------------------------------------------------------------
 *  MONSTERS
 * ------------------------------------------------------------------------ */

/**
 * Is a flat array describing each monster group.  Configured by
 * ui_knowledge.txt.  The last element receives special treatment and is
 * used to catch any type of monster not caught by the other categories.
 * That's intended as a debugging tool while modding the game.
 */
static struct ui_monster_category *monster_group = NULL;

/**
 * Is the number of entries, including the last one receiving special
 * treatment, in monster_group.
 */
static int n_monster_group = 0;

/**
 * Display a monster
 */
static void display_monster(int col, int row, bool cursor, int oid)
{
	/* HACK Get the race index. (Should be a wrapper function) */
	int r_idx = default_item_id(oid);

	/* Access the race */
	struct monster_race *race = &r_info[r_idx];
	struct monster_lore *lore = &l_list[r_idx];

	/* Choose colors */
	uint8_t attr = curs_attrs[CURS_KNOWN][(int)cursor];
	uint8_t a = monster_x_attr[race->ridx];
	wchar_t c = monster_x_char[race->ridx];

	if ((tile_height != 1) && (a & 0x80)) {
		a = race->d_attr;
		c = race->d_char;
		/* If uniques are purple, make it so */
		if (OPT(player, purple_uniques) && rf_has(race->flags, RF_UNIQUE))
			a = COLOUR_VIOLET;
	}
	/* If uniques are purple, make it so */
	else if (OPT(player, purple_uniques) && !(a & 0x80) &&
			 rf_has(race->flags, RF_UNIQUE))
		a = COLOUR_VIOLET;

	/* Display the name */
	if (rf_has(race->flags, RF_PLAYER_GHOST)) {
		c_prt(attr, format("%s, the %s", cave->ghost->name, race->name),
			  row, col);
	} else {
		c_prt(attr, race->name, row, col);
	}

	/* Display symbol */
	big_pad(66, row, a, c);

	/* Display kills */
	if (!race->rarity) {
		put_str(format("%s", "shape"), row, 70);
	} else if (rf_has(race->flags, RF_UNIQUE)) {
		put_str(format("%s", (race->max_num == 0)?  " dead" : "alive"),
				row, 70);
	} else {
		put_str(format("%5d", lore->pkills), row, 70);
	}
}

static int m_cmp_race(const void *a, const void *b)
{
	const int a_val = *(const int *)a;
	const int b_val = *(const int *)b;
	const struct monster_race *r_a = &r_info[default_item_id(a_val)];
	const struct monster_race *r_b = &r_info[default_item_id(b_val)];
	int gid = default_group_id(a_val);

	/* Group by */
	int c = gid - default_group_id(b_val);
	if (c)
		return c;

	/*
	 * If the group specifies monster bases, order those that are included
	 * by the base by those bases.  Those that aren't in any of the bases
	 * appear last.
	 */
	assert(gid >= 0 && gid < n_monster_group);
	if (monster_group[gid].n_inc_bases) {
		int base_a = monster_group[gid].n_inc_bases;
		int base_b = monster_group[gid].n_inc_bases;
		int i;

		for (i = 0; i < monster_group[gid].n_inc_bases; ++i) {
			if (r_a->base == monster_group[gid].inc_bases[i]) {
				base_a = i;
			}
			if (r_b->base == monster_group[gid].inc_bases[i]) {
				base_b = i;
			}
		}
		c = base_a - base_b;
		if (c) {
			return c;
		}
	}

	/*
	 * Within the same base or outside of a specified base, order by level
	 * and then by name.
	 */
	c = r_a->level - r_b->level;
	if (c)
		return c;

	return strcmp(r_a->name, r_b->name);
}

static wchar_t *m_xchar(int oid)
{
	return &monster_x_char[default_join[oid].oid];
}

static uint8_t *m_xattr(int oid)
{
	return &monster_x_attr[default_join[oid].oid];
}

static const char *race_name(int gid)
{
	return monster_group[gid].name;
}

static void mon_lore(int oid)
{
	int r_idx;
	struct monster_race *race;
	const struct monster_lore *lore;
	textblock *tb;

	r_idx = default_item_id(oid);

	assert(r_idx);
	race = &r_info[r_idx];
	lore = get_lore(race);

	/* Update the monster recall window */
	monster_race_track(player->upkeep, race);
	handle_stuff(player);

	tb = textblock_new();
	lore_description(tb, race, lore, false);
	textui_textblock_show(tb, SCREEN_REGION, NULL);
	textblock_free(tb);
}

static void mon_summary(int gid, const int *item_list, int n, int top,
						int row, int col)
{
	int i;
	int kills = 0;

	/* Access the race */
	for (i = 0; i < n; i++) {
		int oid = default_join[item_list[i+top]].oid;
		kills += l_list[oid].pkills;
	}

	/* Different display for the first item if we've got uniques to show */
	if (gid == 0 &&
		rf_has((&r_info[default_join[item_list[0]].oid])->flags, RF_UNIQUE)) {
		c_prt(COLOUR_L_BLUE, format("%d known uniques, %d slain.", n, kills),
					row, col);
	} else {
		int tkills = 0;

		for (i = 0; i < z_info->r_max; i++)
			tkills += l_list[i].pkills;

		c_prt(COLOUR_L_BLUE, format("Creatures slain: %d/%d (in group/in total)", kills, tkills), row, col);
	}
}

static int count_known_monsters(void)
{
	int m_count = 0, i;

	for (i = 0; i < z_info->r_max; ++i) {
		struct monster_race *race = &r_info[i];
		bool classified = false;
		int j;

		if (!l_list[i].all_known && !l_list[i].sights) {
			continue;
		}
		if (!race->name) continue;

		for (j = 0; j < n_monster_group - 1; ++j) {
			bool has_base = false;

			if (monster_group[j].n_inc_bases) {
				int k;

				for (k = 0; k < monster_group[j].n_inc_bases;
						++k) {
					if (race->base == monster_group[j].inc_bases[k]) {
						++m_count;
						has_base = true;
						classified = true;
						break;
					}
				}
			}
			if (!has_base && rf_is_inter(race->flags,
					monster_group[j].inc_flags)) {
				++m_count;
				classified = true;
			}
		}

		if (!classified) {
			++m_count;
		}
	}

	return m_count;
}

/**
 * Display known monsters.
 */
static void do_cmd_knowledge_monsters(const char *name, int row)
{
	group_funcs r_funcs = {race_name, m_cmp_race, default_group_id,
		mon_summary, n_monster_group, false };

	member_funcs m_funcs = {display_monster, mon_lore, m_xchar, m_xattr,
		recall_prompt, 0, 0};

	int *monsters;
	int m_count = count_known_monsters(), i, ind;

	default_join = mem_zalloc(m_count * sizeof(join_t));
	monsters = mem_zalloc(m_count * sizeof(int));

	ind = 0;
	for (i = 0; i < z_info->r_max; ++i) {
		struct monster_race *race = &r_info[i];
		bool classified = false;
		int j;

		if (!l_list[i].all_known && !l_list[i].sights) {
			continue;
		}
		if (!race->name) continue;

		for (j = 0; j < n_monster_group - 1; ++j) {
			bool has_base = false;

			if (monster_group[j].n_inc_bases) {
				int k;

				for (k = 0; k < monster_group[j].n_inc_bases;
						++k) {
					if (race->base == monster_group[j].inc_bases[k]) {
						assert(ind < m_count);
						monsters[ind] = ind;
						default_join[ind].oid = i;
						default_join[ind].gid = j;
						++ind;
						has_base = true;
						classified = true;
						break;
					}
				}
			}
			if (!has_base && rf_is_inter(race->flags,
					monster_group[j].inc_flags)) {
				assert(ind < m_count);
				monsters[ind] = ind;
				default_join[ind].oid = i;
				default_join[ind].gid = j;
				++ind;
				classified = true;
			}
		}

		if (!classified) {
			assert(ind < m_count);
			monsters[ind] = ind;
			default_join[ind].oid = i;
			default_join[ind].gid = n_monster_group - 1;
			++ind;
		}
	}

	display_knowledge("monsters", monsters, m_count, r_funcs, m_funcs,
			"                   Sym  Kills");
	mem_free(default_join);
	mem_free(monsters);
}

/**
 * ------------------------------------------------------------------------
 *  ARTIFACTS
 * ------------------------------------------------------------------------ */

/**
 * These are used for all the object sections
 */
static const grouper object_text_order[] =
{
	{TV_RING,			"Ring"			},
	{TV_AMULET,			"Amulet"		},
	{TV_POTION,			"Potion"		},
	{TV_SCROLL,			"Scroll"		},
	{TV_WAND,			"Wand"			},
	{TV_STAFF,			"Staff"			},
	{TV_ROD,			"Rod"			},
 	{TV_FOOD,			"Food"			},
 	{TV_MUSHROOM,		"Mushroom"		},
	{TV_PRAYER_BOOK,	"Priest Book"	},
	{TV_MAGIC_BOOK,		"Magic Book"	},
	{TV_NATURE_BOOK,	"Nature Book"	},
	{TV_SHADOW_BOOK,	"Shadow Book"	},
	{TV_OTHER_BOOK,		"Mystery Book"	},
	{TV_LIGHT,			"Light"			},
	{TV_FLASK,			"Flask"			},
	{TV_SWORD,			"Sword"			},
	{TV_POLEARM,		"Polearm"		},
	{TV_HAFTED,			"Hafted Weapon" },
	{TV_BOW,			"Bow"			},
	{TV_ARROW,			"Ammunition"	},
	{TV_BOLT,			NULL			},
	{TV_SHOT,			NULL			},
	{TV_SHIELD,			"Shield"		},
	{TV_CROWN,			"Crown"			},
	{TV_HELM,			"Helm"			},
	{TV_GLOVES,			"Gloves"		},
	{TV_BOOTS,			"Boots"			},
	{TV_CLOAK,			"Cloak"			},
	{TV_DRAG_ARMOR,		"Dragon Scale Mail" },
	{TV_HARD_ARMOR,		"Hard Armor"	},
	{TV_SOFT_ARMOR,		"Soft Armor"	},
	{TV_DIGGING,		"Digger"		},
	{TV_GOLD,			"Money"			},
	{0,					NULL			}
};

static int *obj_group_order = NULL;

static void get_artifact_display_name(char *o_name, size_t namelen, int a_idx)
{
	struct object body = OBJECT_NULL, known_body = OBJECT_NULL;
	struct object *obj = &body, *known_obj = &known_body;

	make_fake_artifact(obj, &a_info[a_idx]);
	object_wipe(known_obj);
	object_copy(known_obj, obj);
	obj->known = known_obj;
	object_desc(o_name, namelen, obj,
		ODESC_PREFIX | ODESC_BASE | ODESC_SPOIL, NULL);
	object_wipe(known_obj);
	object_wipe(obj);
}

/**
 * Display an artifact label
 */
static void display_artifact(int col, int row, bool cursor, int oid)
{
	uint8_t attr = curs_attrs[CURS_KNOWN][(int)cursor];
	char o_name[80];

	get_artifact_display_name(o_name, sizeof o_name, oid);

	c_prt(attr, o_name, row, col);
}

/**
 * Look for an artifact
 */
static struct object *find_artifact(struct artifact *artifact)
{
	int y, x, i;
	struct object *obj;

	/* Ground objects */
	for (y = 1; y < cave->height; y++) {
		for (x = 1; x < cave->width; x++) {
			struct loc grid = loc(x, y);
			for (obj = square_object(cave, grid); obj; obj = obj->next) {
				if (obj->artifact == artifact) return obj;
			}
		}
	}

	/* Player objects */
	for (obj = player->gear; obj; obj = obj->next) {
		if (obj->artifact == artifact) return obj;
	}

	/* Monster objects */
	for (i = cave_monster_max(cave) - 1; i >= 1; i--) {
		struct monster *mon = cave_monster(cave, i);
		obj = mon ? mon->held_obj : NULL;

		while (obj) {
			if (obj->artifact == artifact) return obj;
			obj = obj->next;
		}
	}

	/* Store objects */
	for (i = 0; i < world->num_towns; i++) {
		struct town *town = &world->towns[i];
		struct store *s = town->stores;
		while (s) {
			for (obj = s->stock; obj; obj = obj->next) {
				if (obj->artifact == artifact) return obj;
			}
			s = s->next;
		}
	}

	/* Stored chunk objects */
	for (i = 0; i < chunk_list_max; i++) {
		struct chunk *c = chunk_list[i];
		int j;
		if (strstr(c->name, "known")) continue;

		/* Ground objects */
		for (y = 1; y < c->height; y++) {
			for (x = 1; x < c->width; x++) {
				struct loc grid = loc(x, y);
				for (obj = square_object(c, grid); obj; obj = obj->next) {
					if (obj->artifact == artifact) return obj;
				}
			}
		}

		/* Monster objects */
		for (j = cave_monster_max(c) - 1; j >= 1; j--) {
			struct monster *mon = cave_monster(c, j);
			obj = mon ? mon->held_obj : NULL;

			while (obj) {
				if (obj->artifact == artifact) return obj;
				obj = obj->next;
			}
		}
	}	

	return NULL;
}

/**
 * Show artifact lore
 */
static void desc_art_fake(int a_idx)
{
	struct object *obj, *known_obj = NULL;
	struct object object_body = OBJECT_NULL, known_object_body = OBJECT_NULL;
	bool fake = false;

	char header[120];

	textblock *tb;
	region area = { 0, 0, 0, 0 };

	obj = find_artifact(&a_info[a_idx]);

	/* If it's been lost, make a fake artifact for it */
	if (!obj) {
		fake = true;
		obj = &object_body;
		known_obj = &known_object_body;

		make_fake_artifact(obj, &a_info[a_idx]);
		obj->known = known_obj;
		known_obj->artifact = obj->artifact;
		known_obj->kind = obj->kind;

		/* Check the history entry, to see if it was fully known before it
		 * was lost */
		if (history_is_artifact_known(player, obj->artifact))
			/* Be very careful not to influence anything but this object */
			object_copy(known_obj, obj);
	}

	/*Handle stuff */
	handle_stuff(player);

	tb = object_info(obj, OINFO_NONE);
	object_desc(header, sizeof(header), obj,
		ODESC_PREFIX | ODESC_FULL | ODESC_CAPITAL, player);
	if (fake) {
		object_wipe(known_obj);
		object_wipe(obj);
	}

	textui_textblock_show(tb, area, header);
	textblock_free(tb);
}

static int a_cmp_tval(const void *a, const void *b)
{
	const int a_val = *(const int *)a;
	const int b_val = *(const int *)b;
	const struct artifact *a_a = &a_info[a_val];
	const struct artifact *a_b = &a_info[b_val];

	/* Group by */
	int ta = obj_group_order[a_a->tval];
	int tb = obj_group_order[a_b->tval];
	int c = ta - tb;
	if (c) return c;

	/* Order by */
	c = a_a->sval - a_b->sval;
	if (c) return c;
	return strcmp(a_a->name, a_b->name);
}

static const char *kind_name(int gid)
{
	return object_text_order[gid].name;
}

static int art2gid(int oid)
{
	return obj_group_order[a_info[oid].tval];
}

/**
 * Check if the given artifact idx is something we should "Know" about
 */
static bool artifact_is_known(int a_idx)
{
	struct object *obj;

	if (!a_info[a_idx].name)
		return false;

	if (player->wizard)
		return true;

	if (!is_artifact_created(&a_info[a_idx]))
		return false;

	/* Check all objects to see if it exists but hasn't been IDed */
	obj = find_artifact(&a_info[a_idx]);
	if (obj && !object_is_known_artifact(obj))
		return false;

	return true;
}


/**
 * If 'artifacts' is NULL, it counts the number of known artifacts, otherwise
 * it collects the list of known artifacts into 'artifacts' as well.
 */
static int collect_known_artifacts(int *artifacts, size_t artifacts_len)
{
	int a_count = 0;
	int j;

	if (artifacts)
		assert(artifacts_len >= z_info->a_max);

	for (j = 0; j < z_info->a_max; j++) {
		/* Artifact doesn't exist */
		if (!a_info[j].name) continue;

		if (OPT(player, cheat_xtra) || artifact_is_known(j)) {
			if (artifacts)
				artifacts[a_count++] = j;
			else
				a_count++;
		}
	}

	return a_count;
}

/**
 * Display known artifacts
 */
static void do_cmd_knowledge_artifacts(const char *name, int row)
{
	/* HACK -- should be TV_MAX */
	group_funcs obj_f = {kind_name, a_cmp_tval, art2gid, 0, TV_MAX, false};
	member_funcs art_f = {display_artifact, desc_art_fake, 0, 0, recall_prompt,
						  0, 0};

	int *artifacts;
	int a_count = 0;

	artifacts = mem_zalloc(z_info->a_max * sizeof(int));

	/* Collect valid artifacts */
	a_count = collect_known_artifacts(artifacts, z_info->a_max);

	display_knowledge("artifacts", artifacts, a_count, obj_f, art_f, NULL);
	mem_free(artifacts);
}

/**
 * ------------------------------------------------------------------------
 *  EGO ITEMS
 * ------------------------------------------------------------------------ */

static const char *ego_grp_name(int gid)
{
	return object_text_order[gid].name;
}

static void display_ego_item(int col, int row, bool cursor, int oid)
{
	/* Access the object */
	struct ego_item *ego = &e_info[default_item_id(oid)];

	/* Choose a color */
	uint8_t attr = curs_attrs[0 != (int)ego->everseen][0 != (int)cursor];

	/* Display the name */
	c_prt(attr, ego->name, row, col);
}

/**
 * Describe fake ego item "lore"
 */
static void desc_ego_fake(int oid)
{
	int e_idx = default_item_id(oid);
	struct ego_item *ego = &e_info[e_idx];

	textblock *tb;
	region area = { 0, 0, 0, 0 };

	/* List ego flags */
	tb = object_info_ego(ego);

	textui_textblock_show(tb, area, format("%s %s",
										   ego_grp_name(default_group_id(oid)),
										   ego->name));
	textblock_free(tb);
}

/* TODO? Currently ego items will order by e_idx */
static int e_cmp_tval(const void *a, const void *b)
{
	const int a_val = *(const int *)a;
	const int b_val = *(const int *)b;
	const struct ego_item *ea = &e_info[default_item_id(a_val)];
	const struct ego_item *eb = &e_info[default_item_id(b_val)];

	/* Group by */
	int c = default_group_id(a_val) - default_group_id(b_val);
	if (c) return c;

	/* Order by */
	return strcmp(ea->name, eb->name);
}

/**
 * Display known ego_items
 */
static void do_cmd_knowledge_ego_items(const char *name, int row)
{
	group_funcs obj_f =
		{ego_grp_name, e_cmp_tval, default_group_id, 0, TV_MAX, false};

	member_funcs ego_f =
		{display_ego_item, desc_ego_fake, 0, 0, recall_prompt, 0, 0};

	int *egoitems;
	int e_count = 0;
	int i;

	/* Overkill - NRM */
	int max_pairs = z_info->e_max * N_ELEMENTS(object_text_order);
	egoitems = mem_zalloc(max_pairs * sizeof(int));
	default_join = mem_zalloc(max_pairs * sizeof(join_t));

	/* Look at all the ego items */
	for (i = 0; i < z_info->e_max; i++)	{
		struct ego_item *ego = &e_info[i];
		if (ego->everseen || OPT(player, cheat_xtra)) {
			size_t j;
			int *tval = mem_zalloc(N_ELEMENTS(object_text_order) * sizeof(int));
			struct poss_item *poss;

			/* Note the tvals which are possible for this ego */
			for (poss = ego->poss_items; poss; poss = poss->next) {
				struct object_kind *kind = &k_info[poss->kidx];
				assert(obj_group_order[kind->tval] >= 0);
				tval[obj_group_order[kind->tval]]++;
			}

			/* Count and put into the list */
			for (j = 0; j < TV_MAX; j++) {
				int gid = obj_group_order[j];

				/* Skip if nothing in this group */
				if (gid < 0) continue;

				/* Ignore duplicates */
				if ((e_count > 0) && (gid == default_join[e_count - 1].gid)
					&& (i == default_join[e_count - 1].oid))
					continue;

				if (tval[gid]) {
					egoitems[e_count] = e_count;
					default_join[e_count].oid = i;
					default_join[e_count++].gid = gid;
				}
			}
			mem_free(tval);
		}
	}

	display_knowledge("ego items", egoitems, e_count, obj_f, ego_f, NULL);

	mem_free(default_join);
	mem_free(egoitems);
}

/**
 * ------------------------------------------------------------------------
 * ORDINARY OBJECTS
 * ------------------------------------------------------------------------ */

/**
 * Display the objects in a group.
 */
static void display_object(int col, int row, bool cursor, int oid)
{
	struct object_kind *kind = &k_info[oid];
	const char *inscrip = get_autoinscription(kind, kind->aware);

	char o_name[80];

	/* Choose a color */
	bool aware = (!kind->flavor || kind->aware);
	uint8_t attr = curs_attrs[(int)aware][(int)cursor];

	/* Graphics versions of the object_char and object_attr defines */
	uint8_t a = object_kind_attr(kind);
	wchar_t c = object_kind_char(kind);

	/* Don't display special artifacts */
	if (!kf_has(kind->kind_flags, KF_INSTA_ART))
 		object_kind_name(o_name, sizeof(o_name), kind, OPT(player, cheat_xtra));

	/* If the type is "tried", display that */
	if (kind->tried && !aware)
		my_strcat(o_name, " {tried}", sizeof(o_name));

	/* Display the name */
	c_prt(attr, o_name, row, col);

	/* Show ignore status */
	if ((aware && kind_is_ignored_aware(kind)) ||
		(!aware && kind_is_ignored_unaware(kind)))
		c_put_str(attr, "Yes", row, 46);


	/* Show autoinscription if around */
	if (inscrip)
		c_put_str(COLOUR_YELLOW, inscrip, row, 55);

	if (tile_height == 1) {
		big_pad(76, row, a, c);
	}
}

/**
 * Describe fake object
 */
static void desc_obj_fake(int k_idx)
{
	struct object_kind *kind = &k_info[k_idx];
	struct object_kind *old_kind = player->upkeep->object_kind;
	struct object *old_obj = player->upkeep->object;
	struct object *obj = object_new(), *known_obj = object_new();

	char header[120];

	textblock *tb;
	region area = { 0, 0, 0, 0 };

	/* Update the object recall window */
	track_object_kind(player->upkeep, kind);
	handle_stuff(player);

	/* Create the artifact */
	object_prep(obj, kind, 0, EXTREMIFY);

	/* It's fully known */
	if (kind->aware || !kind->flavor)
		object_copy(known_obj, obj);
	obj->known = known_obj;

	/* Handle stuff */
	handle_stuff(player);

	tb = object_info(obj, OINFO_FAKE);
	object_desc(header, sizeof(header), obj,
		ODESC_PREFIX | ODESC_CAPITAL, player);

	textui_textblock_show(tb, area, header);
	object_delete(NULL, NULL, &known_obj);
	object_delete(NULL, NULL, &obj);
	textblock_free(tb);

	/* Restore the old trackee */
	if (old_kind)
		track_object_kind(player->upkeep, old_kind);
	else if (old_obj)
		track_object(player->upkeep, old_obj);
	else
		track_object_cancel(player->upkeep);
}

static int o_cmp_tval(const void *a, const void *b)
{
	const int a_val = *(const int *)a;
	const int b_val = *(const int *)b;
	const struct object_kind *k_a = &k_info[a_val];
	const struct object_kind *k_b = &k_info[b_val];

	/* Group by */
	int ta = obj_group_order[k_a->tval];
	int tb = obj_group_order[k_b->tval];
	int c = ta - tb;
	if (c) return c;

	/* Order by */
	c = k_a->aware - k_b->aware;
	if (c) return -c; /* aware has low sort weight */

	switch (k_a->tval)
	{
		case TV_LIGHT:
		case TV_MAGIC_BOOK:
		case TV_PRAYER_BOOK:
		case TV_NATURE_BOOK:
		case TV_SHADOW_BOOK:
		case TV_OTHER_BOOK:
		case TV_DRAG_ARMOR:
			/* leave sorted by sval */
			break;

		default:
			if (k_a->aware)
				return strcmp(k_a->name, k_b->name);

			/* Then in tried order */
			c = k_a->tried - k_b->tried;
			if (c) return -c;

			return strcmp(k_a->flavor->text, k_b->flavor->text);
	}

	return k_a->sval - k_b->sval;
}

static int obj2gid(int oid)
{
	return obj_group_order[k_info[oid].tval];
}

static wchar_t *o_xchar(int oid)
{
	struct object_kind *kind = objkind_byid(oid);
	if (!kind) return 0;

	if (!kind->flavor || kind->aware)
		return &kind_x_char[kind->kidx];
	else
		return &flavor_x_char[kind->flavor->fidx];
}

static uint8_t *o_xattr(int oid)
{
	struct object_kind *kind = objkind_byid(oid);
	if (!kind) return NULL;

	if (!kind->flavor || kind->aware)
		return &kind_x_attr[kind->kidx];
	else
		return &flavor_x_attr[kind->flavor->fidx];
}

/**
 * Display special prompt for object inscription.
 */
static const char *o_xtra_prompt(int oid)
{
	struct object_kind *kind = objkind_byid(oid);

	const char *no_insc = ", 's' to toggle ignore, 'r'ecall, '{'";
	const char *with_insc = ", 's' to toggle ignore, 'r'ecall, '{', '}'";

	if (!kind) return NULL;

	/* Appropriate prompt */
	if (kind->aware)
		return kind->note_aware ? with_insc : no_insc;
	else
		return kind->note_unaware ? with_insc : no_insc;
}

/**
 * Special key actions for object inscription.
 */
static void o_xtra_act(struct keypress ch, int oid)
{
	struct object_kind *k = objkind_byid(oid);
	if (!k) return;

	/* Toggle ignore */
	if (ignore_tval(k->tval) && (ch.code == 's' || ch.code == 'S')) {
		if (k->aware) {
			if (kind_is_ignored_aware(k))
				kind_ignore_clear(k);
			else
				kind_ignore_when_aware(k);
		} else {
			if (kind_is_ignored_unaware(k))
				kind_ignore_clear(k);
			else
				kind_ignore_when_unaware(k);
		}

		return;
	}

	/* Uninscribe */
	if (ch.code == '}') {
		remove_autoinscription(oid);
	} else if (ch.code == '{') {
		/* Inscribe */
		char text[80] = "";

		/* Avoid the prompt getting in the way */
		screen_save();

		/* Prompt */
		prt("Inscribe with: ", 0, 0);

		/* Default note */
		if (k->note_aware || k->note_unaware)
			strnfmt(text, sizeof(text), "%s", get_autoinscription(k, k->aware));

		/* Get an inscription */
		if (askfor_aux(text, sizeof(text), NULL)) {
			/* Remove old inscription if existent */
			if (k->note_aware || k->note_unaware)
				remove_autoinscription(oid);

			/* Add the autoinscription */
			add_autoinscription(oid, text, k->aware);
			cmdq_push(CMD_AUTOINSCRIBE);

			/* Redraw gear */
			player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
		}

		/* Reload the screen */
		screen_load();
	}
}



/**
 * Display known objects
 */
void textui_browse_object_knowledge(const char *name, int row)
{
	group_funcs kind_f = {kind_name, o_cmp_tval, obj2gid, 0, TV_MAX, false};
	member_funcs obj_f = {display_object, desc_obj_fake, o_xchar, o_xattr,
						  o_xtra_prompt, o_xtra_act, 0};

	int *objects;
	int o_count = 0;
	int i;
	struct object_kind *kind;

	objects = mem_zalloc(z_info->k_max * sizeof(int));

	for (i = 0; i < z_info->k_max; i++) {
		kind = &k_info[i];
		/* It's in the list if we've ever seen it, or it has a flavour,
		 * and it's not one of the special artifacts. This way the flavour
		 * appears in the list until it is found. */
		if ((kind->everseen || kind->flavor || OPT(player, cheat_xtra)) &&
			(!kf_has(kind->kind_flags, KF_INSTA_ART))) {
			int c = obj_group_order[k_info[i].tval];
			if (c >= 0) objects[o_count++] = i;
		}
	}

	display_knowledge("known objects", objects, o_count, kind_f, obj_f,
					  "Ignore  Inscribed          Sym");

	mem_free(objects);
}

/**
 * ------------------------------------------------------------------------
 * OBJECT RUNES
 * ------------------------------------------------------------------------ */

/**
 * Description of each rune group.
 */
static const char *rune_group_text[] =
{
	"Combat",
	"Modifiers",
	"Resists",
	"Brands",
	"Slays",
	"Curses",
	"Other",
	NULL
};

/**
 * Display the runes in a group.
 */
static void display_rune(int col, int row, bool cursor, int oid )
{
	uint8_t attr = curs_attrs[CURS_KNOWN][(int)cursor];
	const char *inscrip = quark_str(rune_note(oid));

	c_prt(attr, rune_name(oid), row, col);

	/* Show autoinscription if around */
	if (inscrip)
		c_put_str(COLOUR_YELLOW, inscrip, row, 47);
}


static const char *rune_var_name(int gid)
{
	return rune_group_text[gid];
}

static int rune_var(int oid)
{
	return (int) rune_variety(oid);
}

static void rune_lore(int oid)
{
	textblock *tb = textblock_new();
	char *title = string_make(rune_name(oid));

	my_strcap(title);
	textblock_append_c(tb, COLOUR_L_BLUE, "%s", title);
	textblock_append(tb, "\n");
	textblock_append(tb, "%s", rune_desc(oid));
	textblock_append(tb, "\n");
	textui_textblock_show(tb, SCREEN_REGION, NULL);
	textblock_free(tb);

	string_free(title);
}

/**
 * Display special prompt for rune inscription.
 */
static const char *rune_xtra_prompt(int oid)
{
	const char *no_insc = ", 'r'ecall, '{'";
	const char *with_insc = ", 'r'ecall, '{', '}'";

	/* Appropriate prompt */
	return rune_note(oid) ? with_insc : no_insc;
}

/**
 * Special key actions for rune inscription.
 */
static void rune_xtra_act(struct keypress ch, int oid)
{
	/* Uninscribe */
	if (ch.code == '}') {
		rune_set_note(oid, NULL);
	} else if (ch.code == '{') {
		/* Inscribe */
		char note_text[80] = "";

		/* Avoid the prompt getting in the way */
		screen_save();

		/* Prompt */
		prt("Inscribe with: ", 0, 0);

		/* Default note */
		if (rune_note(oid))
			strnfmt(note_text, sizeof(note_text), "%s",
					quark_str(rune_note(oid)));

		/* Get an inscription */
		if (askfor_aux(note_text, sizeof(note_text), NULL)) {
			/* Remove old inscription if existent */
			if (rune_note(oid))
				rune_set_note(oid, NULL);

			/* Add the autoinscription */
			rune_set_note(oid, note_text);
			rune_autoinscribe(player, oid);

			/* Redraw gear */
			player->upkeep->redraw |= (PR_INVEN | PR_EQUIP);
		}

		/* Reload the screen */
		screen_load();
	}
}



/**
 * Display rune knowledge.
 */
static void do_cmd_knowledge_runes(const char *name, int row)
{
	group_funcs rune_var_f = {rune_var_name, NULL, rune_var, 0,
							  N_ELEMENTS(rune_group_text), false};

	member_funcs rune_f = {display_rune, rune_lore, NULL, NULL,
						   rune_xtra_prompt, rune_xtra_act, 0};

	int *runes;
	int rune_max = max_runes();
	int count = 0;
	int i;
	char buf[30];

	runes = mem_zalloc(rune_max * sizeof(int));

	for (i = 0; i < rune_max; i++) {
		/* Ignore unknown runes */
		if (!player_knows_rune(player, i))
			continue;

		runes[count++] = i;
	}

	strnfmt(buf, sizeof(buf), "runes (%d unknown)", rune_max - count);

	display_knowledge(buf, runes, count, rune_var_f, rune_f, "Inscribed");
	mem_free(runes);
}

/**
 * ------------------------------------------------------------------------
 * TERRAIN FEATURES
 * ------------------------------------------------------------------------ */

/**
 * Description of each feature group.
 */
static const char *feature_group_text[] =
{
	"Floors",
	"Doors",
	"Stairs",
	"Walls",
	"Streamers",
	"Obstructions",
	"Stores",
	"Other",
	NULL
};


/**
 * Display the features in a group.
 */
static void display_feature(int col, int row, bool cursor, int oid )
{
	struct feature *feat = &f_info[oid];
	uint8_t attr = curs_attrs[CURS_KNOWN][(int)cursor];

	c_prt(attr, feat->name, row, col);

	if (tile_height == 1) {
		/* Display symbols */
		col = 65;
		col += big_pad(col, row, feat_x_attr[LIGHTING_DARK][feat->fidx],
					   feat_x_char[LIGHTING_DARK][feat->fidx]);
		col += big_pad(col, row, feat_x_attr[LIGHTING_LIT][feat->fidx],
					   feat_x_char[LIGHTING_LIT][feat->fidx]);
		col += big_pad(col, row, feat_x_attr[LIGHTING_TORCH][feat->fidx],
					   feat_x_char[LIGHTING_TORCH][feat->fidx]);
		(void) big_pad(col, row, feat_x_attr[LIGHTING_LOS][feat->fidx],
					   feat_x_char[LIGHTING_LOS][feat->fidx]);
	}
}


static int f_cmp_fkind(const void *a, const void *b)
{
	const int a_val = *(const int *)a;
	const int b_val = *(const int *)b;
	const struct feature *fa = &f_info[a_val];
	const struct feature *fb = &f_info[b_val];

	/* Group by */
	int c = feat_order(a_val) - feat_order(b_val);
	if (c) return c;

	/* Order by feature name */
	return strcmp(fa->name, fb->name);
}

static const char *fkind_name(int gid)
{
	return feature_group_text[gid];
}


/**
 * Disgusting hack to allow 4 in 1 editing of terrain visuals
 */
static enum grid_light_level f_uik_lighting = LIGHTING_LIT;

/* XXX needs *better* retooling for multi-light terrain */
static uint8_t *f_xattr(int oid)
{
	return &feat_x_attr[f_uik_lighting][oid];
}
static wchar_t *f_xchar(int oid)
{
	return &feat_x_char[f_uik_lighting][oid];
}
static void feat_lore(int oid)
{
	struct feature *feat = &f_info[oid];

	if (feat->desc) {
		textblock *tb = textblock_new();
		char *title = string_make(feat->name);

		my_strcap(title);
		textblock_append_c(tb, COLOUR_L_BLUE, "%s", title);
		string_free(title);
		textblock_append(tb, "\n");
		textblock_append(tb, "%s", feat->desc);
		textblock_append(tb, "\n");
		textui_textblock_show(tb, SCREEN_REGION, NULL);
		textblock_free(tb);
	}
}
static const char *feat_prompt(int oid)
{
	(void)oid;
		switch (f_uik_lighting) {
				case LIGHTING_LIT:  return ", 't/T' for lighting (lit)";
                case LIGHTING_TORCH: return ", 't/T' for lighting (torch)";
				case LIGHTING_LOS:  return ", 't/T' for lighting (LOS)";
				default:	return ", 't/T' for lighting (dark)";
		}		
}

/**
 * Special key actions for cycling lighting
 */
static void f_xtra_act(struct keypress ch, int oid)
{
	/* XXX must be a better way to cycle this */
	if (ch.code == 't') {
		switch (f_uik_lighting) {
				case LIGHTING_LIT:  f_uik_lighting = LIGHTING_TORCH; break;
                case LIGHTING_TORCH: f_uik_lighting = LIGHTING_LOS; break;
				case LIGHTING_LOS:  f_uik_lighting = LIGHTING_DARK; break;
				default:	f_uik_lighting = LIGHTING_LIT; break;
		}		
	} else if (ch.code == 'T') {
		switch (f_uik_lighting) {
				case LIGHTING_DARK:  f_uik_lighting = LIGHTING_LOS; break;
                case LIGHTING_LOS: f_uik_lighting = LIGHTING_TORCH; break;
				case LIGHTING_LIT:  f_uik_lighting = LIGHTING_DARK; break;
				default:	f_uik_lighting = LIGHTING_LIT; break;
		}
	}
	
}


/**
 * Interact with feature visuals.
 */
static void do_cmd_knowledge_features(const char *name, int row)
{
	group_funcs fkind_f = {fkind_name, f_cmp_fkind, feat_order, 0,
						   N_ELEMENTS(feature_group_text), false};

	member_funcs feat_f = {display_feature, feat_lore, f_xchar, f_xattr,
						   feat_prompt, f_xtra_act, 0};

	int *features;
	int f_count = 0;
	int i;

	features = mem_zalloc(z_info->f_max * sizeof(int));

	for (i = 0; i < z_info->f_max; i++) {
		/* Ignore non-features and mimics */
		if (f_info[i].name == 0 || f_info[i].mimic)
			continue;

		/* Currently no filter for features */
		features[f_count++] = i;
	}

	display_knowledge("features", features, f_count, fkind_f, feat_f,
					  "                    Sym");
	mem_free(features);
}

/**
 * ------------------------------------------------------------------------
 * TRAPS
 * ------------------------------------------------------------------------ */

/**
 * Description of each feature group.
 */
static const char *trap_group_text[] =
{
	"Runes",
	"Locks",
	"Player Traps",
	"Monster Traps",
	"Other",
	NULL
};


/**
 * Display the features in a group.
 */
static void display_trap(int col, int row, bool cursor, int oid )
{
	struct trap_kind *trap = &trap_info[oid];
	uint8_t attr = curs_attrs[CURS_KNOWN][(int)cursor];

	c_prt(attr, trap->desc, row, col);

	if (tile_height == 1) {
		/* Display symbols */
		col = 65;
		col += big_pad(col, row, trap_x_attr[LIGHTING_DARK][trap->tidx],
				trap_x_char[LIGHTING_DARK][trap->tidx]);
		col += big_pad(col, row, trap_x_attr[LIGHTING_LIT][trap->tidx],
				trap_x_char[LIGHTING_LIT][trap->tidx]);
		col += big_pad(col, row, trap_x_attr[LIGHTING_TORCH][trap->tidx],
				trap_x_char[LIGHTING_TORCH][trap->tidx]);
		(void) big_pad(col, row, trap_x_attr[LIGHTING_LOS][trap->tidx],
				trap_x_char[LIGHTING_LOS][trap->tidx]);
	}
}

static int trap_order(int trap)
{
	const struct trap_kind *t = &trap_info[trap];

	if (trf_has(t->flags, TRF_GLYPH))
		return 0;
	else if (trf_has(t->flags, TRF_LOCK))
		return 1;
	else if (trf_has(t->flags, TRF_TRAP))
		return 2;
	else if (trf_has(t->flags, TRF_M_TRAP))
		return 3;
	else
		return 4;
}

static int t_cmp_tkind(const void *a, const void *b)
{
	const int a_val = *(const int *)a;
	const int b_val = *(const int *)b;
	const struct trap_kind *ta = &trap_info[a_val];
	const struct trap_kind *tb = &trap_info[b_val];

	/* Group by */
	int c = trap_order(a_val) - trap_order(b_val);
	if (c) return c;

	/* Order by name */
	if (ta->name) {
		if (tb->name)
			return strcmp(ta->name, tb->name);
		else
			return 1;
	} else if (tb->name) {
		return -1;
	}

	return 0;
}

static const char *tkind_name(int gid)
{
	return trap_group_text[gid];
}


/**
 * Disgusting hack to allow 4 in 1 editing of trap visuals
 */
static enum grid_light_level t_uik_lighting = LIGHTING_LIT;

/* XXX needs *better* retooling for multi-light terrain */
static uint8_t *t_xattr(int oid)
{
	return &trap_x_attr[t_uik_lighting][oid];
}
static wchar_t *t_xchar(int oid)
{
	return &trap_x_char[t_uik_lighting][oid];
}
static void trap_lore(int oid)
{
	struct trap_kind *trap = &trap_info[oid];

	if (trap->text) {
		textblock *tb = textblock_new();
		char *title = string_make(trap->desc);
		textblock *tbe = effect_describe(trap->effect, "This trap ", 0, false);
		textblock *tbex = effect_describe(trap->effect_xtra,
										  "if you're unlucky it also ", 0,
										  false);

		my_strcap(title);
		textblock_append_c(tb, COLOUR_L_BLUE, "%s", title);
		string_free(title);
		textblock_append(tb, "\n");
		textblock_append(tb, "%s", trap->text);
		textblock_append(tb, "\n");
		if (tbe) {
			textblock_append(tb, "\n");
			textblock_append_textblock(tb, tbe);
			textblock_free(tbe);
			if (tbex) {
				textblock_append(tb, "; ");
				textblock_append_textblock(tb, tbex);
				textblock_free(tbex);
			}
			textblock_append(tb, ".\n");
		}
		textui_textblock_show(tb, SCREEN_REGION, NULL);
		textblock_free(tb);
	}
}

static const char *trap_prompt(int oid)
{
	(void)oid;
	return ", 't' to cycle lighting";
}

/**
 * Special key actions for cycling lighting
 */
static void t_xtra_act(struct keypress ch, int oid)
{
	/* XXX must be a better way to cycle this */
	if (ch.code == 't') {
		switch (t_uik_lighting) {
				case LIGHTING_LIT:  t_uik_lighting = LIGHTING_TORCH; break;
                case LIGHTING_TORCH: t_uik_lighting = LIGHTING_LOS; break;
				case LIGHTING_LOS:  t_uik_lighting = LIGHTING_DARK; break;
				default:	t_uik_lighting = LIGHTING_LIT; break;
		}		
	} else if (ch.code == 'T') {
		switch (t_uik_lighting) {
				case LIGHTING_DARK:  t_uik_lighting = LIGHTING_LOS; break;
                case LIGHTING_LOS: t_uik_lighting = LIGHTING_TORCH; break;
				case LIGHTING_LIT:  t_uik_lighting = LIGHTING_DARK; break;
				default:	t_uik_lighting = LIGHTING_LIT; break;
		}
	}
	
}


/**
 * Interact with trap visuals.
 */
static void do_cmd_knowledge_traps(const char *name, int row)
{
	group_funcs tkind_f = {tkind_name, t_cmp_tkind, trap_order, 0,
						   N_ELEMENTS(trap_group_text), false};

	member_funcs trap_f = {display_trap, trap_lore, t_xchar, t_xattr,
						   trap_prompt, t_xtra_act, 0};

	int *traps;
	int t_count = 0;
	int i;

	traps = mem_zalloc(z_info->trap_max * sizeof(int));

	for (i = 0; i < z_info->trap_max; i++) {
		if (!trap_info[i].name) continue;

		traps[t_count++] = i;
	}

	display_knowledge("traps", traps, t_count, tkind_f, trap_f,
					  "                    Sym");
	mem_free(traps);
}


/**
 * ------------------------------------------------------------------------
 * SHAPECHANGE
 * ------------------------------------------------------------------------ */

/**
 * Counts the number of interesting shapechanges and returns it.
 */
static int count_interesting_shapes(void)
{
	int count = 0;
	struct player_shape *s;

	for (s = shapes; s; s = s->next) {
		if (! streq(s->name, "normal")) {
			++count;
		}
	}

	return count;
}


/**
 * Is a comparison function for an array of struct player_shape* which is
 * compatible with sort() and puts the elements in ascending alphabetical
 * order by name.
 */
static int compare_shape_names(const void *left, const void *right)
{
	const struct player_shape * const *sleft = left;
	const struct player_shape * const *sright = right;

	return my_stricmp((*sleft)->name, (*sright)->name);
}


static void shape_lore_helper_append_to_list(const char* item,
	const char ***list, int* p_nmax, int *p_n)
{
	if (*p_n >= *p_nmax) {
		if (*p_nmax == 0) {
			*p_nmax = 4;
		} else {
			assert(*p_nmax > 0);
			*p_nmax *= 2;
		}
		*list = mem_realloc(*list, *p_nmax * sizeof(**list));
	}
	(*list)[*p_n] = string_make(item);
	++*p_n;
}


static const char *skill_index_to_name(int i)
{
	const char *name;

	switch (i) {
	case SKILL_DISARM_PHYS:
		name = "physical disarming";
		break;

	case SKILL_DISARM_MAGIC:
		name = "magical disarming";
		break;

	case SKILL_DEVICE:
		name = "magic devices";
		break;

	case SKILL_SAVE:
		name = "saving throws";
		break;

	case SKILL_SEARCH:
		name = "searching";
		break;

	case SKILL_TO_HIT_MELEE:
		name = "melee to hit";
		break;

	case SKILL_TO_HIT_BOW:
		name = "shooting to hit";
		break;

	case SKILL_TO_HIT_THROW:
		name = "throwing to hit";
		break;

	case SKILL_DIGGING:
		name = "digging";
		break;

	default:
		name = "unknown skill";
		break;
	}

	return name;
}


static void shape_lore_append_list(textblock *tb,
	const char * const *list, int n)
{
	int i;

	if (n > 0) {
		textblock_append(tb, " %s", list[0]);
	}
	for (i = 1; i < n; ++i) {
		textblock_append(tb, "%s %s", (i < n - 1) ? "," : " and",
			list[i]);
	}
}


static void shape_lore_append_basic_combat(textblock *tb,
	const struct player_shape *s)
{
	char toa_msg[24];
	char toh_msg[24];
	char tod_msg[24];
	const char* msgs[3];
	int n = 0;

	if (s->to_a != 0) {
		strnfmt(toa_msg, sizeof(toa_msg), "%+d to AC", s->to_a);
		msgs[n] = toa_msg;
		++n;
	}
	if (s->to_h != 0) {
		strnfmt(toh_msg, sizeof(toh_msg), "%+d to hit", s->to_h);
		msgs[n] = toh_msg;
		++n;
	}
	if (s->to_d != 0) {
		strnfmt(tod_msg, sizeof(tod_msg), "%+d to damage", s->to_d);
		msgs[n] = tod_msg;
		++n;
	}
	if (n > 0) {
		textblock_append(tb, "Adds");
		shape_lore_append_list(tb, msgs, n);
		textblock_append(tb, ".\n");
	}
}


static void shape_lore_append_skills(textblock *tb,
	const struct player_shape *s)
{
	const char **msgs = NULL;
	int nmax = 0, n = 0;
	int i;

	for (i = 0; i < SKILL_MAX; ++i) {
		if (s->skills[i] != 0) {
			shape_lore_helper_append_to_list(
				format("%+d to %s", s->skills[i],
					skill_index_to_name(i)),
				&msgs, &nmax, &n);
		}
	}

	if (n > 0) {
		textblock_append(tb, "Adds");
		shape_lore_append_list(tb, msgs, n);
		textblock_append(tb, ".\n");
		for (i = 0; i < n; ++i) {
			string_free((char*) msgs[i]);
		}
	}

	mem_free(msgs);
}


static void shape_lore_append_non_stat_modifiers(textblock *tb,
	const struct player_shape *s)
{
	const char **msgs = NULL;
	int nmax = 0, n = 0;
	int i;

	for (i = STAT_MAX; i < OBJ_MOD_MAX; ++i) {
		if (s->modifiers[i] != 0) {
			shape_lore_helper_append_to_list(
				format("%+d to %s",
					s->modifiers[i],
					lookup_obj_property(OBJ_PROPERTY_MOD, i)->name),
				&msgs, &nmax, &n);
		}
	}

	if (n > 0) {
		textblock_append(tb, "Adds");
		shape_lore_append_list(tb, msgs, n);
		textblock_append(tb, ".\n");
		for (i = 0; i < n; ++i) {
			string_free((char*) msgs[i]);
		}
	}

	mem_free(msgs);
}


static void shape_lore_append_stat_modifiers(textblock *tb,
	const struct player_shape *s)
{
	const char **msgs = NULL;
	int nmax = 0, n = 0;
	int i;

	for (i = 0; i < STAT_MAX; ++i) {
		if (s->modifiers[i] != 0) {
			shape_lore_helper_append_to_list(
				format("%+d to %s",
					s->modifiers[i],
					lookup_obj_property(OBJ_PROPERTY_MOD, i)->name),
				&msgs, &nmax, &n);
		}
	}

	if (n > 0) {
		textblock_append(tb, "Adds");
		shape_lore_append_list(tb, msgs, n);
		textblock_append(tb, ".\n");
		for (i = 0; i < n; ++i) {
			string_free((char*) msgs[i]);
		}
	}

	mem_free(msgs);
}


static void shape_lore_append_resistances(textblock *tb,
	const struct player_shape *s)
{
	const char* vul[ELEM_MAX];
	const char* res[ELEM_MAX];
	const char* imm[ELEM_MAX];
	int nvul = 0, nres = 0, nimm = 0;
	int i;

	for (i = 0; i < ELEM_MAX; ++i) {
		if (s->el_info[i].res_level == RES_LEVEL_MAX) {
			imm[nimm] = projections[i].name;
			++nimm;
		} else if (s->el_info[i].res_level < RES_LEVEL_BASE) {
			res[nres] = projections[i].name;
			++nres;
		} else if (s->el_info[i].res_level > RES_LEVEL_BASE) {
			vul[nvul] = projections[i].name;
			++nvul;
		}
	}

	if (nvul != 0) {
		textblock_append(tb, "Makes you vulnerable to");
		shape_lore_append_list(tb, vul, nvul);
		textblock_append(tb, ".\n");
	}

	if (nres != 0) {
		textblock_append(tb, "Makes you resistant to");
		shape_lore_append_list(tb, res, nres);
		textblock_append(tb, ".\n");
	}

	if (nimm != 0) {
		textblock_append(tb, "Makes you immune to");
		shape_lore_append_list(tb, imm, nimm);
		textblock_append(tb, ".\n");
	}
}


static void shape_lore_append_protection_flags(textblock *tb,
	const struct player_shape *s)
{
	const char **msgs = NULL;
	int nmax = 0, n = 0;
	int i;

	for (i = 1; i < OF_MAX; ++i) {
		struct obj_property *prop =
			lookup_obj_property(OBJ_PROPERTY_FLAG, i);

		if (prop->subtype == OFT_PROT &&
			of_has(s->flags, prop->index)) {
			shape_lore_helper_append_to_list(
				prop->desc, &msgs, &nmax, &n);
		}
	}

	if (n > 0) {
		textblock_append(tb, "Provides protection from");
		shape_lore_append_list(tb, msgs, n);
		textblock_append(tb, ".\n");
		for (i = 0; i < n; ++i) {
			string_free((char*) msgs[i]);
		}
	}

	mem_free(msgs);
}


static void shape_lore_append_sustains(textblock *tb,
	const struct player_shape *s)
{
	const char **msgs = NULL;
	int nmax = 0, n = 0;
	int i;

	for (i = 0; i < STAT_MAX; ++i) {
		struct obj_property *prop =
			lookup_obj_property(OBJ_PROPERTY_STAT, i);

		if (of_has(s->flags, sustain_flag(prop->index))) {
			shape_lore_helper_append_to_list(
				prop->name, &msgs, &nmax, &n);
		}
	}

	if (n > 0) {
		textblock_append(tb, "Sustains");
		shape_lore_append_list(tb, msgs, n);
		textblock_append(tb, ".\n");
		for (i = 0; i < n; ++i) {
			string_free((char*) msgs[i]);
		}
	}

	mem_free(msgs);
}


static void shape_lore_append_misc_flags(textblock *tb,
	const struct player_shape *s)
{
	int n = 0;
	int i;
	struct player_ability *ability;

	for (i = 1; i < OF_MAX; ++i) {
		struct obj_property *prop =
			lookup_obj_property(OBJ_PROPERTY_FLAG, i);

		if ((prop->subtype == OFT_MISC || prop->subtype == OFT_WEAPON ||
			prop->subtype == OFT_BAD) &&
			of_has(s->flags, prop->index)) {
			textblock_append(tb, "%s%s.", (n > 0) ? "  " : "",
				prop->desc);
			++n;
		}
	}

	for (ability = player_abilities; ability; ability = ability->next) {
		if (streq(ability->type, "player") &&
			pf_has(s->pflags, ability->index)) {
			textblock_append(tb, "%s%s", (n > 0) ? "  " : "",
				ability->desc);
			++n;
		}
	}

	if (n > 0) {
		textblock_append(tb, "\n");
	}
}


static void shape_lore_append_change_effects(textblock *tb,
	const struct player_shape *s)
{
	textblock *tbe = effect_describe(s->effect, "Changing into the shape ",
		0, false);

	if (tbe) {
		textblock_append_textblock(tb, tbe);
		textblock_free(tbe);
		textblock_append(tb, ".\n");
	}
}


static void shape_lore_append_triggering_spells(textblock *tb,
	const struct player_shape *s)
{
	int n = 0;
	struct player_class *c;

	for (c = classes; c; c = c->next) {
		int ibook;

		for (ibook = 0; ibook < c->magic.num_books; ++ibook) {
			const struct class_book *book = c->magic.books + ibook;
			const struct object_kind *kind =
				lookup_kind(book->tval, book->sval);
			int ispell;

			if (!kind || !kind->name) {
				continue;
			}
			for (ispell = 0; ispell < book->num_spells; ++ispell) {
				const struct class_spell *spell =
					book->spells + ispell;
				const struct effect *effect;

				for (effect = spell->effect;
					effect;
					effect = effect->next) {
					if (effect->index == EF_SHAPECHANGE &&
						effect->subtype == s->sidx) {
						if (n == 0) {
							textblock_append(tb, "\n");
						}
						textblock_append(tb,
							"The %s spell, %s, from %s triggers the shapechange.",
							c->name,
							spell->name,
							kind->name
						);
						++n;
					}
				}
			}
		}
	}

	if (n > 0) {
		textblock_append(tb, "\n");
	}
}


/**
 * Display information about a shape change.
 */
static void shape_lore(const struct player_shape *s)
{
	textblock *tb = textblock_new();

	textblock_append(tb, "%s", s->name);
	textblock_append(tb, "\nLike all shapes, the equipment at the time of "
		"the shapechange sets the base attributes, including damage "
		"per blow, number of blows and resistances.  While changed, "
		"items in your pack or on the floor (except for pickup or "
		"eating) are inaccessible.  To switch back to your normal "
		"shape, cast a spell or use an item command other than eat "
		"(drop, for instance).\n");
	shape_lore_append_basic_combat(tb, s);
	shape_lore_append_skills(tb, s);
	shape_lore_append_non_stat_modifiers(tb, s);
	shape_lore_append_stat_modifiers(tb, s);
	shape_lore_append_resistances(tb, s);
	shape_lore_append_protection_flags(tb, s);
	shape_lore_append_sustains(tb, s);
	shape_lore_append_misc_flags(tb, s);
	shape_lore_append_change_effects(tb, s);
	shape_lore_append_triggering_spells(tb, s);

	textui_textblock_show(tb, SCREEN_REGION, NULL);
	textblock_free(tb);
}


static void do_cmd_knowledge_shapechange(const char *name, int row)
{
	region header_region = { 0, 0, -1, 5 };
	region list_region = { 0, 6, -1, -2 };
	int count = count_interesting_shapes();
	struct menu* m;
	struct player_shape **sarray;
	const char **narray;
	int h, mark, mark_old;
	bool displaying, redraw;
	struct player_shape *s;
	int i;

	if (!count) {
		return;
	}

	m = menu_new(MN_SKIN_SCROLL, menu_find_iter(MN_ITER_STRINGS));

	/* Set up an easily indexable list of the interesting shapes. */
	sarray = mem_alloc(count * sizeof(*sarray));
	for (s = shapes, i = 0; s; s = s->next) {
		if (streq(s->name, "normal")) {
			continue;
		}
		sarray[i] = s;
		++i;
	}

	/*
	 * Sort them alphabetically by name and set up an array with just the
	 * names.
	 */
	sort(sarray, count, sizeof(sarray[0]), compare_shape_names);
	narray = mem_alloc(count * sizeof(*narray));
	for (i = 0; i < count; ++i) {
		narray[i] = sarray[i]->name;
	}

	menu_setpriv(m, count, narray);
	menu_layout(m, &list_region);
	m->flags |= MN_DBL_TAP;

	screen_save();
	clear_from(0);

	h = 0;
	mark = 0;
	mark_old = -1;
	displaying = true;
	redraw = true;
	while (displaying) {
		bool recall = false;
		int wnew, hnew;
		ui_event ke0 = EVENT_EMPTY;
		ui_event ke;

		Term_get_size(&wnew, &hnew);
		if (h != hnew) {
			h = hnew;
			redraw = true;
		}

		if (redraw) {
			region_erase(&header_region);
			prt("Knowledge - shapes", 2, 0);
			prt("Name", 4, 0);
			for (i = 0; i < MIN(80, wnew); i++) {
				Term_putch(i, 5, COLOUR_WHITE, L'=');
			}
			prt("<dir>, 'r' to recall, ESC", h - 2, 0);
			redraw = false;
		}

		if (mark_old != mark) {
			mark_old = mark;
			m->cursor = mark;
		}

		menu_refresh(m, false);

		handle_stuff(player);

		ke = inkey_ex();
		if (ke.type == EVT_MOUSE) {
			menu_handle_mouse(m, &ke, &ke0);
		} else if (ke.type == EVT_KBRD) {
			menu_handle_keypress(m, &ke, &ke0);
		}
		if (ke0.type != EVT_NONE) {
			ke = ke0;
		}

		switch (ke.type) {
			case EVT_KBRD:
				if (ke.key.code == 'r' || ke.key.code == 'R') {
					recall = true;
				}
				break;

			case EVT_ESCAPE:
				displaying = false;
				break;

			case EVT_SELECT:
				if (mark == m->cursor) {
					recall = true;
				}
				break;

			case EVT_MOVE:
				mark = m->cursor;
				break;

			default:
				break;
		}

		if (recall) {
			assert(mark >= 0 && mark < count);
			shape_lore(sarray[mark]);
		}
	}

	screen_load();

	mem_free(narray);
	mem_free(sarray);
	menu_free(m);
}


/**
 * ------------------------------------------------------------------------
 * ui_knowledge.txt parsing
 * ------------------------------------------------------------------------
 */
static enum parser_error parse_monster_category(struct parser *p)
{
	struct ui_knowledge_parse_state *s =
		(struct ui_knowledge_parse_state*) parser_priv(p);
	struct ui_monster_category *c;

	assert(s);
	c = mem_zalloc(sizeof(*c));
	c->next = s->categories;
	c->name = string_make(parser_getstr(p, "name"));
	s->categories = c;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_mcat_include_base(struct parser *p)
{
	struct ui_knowledge_parse_state *s =
		(struct ui_knowledge_parse_state*) parser_priv(p);
	struct monster_base *b;

	assert(s);
	if (!s->categories) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}
	b = lookup_monster_base(parser_getstr(p, "name"));
	if (!b) {
		return PARSE_ERROR_INVALID_MONSTER_BASE;
	}
	assert(s->categories->n_inc_bases >= 0
		&& s->categories->n_inc_bases <= s->categories->max_inc_bases);
	if (s->categories->n_inc_bases == s->categories->max_inc_bases) {
		if (s->categories->max_inc_bases > INT_MAX
				/ (2 * (int) sizeof(struct monster_base*))) {
			return PARSE_ERROR_TOO_MANY_ENTRIES;
		}
		s->categories->max_inc_bases = (s->categories->max_inc_bases)
			? 2 * s->categories->max_inc_bases : 2;
		s->categories->inc_bases = mem_realloc(
			s->categories->inc_bases,
			s->categories->max_inc_bases
			* sizeof(struct monster_base*));
	}
	s->categories->inc_bases[s->categories->n_inc_bases] = b;
	++s->categories->n_inc_bases;

	return PARSE_ERROR_NONE;
}

static enum parser_error parse_mcat_include_flag(struct parser *p)
{
	struct ui_knowledge_parse_state *s =
		(struct ui_knowledge_parse_state*) parser_priv(p);
	char *flags, *next_flag;

	assert(s);
	if (!s->categories) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}

	if (!parser_hasval(p, "flags")) {
		return PARSE_ERROR_NONE;
	}
	flags = string_make(parser_getstr(p, "flags"));
	next_flag = strtok(flags, " |");
	while (next_flag) {
		if (grab_flag(s->categories->inc_flags, RF_SIZE, r_info_flags,
				next_flag)) {
			string_free(flags);
			return PARSE_ERROR_INVALID_FLAG;
		}
		next_flag = strtok(NULL, " |");
	}
	string_free(flags);

	return PARSE_ERROR_NONE;
}

static struct parser *init_ui_knowledge_parser(void)
{
	struct ui_knowledge_parse_state *s = mem_zalloc(sizeof(*s));
	struct parser *p = parser_new();

	parser_setpriv(p, s);
	parser_reg(p, "monster-category str name", parse_monster_category);
	parser_reg(p, "mcat-include-base str name", parse_mcat_include_base);
	parser_reg(p, "mcat-include-flag ?str flags", parse_mcat_include_flag);

	return p;
}

static errr run_ui_knowledge_parser(struct parser *p)
{
	return parse_file_quit_not_found(p, "ui_knowledge");
}

static errr finish_ui_knowledge_parser(struct parser *p)
{
	struct ui_knowledge_parse_state *s =
		(struct ui_knowledge_parse_state*) parser_priv(p);
	struct ui_monster_category *cursor;
	size_t count;

	assert(s);

	/* Count the number of categories and allocate a flat array for them. */
	count = 0;
	for (cursor = s->categories; cursor; cursor = cursor->next) {
		++count;
	}
	if (count > INT_MAX - 1) {
		/*
		 * The sorting and display logic for monster groups assumes
		 * the number of categories fits in an int.
		 */
		cursor = s->categories;
		while (cursor) {
			struct ui_monster_category *tgt = cursor;

			cursor = cursor->next;
			string_free((char*) tgt->name);
			mem_free(tgt->inc_bases);
			mem_free(tgt);
		}
		mem_free(s);
		parser_destroy(p);
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	if (monster_group) {
		cleanup_ui_knowledge_parsed_data();
	}
	monster_group = mem_alloc((count + 1) * sizeof(*monster_group));
	n_monster_group = (int) (count + 1);

	/* Set the element at the end which receives special treatment. */
	monster_group[count].next = NULL;
	monster_group[count].name = string_make("***Unclassified***");
	monster_group[count].inc_bases = NULL;
	rf_wipe(monster_group[count].inc_flags);
	monster_group[count].n_inc_bases = 0;
	monster_group[count].max_inc_bases = 0;

	/*
	 * Set the others, restoring the order they had in the data file.
	 * Release the memory for the linked list (but not pointed to data
	 * as ownership for that is transferred to the flat array).
	 */
	cursor = s->categories;
	while (cursor) {
		struct ui_monster_category *src = cursor;

		cursor = cursor->next;
		--count;
		monster_group[count].next = monster_group + count + 1;
		monster_group[count].name = src->name;
		monster_group[count].inc_bases = src->inc_bases;
		rf_copy(monster_group[count].inc_flags, src->inc_flags);
		monster_group[count].n_inc_bases = src->n_inc_bases;
		monster_group[count].max_inc_bases = src->max_inc_bases;
		mem_free(src);
	}

	mem_free(s);
	parser_destroy(p);
	return 0;
}

static void cleanup_ui_knowledge_parsed_data(void)
{
	int i;

	for (i = 0; i < n_monster_group; ++i) {
		string_free((char*) monster_group[i].name);
		mem_free(monster_group[i].inc_bases);
	}
	mem_free(monster_group);
	monster_group = NULL;
	n_monster_group = 0;
}

/**
 * ------------------------------------------------------------------------
 * Main knowledge menus
 * ------------------------------------------------------------------------ */

static void do_cmd_knowledge_home(const char *name, int row)
{
	textui_store_knowledge(store_home(player));
}

static void do_cmd_knowledge_scores(const char *name, int row)
{
	show_scores();
}

static void do_cmd_knowledge_history(const char *name, int row)
{
	history_display();
}

static void do_cmd_knowledge_equip_cmp(const char* name, int row)
{
	equip_cmp_display();
}

/**
 * Definition of the "player knowledge" menu.
 */
static menu_action knowledge_actions[] =
{
{ 0, 0, "Display object knowledge",   	   textui_browse_object_knowledge },
{ 0, 0, "Display rune knowledge",   	   do_cmd_knowledge_runes },
{ 0, 0, "Display artifact knowledge", 	   do_cmd_knowledge_artifacts },
{ 0, 0, "Display ego item knowledge", 	   do_cmd_knowledge_ego_items },
{ 0, 0, "Display monster knowledge",  	   do_cmd_knowledge_monsters  },
{ 0, 0, "Display feature knowledge",  	   do_cmd_knowledge_features  },
{ 0, 0, "Display trap knowledge",          do_cmd_knowledge_traps  },
{ 0, 0, "Display shapechange effects",     do_cmd_knowledge_shapechange },
{ 0, 0, "Display contents of home",        do_cmd_knowledge_home },
{ 0, 0, "Display hall of fame",       	   do_cmd_knowledge_scores    },
{ 0, 0, "Display character history",  	   do_cmd_knowledge_history   },
{ 0, 0, "Display equippable comparison",   do_cmd_knowledge_equip_cmp },
};

static struct menu knowledge_menu;

void textui_knowledge_init(void)
{
	/* Initialize the menus */
	struct menu *menu = &knowledge_menu;
	menu_init(menu, MN_SKIN_SCROLL, menu_find_iter(MN_ITER_ACTIONS));
	menu_setpriv(menu, N_ELEMENTS(knowledge_actions), knowledge_actions);

	menu->title = "Display current knowledge";
	menu->selections = all_letters_nohjkl;

	/* initialize other static variables */
	if (run_parser(&ui_knowledge_parser) != PARSE_ERROR_NONE) {
		quit_fmt("Encountered error parsing ui_knowledge.txt");
	}

	if (!obj_group_order) {
		int i;
		int gid = -1;

		obj_group_order = mem_zalloc((TV_MAX + 1) * sizeof(int));

		/* Allow for missing values */
		for (i = 0; i < TV_MAX; i++)
			obj_group_order[i] = -1;

		for (i = 0; 0 != object_text_order[i].tval; i++) {
			if (kb_info[object_text_order[i].tval].num_svals == 0) continue;
			if (object_text_order[i].name) gid = i;
			obj_group_order[object_text_order[i].tval] = gid;
		}
	}
}


void textui_knowledge_cleanup(void)
{
	mem_free(obj_group_order);
	obj_group_order = NULL;
	cleanup_parser(&ui_knowledge_parser);
}


/**
 * Display the "player knowledge" menu, greying out items that won't display
 * anything.
 */
void textui_browse_knowledge(void)
{
	int i, rune_max = max_runes();
	region knowledge_region = { 0, 0, -1, 2 + (int)N_ELEMENTS(knowledge_actions) };

	/* Runes */
	knowledge_actions[1].flags = MN_ACT_GRAYED;
	for (i = 0; i < rune_max; i++) {
		if (player_knows_rune(player, i) || OPT(player, cheat_xtra)) {
			knowledge_actions[1].flags = 0;
		    break;
		}
	}
		
	/* Artifacts */
	if (collect_known_artifacts(NULL, 0) > 0)
		knowledge_actions[2].flags = 0;
	else
		knowledge_actions[2].flags = MN_ACT_GRAYED;

	/* Ego items */
	knowledge_actions[3].flags = MN_ACT_GRAYED;
	for (i = 0; i < z_info->e_max; i++) {
		if (e_info[i].everseen || OPT(player, cheat_xtra)) {
			knowledge_actions[3].flags = 0;
			break;
		}
	}

	/* Monsters */
	if (count_known_monsters() > 0)
		knowledge_actions[4].flags = 0;
	else
		knowledge_actions[4].flags = MN_ACT_GRAYED;

	/* Shapechanges */
	knowledge_actions[7].flags = (count_interesting_shapes() > 0) ?
		0 : MN_ACT_GRAYED;

	screen_save();
	menu_layout(&knowledge_menu, &knowledge_region);

	clear_from(0);
	menu_select(&knowledge_menu, 0, false);

	screen_load();
}


/**
 * ------------------------------------------------------------------------
 * Other knowledge functions
 * ------------------------------------------------------------------------ */

/**
 * Recall the most recent message
 */
void do_cmd_message_one(void)
{
	/* Recall one message XXX XXX XXX */
	c_prt(message_color(0), format( "> %s", message_str(0)), 0, 0);
}


/**
 * Show previous messages to the user
 *
 * The screen format uses line 0 and 23 for headers and prompts,
 * skips line 1 and 22, and uses line 2 thru 21 for old messages.
 *
 * This command shows you which commands you are viewing, and allows
 * you to "search" for strings in the recall.
 *
 * Note that messages may be longer than 80 characters, but they are
 * displayed using "infinite" length, with a special sub-command to
 * "slide" the virtual display to the left or right.
 *
 * Attempt to only highlight the matching portions of the string.
 */
void do_cmd_messages(void)
{
	ui_event ke;

	bool more = true;

	int i, j, n, q;
	int wid, hgt;

	char shower[80] = "";

	/* Total messages */
	n = messages_num();

	/* Start on first message */
	i = 0;

	/* Start at leftmost edge */
	q = 0;

	/* Get size */
	Term_get_size(&wid, &hgt);

	/* Save screen */
	screen_save();

	/* Process requests until done */
	while (more) {
		/* Clear screen */
		Term_clear();

		/* Dump messages */
		for (j = 0; (j < hgt - 4) && (i + j < n); j++) {
			const char *msg;
			const char *str = message_str(i + j);
			uint8_t attr = message_color(i + j);
			uint16_t count = message_count(i + j);

			if (count == 1)
				msg = str;
			else
				msg = format("%s <%dx>", str, count);

			/* Apply horizontal scroll */
			msg = ((int)strlen(msg) >= q) ? (msg + q) : "";

			/* Dump the messages, bottom to top */
			Term_putstr(0, hgt - 3 - j, -1, attr, msg);

			/* Highlight "shower" */
			if (strlen(shower)) {
				str = msg;

				/* Display matches */
				while ((str = my_stristr(str, shower)) != NULL) {
					int len = strlen(shower);

					/* Display the match */
					Term_putstr(str-msg, hgt - 3 - j, len, COLOUR_YELLOW, str);

					/* Advance */
					str += len;
				}
			}
		}

		/* Display header */
		prt(format("Message recall (%d-%d of %d), offset %d",
				   i, i + j - 1, n, q), 0, 0);

		/* Display prompt (not very informative) */
		if (strlen(shower))
			prt("[Movement keys to navigate, '-' for next, '=' to find]",
				hgt - 1, 0);
		else
			prt("[Movement keys to navigate, '=' to find, or ESCAPE to exit]",
				hgt - 1, 0);
			
		/* Get a command */
		ke = inkey_ex();

		/* Scroll forwards or backwards using mouse clicks */
		if (ke.type == EVT_MOUSE) {
			if (ke.mouse.button == 1) {
				if (ke.mouse.y <= hgt / 2) {
					/* Go older if legal */
					if (i + 20 < n)
						i += 20;
				} else {
					/* Go newer */
					i = (i >= 20) ? (i - 20) : 0;
				}
			} else if (ke.mouse.button == 2) {
				more = false;
			}
		} else if (ke.type == EVT_KBRD) {
			switch (ke.key.code) {
				case ESCAPE:
				{
					more = false;
					break;
				}

				case '=':
				{
					/* Get the string to find */
					prt("Find: ", hgt - 1, 0);
					if (!askfor_aux(shower, sizeof shower, NULL)) continue;
		
					/* Set to find */
					ke.key.code = '-';
					break;
				}

				case ARROW_LEFT:
				case '4':
				case 'h':
					q = (q >= wid / 2) ? (q - wid / 2) : 0;
					break;

				case ARROW_RIGHT:
				case '6':
				case 'l':
					q = q + wid / 2;
					break;

				case ARROW_UP:
				case '8':
				case 'k':
					if (i + 1 < n) i += 1;
					break;

				case ARROW_DOWN:
				case '2':
				case 'j':
				case KC_ENTER:
					i = (i >= 1) ? (i - 1) : 0;
					break;

				case KC_PGUP:
				case 'p':
				case ' ':
					if (i + 20 < n) i += 20;
					break;

				case KC_PGDOWN:
				case 'n':
					i = (i >= 20) ? (i - 20) : 0;
					break;
			}
		}

		/* Find the next item */
		if (ke.key.code == '-' && strlen(shower)) {
			int16_t z;

			/* Scan messages */
			for (z = i + 1; z < n; z++) {
				/* Search for it */
				if (my_stristr(message_str(z), shower)) {
					/* New location */
					i = z;

					/* Done */
					break;
				}
			}
		}
	}

	/* Load screen */
	screen_load();
}



#define GET_ITEM_PARAMS \
 	(USE_EQUIP | USE_INVEN | USE_QUIVER | USE_FLOOR | SHOW_QUIVER | SHOW_EMPTY | IS_HARMLESS)
 
/**
 * Display inventory
 */
void do_cmd_inven(void)
{
	struct object *obj = NULL;
	int ret = 3;

	if (player->upkeep->inven[0] == NULL) {
		msg("You have nothing in your inventory.");
		return;
	}

	/* Start in "inventory" mode */
	player->upkeep->command_wrk = (USE_INVEN);

	/* Loop this menu until an object context menu says differently */
	while (ret == 3) {
		/* Save screen */
		screen_save();

		/* Get an item to use a context command on (Display the inventory) */
		if (get_item(&obj, "Select Item:",
				"Error in do_cmd_inven(), please report.",
				CMD_NULL, NULL, GET_ITEM_PARAMS)) {
			/* Load screen */
			screen_load();

			if (obj && obj->kind) {
				/* Track the object */
				track_object(player->upkeep, obj);

				if (!player_is_shapechanged(player)) {
					while ((ret = context_menu_object(obj)) == 2);
				}
			}
		} else {
			/* Load screen */
			screen_load();

			ret = -1;
		}
	}
}


/**
 * Display equipment
 */
void do_cmd_equip(void)
{
	struct object *obj = NULL;
	int ret = 3;

	if (!player->upkeep->equip_cnt) {
		msg("You are not wielding or wearing anything.");
		return;
	}

	/* Start in "equipment" mode */
	player->upkeep->command_wrk = (USE_EQUIP);

	/* Loop this menu until an object context menu says differently */
	while (ret == 3) {
		/* Save screen */
		screen_save();

		/* Get an item to use a context command on (Display the equipment) */
		if (get_item(&obj, "Select Item:",
				"Error in do_cmd_equip(), please report.",
				CMD_NULL, NULL, GET_ITEM_PARAMS)) {
			/* Load screen */
			screen_load();

			if (obj && obj->kind) {
				/* Track the object */
				track_object(player->upkeep, obj);

				if (!player_is_shapechanged(player)) {
					while ((ret = context_menu_object(obj)) == 2);
				}

				/* Stay in "equipment" mode */
				player->upkeep->command_wrk = (USE_EQUIP);
			}
		} else {
			/* Load screen */
			screen_load();

			ret = -1;
		}
	}
}


/**
 * Display equipment
 */
void do_cmd_quiver(void)
{
	struct object *obj = NULL;
	int ret = 3;

	if (player->upkeep->quiver_cnt == 0) {
		msg("You have nothing in your quiver.");
		return;
	}

	/* Start in "quiver" mode */
	player->upkeep->command_wrk = (USE_QUIVER);

	/* Loop this menu until an object context menu says differently */
	while (ret == 3) {
		/* Save screen */
		screen_save();

		/* Get an item to use a context command on (Display the quiver) */
		if (get_item(&obj, "Select Item:",
				"Error in do_cmd_quiver(), please report.",
				CMD_NULL, NULL, GET_ITEM_PARAMS)) {
			/* Load screen */
			screen_load();

			if (obj && obj->kind) {
				/* Track the object */
				track_object(player->upkeep, obj);

				if (!player_is_shapechanged(player)) {
					while  ((ret = context_menu_object(obj)) == 2);
				}

				/* Stay in "quiver" mode */
				player->upkeep->command_wrk = (USE_QUIVER);
			}
		} else {
			/* Load screen */
			screen_load();

			ret = -1;
		}
	}
}


/**
 * Look command
 */
void do_cmd_look(void)
{
	/* Look around */
	if (target_set_interactive(TARGET_LOOK, -1, -1))
	{
		msg("Target Selected.");
	}
}


/**
 * Allow the player to examine other sectors on the map
 */
void do_cmd_locate(void)
{
	int panel_hgt, panel_wid;
	int y1, x1;

	/* Use dimensions that match those in ui-output.c. */
	if (Term == term_screen) {
		panel_hgt = SCREEN_HGT;
		panel_wid = SCREEN_WID;
	} else {
		panel_hgt = Term->hgt / tile_height;
		panel_wid = Term->wid / tile_width;
	}
	/* Bound below to avoid division by zero. */
	panel_hgt = MAX(panel_hgt, 1);
	panel_wid = MAX(panel_wid, 1);

	/* Start at current panel */
	y1 = Term->offset_y;
	x1 = Term->offset_x;

	/* Show panels until done */
	while (1) {
		char tmp_val[80];
		char out_val[160];

		/* Assume no direction */
		int dir = 0;

		/* Get the current panel */
		int y2 = Term->offset_y;
		int x2 = Term->offset_x;
		
		/* Describe the location */
		if ((y2 == y1) && (x2 == x1)) {
			tmp_val[0] = '\0';
		} else {
			strnfmt(tmp_val, sizeof(tmp_val), "%s%s of",
			        ((y2 < y1) ? " north" : (y2 > y1) ? " south" : ""),
			        ((x2 < x1) ? " west" : (x2 > x1) ? " east" : ""));
		}

		/* Prepare to ask which way to look */
		strnfmt(out_val, sizeof(out_val),
		        "Map sector [%d,%d], which is%s your sector.  Direction?",
		        (2 * y2) / panel_hgt, (2 * x2) / panel_wid, tmp_val);

		/* More detail */
		if (OPT(player, center_player)) {
			strnfmt(out_val, sizeof(out_val),
		        	"Map sector [%d(%02d),%d(%02d)], which is%s your sector.  Direction?",
					(2 * y2) / panel_hgt, (2 * y2) % panel_hgt,
					(2 * x2) / panel_wid, (2 * x2) % panel_wid, tmp_val);
		}

		/* Get a direction */
		while (!dir) {
			struct keypress command = KEYPRESS_NULL;

			/* Get a command (or Cancel) */
			if (!get_com(out_val, (char *)&command.code)) break;

			/* Extract direction */
			dir = target_dir(command);

			/* Error */
			if (!dir) bell();
		}

		/* No direction */
		if (!dir) break;

		/* Apply the motion */
		change_panel(dir);

		/* Handle stuff */
		handle_stuff(player);
	}

	/* Verify panel */
	verify_panel();
}

static int cmp_mexp(const void *a, const void *b)
{
	uint16_t ia = *(const uint16_t *)a;
	uint16_t ib = *(const uint16_t *)b;
	if (r_info[ia].mexp < r_info[ib].mexp)
		return -1;
	if (r_info[ia].mexp > r_info[ib].mexp)
		return 1;
	return (a < b ? -1 : (a > b ? 1 : 0));
}

static int cmp_level(const void *a, const void *b)
{
	uint16_t ia = *(const uint16_t *)a;
	uint16_t ib = *(const uint16_t *)b;
	if (r_info[ia].level < r_info[ib].level)
		return -1;
	if (r_info[ia].level > r_info[ib].level)
		return 1;
	return cmp_mexp(a, b);
}

static int cmp_tkill(const void *a, const void *b)
{
	uint16_t ia = *(const uint16_t *)a;
	uint16_t ib = *(const uint16_t *)b;
	if (l_list[ia].tkills < l_list[ib].tkills)
		return -1;
	if (l_list[ia].tkills > l_list[ib].tkills)
		return 1;
	return cmp_level(a, b);
}

static int cmp_pkill(const void *a, const void *b)
{
	uint16_t ia = *(const uint16_t *)a;
	uint16_t ib = *(const uint16_t *)b;
	if (l_list[ia].pkills < l_list[ib].pkills)
		return -1;
	if (l_list[ia].pkills > l_list[ib].pkills)
		return 1;
	return cmp_tkill(a, b);
}

int cmp_monsters(const void *a, const void *b)
{
	return cmp_level(a, b);
}

/**
 * Search the monster, item, and feature types to find the
 * meaning for the given symbol.
 *
 * Note: We currently search items first, then features, then
 * monsters, and we return the first hit for a symbol.
 * This is to prevent mimics and lurkers from matching
 * a symbol instead of the item or feature it is mimicking.
 *
 * Todo: concatenate all matches into buf. This will be much
 * easier once we can loop through item tvals instead of items
 * (see note below.)
 *
 * Todo: Should this take the user's pref files into account?
 */
static void lookup_symbol(char sym, char *buf, size_t max)
{
	int i;
	struct monster_base *race;

	/* Look through items */
	/* Note: We currently look through all items, and grab the tval when we
	 * find a match.
	 * It would make more sense to loop through tvals, but then we need to
	 * associate a display character with each tval. */
	for (i = 0; i < z_info->k_max; i++) {
		if (char_matches_key(k_info[i].d_char, sym)) {
			strnfmt(buf, max, "%c - %s.", sym, tval_find_name(k_info[i].tval));
			return;
		}
	}

	/* Look through features */
	/* Note: We need a better way of doing this. Currently '#' matches secret
	 * door, and '^' matches trap door (instead of the more generic "trap"). */
	for (i = 1; i < z_info->f_max; i++) {
		if (char_matches_key(f_info[i].d_char, sym)) {
			strnfmt(buf, max, "%c - %s.", sym, f_info[i].name);
			return;
		}
	}
	
	/* Look through monster templates */
	for (race = rb_info; race; race = race->next) {
		/* Slight hack - P appears twice */
		if (streq(race->name, "Morgoth")) continue;
		if (char_matches_key(race->d_char, sym)) {
			strnfmt(buf, max, "%c - %s.", sym, race->text);
			return;
		}
	}

	/* No matches */
        if (isprint(sym)) {
			strnfmt(buf, max, "%c - Unknown Symbol.", sym);
        } else {
			strnfmt(buf, max, "? - Unknown Symbol.");
        }
	
	return;
}

/**
 * Identify a character, allow recall of monsters
 *
 * Several "special" responses recall "multiple" monsters:
 *   ^A (all monsters)
 *   ^U (all unique monsters)
 *   ^N (all non-unique monsters)
 *
 * The responses may be sorted in several ways, see below.
 *
 * Note that the player ghosts are ignored, since they do not exist.
 */
void do_cmd_query_symbol(void)
{
	int idx, num;
	char buf[128];

	char sym;
	struct keypress query;

	bool all = false;
	bool uniq = false;
	bool norm = false;

	bool recall = false;

	uint16_t *who;

	/* Get a character, or abort */
	if (!get_com("Enter character to be identified, or control+[ANU]: ", &sym))
		return;

	/* Describe */
	if (sym == KTRL('A')) {
		all = true;
		my_strcpy(buf, "Full monster list.", sizeof(buf));
	} else if (sym == KTRL('U')) {
		all = uniq = true;
		my_strcpy(buf, "Unique monster list.", sizeof(buf));
	} else if (sym == KTRL('N')) {
		all = norm = true;
		my_strcpy(buf, "Non-unique monster list.", sizeof(buf));
	} else {
		lookup_symbol(sym, buf, sizeof(buf));
	}

	/* Display the result */
	prt(buf, 0, 0);

	/* Allocate the "who" array */
	who = mem_zalloc(z_info->r_max * sizeof(uint16_t));

	/* Collect matching monsters */
	for (num = 0, idx = 1; idx < z_info->r_max; idx++) {
		struct monster_race *race = &r_info[idx];
		struct monster_lore *lore = &l_list[idx];

		/* Nothing to recall */
		if (!lore->all_known && !lore->sights)
			continue;

		/* Require non-unique monsters if needed */
		if (norm && rf_has(race->flags, RF_UNIQUE)) continue;

		/* Require unique monsters if needed */
		if (uniq && !rf_has(race->flags, RF_UNIQUE)) continue;

		/* Collect "appropriate" monsters */
		if (all || char_matches_key(race->d_char, sym)) who[num++] = idx;
	}

	/* No monsters to recall */
	if (!num) {
		/* Free the "who" array */
		mem_free(who);
		return;
	}

	/* Prompt */
	put_str("Recall details? (y/k/n): ", 0, 40);

	/* Query */
	query = inkey();

	/* Restore */
	prt(buf, 0, 0);

	/* Interpret the response */
	if (query.code == 'k') {
		/* Sort by kills (and level) */
		sort(who, num, sizeof(*who), cmp_pkill);
	} else if (query.code == 'y' || query.code == 'p') {
		/* Sort by level; accept 'p' as legacy */
		sort(who, num, sizeof(*who), cmp_level);
	} else {
		/* Any unsupported response is "nope, no history please" */
		mem_free(who);
		return;
	}

	/* Start at the end, as the array is sorted lowest to highest */
	idx = num - 1;

	/* Scan the monster memory */
	while (1) {
		textblock *tb;

		/* Extract a race */
		int r_idx = who[idx];
		struct monster_race *race = &r_info[r_idx];
		struct monster_lore *lore = &l_list[r_idx];

		/* Auto-recall */
		monster_race_track(player->upkeep, race);

		/* Do any necessary updates or redraws */
		handle_stuff(player);

		tb = textblock_new();
		lore_title(tb, race);

		textblock_append(tb, " [(r)ecall, ESC]");
		textui_textblock_place(tb, SCREEN_REGION, NULL);
		textblock_free(tb);

		/* Interact */
		while (1) {
			/* Ignore keys during recall presentation, otherwise, the 'r' key
			 * acts like a toggle and instead of a one-off command */
			if (recall)
				lore_show_interactive(race, lore);
			else
				query = inkey();

			/* Normal commands */
			if (query.code != 'r') break;

			/* Toggle recall */
			recall = !recall;
		}

		/* Stop scanning */
		if (query.code == ESCAPE) break;

		/* Move to previous or next monster */
		if (query.code == '-') {
			/* Previous is a step forward in the array */
			idx++;
			/* Wrap if we're at the end of the array */
			if (idx == num) {
				idx = 0;
			}
		} else {
			/* Next is a step back in the array */
			idx--;
			/* Wrap if we're at the start of the array */
			if (idx < 0) {
				idx = num - 1;
			}
		}
	}

	/* Re-display the identity */
	prt(buf, 0, 0);

	/* Free the "who" array */
	mem_free(who);
}

/**
 * Centers the map on the player
 */
void do_cmd_center_map(void)
{
	center_panel();
}



/**
 * Display the main-screen monster list.
 */
void do_cmd_monlist(void)
{
	/* Save the screen and display the list */
	screen_save();

    monster_list_show_interactive(Term->hgt, Term->wid);

	/* Return */
	screen_load();
}


/**
 * Display the main-screen item list.
 */
void do_cmd_itemlist(void)
{
	/* Save the screen and display the list */
	screen_save();

    object_list_show_interactive(Term->hgt, Term->wid);

	/* Return */
	screen_load();
}
