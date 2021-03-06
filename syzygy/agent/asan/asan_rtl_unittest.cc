// Copyright 2012 Google Inc. All Rights Reserved.
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

#include <windows.h>

#include "gtest/gtest.h"
#include "syzygy/agent/asan/asan_rtl_impl.h"
#include "syzygy/agent/asan/asan_runtime.h"
#include "syzygy/agent/asan/heap_checker.h"
#include "syzygy/agent/asan/unittest_util.h"

namespace agent {
namespace asan {

namespace {

using testing::AsanBlockInfoVector;
using testing::MemoryAccessorTester;
using testing::ScopedAsanAlloc;

// An arbitrary size for the buffer we allocate in the different unittests.
const size_t kAllocSize = 13;

class AsanRtlTest : public testing::TestAsanRtl {
 public:
  AsanRtlTest() : memory_src_(NULL), memory_dst_(NULL), memory_length_(0),
      memory_size_(0) { }

  void SetUp() OVERRIDE {
    testing::TestAsanRtl::SetUp();

    // Setup the callback to detect invalid accesses.
    SetCallBackFunction(&MemoryAccessorTester::AsanErrorCallback);
  }
 protected:
  void AllocMemoryBuffers(int32 length, int32 element_size);
  void FreeMemoryBuffers();

