/*
 * Copyright (c) 2010-2022 Belledonne Communications SARL.
 *
 * This file is part of oRTP 
 * (see https://gitlab.linphone.org/BC/public/ortp).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ortp_tester.h"
#include <ortp/ortp.h>

static int tester_before_all(void) {
	ortp_init();
	ortp_scheduler_init();

	return 0;
}

static int tester_after_all(void) {
	ortp_exit();

	return 0;
}

static void send_packets_through_tranfer_session(void) {
	RtpSession *session;
	RtpSession *transfer_session;
	int rtp_port, rtcp_port;
	FILE *infile;
	unsigned char buffer[160];
	uint32_t user_ts = 0;
	size_t len = 0;
	bool_t error = FALSE;

	char *filepath = bc_tester_res("raw/h265-iframe");

	infile = fopen(filepath, "rb");

	if (!BC_ASSERT_PTR_NOT_NULL(infile)) {
		if (filepath) bctbx_free(filepath);
		return;
	}

	// Create the default session
	session = rtp_session_new(RTP_SESSION_SENDRECV);

	rtp_session_set_scheduling_mode(session, 1);
	rtp_session_set_blocking_mode(session, 1);
	rtp_session_set_connected_mode(session, TRUE);
	rtp_session_set_local_addr(session, "127.0.0.1", -1, -1);
	rtp_session_set_payload_type(session, 0);
	rtp_session_enable_jitter_buffer(session, FALSE);

	// Create the session that will be used to transfer the packets
	transfer_session = rtp_session_new(RTP_SESSION_SENDRECV);

	rtp_session_set_scheduling_mode(transfer_session, 1);
	rtp_session_set_blocking_mode(transfer_session, 1);
	rtp_session_set_connected_mode(transfer_session, TRUE);
	rtp_session_set_local_addr(transfer_session, "127.0.0.1", -1, -1);
	rtp_session_enable_transfer_mode(transfer_session, TRUE);

	// Connect the two sessions
	rtp_port = rtp_session_get_local_port(transfer_session);
	rtcp_port = rtp_session_get_local_rtcp_port(transfer_session);
	rtp_session_set_remote_addr_full(session, "127.0.0.1", rtp_port, "127.0.0.1", rtcp_port);

	rtp_port = rtp_session_get_local_port(session);
	rtcp_port = rtp_session_get_local_rtcp_port(session);
	rtp_session_set_remote_addr_full(transfer_session, "127.0.0.1", rtp_port, "127.0.0.1", rtcp_port);

	while(((len = fread(buffer, 1, 160, infile)) > 0) && !error) {
		mblk_t *transfered_packet;
		mblk_t *received_packet;

		// Send a packet through the "normal" session and retrieve it with the transfer session
		mblk_t *sent_packet = rtp_session_create_packet(session, RTP_FIXED_HEADER_SIZE, (uint8_t *)buffer, len);

		int size = rtp_session_sendm_with_ts(session, copymsg(sent_packet), user_ts);
		BC_ASSERT_GREATER(size, 0, int, "%d");

		transfered_packet = rtp_session_recvm_with_ts(transfer_session, user_ts);
		if (!BC_ASSERT_PTR_NOT_NULL(transfered_packet)) {
			error = TRUE;
		} else {
			// We cannot compare bytes by bytes here as sent_packet has been modified by rtp_session_sendm_with_ts before sending
			// So we check the parts that this function didn't change which is everything but timestamp
			BC_ASSERT_EQUAL(rtp_get_version(transfered_packet), rtp_get_version(sent_packet), uint16_t, "%hu");
			BC_ASSERT_EQUAL(rtp_get_padbit(transfered_packet), rtp_get_padbit(sent_packet), uint16_t, "%hu");
			BC_ASSERT_EQUAL(rtp_get_markbit(transfered_packet), rtp_get_markbit(sent_packet), uint16_t, "%hu");
			BC_ASSERT_EQUAL(rtp_get_extbit(transfered_packet), rtp_get_extbit(sent_packet), uint16_t, "%hu");
			BC_ASSERT_TRUE(rtp_get_seqnumber(transfered_packet) == rtp_get_seqnumber(sent_packet)); // BC_ASSERT_EQUAL here doesn't want to compile on some platforms
			BC_ASSERT_EQUAL(rtp_get_payload_type(transfered_packet), rtp_get_payload_type(sent_packet), uint16_t, "%hu");
			BC_ASSERT_TRUE(rtp_get_ssrc(transfered_packet) == rtp_get_ssrc(sent_packet)); // Same here
			BC_ASSERT_EQUAL(rtp_get_cc(transfered_packet), rtp_get_cc(sent_packet), uint16_t, "%hu");
			BC_ASSERT_EQUAL(memcmp(transfered_packet->b_rptr + RTP_FIXED_HEADER_SIZE, sent_packet->b_rptr + RTP_FIXED_HEADER_SIZE, msgdsize(transfered_packet) - RTP_FIXED_HEADER_SIZE), 0, int, "%d");

			// Send it again via the transfer session and retrieve it with the "normal" session
			size = rtp_session_sendm_with_ts(transfer_session, copymsg(transfered_packet), user_ts);
			BC_ASSERT_GREATER(size, 0, int, "%d");

			received_packet = rtp_session_recvm_with_ts(session, user_ts);
			if (!BC_ASSERT_PTR_NOT_NULL(received_packet)) {
				error = TRUE;
			} else {
				// Check that the packet received is the same as the transfered one as the "transfer" session shouldn't modify it's content
				BC_ASSERT_EQUAL(memcmp(received_packet->b_rptr, transfered_packet->b_rptr, msgdsize(received_packet)), 0, int, "%d");

				freemsg(received_packet);
			}

			freemsg(transfered_packet);
		}

		freemsg(sent_packet);

		user_ts += 160;
	}

	fclose(infile);

	if (filepath) bctbx_free(filepath);
	rtp_session_destroy(session);
	rtp_session_destroy(transfer_session);
}

static test_t tests[] = {
	TEST_NO_TAG("Send packets through a transfer session", send_packets_through_tranfer_session)
};

test_suite_t rtp_test_suite = {
	"Rtp",							  // Name of test suite
	tester_before_all,				  // Before all callback
	tester_after_all,				  // After all callback
	NULL,							  // Before each callback
	NULL,							  // After each callback
	sizeof(tests) / sizeof(tests[0]), // Size of test table
	tests							  // Table of test suite
};