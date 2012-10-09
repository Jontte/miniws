#ifndef FRAME_H_INCLUDED_
#define FRAME_H_INCLUDED_

#include <memory>
#include <boost/asio.hpp>

namespace WS
{
typedef boost::asio::buffers_iterator<boost::asio::streambuf::const_buffers_type> buffer_iterator;
typedef std::shared_ptr< std::vector<char> > MessagePtr;

/*
 * This class represents the header of a frame as described in the RFC.
 */
struct Frame
{
	bool fin;
	bool rsv1;
	bool rsv2;
	bool rsv3;
	uint8_t opcode; // 4-bits
	bool have_mask;
	uint64_t payload_length;
	uint8_t masking_key[4];
	MessagePtr payload_data;
	
	bool is_control_frame() { return opcode & 0x8; }
	
	void print();

	// Return amount of bytes consumed or 0 on failure
	uint64_t parse(buffer_iterator begin, buffer_iterator end);
	
	// Write header out and return it (does not include payload_data!)
	MessagePtr write();
};
}

#endif
