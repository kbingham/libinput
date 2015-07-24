/*
 * Copyright © 2015 Kieran Bingham <kieranbingham@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#include <check.h>
#include <stdio.h>
#include <libinput.h>

#include "libinput-util.h"
#include "litest.h"

#define litest_log(...) fprintf(stderr, __VA_ARGS__)

struct keys {
	int code;
	int is_press;
};

START_TEST(halfkey_default_enabled)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	enum libinput_config_status status;
	int available;
	enum libinput_config_halfkey_state state, deflt;

	/* We would expect halfkey to be a user enabled feature
	 * The only exception could be a specific half sized keyboard
	 */
	deflt = false;

	available = libinput_device_config_halfkey_is_available(device);
	ck_assert(available);

	state = libinput_device_config_halfkey_get_enabled(device);
	ck_assert_int_eq(state, deflt);

	state = libinput_device_config_halfkey_get_default_enabled(
					    device);
	ck_assert_int_eq(state, deflt);

	status = libinput_device_config_halfkey_set_enabled(device,
					    LIBINPUT_CONFIG_HALFKEY_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_halfkey_set_enabled(device,
					    LIBINPUT_CONFIG_HALFKEY_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	status = libinput_device_config_halfkey_set_enabled(device, 3);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_INVALID);
}
END_TEST

static void
halfkey_test_sequence(struct keys *input, unsigned in_length,
		      struct keys *expected, unsigned expected_length)
{
	struct litest_device *dev = litest_current_device();
	struct libinput *libinput = dev->libinput;
	struct libinput_device *device = dev->libinput_device;
	struct libinput_event *event;
	struct libinput_event_keyboard *kevent;
	enum libinput_config_status status;

	unsigned i;

	status = libinput_device_config_halfkey_set_enabled(device,
					    LIBINPUT_CONFIG_HALFKEY_ENABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_drain_events(libinput);

	for (i = 0; i < in_length; ++i) {
		int key = input[i].code;
		int is_press = input[i].is_press;

		litest_log("%s key %s\n", (is_press?"Press":"Release"),
			libevdev_event_code_get_name(EV_KEY, key));

		litest_event(dev, EV_KEY, key, is_press);
		litest_event(dev, EV_SYN, SYN_REPORT, 0);

		libinput_dispatch(libinput);
	}

	for (i = 0; i < expected_length; ++i) {
		int key = expected[i].code;
		int is_press = expected[i].is_press;

		litest_wait_for_event(libinput);
		event = libinput_get_event(libinput);
		kevent = litest_is_keyboard_event(event, key, is_press);
		libinput_event_destroy(event);
	}

	litest_drain_events(libinput);
}

START_TEST(halfkey_test_keypress)
{
	struct keys input[] = {
		/* Test our Modifier still works as expected */
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },

		/* Test a key we would modify */
		{ KEY_0, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_0, LIBINPUT_KEY_STATE_RELEASED },

		/* Test a key we would not modify */
		{ KEY_F1, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_F1, LIBINPUT_KEY_STATE_RELEASED },
	};

	/* In this sequence we expect exactly what we put in to come out */
	halfkey_test_sequence(input, ARRAY_LENGTH(input),
			      input, ARRAY_LENGTH(input));
}
END_TEST

START_TEST(halfkey_test_mixed_space)
{
	struct keys input[] = {
		{ KEY_0, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_0, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },
	};

	struct keys expected[] = {
		/* Note the inverted sequence of the space down */
		{ KEY_0, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_0, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },
	};

	halfkey_test_sequence(input, ARRAY_LENGTH(input),
			      expected, ARRAY_LENGTH(expected));
}
END_TEST

START_TEST(halfkey_test_mixed_nonmirrored_key)
{
	struct keys input[] = {
		/* The sequence that caught me out gitk --all : gitk- -all */
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_MINUS, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_MINUS, LIBINPUT_KEY_STATE_RELEASED },

		/* An extra variation check */
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_MINUS, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_MINUS, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },

		/* And more complicated */
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_MINUS, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_MINUS, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_MINUS, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_MINUS, LIBINPUT_KEY_STATE_RELEASED },
	};

	/* In this sequence we expect exactly what we put in to come out */
	halfkey_test_sequence(input, ARRAY_LENGTH(input),
			      input, ARRAY_LENGTH(input));
}
END_TEST

