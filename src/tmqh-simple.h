/* Copyright (c) 2008 Victor Julien <victor@inliniac.net> */

#ifndef __TMQH_SIMPLE_H__
#define __TMQH_SIMPLE_H__

Packet *TmqhInputSimpleOnQ(PacketQueue *);
void TmqhOutputSimpleOnQ(PacketQueue *, Packet *);
void TmqhSimpleRegister (void);

#endif /* __TMQH_SIMPLE_H__ */
