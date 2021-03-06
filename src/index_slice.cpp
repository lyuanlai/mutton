/*
  Copyright (c) 2013 Matthew Stump

  This file is part of libmutton.

  libmutton is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  libmutton is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <stdint.h>

#include "encode.hpp"
#include "index_reader_writer.hpp"

#include "index_slice.hpp"

inline void
get_address(
    mtn_index_address_t    position,
    mtn_index_address_t*   segment,
    mtn_index_partition_t* segment_index,
    mtn_index_partition_t* bit_offset)
{
    *segment = position >> 11; // div by 2048
    mtn_index_partition_t segment_offset = position & 0x7FF; // mod by 2048
    *segment_index = segment_offset >> 6; // div by 64
    *bit_offset = segment_offset & 0x3F; // mod by 64
}

inline void
set_bit(
    mtn::index_segment_ptr input,
    mtn_index_partition_t  bucket_index,
    mtn_index_partition_t  bit_offset,
    bool                   val)
{
    if (val) {
        input[bucket_index] |= 1ULL << bit_offset;
    }
    else {
        input[bucket_index] &= 0ULL << bit_offset;
    }
}

inline void
segment_union(
    mtn::index_segment_ptr a,
    mtn::index_segment_ptr b,
    mtn::index_segment_ptr o)
{
    for (int i = 0; i < MTN_INDEX_SEGMENT_LENGTH; ++i) {
        o[i] = a[i] | b[i];
    }
}

inline void
segment_intersection(
    mtn::index_segment_ptr a,
    mtn::index_segment_ptr b,
    mtn::index_segment_ptr o)
{
    for (int i = 0; i < MTN_INDEX_SEGMENT_LENGTH; ++i) {
        o[i] = a[i] & b[i];
    }
}

inline void
segment_invert(
    mtn::index_segment_ptr a,
    mtn::index_segment_ptr o)
{
    for (int i = 0; i < MTN_INDEX_SEGMENT_LENGTH; ++i) {
        o[i] = ~a[i];
    }
}

inline mtn::index_slice_t::iterator
find_insertion_point(
    mtn::index_slice_t::iterator begin,
    mtn::index_slice_t::iterator end,
    mtn_index_address_t          bucket)
{
    return std::find_if(begin,
                        end,
                        boost::bind(&mtn::index_slice_t::index_node_t::offset, _1 ) >= bucket);
}

inline mtn::index_slice_t::iterator
get_union_output_node(
    mtn::index_slice_t&          output,
    mtn::index_slice_t::iterator output_iter,
    mtn_index_address_t          offset)
{
    output_iter = find_insertion_point(output_iter, output.end(), offset);
    if (output_iter == output.end() || output_iter->offset != offset) {
        return output.insert(output_iter, new mtn::index_slice_t::index_node_t(offset));
    }
    return output_iter;
}

inline mtn::index_slice_t::iterator
get_intersection_output_node(
    mtn::index_slice_t&          output,
    mtn::index_slice_t::iterator output_iter,
    mtn_index_address_t          offset)
{
    for (;;) {
        if (output_iter == output.end() || output_iter->offset > offset) {
            return output.insert(output_iter, new mtn::index_slice_t::index_node_t(offset));
        }
        else if (output_iter->offset == offset) {
            return output_iter;
        }
        else if (output_iter->offset < offset) {
            output_iter = output.erase(output_iter);
        }
        ++output_iter;
    }
}

inline mtn::status_t
union_behavior(
    mtn::index_slice_t& a_index,
    mtn::index_slice_t& b_index,
    mtn::index_slice_t& output)
{

    mtn::index_slice_t::iterator a_iter = a_index.begin();
    mtn::index_slice_t::iterator b_iter = b_index.begin();
    mtn::index_slice_t::iterator output_iter = output.begin();

    for (;;) {
        if (a_iter == a_index.end() && b_iter == b_index.end()) {
            break;
        }
        else if (a_iter == a_index.end()) {
            if (&output == &b_index) {
                break;
            }
            output_iter = get_union_output_node(output, output_iter, b_iter->offset);
            memcpy(output_iter->segment, b_iter->segment, MTN_INDEX_SEGMENT_SIZE);
            ++b_iter;
        }
        else if (b_iter == b_index.end()) {
            if (&output == &a_index) {
                break;
            }
            output_iter = get_union_output_node(output, output_iter, a_iter->offset);
            memcpy(output_iter->segment, a_iter->segment, MTN_INDEX_SEGMENT_SIZE);
            ++a_iter;
        }
        else if (a_iter->offset < b_iter->offset) {
            output_iter = get_union_output_node(output, output_iter, a_iter->offset);
            memcpy(output_iter->segment, a_iter->segment, MTN_INDEX_SEGMENT_SIZE);
            ++a_iter;
        }
        else if (a_iter->offset > b_iter->offset) {
            output_iter = get_union_output_node(output, output_iter, b_iter->offset);
            memcpy(output_iter->segment, b_iter->segment, MTN_INDEX_SEGMENT_SIZE);
            ++b_iter;
        }
        else if (a_iter->offset == b_iter->offset) {
            output_iter = get_union_output_node(output, output_iter, b_iter->offset);
            segment_union(a_iter->segment, b_iter->segment, output_iter->segment);
            ++a_iter;
            ++b_iter;
        }
        else {
            std::stringstream message;
            message << "shit's gone crazy in index union: "
                    << a_iter->offset
                    << ":" << b_iter->offset
                    << ":" << output_iter->offset;

            return mtn::status_t(MTN_ERROR_INDEX_OPERATION, message.str());
        }
    }
    return mtn::status_t();
}

inline mtn::status_t
intersection_behavior(
    mtn::index_slice_t& a_index,
    mtn::index_slice_t& b_index,
    mtn::index_slice_t& output)
{
    mtn::index_slice_t::iterator a_iter = a_index.begin();
    mtn::index_slice_t::iterator b_iter = b_index.begin();
    mtn::index_slice_t::iterator output_iter = output.begin();

    for (;;) {
        if (b_iter == b_index.end() || a_iter == a_index.end()) {
            output.erase(output_iter, output.end());
            break;
        }
        else if (a_iter->offset < b_iter->offset) {
            ++a_iter;
        }
        else if (a_iter->offset > b_iter->offset) {
            ++b_iter;
        }
        else if (a_iter->offset == b_iter->offset) {
            output_iter = get_intersection_output_node(output, output_iter, b_iter->offset);
            segment_intersection(a_iter->segment, b_iter->segment, output_iter->segment);
            ++a_iter;
            ++b_iter;
            ++output_iter;
        }
        else {
            std::stringstream message;
            message << "shit's gone crazy in index intersection: "
                    << a_iter->offset
                    << ":" << b_iter->offset
                    << ":" << output_iter->offset;
        }
    }
    return mtn::status_t();
}

mtn::index_slice_t::index_node_t::index_node_t(
    const index_node_t& node) :
    offset(node.offset)
{
    memcpy(segment, node.segment, MTN_INDEX_SEGMENT_SIZE);
}

mtn::index_slice_t::index_node_t::index_node_t(
    mtn_index_address_t offset) :
    offset(offset)
{}

mtn::index_slice_t::index_node_t::index_node_t(
    mtn_index_address_t     offset,
    const index_segment_ptr data) :
    offset(offset)
{
    memcpy(segment, data, MTN_INDEX_SEGMENT_SIZE);
}

void
mtn::index_slice_t::index_node_t::zero()
{
    memset(segment, 0, MTN_INDEX_SEGMENT_SIZE);
}

mtn::index_slice_t::index_slice_t() :
    _partition(0),
    _value(0)
{}

mtn::index_slice_t::index_slice_t(
    mtn_index_partition_t           partition,
    const std::vector<mtn::byte_t>& bucket,
    const std::vector<mtn::byte_t>& field,
    mtn_index_address_t             value) :
    _partition(partition),
    _bucket(bucket),
    _field(field),
    _value(value)
{}

mtn::index_slice_t::index_slice_t(
    mtn_index_partition_t partition,
    const mtn::byte_t*    bucket,
    size_t                bucket_size,
    const mtn::byte_t*    field,
    size_t                field_size,
    mtn_index_address_t   value) :
    _partition(partition),
    _bucket(bucket, bucket + bucket_size),
    _field(field, field + field_size),
    _value(value)
{}

mtn::index_slice_t::index_slice_t(
    const mtn::index_slice_t::index_slice_t& other)  :
    _partition(other.partition()),
    _field(other.field()),
    _value(other.value())
{
    for (mtn::index_slice_t::const_iterator iter = other.cbegin(); iter != other.cend(); ++iter) {
        _index_slice.insert(_index_slice.end(), new mtn::index_slice_t::index_node_t(*iter));
    }
}

void
mtn::index_slice_t::invert()
{
    for (mtn::index_slice_t::iterator iter = begin(); iter != end(); ++iter) {
        segment_invert(iter->segment, iter->segment);
    }
}

mtn::status_t
mtn::index_slice_t::execute(
    index_operation_enum operation,
    mtn::index_slice_t&  a_index,
    mtn::index_slice_t&  b_index,
    mtn::index_slice_t&  output)
{
    if (operation == MTN_INDEX_OP_INTERSECTION) {
        return intersection_behavior(a_index, b_index, output);
    }
    else if (operation == MTN_INDEX_OP_UNION) {
        return union_behavior(a_index, b_index, output);
    }
    return mtn::status_t(MTN_ERROR_INDEX_OPERATION, "unkown/unsupported index operation");
}

mtn::status_t
mtn::index_slice_t::bit(
    mtn::index_reader_writer_t& rw,
    mtn_index_address_t         bit,
    bool                        state)
{
    mtn_index_address_t   segment       = 0;
    mtn_index_partition_t segment_index = 0;
    mtn_index_partition_t bit_offset    = 0;
    get_address(bit, &segment, &segment_index, &bit_offset);

    mtn::index_slice_t::iterator it = find_insertion_point(begin(), end(), segment);

    mtn::status_t status;
    if (it == end() || it->offset != segment) {
        it = mtn::index_slice_t::iterator(_index_slice.insert(it.base(), new index_node_t(segment)));
        status = rw.read_segment(_partition, _bucket, _field, _value, segment, it->segment);
    }

    if (status) {
        set_bit(it->segment, segment_index, bit_offset, state);
        status = rw.write_segment(_partition, _bucket, _field, _value, segment, it->segment);
    }
    return status;
}

bool
mtn::index_slice_t::bit(
    mtn_index_address_t bit)
{
    mtn_index_address_t   segment       = 0;
    mtn_index_partition_t segment_index = 0;
    mtn_index_partition_t bit_offset    = 0;
    get_address(bit, &segment, &segment_index, &bit_offset);

    mtn::index_slice_t::iterator it = find_insertion_point(begin(), end(), segment);

    if (it == end() || it->offset != segment) {
        return false;
    }
    return (it->segment[segment_index] & 1ULL << bit_offset);
}

mtn::index_slice_t&
mtn::index_slice_t::operator=(
    const index_slice_t& other)
{
    _field = other.field();
    _partition = other.partition();
    _value = other.value();

    for (mtn::index_slice_t::const_iterator iter = other.cbegin(); iter != other.cend(); ++iter) {
        _index_slice.insert(_index_slice.end(), new mtn::index_slice_t::index_node_t(*iter));
    }
    return *this;
}
