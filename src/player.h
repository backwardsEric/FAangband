/* player/player.h - player interface */

#ifndef PLAYER_PLAYER_H
#define PLAYER_PLAYER_H

/* spell.c */
s16b modify_stat_value(int value, int amount);
int spell_collect_from_book(const object_type *o_ptr, int spells[PY_MAX_SPELLS]);
int spell_book_count_spells(const object_type *o_ptr, bool (*tester)(int spell));
bool spell_okay_list(bool (*spell_test)(int spell), const int spells[], int n_spells);
bool spell_okay_to_cast(int spell);
bool spell_okay_to_study(int spell);
bool spell_okay_to_browse(int spell);
bool spell_in_book(int spell, int book);
s16b spell_chance(int spell);
void spell_learn(int spell);
bool spell_cast(int spell, int dir);

/* timed.c */
bool set_timed(int idx, int v, bool notify);
bool inc_timed(int idx, int v, bool notify);
bool dec_timed(int idx, int v, bool notify);
bool clear_timed(int idx, bool notify);
bool set_food(int v);

/* util.c */
bool player_can_cast(void);
bool player_can_study(void);
bool player_can_read(void);
bool player_can_fire(void);

#endif /* !PLAYER_PLAYER_H */
