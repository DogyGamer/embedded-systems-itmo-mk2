#include <string.h>
#include "main.h"
#include "kb.h"
#include "oled.h"
#include "fonts.h"
#include "sdk_uart.h"
#include "calc.h"

#define DEBOUNCE_MS     25
#define LONGPRESS_MS    600
#define MAX_DIGITS      6

typedef enum {
	CALC_INPUT_A = 0,
	CALC_INPUT_B,
	CALC_RESULT
} CalcState;

static CalcState state;
static int64_t operandA;
static int64_t operandB;
static int64_t result;
static uint8_t digitsA;
static uint8_t digitsB;
static char    op;

static int8_t  s_stableKey;
static int8_t  s_candidateKey;
static uint32_t s_candidateSince;
static uint32_t s_pressStartTick;

static int8_t Keypad_Scan(void) {
	static const uint8_t rows[4] = { ROW1, ROW2, ROW3, ROW4 };

	for (uint8_t r = 0; r < 4; r++) {
		uint8_t bits = Check_Row(rows[r]);
		if (bits == 0x04) return (int8_t)(r * 3 + 0);
		if (bits == 0x02) return (int8_t)(r * 3 + 1);
		if (bits == 0x01) return (int8_t)(r * 3 + 2);
	}
	return -1;
}

static int8_t DigitForKey(int8_t key) {
	if (key >= 0 && key <= 8) return (int8_t)(key + 1);
	if (key == 10) return 0;
	return -1;
}


static char NextOp(char cur) {
	if (cur == '+') return '-';
	if (cur == '-') return '*';
	return '+';
}

static int64_t Calc_Apply(char oper, int64_t a, int64_t b) {
	switch (oper) {
		case '+': return a + b;
		case '-': return a - b;
		case '*': return a * b;
		default:  return b;
	}
}

static void Calc_Reset(void) {
	state = CALC_INPUT_A;
	operandA = 0;
	operandB = 0;
	result = 0;
	digitsA = 0;
	digitsB = 0;
	op = 0;
}

static void Calc_OnDigit(uint8_t d) {
	if (state == CALC_RESULT) {
		Calc_Reset();
	}

	if (state == CALC_INPUT_A) {
		if (digitsA < MAX_DIGITS) {
			operandA = operandA * 10 + d;
			digitsA++;
		}
	} else { /* CALC_INPUT_B */
		if (digitsB < MAX_DIGITS) {
			operandB = operandB * 10 + d;
			digitsB++;
		}
	}
}

static void Calc_OnOperator(void) {
	switch (state) {
		case CALC_INPUT_A:
			state = CALC_INPUT_B;
			op = '+';
			operandB = 0;
			digitsB = 0;
			break;

		case CALC_INPUT_B:
			if (digitsB == 0) {
				op = NextOp(op);
			} else {
				operandA = Calc_Apply(op, operandA, operandB);
				op = '+';
				operandB = 0;
				digitsB = 0;
			}
			break;

		case CALC_RESULT:
			operandA = result;
			state = CALC_INPUT_B;
			op = '+';
			operandB = 0;
			digitsB = 0;
			break;
	}
}

static void Calc_OnEquals(void) {
	if (state == CALC_INPUT_B) {
		result = Calc_Apply(op, operandA, operandB);
		state = CALC_RESULT;
	}
}

static void Calc_OnBackspace(void) {
	switch (state) {
		case CALC_INPUT_A:
			if (digitsA > 0) {
				operandA /= 10;
				digitsA--;
			}
			break;

		case CALC_INPUT_B:
			if (digitsB > 0) {
				operandB /= 10;
				digitsB--;
			} else {
				state = CALC_INPUT_A;
				op = 0;
			}
			break;

		case CALC_RESULT:
			Calc_Reset();
			break;
	}
}

static void Calc_OnClear(void) {
	Calc_Reset();
}


