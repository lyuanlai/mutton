/*
  Copyright (c) 2013 Matthew Stump

  This file is part of libmtn.

  libmtn is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  libmtn is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __X_INDEX_READER_HPP_INCLUDED__
#define __X_INDEX_READER_HPP_INCLUDED__

#include "base_types.hpp"
#include "status.hpp"

namespace mtn {

    class index_t;
    class index_slice_t;

    class index_reader_t
    {

    public:

        virtual
        ~index_reader_t()
        {}

        virtual mtn::status_t
        read_index(mtn::index_partition_t partition,
                   const mtn::byte_t*     field,
                   size_t                 field_size,
                   mtn::index_t*          output) = 0;

        virtual mtn::status_t
        read_index_slice(mtn::index_partition_t partition,
                         const mtn::byte_t*     field,
                         size_t                 field_size,
                         mtn::index_address_t   value,
                         mtn::index_slice_t*    output) = 0;

        virtual mtn::status_t
        read_segment(mtn::index_partition_t partition,
                     const byte_t*          field,
                     size_t                 field_size,
                     mtn::index_address_t   value,
                     mtn::index_address_t   offset,
                     mtn::index_segment_ptr output) = 0;

        virtual mtn::status_t
        estimateSize(mtn::index_partition_t partition,
                     const byte_t*          field,
                     size_t                 field_size,
                     mtn::index_address_t   value,
                     uint64_t*              output) = 0;
    };

} // namespace mtn

#endif // __X_INDEX_READER_HPP_INCLUDED__
