/* Copyright ©2007-2010 Kris Maglione <maglione.k at Gmail>
 * See LICENSE file for license details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ixp_local.h"

int _IXP_ASSERT_VERSION;

enum {
	SByte = 1,
	SWord = 2,
	SDWord = 4,
	SQWord = 8,
};

static void
ixp_puint(IxpMsg *msg, uint size, uint32_t *val) {
	uint8_t *pos;
	int v;

	if(msg->pos + size <= msg->end) {
		pos = (uint8_t*)msg->pos;
		switch(msg->mode) {
		case MsgPack:
			v = *val;
			switch(size) {
			case SDWord:
				pos[3] = v>>24;
				pos[2] = v>>16;
			case SWord:
				pos[1] = v>>8;
			case SByte:
				pos[0] = v;
				break;
			}
		case MsgUnpack:
			v = 0;
			switch(size) {
			case SDWord:
				v |= pos[3]<<24;
				v |= pos[2]<<16;
			case SWord:
				v |= pos[1]<<8;
			case SByte:
				v |= pos[0];
				break;
			}
			*val = v;
		}
	}
	msg->pos += size;
}

/**
 * Function: ixp_pu8
 * Function: ixp_pu16
 * Function: ixp_pu32
 * Function: ixp_pu64
 *
 * These functions pack or unpack an unsigned integer of the
 * specified size.
 *
 * If P<msg>->mode is MsgPack, the value pointed to by P<val> is
 * packed into the buffer at P<msg>->pos. If P<msg>->mode is
 * MsgUnpack, the packed value at P<msg>->pos is loaded into the
 * location pointed to by P<val>. In both cases, P<msg>->pos is
 * advanced by the number of bytes read or written. If the call
 * would advance P<msg>->pos beyond P<msg>->end, P<msg>->pos is
 * advanced, but nothing is modified.
 *
 * See also:
 *	T<IxpMsg>
 */
void
ixp_pu8(IxpMsg *msg, uint8_t *val) {
	uint32_t v;

	v = *val;
	ixp_puint(msg, SByte, &v);
	*val = (uint8_t)v;
}
void
ixp_pu16(IxpMsg *msg, uint16_t *val) {
	uint32_t v;

	v = *val;
	ixp_puint(msg, SWord, &v);
	*val = (uint16_t)v;
}
void
ixp_pu32(IxpMsg *msg, uint32_t *val) {
	ixp_puint(msg, SDWord, val);
}
void
ixp_pu64(IxpMsg *msg, uint64_t *val) {
	uint32_t vl, vb;

	vl = (uint)*val;
	vb = (uint)(*val>>32);
	ixp_puint(msg, SDWord, &vl);
	ixp_puint(msg, SDWord, &vb);
	*val = vl | ((uint64_t)vb<<32);
}

/**
 * Function: ixp_pstring
 *
 * Packs or unpacks a UTF-8 encoded string. The packed
 * representation of the string consists of a 16-bit unsigned
 * integer followed by the contents of the string. The unpacked
 * representation is a nul-terminated character array.
 *
 * If P<msg>->mode is MsgPack, the string pointed to by P<s> is
 * packed into the buffer at P<msg>->pos. If P<msg>->mode is
 * MsgUnpack, the address pointed to by P<s> is loaded with a
 * malloc(3) allocated, nul-terminated representation of the
 * string packed at P<msg>->pos. In either case, P<msg>->pos is
 * advanced by the number of bytes read or written. If the
 * action would advance P<msg>->pos beyond P<msg>->end,
 * P<msg>->pos is still advanced but no other action is taken.
 *
 * See also:
 *	T<IxpMsg>, F<ixp_pstrings>, F<ixp_pdata>
 */
void
ixp_pstring(IxpMsg *msg, char **s) {
	uint16_t len;

	if(msg->mode == MsgPack)
		len = strlen(*s);
	ixp_pu16(msg, &len);

	if(msg->pos + len <= msg->end) {
		if(msg->mode == MsgUnpack) {
			*s = emalloc(len + 1);
			memcpy(*s, msg->pos, len);
			(*s)[len] = '\0';
		}else
			memcpy(msg->pos, *s, len);
	}
	msg->pos += len;
}

/**
 * Function: ixp_pstrings
 *
 * Packs or unpacks an array of UTF-8 encoded strings. The packed
 * representation consists of a 16-bit element count followed by
 * an array of strings as packed by F<ixp_pstring>. The unpacked
 * representation is an array of nul-terminated character arrays.
 *
 * If P<msg>->mode is MsgPack, P<*num> strings in the array
 * pointed to by P<strings> are packed into the buffer at
 * P<msg>->pos. If P<msg>->mode is MsgUnpack, P<*num> is loaded
 * with the number of strings unpacked, the array at
 * P<*strings> is loaded with pointers to the unpacked strings,
 * and P<(*strings)[0]> must be freed by the user. In either
 * case, P<msg>->pos is advanced by the number of bytes read or
 * written. If the action would advance P<msg>->pos beyond
 * P<msg>->end, P<msg>->pos is still advanced, but no other
 * action is taken. If P<*num> is greater than P<max>,
 * P<msg>->pos is set beyond P<msg>->end and no other action is
 * taken.
 * 
 * See also:
 *	P<IxpMsg>, P<ixp_pstring>, P<ixp_pdata>
 */
