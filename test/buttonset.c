/*
 * Copyright © 2015 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <config.h>

#include <check.h>
#include <libevdev/libevdev.h>
#include <libinput.h>
#include <limits.h>

#include "litest.h"
#include "libinput-util.h"

START_TEST(buttonset_has_cap)
{
	struct litest_device *dev = litest_current_device();

	ck_assert(libinput_device_has_capability(dev->libinput_device,
						 LIBINPUT_DEVICE_CAP_BUTTONSET));
}
END_TEST

START_TEST(buttonset_has_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libevdev *evdev = dev->evdev;
	unsigned int code;

	for (code = 0; code < KEY_CNT; code++)
		ck_assert_int_eq(libevdev_has_event_code(evdev, EV_KEY, code),
				 libinput_device_buttonset_has_button(device, code));
}
END_TEST

START_TEST(buttonset_buttons)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;
	struct libinput_event *ev;
	struct libinput_event_buttonset *bs __attribute__((unused));
	unsigned int code;
	enum libinput_button_state state;

	litest_drain_events(li);

	for (code = 0; code < KEY_CNT; code++) {
		if (!libinput_device_buttonset_has_button(device, code))
			continue;

		litest_button_click(dev, code, true);
		litest_wait_for_event(li);
		state = LIBINPUT_BUTTON_STATE_PRESSED;

		ev = libinput_get_event(li);
		bs = litest_is_buttonset_button_event(ev, code, state);
		libinput_event_destroy(ev);
		litest_assert_empty_queue(li);

		litest_button_click(dev, code, false);
		litest_wait_for_event(li);
		state = LIBINPUT_BUTTON_STATE_RELEASED;

		ev = libinput_get_event(li);
		bs = litest_is_buttonset_button_event(ev, code, state);
		libinput_event_destroy(ev);
		litest_assert_empty_queue(li);
	}
}
END_TEST

START_TEST(buttonset_release_on_disable)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;
	unsigned long buttonmask[NLONGS(KEY_CNT)] = {0};
	enum libinput_config_status status;
	struct libinput_event *event;
	struct libinput_event_buttonset *bs;
	uint32_t button;
	unsigned int code;
	enum libinput_button_state state;

	litest_drain_events(li);

	for (code = 0; code < KEY_CNT; code++) {
		if (!libinput_device_buttonset_has_button(device, code))
			continue;

		litest_button_click(dev, code, true);
		litest_drain_events(li);
		long_set_bit(buttonmask, code);
	}

	status = libinput_device_config_send_events_set_mode(device,
			LIBINPUT_CONFIG_SEND_EVENTS_DISABLED);
	ck_assert_int_eq(status, LIBINPUT_CONFIG_STATUS_SUCCESS);

	litest_wait_for_event(li);

	while ((event = libinput_get_event(li))) {
		bs = libinput_event_get_buttonset_event(event);
		ck_assert_notnull(bs);

		button = libinput_event_buttonset_get_button(bs);
		state = libinput_event_buttonset_get_button_state(bs);
		ck_assert_int_eq(state, LIBINPUT_BUTTON_STATE_RELEASED);

		ck_assert(long_bit_is_set(buttonmask, button));
		long_clear_bit(buttonmask, button);
		libinput_event_destroy(event);
	}

	for (code = 0; code < KEY_CNT; code++)
		ck_assert(!long_bit_is_set(buttonmask, button));
}
END_TEST

START_TEST(buttonset_wacom_pad_ring)
{
	struct litest_device *dev = litest_current_device();
	struct libinput_device *device = dev->libinput_device;
	struct libinput *li = dev->libinput;
	struct libinput_event *event;
	struct libinput_event_buttonset *bs;
	unsigned int ring_axis = UINT_MAX;
	unsigned int i;
	double val, oldval = -1;
	double delta, expected_delta;
	double expected_discrete = 0.0;

	litest_drain_events(li);

	for (i = 0; i < libinput_device_buttonset_get_num_axes(device); i++) {
		if (libinput_device_buttonset_get_axis_type(device, i) ==
		    LIBINPUT_BUTTONSET_AXIS_RING) {
			ring_axis = i;
		}
	}

	ck_assert_int_lt(ring_axis, UINT_MAX);

	litest_buttonset_ring_start(dev, 30);
	litest_buttonset_ring_change(dev, 40);
	litest_buttonset_ring_change(dev, 50);
	litest_buttonset_ring_end(dev);

	litest_wait_for_event(li);

	expected_delta = 0; /* first event has no delta */
	while ((event = libinput_get_event(li))) {
		bs = libinput_event_get_buttonset_event(event);
		ck_assert_notnull(bs);
		ck_assert_int_eq(libinput_event_get_type(event),
				 LIBINPUT_EVENT_BUTTONSET_AXIS);

		ck_assert(libinput_event_buttonset_axis_has_changed(bs,
								    ring_axis));

		val = libinput_event_buttonset_get_axis_value(bs,
							      ring_axis);
		ck_assert(val > oldval);
		oldval = val;

		val = libinput_event_buttonset_get_axis_value_transformed(
								  bs,
								  ring_axis,
								  100);
		ck_assert(val < 360);
		ck_assert(val > 0);

		/* wheel doesn't do discrete */
		val = libinput_event_buttonset_get_axis_delta_discrete(bs,
								  ring_axis);
		ck_assert_int_eq(round(val), expected_discrete);
		expected_discrete = 7.0; /* 10% are 7 steps on the wheel */

		delta = libinput_event_buttonset_get_axis_delta(bs, ring_axis);
		ck_assert_int_ge(round(delta), expected_delta - 1);
		ck_assert_int_le(round(delta), expected_delta + 1);
		expected_delta = 36; /* 10% increases in the ring data == 36 deg */

		/* FIXME: not implemented yet */
		ck_assert_int_eq(libinput_event_buttonset_get_axis_source(bs, ring_axis),
				 LIBINPUT_BUTTONSET_AXIS_SOURCE_UNKNOWN);

		libinput_event_destroy(event);
		libinput_dispatch(li);
	}
}
END_TEST

void
litest_setup_tests(void)
{
	litest_add("buttonset:capability", buttonset_has_cap, LITEST_BUTTONSET, LITEST_ANY);
	litest_add("buttonset:buttons", buttonset_has_buttons, LITEST_BUTTONSET, LITEST_ANY);
	litest_add("buttonset:buttons", buttonset_buttons, LITEST_BUTTONSET, LITEST_ANY);
	litest_add("buttonset:buttons", buttonset_release_on_disable, LITEST_BUTTONSET, LITEST_ANY);

	litest_add_for_device("buttonset:ring", buttonset_wacom_pad_ring, LITEST_WACOM_PAD);
}
