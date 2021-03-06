// Copyright 2014 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "syzygy/agent/asan/block_utils.h"

#include "syzygy/agent/asan/page_protection_helpers.h"

namespace agent {
namespace asan {

bool IsBlockCorrupt(const uint8* block_header, BlockInfo* block_info) {
  // If no output structure is provided then use a local one.
  BlockInfo local_block_info = {};
  if (block_info == NULL) {
    block_info = &local_block_info;
  } else {
    ::memset(block_info, 0, sizeof(BlockInfo));
  }

  if (!GetBlockInfo(block_header, block_info) ||
      block_info->header->magic != kBlockHeaderMagic ||
      !BlockChecksumIsValid(*block_info)) {
    return true;
  }
  return false;
}

}  // namespace asan
}  // namespace agent