void
ixp_pstrings(IxpMsg *msg, uint16_t *num, char *strings[], uint max) {
	char *s;
	uint i, size;
	uint16_t len;

	ixp_pu16(msg, num);
	if(*num > max) {
		msg->pos = msg->end+1;
		return;
	}

	SET(s);
	if(msg->mode == MsgUnpack) {
		s = msg->pos;
		size = 0;
		for(i=0; i < *num; i++) {
			ixp_pu16(msg, &len);
			msg->pos += len;
			size += len;
			if(msg->pos > msg->end)
				return;
		}
		msg->pos = s;
		size += *num;
		s = emalloc(size);
	}

	for(i=0; i < *num; i++) {
		if(msg->mode == MsgPack)
			len = strlen(strings[i]);
		ixp_pu16(msg, &len);

		if(msg->mode == MsgUnpack) {
			memcpy(s, msg->pos, len);
			strings[i] = (char*)s;
			s += len;
			msg->pos += len;
			*s++ = '\0';
		}else
			ixp_pdata(msg, &strings[i], len);
	}
}

/**
 * Function: ixp_pdata
 *
 * Packs or unpacks a raw character buffer of size P<len>.
 *
 * If P<msg>->mode is MsgPack, buffer pointed to by P<data> is
 * packed into the buffer at P<msg>->pos. If P<msg>->mode is
 * MsgUnpack, the address pointed to by P<s> is loaded with a
 * malloc(3) allocated buffer with the contents of the buffer at
 * P<msg>->pos.  In either case, P<msg>->pos is advanced by the
 * number of bytes read or written. If the action would advance
 * P<msg>->pos beyond P<msg>->end, P<msg>->pos is still advanced
 * but no other action is taken.
 *
 * See also:
 *	T<IxpMsg>, F<ixp_pstring>
 */
void
ixp_pdata(IxpMsg *msg, char **data, uint len) {
	if(msg->pos + len <= msg->end) {
		if(msg->mode == MsgUnpack) {
			*data = emalloc(len);
			memcpy(*data, msg->pos, len);
		}else
			memcpy(msg->pos, *data, len);
		}
	msg->pos += len;
}

/**
 * Function: ixp_pfcall
 * Function: ixp_pqid
 * Function: ixp_pqids
 * Function: ixp_pstat
 * Function: ixp_sizeof_stat
 *
 * These convenience functions pack or unpack the contents of
 * libixp structures into their wire format. They behave as if
 * F<ixp_pu8>, F<ixp_pu16>, F<ixp_pu32>, F<ixp_pu64>, and
 * F<ixp_pstring> were called for each member of the structure
 * in question. ixp_pqid is to ixp_pqid as F<ixp_pstrings> is to
 * ixp_pstring.
 *
 * ixp_sizeof_stat returns the size of the packed represention
 * of P<stat>.
 *
 * See also:
 *	T<IxpMsg>, F<ixp_pu8>, F<ixp_pu16>, F<ixp_pu32>,
 *	F<ixp_pu64>, F<ixp_pstring>, F<ixp_pstrings>
 */
void
ixp_pqid(IxpMsg *msg, IxpQid *qid) {
	ixp_pu8(msg, &qid->type);
	ixp_pu32(msg, &qid->version);
	ixp_pu64(msg, &qid->path);
}

void
ixp_pqids(IxpMsg *msg, uint16_t *num, IxpQid qid[], uint max) {
	int i;

	ixp_pu16(msg, num);
	if(*num > max) {
		msg->pos = msg->end+1;
		return;
	}

	for(i = 0; i < *num; i++)
		ixp_pqid(msg, &qid[i]);
}

void
ixp_pstat(IxpMsg *msg, IxpStat *stat) {
	uint16_t size;

	if(msg->mode == MsgPack)
		size = ixp_sizeof_stat(stat) - 2;

	ixp_pu16(msg, &size);
	ixp_pu16(msg, &stat->type);
	ixp_pu32(msg, &stat->dev);
	ixp_pqid(msg, &stat->qid);
	ixp_pu32(msg, &stat->mode);
	ixp_pu32(msg, &stat->atime);
	ixp_pu32(msg, &stat->mtime);
	ixp_pu64(msg, &stat->length);
	ixp_pstring(msg, &stat->name);
	ixp_pstring(msg, &stat->uid);
	ixp_pstring(msg, &stat->gid);
	ixp_pstring(msg, &stat->muid);
}
