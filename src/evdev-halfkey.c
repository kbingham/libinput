/*
 * Copyright © 2015 Red Hat, Inc.
 * Copyright © 2015 Kieran Bingham
 * Portions from John Meacham : http://repetae.net/computer/hk/
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

#include <stdint.h>

#include "evdev.h"

#define CASE_RETURN_STRING(a) case a: return #a;

static inline const char*
halfkey_state_to_str(enum evdev_halfkey_state state)
{
	switch (state) {
	CASE_RETURN_STRING(HALFKEY_SPACE_IDLE);
	CASE_RETURN_STRING(HALFKEY_SPACE_PRESSED);
	CASE_RETURN_STRING(HALFKEY_SPACE_MODIFIED);
	}

	return NULL;
}

static inline const char*
halfkey_event_to_str(enum evdev_halfkey_event event)
{
	switch (event) {
	CASE_RETURN_STRING(HALFKEY_SPACE_DOWN);
	CASE_RETURN_STRING(HALFKEY_SPACE_UP);
	CASE_RETURN_STRING(HALFKEY_MIRROR_DOWN);
	CASE_RETURN_STRING(HALFKEY_MIRROR_UP);
	CASE_RETURN_STRING(HALFKEY_OTHERKEY);
	}

	return NULL;
}

static inline const char*
halfkey_action_to_str(enum evdev_halfkey_action action)
{
	switch (action) {
	CASE_RETURN_STRING(HALFKEY_PASSTHROUGH);
	CASE_RETURN_STRING(HALFKEY_DISCARD);
	CASE_RETURN_STRING(HALFKEY_INJECTMIRROR);
	}

	return NULL;
}

static void
halfkey_state_error(struct evdev_device *device, enum evdev_halfkey_event event)
{
	log_bug_libinput(device->base.seat->libinput,
			 "Invalid event %s in halfkey input state %s\n",
			 halfkey_event_to_str(event),
			 halfkey_state_to_str(device->halfkey.state));
}

static void
halfkey_set_key_down(struct evdev_device *device, int code, int pressed)
{
	long_set_bit_state(device->halfkey.keymask, code, pressed);
}

static int
halfkey_is_key_down(struct evdev_device *device, int code)
{
	return long_bit_is_set(device->halfkey.keymask, code);
}

static void
halfkey_debug_line(struct evdev_device *device)
{
	char keys_down[1024] = "";
	char * ptr = keys_down;

	for (unsigned i = 0; i < KEY_CNT; ++i)
		if (halfkey_is_key_down(device, i))
			ptr += sprintf(ptr, "%s ",
				libevdev_event_code_get_name(EV_KEY, i));

	log_bug_libinput(device->base.seat->libinput,
		"State: %s : Keys Down %s\n",
		 halfkey_state_to_str(device->halfkey.state), keys_down);
}

static int
halfkey_mirror_key(int keycode)
{
	/************************
	 * Half-Key heavily based on code by John Meacham
	 * john@foo.net
	 * Adapted for X Keycodes, special cases and extra rows.
	 ***********************/
	int t=0;

	if(keycode >= KEY_1 && keycode <= KEY_0) t = KEY_1;
	if(keycode >= KEY_Q && keycode <= KEY_P) t = KEY_Q;
	if(keycode >= KEY_A && keycode <= KEY_SEMICOLON) t = KEY_A;
	if(keycode >= KEY_Z && keycode <= KEY_SLASH) t = KEY_Z;
	if(t) {
		int temp = keycode;
		temp -=t+4;
		if(temp < 1)
			temp--;
		temp = -temp;
		if(temp < 1)
			temp++;
		temp +=t+4;
		keycode = temp;
	}

	/* Swap special cases */
	switch(keycode) {
	case KEY_BACKSPACE:
		keycode = KEY_TAB;
		break;
	case KEY_TAB:
		keycode = KEY_BACKSPACE;
		break;

	case KEY_ENTER:
		keycode = KEY_CAPSLOCK;
		break;
	case KEY_CAPSLOCK:
		keycode = KEY_ENTER;
		break;
	}

	return keycode;
}

    /*
     * SPACE State Table
     *
     * Event \ \ State	IDLE		PRESSED		MODIFIED	RELEASE
     *
     * SpaceDown	Queued		Discarded	Invalid 	Queued
     * 			-> PRESSED	-> PRESSED	-> MODIFIED 	-> PRESSED
     *
     * SpaceUp		Passthrough	SpaceDown/Pass	Discard		Invalid
     * 			-> IDLE		-> IDLE		-> RELEASE	-> RELEASE
     *
     * MirroredDown	Passthrough	InjectMirror	InjectMirror	Passthrough
     * 			-> IDLE		-> MODIFIED	-> MODIFIED	-> RELEASE
     *
     * MirroredUp	Passthrough	Passthrough	InjectMirror	CheckMirror
     * 			-> IDLE		-> PRESSED	-> MODIFIED	-> RELEASE/IDLE
     *
     * OtherKey		Passthrough	SpaceDown/Pass	Passthrough	Passthrough
     * 			-> IDLE		-> IDLE 	-> MODIFIED	-> RELEASE
     */