  // Memory buffers used to test special instructions.
  void* memory_src_;
  void* memory_dst_;
  int32 memory_length_;
  int32 memory_size_;
};

void AsanRtlTest::AllocMemoryBuffers(int32 length, int32 element_size) {
  ASSERT_EQ(reinterpret_cast<void*>(NULL), memory_src_);
  ASSERT_EQ(reinterpret_cast<void*>(NULL), memory_dst_);
  ASSERT_EQ(0, memory_length_);
  ASSERT_EQ(0, memory_size_);

  // Keep track of memory size.
  memory_length_ = length;
  memory_size_ = length * element_size;

  // Allocate memory space.
  memory_src_ = HeapAllocFunction(heap_, 0, memory_size_);
  ASSERT_TRUE(memory_src_ != NULL);
  memory_dst_ = HeapAllocFunction(heap_, 0, memory_size_);
  ASSERT_TRUE(memory_dst_ != NULL);

  // Initialize memory.
  ::memset(memory_src_, 0, memory_size_);
  ::memset(memory_dst_, 0, memory_size_);
}

void AsanRtlTest::FreeMemoryBuffers() {
  ASSERT_NE(reinterpret_cast<void*>(NULL), memory_src_);
  ASSERT_NE(reinterpret_cast<void*>(NULL), memory_dst_);

  ASSERT_TRUE(HeapFreeFunction(heap_, 0, memory_src_));
  ASSERT_TRUE(HeapFreeFunction(heap_, 0, memory_dst_));

  memory_length_ = 0;
  memory_size_ = 0;
  memory_src_ = NULL;
  memory_dst_ = NULL;
}

}  // namespace

TEST_F(AsanRtlTest, GetProcessHeap) {
  agent::asan::AsanRuntime* runtime = GetActiveRuntimeFunction();
  ASSERT_NE(reinterpret_cast<agent::asan::AsanRuntime*>(NULL), runtime);
  HANDLE asan_heap_handle = GetProcessHeapFunction();
  EXPECT_NE(static_cast<HANDLE>(NULL), asan_heap_handle);
  EXPECT_EQ(reinterpret_cast<HANDLE>(runtime->GetProcessHeap()),
                                     asan_heap_handle);
}

TEST_F(AsanRtlTest, AsanCheckGoodAccess) {
  FARPROC check_access_fn =
      ::GetProcAddress(asan_rtl_, "asan_check_4_byte_read_access");
  ASSERT_TRUE(check_access_fn != NULL);

  // Run through access checking an allocation that's larger than our
  // block size (8), but not a multiple thereof to exercise all paths
  // in the access check function (save for the failure path).
  ScopedAsanAlloc<uint8> mem(this, kAllocSize);
  ASSERT_TRUE(mem.get() != NULL);

  MemoryAccessorTester tester;
  for (size_t i = 0; i < kAllocSize; ++i) {
    ASSERT_NO_FATAL_FAILURE(
        tester.CheckAccessAndCompareContexts(check_access_fn, mem.get() + i));
  }
}

TEST_F(AsanRtlTest, AsanCheckHeapBufferOverflow) {
  FARPROC check_access_fn =
      ::GetProcAddress(asan_rtl_, "asan_check_4_byte_read_access");
  ASSERT_TRUE(check_access_fn != NULL);

  ScopedAsanAlloc<uint8> mem(this, kAllocSize);
  ASSERT_TRUE(mem.get() != NULL);

  MemoryAccessorTester tester;
  tester.AssertMemoryErrorIsDetected(
      check_access_fn, mem.get() + kAllocSize, HEAP_BUFFER_OVERFLOW);
  EXPECT_TRUE(LogContains("previously allocated here"));
  EXPECT_TRUE(LogContains(kHeapBufferOverFlow));
}

TEST_F(AsanRtlTest, AsanCheckHeapBufferUnderflow) {
  FARPROC check_access_fn =
      ::GetProcAddress(asan_rtl_, "asan_check_4_byte_read_access");
  ASSERT_TRUE(check_access_fn != NULL);

  ScopedAsanAlloc<uint8> mem(this, kAllocSize);
  ASSERT_TRUE(mem.get() != NULL);

  MemoryAccessorTester tester;
  tester.AssertMemoryErrorIsDetected(
      check_access_fn, mem.get() - 1, HEAP_BUFFER_UNDERFLOW);
  EXPECT_TRUE(LogContains("previously allocated here"));
  EXPECT_TRUE(LogContains(kHeapBufferUnderFlow));
}

TEST_F(AsanRtlTest, AsanCheckUseAfterFree) {
  FARPROC check_access_fn =
      ::GetProcAddress(asan_rtl_, "asan_check_4_byte_read_access");
  ASSERT_TRUE(check_access_fn != NULL);

  ScopedAsanAlloc<uint8> mem(this, kAllocSize);
  ASSERT_TRUE(mem.get() != NULL);

  uint8* mem_ptr = mem.get();
  mem.reset(NULL);

  MemoryAccessorTester tester;
  tester.AssertMemoryErrorIsDetected(check_access_fn, mem_ptr, USE_AFTER_FREE);
  EXPECT_TRUE(LogContains("previously allocated here"));
  EXPECT_TRUE(LogContains("freed here"));
  EXPECT_TRUE(LogContains(kHeapUseAfterFree));
}

TEST_F(AsanRtlTest, AsanCheckDoubleFree) {
  FARPROC check_access_fn =
      ::GetProcAddress(asan_rtl_, "asan_check_4_byte_read_access");
  ASSERT_TRUE(check_access_fn != NULL);

  uint8* mem_ptr = NULL;
  {
    ScopedAsanAlloc<uint8> mem(this, kAllocSize);
    ASSERT_TRUE(mem.get() != NULL);
    mem_ptr = mem.get();
  }

  MemoryAccessorTester tester;
  tester.set_expected_error_type(DOUBLE_FREE);
  EXPECT_FALSE(HeapFreeFunction(heap_, 0, mem_ptr));
  EXPECT_TRUE(tester.memory_error_detected());
  EXPECT_TRUE(LogContains(kAttemptingDoubleFree));
  EXPECT_TRUE(LogContains("previously allocated here"));
  EXPECT_TRUE(LogContains("freed here"));
}

TEST_F(AsanRtlTest, AsanCheckWildAccess) {
  FARPROC check_access_fn =
      ::GetProcAddress(asan_rtl_, "asan_check_4_byte_read_access");
  ASSERT_TRUE(check_access_fn != NULL);

  MemoryAccessorTester tester;
  tester.AssertMemoryErrorIsDetected(
      check_access_fn, reinterpret_cast<void*>(0x80000000), WILD_ACCESS);
  EXPECT_TRUE(LogContains(kWildAccess));
}

TEST_F(AsanRtlTest, AsanCheckInvalidAccess) {
  FARPROC check_access_fn =
      ::GetProcAddress(asan_rtl_, "asan_check_4_byte_read_access");
  ASSERT_TRUE(check_access_fn != NULL);

  MemoryAccessorTester tester;
  tester.AssertMemoryErrorIsDetected(
      check_access_fn, reinterpret_cast<void*>(0x00000000), INVALID_ADDRESS);
  EXPECT_TRUE(LogContains(kInvalidAddress));
}

TEST_F(AsanRtlTest, AsanCheckCorruptBlock) {
  void* mem = HeapAllocFunction(heap_, 0, kAllocSize);
  reinterpret_cast<uint8*>(mem)[-1]--;
  MemoryAccessorTester tester;
  tester.set_expected_error_type(CORRUPT_BLOCK);
  EXPECT_TRUE(HeapFreeFunction(heap_, 0, mem));
  EXPECT_TRUE(tester.memory_error_detected());
  EXPECT_TRUE(LogContains(kHeapCorruptBlock));
  EXPECT_TRUE(LogContains("previously allocated here"));
}

TEST_F(AsanRtlTest, AsanCheckCorruptHeap) {
  FARPROC check_access_fn =
      ::GetProcAddress(asan_rtl_, "asan_check_4_byte_read_access");
  ASSERT_TRUE(check_access_fn != NULL);

  agent::asan::AsanRuntime* runtime = GetActiveRuntimeFunction();
  ASSERT_NE(reinterpret_cast<agent::asan::AsanRuntime*>(NULL), runtime);
  runtime->params().check_heap_on_failure = true;

  ScopedAsanAlloc<uint8> mem(this, kAllocSize);
  ASSERT_TRUE(mem.get() != NULL);

  const size_t kMaxIterations = 10;

  // Retrieves the information about this block.
  BlockHeader* header = BlockGetHeaderFromBody(mem.get());
  BlockInfo block_info = {};
  EXPECT_TRUE(BlockInfoFromMemory(header, &block_info));

  // We'll update a non essential value of the block trailer to corrupt it.
  uint8* mem_in_trailer = reinterpret_cast<uint8*>(
      &block_info.trailer->alloc_tid);

  // This can fail because of a checksum collision. However, we run it a handful
  // of times to keep the chances as small as possible.
  for (size_t i = 0; i < kMaxIterations; ++i) {
    (*mem_in_trailer)++;
    MemoryAccessorTester tester;
    tester.AssertMemoryErrorIsDetected(
        check_access_fn, mem.get() + kAllocSize, HEAP_BUFFER_OVERFLOW);
    EXPECT_TRUE(LogContains("previously allocated here"));
    EXPECT_TRUE(LogContains(kHeapBufferOverFlow));

    if (!tester.last_error_info().heap_is_corrupt &&
          i + 1 < kMaxIterations)
      continue;

    EXPECT_TRUE(tester.last_error_info().heap_is_corrupt);

    EXPECT_EQ(1, tester.last_error_info().corrupt_range_count);
    EXPECT_EQ(1, tester.last_corrupt_ranges().size());
    const AsanCorruptBlockRange* corrupt_range =
        &tester.last_corrupt_ranges()[0].first;
    AsanBlockInfoVector blocks_info = tester.last_corrupt_ranges()[0].second;

    EXPECT_EQ(1, blocks_info.size());
    EXPECT_EQ(kDataIsCorrupt, blocks_info[0].analysis.block_state);
    EXPECT_EQ(kAllocSize, blocks_info[0].user_size);
    EXPECT_EQ(block_info.header, blocks_info[0].header);
    EXPECT_NE(0U, blocks_info[0].alloc_stack_size);
    for (size_t j = 0; j < blocks_info[0].alloc_stack_size; ++j)
      EXPECT_NE(reinterpret_cast<void*>(NULL), blocks_info[0].alloc_stack[j]);
    EXPECT_EQ(0U, blocks_info[0].free_stack_size);

    // An error should be triggered when we free this block.
    tester.set_memory_error_detected(false);
    tester.set_expected_error_type(CORRUPT_BLOCK);
    mem.reset(NULL);
    EXPECT_TRUE(tester.memory_error_detected());

    break;
  }
}

TEST_F(AsanRtlTest, AsanSingleSpecial1byteInstructionCheckGoodAccess) {
  static const char* function_names[] = {
      "asan_check_1_byte_movs_access",
      "asan_check_1_byte_cmps_access",
      "asan_check_1_byte_stos_access"
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint8));
  uint8* src = reinterpret_cast<uint8*>(memory_src_);
  uint8* dst = reinterpret_cast<uint8*>(memory_dst_);

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    for (int32 i = 0; i < memory_length_; ++i) {
      MemoryAccessorTester tester;
      tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        false, &dst[i], &src[i], 0xDEADDEAD,
        UNKNOWN_BAD_ACCESS);
    }
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AsanSingleSpecial2byteInstructionCheckGoodAccess) {
  static const char* function_names[] = {
      "asan_check_2_byte_movs_access",
      "asan_check_2_byte_cmps_access",
      "asan_check_2_byte_stos_access"
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint16));
  uint16* src = reinterpret_cast<uint16*>(memory_src_);
  uint16* dst = reinterpret_cast<uint16*>(memory_dst_);

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    for (int32 i = 0; i < memory_length_; ++i) {
      MemoryAccessorTester tester;
      tester.ExpectSpecialMemoryErrorIsDetected(
          check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
          false, &dst[i], &src[i], 0xDEADDEAD, UNKNOWN_BAD_ACCESS);
    }
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AsanSingleSpecial4byteInstructionCheckGoodAccess) {
  static const char* function_names[] = {
      "asan_check_4_byte_movs_access",
      "asan_check_4_byte_cmps_access",
      "asan_check_4_byte_stos_access"
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint32));
  uint32* src = reinterpret_cast<uint32*>(memory_src_);
  uint32* dst = reinterpret_cast<uint32*>(memory_dst_);

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    for (int32 i = 0; i < memory_length_; ++i) {
      MemoryAccessorTester tester;
      tester.ExpectSpecialMemoryErrorIsDetected(
          check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
          false, &dst[i], &src[i], 0xDEADDEAD, UNKNOWN_BAD_ACCESS);
    }
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AsanSingleSpecialInstructionCheckBadAccess) {
  static const char* function_names[] = {
      "asan_check_1_byte_movs_access",
      "asan_check_1_byte_cmps_access",
      "asan_check_2_byte_movs_access",
      "asan_check_2_byte_cmps_access",
      "asan_check_4_byte_movs_access",
      "asan_check_4_byte_cmps_access"
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint32));
  uint32* src = reinterpret_cast<uint32*>(memory_src_);
  uint32* dst = reinterpret_cast<uint32*>(memory_dst_);

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    MemoryAccessorTester tester;
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        true, &dst[0], &src[-1], 0xDEADDEAD, HEAP_BUFFER_UNDERFLOW);
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        true, &dst[-1], &src[0], 0xDEADDEAD, HEAP_BUFFER_UNDERFLOW);

    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        true, &dst[0], &src[memory_length_], 0xDEADDEAD, HEAP_BUFFER_OVERFLOW);
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        true, &dst[memory_length_], &src[0], 0xDEADDEAD, HEAP_BUFFER_OVERFLOW);
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AsanSingleStoInstructionCheckBadAccess) {
  static const char* function_names[] = {
      "asan_check_1_byte_stos_access",
      "asan_check_2_byte_stos_access",
      "asan_check_4_byte_stos_access"
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint32));
  uint32* src = reinterpret_cast<uint32*>(memory_src_);
  uint32* dst = reinterpret_cast<uint32*>(memory_dst_);

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    MemoryAccessorTester tester;
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        false, &dst[0], &src[-1], 0xDEAD, HEAP_BUFFER_UNDERFLOW);
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        true, &dst[-1], &src[0], 0xDEAD, HEAP_BUFFER_UNDERFLOW);

    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        false, &dst[0], &src[memory_length_], 0xDEADDEAD, HEAP_BUFFER_OVERFLOW);
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        true, &dst[memory_length_], &src[0], 0xDEADDEAD, HEAP_BUFFER_OVERFLOW);
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AsanPrefixedSpecialInstructionCheckGoodAccess) {
  static const char* function_names[] = {
      "asan_check_repz_4_byte_movs_access",
      "asan_check_repz_4_byte_cmps_access",
      "asan_check_repz_4_byte_stos_access"
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint32));
  uint32* src = reinterpret_cast<uint32*>(memory_src_);
  uint32* dst = reinterpret_cast<uint32*>(memory_dst_);

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    MemoryAccessorTester tester;
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        false, &dst[0], &src[0], memory_length_, UNKNOWN_BAD_ACCESS);
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AsanPrefixedSpecialInstructionCheckBadAccess) {
  static const char* function_names[] = {
      "asan_check_repz_4_byte_movs_access",
      "asan_check_repz_4_byte_cmps_access",
      "asan_check_repz_4_byte_stos_access"
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint32));
  uint32* src = reinterpret_cast<uint32*>(memory_src_);
  uint32* dst = reinterpret_cast<uint32*>(memory_dst_);

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    MemoryAccessorTester tester;
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        true, &dst[0], &src[0], memory_length_ + 1, HEAP_BUFFER_OVERFLOW);
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        true, &dst[-1], &src[-1], memory_length_, HEAP_BUFFER_UNDERFLOW);
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        true, &dst[-1], &src[0], memory_length_, HEAP_BUFFER_UNDERFLOW);
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AsanDirectionSpecialInstructionCheckGoodAccess) {
  static const char* function_names[] = {
      "asan_check_repz_4_byte_movs_access",
      "asan_check_repz_4_byte_cmps_access",
      "asan_check_repz_4_byte_stos_access"
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint32));
  uint32* src = reinterpret_cast<uint32*>(memory_src_);
  uint32* dst = reinterpret_cast<uint32*>(memory_dst_);

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    MemoryAccessorTester tester;
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_BACKWARD,
        false, &dst[memory_length_ - 1],
        &src[memory_length_ - 1], memory_length_,
        UNKNOWN_BAD_ACCESS);
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AsanSpecialInstructionCheckZeroAccess) {
  static const char* function_names[] = {
      "asan_check_repz_1_byte_movs_access",
      "asan_check_repz_1_byte_cmps_access",
      "asan_check_repz_1_byte_stos_access",
      "asan_check_repz_2_byte_movs_access",
      "asan_check_repz_2_byte_cmps_access",
      "asan_check_repz_2_byte_stos_access",
      "asan_check_repz_4_byte_movs_access",
      "asan_check_repz_4_byte_cmps_access",
      "asan_check_repz_4_byte_stos_access"
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint32));
  uint32* src = reinterpret_cast<uint32*>(memory_src_);
  uint32* dst = reinterpret_cast<uint32*>(memory_dst_);

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    // A prefixed instruction with a count of zero do not have side effects.
    MemoryAccessorTester tester;
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        false, &dst[-1], &src[-1], 0, UNKNOWN_BAD_ACCESS);
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AsanSpecialInstructionCheckShortcutAccess) {
  static const char* function_names[] = {
      "asan_check_repz_1_byte_cmps_access",
      "asan_check_repz_2_byte_cmps_access",
      "asan_check_repz_4_byte_cmps_access",
  };

  // Allocate memory space.
  AllocMemoryBuffers(kAllocSize, sizeof(uint32));
  uint32* src = reinterpret_cast<uint32*>(memory_src_);
  uint32* dst = reinterpret_cast<uint32*>(memory_dst_);

  src[1] = 0x12345667;

  // Validate memory accesses.
  for (int32 function = 0; function < arraysize(function_names); ++function) {
    FARPROC check_access_fn =
        ::GetProcAddress(asan_rtl_, function_names[function]);
    ASSERT_TRUE(check_access_fn != NULL);

    // Compare instruction stop their execution when values differ.
    MemoryAccessorTester tester;
    tester.ExpectSpecialMemoryErrorIsDetected(
        check_access_fn, MemoryAccessorTester::DIRECTION_FORWARD,
        false, &dst[0], &src[0], memory_length_ + 1, UNKNOWN_BAD_ACCESS);
  }

  FreeMemoryBuffers();
}

TEST_F(AsanRtlTest, AllocationFilterFlag) {
  agent::asan::AsanRuntime* runtime = GetActiveRuntimeFunction();
  SetAllocationFilterFlagFunction();
  EXPECT_TRUE(runtime->allocation_filter_flag());
  ClearAllocationFilterFlagFunction();
  EXPECT_FALSE(runtime->allocation_filter_flag());
  SetAllocationFilterFlagFunction();
  EXPECT_TRUE(runtime->allocation_filter_flag());
}

}  // namespace asan
}  // namespace agent
