#include "frame.h"
#include <iostream>

namespace
{
union u64
{
	uint64_t integer;
	unsigned char data[8];
};
union u32
{
	uint32_t integer;
	unsigned char data[4];
};
union u16
{
	uint16_t integer;
	unsigned char data[2];
};

inline uint64_t ntohll(uint64_t in)
{
	return be64toh(in);
}
inline uint64_t htonll(uint64_t in)
{
	return htobe64(in);
}

}

namespace WS
{

uint64_t Frame::parse(buffer_iterator begin, buffer_iterator end)
{
	buffer_iterator i = begin;
	uint64_t counter = 0;

	if(i == end)return 0;

	uint8_t firstbyte = *i;

	fin  = firstbyte & (1<<7);
	rsv1 = firstbyte & (1<<6);
	rsv2 = firstbyte & (1<<5);
	rsv3 = firstbyte & (1<<4);

	opcode = firstbyte & 0xF;

	if(++i == end)return 0;

	uint64_t length = *i;

	have_mask = length & 128;

	length &= 127;

	if(length == 126)
	{
		// 16-bit length follows

		u16 len;
		if(++i == end)return 0;
		len.data[0] = *i;
		if(++i == end)return 0;
		len.data[1] = *i;

		length = ntohs(len.integer);
		counter = 4 + length;
	}
	else if(length == 127)
	{
		// 64-bit length follows
		// make sure there's at least 8 bytes left
		u64 len;
		for(int a = 0; a < 8; a++)
		{
			if(++i == end)return 0;
			len.data[a] = *i;
		}
		length = ntohll(len.integer);

		counter = 10 + length;
	}
	else
	{
		counter = 2 + length;
	}

	payload_length = length;


	if(have_mask)
	{
		for(int a = 0; a < 4; a++)
		{
			if(++i == end)return 0;
			masking_key[a] = *i;
		}
		counter += 4;
	}
	// Check that there's at least length bytes left:
	buffer_iterator body_begin = i;
	for(uint64_t a = 0; a < length; a++)
	{
		if(++body_begin == end)return 0;
	}

	if(!payload_data)
		payload_data = std::make_shared<std::vector<char> >(payload_length);
	else
		payload_data->resize(payload_length);

	for(uint64_t a = 0; a < length; a++)
	{
		(*payload_data)[a] = *++i;
		if(have_mask)
			(*payload_data)[a] ^= masking_key[a % 4];
	}

	return counter;
}
MessagePtr Frame::write()
{
	MessagePtr msg = std::make_shared<std::vector <char> >();
	msg->reserve(10);

	unsigned char firstbyte = 0;
	firstbyte |= fin << 7;
	firstbyte |= rsv1 << 6;
	firstbyte |= rsv2 << 5;
	firstbyte |= rsv3 << 4;
	firstbyte |= opcode & 0xF;

	msg->push_back(firstbyte);

	// host never masks..
	payload_length = payload_data->size();
	if(payload_length < 126)
	{
		msg->push_back(uint8_t(payload_length));
	}
	else if(payload_length <= 0xFFFF)
	{
		msg->push_back(uint8_t(126));
		u16 conv;
		conv.integer = htons(uint16_t(payload_length));
		msg->push_back(conv.data[0]);
		msg->push_back(conv.data[1]);
	}
	else
	{
		msg->push_back(uint8_t(127));
		u64 conv;
		conv.integer = htonll(payload_length);
		for(int i = 0; i < 8; i++)
			msg->push_back(conv.data[i]);
	}
	return msg;
}
void Frame::print()
{
	using std::cout;
	using std::endl;

	cout << "== BEGIN FRAME ==" << endl;
	cout << "fin 	| " << fin << endl;
	cout << "rsv1	| " << rsv1 << endl;
	cout << "rsv2	| " << rsv2 << endl;
	cout << "rsv3	| " << rsv3 << endl;
	cout << "opcode	| " << int(opcode) << endl;
	cout << "masked	| " << have_mask << endl;
	cout << "length	| " << payload_length << endl;
	if(have_mask) {
		cout << "mask	| " << int(masking_key[0]) << ", "
		<< int(masking_key[1]) << ", "
		<< int(masking_key[2]) << ", "
		<< int(masking_key[3]) << endl;
	}
	cout << "== END FRAME ==" << endl;
}

}