static enum evdev_halfkey_action
evdev_halfkey_idle_handle_event(struct evdev_device *device,
				uint64_t time,
				enum evdev_halfkey_event event)
{
	enum evdev_halfkey_action action = HALFKEY_PASSTHROUGH;

	switch(event) {
	case HALFKEY_SPACE_DOWN:
		device->halfkey.state = HALFKEY_SPACE_PRESSED;
		device->halfkey.modifier_queued = time;

		/* Change state but swallow the Space Input Event */
		action = HALFKEY_DISCARD;
		break;
	case HALFKEY_SPACE_UP:
	case HALFKEY_MIRROR_DOWN:
	case HALFKEY_MIRROR_UP:
		/* Mirrored keys are passed through in idle state */
	case HALFKEY_OTHERKEY:
		/* Allow evdev to handle this key event */
		action = HALFKEY_PASSTHROUGH;
		break;
	}

	return action;
}

static enum evdev_halfkey_action
evdev_halfkey_spacepressed_handle_event(struct evdev_device *device,
					uint64_t time,
					enum evdev_halfkey_event event)
{
	enum evdev_halfkey_action action = HALFKEY_PASSTHROUGH;

	switch(event) {
	case HALFKEY_SPACE_DOWN:
		halfkey_state_error(device, event);
		action = HALFKEY_DISCARD;
		break;
	case HALFKEY_OTHERKEY:
	case HALFKEY_SPACE_UP:
		device->halfkey.state = HALFKEY_SPACE_IDLE;
		/* Allow evdev to inject the SPACE_UP or Other key event */
		action = HALFKEY_PASSTHROUGH;
		break;
	case HALFKEY_MIRROR_DOWN:
		/* Inject a mirrored key.
		 * Space can no longer insert a space
		 */
		device->halfkey.state = HALFKEY_SPACE_MODIFIED;
		action = HALFKEY_INJECTMIRROR;
		break;
	case HALFKEY_MIRROR_UP:
		/* If we have a MIRROR_UP in 'SPACE PRESSED' then the key was
		 * pressed before the space key. Therefore we just let this
		 * one passthrough without changing state
		 */
		action = HALFKEY_PASSTHROUGH;
		break;
	}

	return action;
}

static enum evdev_halfkey_action
evdev_halfkey_modified_handle_event(struct evdev_device *device,
				    uint64_t time,
				    enum evdev_halfkey_event event)
{
	enum evdev_halfkey_action action = HALFKEY_PASSTHROUGH;

	switch(event) {
	case HALFKEY_SPACE_DOWN:
		/* Shouldn't occur, so we simply discard */
		halfkey_state_error(device, event);
		action = HALFKEY_DISCARD;
		break;
	case HALFKEY_SPACE_UP:
		/* Consume this space up event.
		   We have completed a mirrored sequence */
		device->halfkey.state = HALFKEY_SPACE_IDLE;
		action = HALFKEY_DISCARD;
		break;
	case HALFKEY_MIRROR_DOWN:
	case HALFKEY_MIRROR_UP:
		/* Inject a mirrored key */
		action = HALFKEY_INJECTMIRROR;
		break;
	case HALFKEY_OTHERKEY:
		action = HALFKEY_PASSTHROUGH;
		break;
	}