static void Int64ToStr(int64_t value, char *out) {
	char tmp[24];
	int i = 0;
	uint8_t neg = 0;
	uint64_t v;

	if (value < 0) {
		neg = 1;
		v = (uint64_t)(-value);
	} else {
		v = (uint64_t)value;
	}

	if (v == 0) {
		tmp[i++] = '0';
	}
	while (v > 0) {
		tmp[i++] = (char)('0' + (v % 10));
		v /= 10;
	}

	int j = 0;
	if (neg) out[j++] = '-';
	while (i > 0) out[j++] = tmp[--i];
	out[j] = 0;
}

static void Calc_Draw(void) {
	char top[24] = { 0 };
	char main_[24] = { 0 };
	char a[16], b[16];
	char opstr[2];

	switch (state) {
		case CALC_INPUT_A:
			Int64ToStr(operandA, main_);
			break;

		case CALC_INPUT_B:
			Int64ToStr(operandA, a);
			strcpy(top, a);
			opstr[0] = op ? op : '+';
			opstr[1] = 0;
			strcat(top, opstr);
			Int64ToStr(operandB, main_);
			break;

		case CALC_RESULT:
			Int64ToStr(operandA, a);
			Int64ToStr(operandB, b);
			strcpy(top, a);
			opstr[0] = op ? op : '+';
			opstr[1] = 0;
			strcat(top, opstr);
			strcat(top, b);
			strcat(top, "=");
			Int64ToStr(result, main_);
			break;
	}

	oled_Fill(Black);

	oled_SetCursor(0, 0);
	oled_WriteString(top, Font_7x10, White);


	const FontDef *font = &Font_16x26;
	uint16_t len = (uint16_t)strlen(main_);
	if (len * font->FontWidth > OLED_WIDTH) {
		font = &Font_11x18;
		if (len * font->FontWidth > OLED_WIDTH) {
			font = &Font_7x10;
		}
	}

	uint16_t w = (uint16_t)(len * font->FontWidth);
	uint8_t x = (w < OLED_WIDTH) ? (uint8_t)(OLED_WIDTH - w) : 0;
	uint8_t y = (uint8_t)(OLED_HEIGHT - font->FontHeight - 4);

	oled_SetCursor(x, y);
	oled_WriteString(main_, *font, White);

	oled_UpdateScreen();
}


void Calc_Init(void) {
	Calc_Reset();

	s_stableKey = -1;
	s_candidateKey = -1;
	s_candidateSince = HAL_GetTick();
	s_pressStartTick = 0;

	UART_Transmit((uint8_t*)"Calculator ready\n");
	UART_Transmit((uint8_t*)"Digits: 1-9 and 0\n");
	UART_Transmit((uint8_t*)"Bottom-left key : tap = +/-/x, hold = Clear\n");
	UART_Transmit((uint8_t*)"Bottom-right key: tap = '=',  hold = Backspace\n");

	Calc_Draw();
}

void Calc_Poll(void) {
	int8_t raw = Keypad_Scan();
	uint32_t now = HAL_GetTick();

	if (raw != s_candidateKey) {
		s_candidateKey = raw;
		s_candidateSince = now;
		return;
	}

	if ((now - s_candidateSince) < DEBOUNCE_MS) {
		return;
	}

	if (raw == s_stableKey) {
		return;
	}

	int8_t prevStable = s_stableKey;
	s_stableKey = raw;

	if (s_stableKey != -1) {
		s_pressStartTick = now;

		int8_t digit = DigitForKey(s_stableKey);
		if (digit >= 0) {
			Calc_OnDigit((uint8_t)digit);
			Calc_Draw();
		}
	} else {
		uint32_t heldMs = now - s_pressStartTick;

		if (prevStable == 9) { /* OP */
			if (heldMs >= LONGPRESS_MS) {
				Calc_OnClear();
			} else {
				Calc_OnOperator();
			}
			Calc_Draw();
		} else if (prevStable == 11) { /* ENT */
			if (heldMs >= LONGPRESS_MS) {
				Calc_OnBackspace();
			} else {
				Calc_OnEquals();
			}
			Calc_Draw();
		}
	}
}
