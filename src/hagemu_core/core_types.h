#ifndef HAGEMU_CORE_TYPES_H
#define HAGEMU_CORE_TYPES_H

enum GBModel {
	MODEL_DMG, // Original gameboy (default)
	MODEL_CGB, // Gameboy color
	MODEL_CGB_BACKCOMPAT, // Gameboy color in DMG mode
	MODEL_MGB, // Gameboy pocket
};

typedef enum HagemuButton {
	HAGEMU_BUTTON_RIGHT,
	HAGEMU_BUTTON_LEFT,
	HAGEMU_BUTTON_UP,
	HAGEMU_BUTTON_DOWN,
	HAGEMU_BUTTON_A,
	HAGEMU_BUTTON_B,
	HAGEMU_BUTTON_START,
	HAGEMU_BUTTON_SELECT,
} HagemuButton;

#endif