	return action;
}

static void
evdev_halfkey_handle_state_change(struct evdev_device *device)
{
	enum evdev_halfkey_state new_state = device->halfkey.state;
	switch (new_state) {
	case HALFKEY_SPACE_IDLE:
		/* Dispatch any queued modifier down actions */
		if (device->halfkey.modifier_queued) {
			uint64_t time = device->halfkey.modifier_queued;

			device->halfkey.modifier_queued = 0;

			evdev_keyboard_notify_key(device,
						  time,
						  KEY_SPACE,
						  1);
		}
		break;
	case HALFKEY_SPACE_PRESSED:
		break;
	case HALFKEY_SPACE_MODIFIED:
		/* Discard any queued modifier down */
		device->halfkey.modifier_queued = 0;
		break;
	}
}

static enum evdev_halfkey_action
evdev_halfkey_handle_event(struct evdev_device *device,
			   uint64_t time,
			   enum evdev_halfkey_event event)
{
	enum evdev_halfkey_action action = HALFKEY_PASSTHROUGH;
	enum evdev_halfkey_state current;

	current = device->halfkey.state;

	switch (current) {
	case HALFKEY_SPACE_IDLE:
		action = evdev_halfkey_idle_handle_event(device, time, event);
		break;
	case HALFKEY_SPACE_PRESSED:
		action = evdev_halfkey_spacepressed_handle_event(device, time, event);
		break;
	case HALFKEY_SPACE_MODIFIED:
		action = evdev_halfkey_modified_handle_event(device, time, event);
		break;
	}

	if (device->halfkey.state != current)
		evdev_halfkey_handle_state_change(device);

	log_debug(device->base.seat->libinput,
		  "halfkeystate: %s → %s → %s, action %s\n",
		  halfkey_state_to_str(current),
		  halfkey_event_to_str(event),
		  halfkey_state_to_str(device->halfkey.state),
		  halfkey_action_to_str(action));

	return action;
}


enum evdev_halfkey_action
evdev_halfkey_filter_key(struct evdev_device *device,
			 uint64_t time,
			 int keycode,
			 enum libinput_key_state state)
{
	enum evdev_halfkey_action action = HALFKEY_PASSTHROUGH;
	enum evdev_halfkey_event event;
	bool is_press = state == LIBINPUT_KEY_STATE_PRESSED;

	int mirrored_key;
	bool mirrored;

	/* KPENTER is FN and ENTER key */
	if (keycode == KEY_KPENTER) {
		if (is_press) {

			device->halfkey.enabled = !device->halfkey.enabled;
			log_bug_libinput(device->base.seat->libinput,
				 "Key %s Pressed. Halfkey now %s\n",
				 libevdev_event_code_get_name(EV_KEY, keycode),
				 device->halfkey.enabled ? "Enabled" :
				 			   "Disabled");
		}

		/* Reinitialise State Machine */
		device->halfkey.state = HALFKEY_SPACE_IDLE;

		/* Swallow both up and down for this key */
		return HALFKEY_DISCARD;
	}

	/* Don't do any more work than necessary if we are disabled */
	if (!device->halfkey.enabled)
		return HALFKEY_PASSTHROUGH;

	mirrored_key = halfkey_mirror_key(keycode);
	mirrored = (keycode != mirrored_key);

	/* Decide what event occured */
	if(keycode == KEY_SPACE) {
		if (is_press)
			event = HALFKEY_SPACE_DOWN;
		else
			event = HALFKEY_SPACE_UP;
	} else {
		if (mirrored) {
			if (is_press)
				event = HALFKEY_MIRROR_DOWN;
			else
				event = HALFKEY_MIRROR_UP;
		}
		else
			event = HALFKEY_OTHERKEY;
	}

	action = evdev_halfkey_handle_event(device, time, event);

