/* $Id$ */

/** @file texteff.hpp Functions related to text effects. */

#ifndef TEXTEFF_HPP
#define TEXTEFF_HPP

#include "gfx_type.h"
#include "economy_type.h"

/**
 * Text effect modes.
 */
enum TextEffectMode {
	TE_RISING, ///< Make the text effect slowly go upwards
	TE_STATIC, ///< Keep the text effect static

	INVALID_TE_ID = 0xFFFF,
};

typedef uint16 TextEffectID;

struct TextEffect {
	StringID string_id;
	int32 x;
	int32 y;
	int32 right;
	int32 bottom;
	uint16 duration;
	uint64 params_1;
	uint64 params_2;
	TextEffectMode mode;
};

extern TextEffect *_text_effect_list;
extern uint16 _num_text_effects;


void MoveAllTextEffects();
TextEffectID AddTextEffect(StringID msg, int x, int y, uint16 duration, TextEffectMode mode);
void InitTextEffects();
void DrawTextEffects(DrawPixelInfo *dpi);
void UpdateTextEffect(TextEffectID effect_id, StringID msg);
void RemoveTextEffect(TextEffectID effect_id);

/* misc_gui.cpp */
TextEffectID ShowFillingPercent(int x, int y, int z, uint8 percent, Money payment, StringID colour);
void UpdateFillingPercent(TextEffectID te_id, uint8 percent, Money payment, StringID colour);
void HideFillingPercent(TextEffectID *te_id);

#endif /* TEXTEFF_HPP */
