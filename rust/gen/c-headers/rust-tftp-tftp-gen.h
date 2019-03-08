/* Copyright (C) 2017 Open Information Security Foundation
 *
 * You can copy, redistribute or modify this Program under the terms of
 * the GNU General Public License version 2 as published by the Free
 * Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
 * DO NOT EDIT. This file is automatically generated.
 */

#ifndef __RUST_TFTP_TFTP_GEN_H__
#define __RUST_TFTP_TFTP_GEN_H__

void * rs_tftp_state_alloc(void);
void rs_tftp_state_free(void * state);
void rs_tftp_state_tx_free(TFTPState * state, uint32_t tx_id);
void * rs_tftp_get_tx(TFTPState * state, uint64_t tx_id);
uint32_t rs_tftp_get_tx_logged(TFTPState * _state, TFTPTransaction * tx);
void rs_tftp_set_tx_logged(TFTPState * _state, TFTPTransaction * tx, uint32_t logged);
int64_t rs_tftp_has_event(TFTPState * state);
uint64_t rs_tftp_get_tx_cnt(TFTPState * state);
int64_t rs_tftp_request(TFTPState * state, const uint8_t * input, uint32_t len);

#endif /* ! __RUST_TFTP_TFTP_GEN_H__ */