START_TEST(halfkey_test_mirrored_key)
{
	/* The Space press and release should be consumed
	 * and the input key should be mirrored */
	struct keys input[] = {
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_F, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_F, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },
	};

	struct keys expected[] = {
		{ KEY_J, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_J, LIBINPUT_KEY_STATE_RELEASED },
	};

	halfkey_test_sequence(input, ARRAY_LENGTH(input),
			      expected, ARRAY_LENGTH(expected));
}
END_TEST

START_TEST(halfkey_test_mirrored_sequence)
{
	struct keys input[] = {
		/* Sequence Start */
		{ KEY_A, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_A, LIBINPUT_KEY_STATE_RELEASED },

		/* Simple Mirrored Key */
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_F, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_F, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },

		/* Now a tricky one. Space down, key down, space up key up */
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_V, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_V, LIBINPUT_KEY_STATE_RELEASED },

		/* Sequence End */
		{ KEY_Z, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_Z, LIBINPUT_KEY_STATE_RELEASED },
	};

	struct keys expected[] = {
		/* Sequence Start */
		{ KEY_A, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_A, LIBINPUT_KEY_STATE_RELEASED },

		/* Simple Mirrored Key */
		{ KEY_J, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_J, LIBINPUT_KEY_STATE_RELEASED },

		/* Complex Mirrored Key */
		{ KEY_M, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_M, LIBINPUT_KEY_STATE_RELEASED },

		/* Sequence End */
		{ KEY_Z, LIBINPUT_KEY_STATE_PRESSED },
		{ KEY_Z, LIBINPUT_KEY_STATE_RELEASED },
	};

	halfkey_test_sequence(input, ARRAY_LENGTH(input),
			      expected, ARRAY_LENGTH(expected));
}
END_TEST

START_TEST(halfkey_test_corner_cases)
{
	struct keys input[] = {
		/* Press a key down, then mirrored key down! */
		{ KEY_F, LIBINPUT_KEY_STATE_PRESSED }, /* Normal F */
		{ KEY_SPACE, LIBINPUT_KEY_STATE_PRESSED }, /* State changer */
		{ KEY_J, LIBINPUT_KEY_STATE_PRESSED }, /* This is now 2nd F */
		{ KEY_J, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_SPACE, LIBINPUT_KEY_STATE_RELEASED },
		{ KEY_F, LIBINPUT_KEY_STATE_RELEASED },
	};

	struct keys expected[] = {
		{ KEY_F, LIBINPUT_KEY_STATE_PRESSED },
		/* Space is swallowed as a state change */
		{ KEY_F, LIBINPUT_KEY_STATE_PRESSED }, /* Mirrored J Down */
		{ KEY_F, LIBINPUT_KEY_STATE_RELEASED }, /* Mirrored F Up */
		{ KEY_F, LIBINPUT_KEY_STATE_RELEASED }, /* Actual F Up */
	};

	halfkey_test_sequence(input, ARRAY_LENGTH(input),
			      expected, ARRAY_LENGTH(expected));
}
END_TEST

void
litest_setup_tests(void)
{
	litest_add("keyboard:halfkey", halfkey_default_enabled, LITEST_KEYS, LITEST_ANY);
	litest_add_for_device("keyboard:halfkey", halfkey_test_keypress, LITEST_KEYBOARD);
	litest_add_for_device("keyboard:halfkey", halfkey_test_mixed_nonmirrored_key, LITEST_KEYBOARD);
	litest_add_for_device("keyboard:halfkey", halfkey_test_mixed_space, LITEST_KEYBOARD);
	litest_add_for_device("keyboard:halfkey", halfkey_test_mirrored_key, LITEST_KEYBOARD);
	litest_add_for_device("keyboard:halfkey", halfkey_test_mirrored_sequence, LITEST_KEYBOARD);
	//litest_add_for_device("keyboard:halfkey", halfkey_test_corner_cases, LITEST_KEYBOARD);
}