	/*
	 * If key pressed was a mirror, but then the space is released,
	 * the system will try to 'up' a key which is already up.
	 * We catch this here, and 'up' the mirror which we have put down.
	 */
	if (!is_press &&
	    action != HALFKEY_INJECTMIRROR &&
	    halfkey_is_key_down(device, keycode) == LIBINPUT_KEY_STATE_RELEASED &&
	    halfkey_is_key_down(device, mirrored_key) == LIBINPUT_KEY_STATE_PRESSED) {
		log_bug_libinput(device->base.seat->libinput,
			"State Inversion. %s is already %s. "
			"Using %s instead\n",
			libevdev_event_code_get_name(EV_KEY, keycode),
			is_press ? "pressed" : "released",
			libevdev_event_code_get_name(EV_KEY, mirrored_key));

		/* Inject the mirrored release code instead */
		action = HALFKEY_INJECTMIRROR;
	}

	log_bug_libinput(device->base.seat->libinput,
			 "Key %s %s. would be mirrored as %s\n",
				is_press ? "Pressed" : "Released",
				libevdev_event_code_get_name(EV_KEY, keycode),
				libevdev_event_code_get_name(EV_KEY, mirrored_key));

	if (action == HALFKEY_INJECTMIRROR)
	{
		halfkey_set_key_down(device, mirrored_key, is_press);
		evdev_keyboard_notify_key(device, time, mirrored_key, is_press);

		log_bug_libinput(device->base.seat->libinput,
			 "Key %s %s. Consumed by Halfkey as %s\n",
				is_press ? "Pressed" : "Released",
				libevdev_event_code_get_name(EV_KEY, keycode),
				libevdev_event_code_get_name(EV_KEY, mirrored_key));
	}

	halfkey_debug_line(device);

	/* PASSTHROUGH, and DISCARD will be handled by evdev */

	return action;
}

static inline void
evdev_halfkey_apply_config(struct evdev_device *device)
{
	if (device->halfkey.want_enabled ==
	    device->halfkey.enabled)
		return;

	for (unsigned i = 0; i < NLONGS(KEY_CNT); i++)
		if (device->halfkey.keymask[i] != 0) {
			log_bug_libinput(device->base.seat->libinput,
				"halfkey: Keymask set. Config not applied\n");
			return;
		}

	device->halfkey.enabled = device->halfkey.want_enabled;
}

static int
evdev_halfkey_is_available(struct libinput_device *device)
{
	return 1;
}

static enum libinput_config_status
evdev_halfkey_set(struct libinput_device *device,
		  enum libinput_config_halfkey_state enable)
{
	struct evdev_device *evdev = (struct evdev_device*)device;

	switch (enable) {
	case LIBINPUT_CONFIG_HALFKEY_ENABLED:
		evdev->halfkey.want_enabled = true;
		break;
	case LIBINPUT_CONFIG_HALFKEY_DISABLED:
		evdev->halfkey.want_enabled = false;
		break;
	default:
		return LIBINPUT_CONFIG_STATUS_INVALID;
	}

	evdev_halfkey_apply_config(evdev);

	return LIBINPUT_CONFIG_STATUS_SUCCESS;
}

static enum libinput_config_halfkey_state
evdev_halfkey_get(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;

	return evdev->halfkey.enabled ?
			LIBINPUT_CONFIG_HALFKEY_ENABLED :
			LIBINPUT_CONFIG_HALFKEY_DISABLED;
}

static enum libinput_config_halfkey_state
evdev_halfkey_get_default(struct libinput_device *device)
{
	struct evdev_device *evdev = (struct evdev_device*)device;

	return evdev->halfkey.enabled_default ?
			LIBINPUT_CONFIG_HALFKEY_ENABLED :
			LIBINPUT_CONFIG_HALFKEY_DISABLED;
}

void
evdev_init_halfkey(struct evdev_device *device,
		   bool enable,
		   bool want_config)
{
	memset(device->halfkey.keymask, 0, sizeof(device->halfkey.keymask));

	device->halfkey.enabled_default = enable;
	device->halfkey.want_enabled = enable;
	device->halfkey.enabled = enable;
	device->halfkey.modifier_queued = 0;

	if (!want_config)
		return;

	device->halfkey.config.available = evdev_halfkey_is_available;
	device->halfkey.config.set = evdev_halfkey_set;
	device->halfkey.config.get = evdev_halfkey_get;
	device->halfkey.config.get_default = evdev_halfkey_get_default;
	device->base.config.halfkey = &device->halfkey.config;
}
