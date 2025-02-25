// Protocol Buffers - Google's data interchange format
// Copyright 2008 Google Inc.  All rights reserved.
// https://developers.google.com/protocol-buffers/
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Author: kenton@google.com (Kenton Varda)
//  Based on original Protocol Buffers design by
//  Sanjay Ghemawat, Jeff Dean, and others.
//
// This file needs to be included as .inc as it depends on certain macros being
// defined prior to its inclusion.

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cmath>
#include <limits>

#include <google/protobuf/message.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <fstream>
#include <sstream>

#include <google/protobuf/stubs/logging.h>
#include <google/protobuf/stubs/common.h>
#include <google/protobuf/descriptor.pb.h>
#include <gmock/gmock.h>
#include <google/protobuf/testing/googletest.h>
#include <gtest/gtest.h>
#include <google/protobuf/stubs/logging.h>
#include <google/protobuf/stubs/substitute.h>
#include <google/protobuf/arena.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/io/io_win32.h>
#include <google/protobuf/generated_message_reflection.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/test_util2.h>


namespace google {
namespace protobuf {

#if defined(_WIN32)
// DO NOT include <io.h>, instead create functions in io_win32.{h,cc} and import
// them like we do below.
using google::protobuf::io::win32::close;
using google::protobuf::io::win32::open;
#endif

#ifndef O_BINARY
#ifdef _O_BINARY
#define O_BINARY _O_BINARY
#else
#define O_BINARY 0  // If this isn't defined, the platform doesn't need it.
#endif
#endif

TEST(MESSAGE_TEST_NAME, SerializeHelpers) {
  // TODO(kenton):  Test more helpers?  They're all two-liners so it seems
  //   like a waste of time.

  UNITTEST::TestAllTypes message;
  TestUtil::SetAllFields(&message);
  std::stringstream stream;

  std::string str1("foo");
  std::string str2("bar");

  EXPECT_TRUE(message.SerializeToString(&str1));
  EXPECT_TRUE(message.AppendToString(&str2));
  EXPECT_TRUE(message.SerializeToOstream(&stream));

  EXPECT_EQ(str1.size() + 3, str2.size());
  EXPECT_EQ("bar", str2.substr(0, 3));
  // Don't use EXPECT_EQ because we don't want to dump raw binary data to
  // stdout.
  EXPECT_TRUE(str2.substr(3) == str1);

  // GCC gives some sort of error if we try to just do stream.str() == str1.
  std::string temp = stream.str();
  EXPECT_TRUE(temp == str1);

  EXPECT_TRUE(message.SerializeAsString() == str1);

}

TEST(MESSAGE_TEST_NAME, SerializeToBrokenOstream) {
  std::ofstream out;
  UNITTEST::TestAllTypes message;
  message.set_optional_int32(123);

  EXPECT_FALSE(message.SerializeToOstream(&out));
}

TEST(MESSAGE_TEST_NAME, ParseFromFileDescriptor) {
  std::string filename =
      TestUtil::GetTestDataPath("third_party/protobuf/testdata/golden_message");
  int file = open(filename.c_str(), O_RDONLY | O_BINARY);
  ASSERT_GE(file, 0);

  UNITTEST::TestAllTypes message;
  EXPECT_TRUE(message.ParseFromFileDescriptor(file));
  TestUtil::ExpectAllFieldsSet(message);

  EXPECT_GE(close(file), 0);
}

TEST(MESSAGE_TEST_NAME, ParsePackedFromFileDescriptor) {
  std::string filename = TestUtil::GetTestDataPath(
      "third_party/protobuf/testdata/golden_packed_fields_message");
  int file = open(filename.c_str(), O_RDONLY | O_BINARY);
  ASSERT_GE(file, 0);

  UNITTEST::TestPackedTypes message;
  EXPECT_TRUE(message.ParseFromFileDescriptor(file));
  TestUtil::ExpectPackedFieldsSet(message);

  EXPECT_GE(close(file), 0);
}

TEST(MESSAGE_TEST_NAME, ParseHelpers) {
  // TODO(kenton):  Test more helpers?  They're all two-liners so it seems
  //   like a waste of time.
  std::string data;

  {
    // Set up.
    UNITTEST::TestAllTypes message;
    TestUtil::SetAllFields(&message);
    message.SerializeToString(&data);
  }

  {
    // Test ParseFromString.
    UNITTEST::TestAllTypes message;
    EXPECT_TRUE(message.ParseFromString(data));
    TestUtil::ExpectAllFieldsSet(message);
  }

  {
    // Test ParseFromIstream.
    UNITTEST::TestAllTypes message;
    std::stringstream stream(data);
    EXPECT_TRUE(message.ParseFromIstream(&stream));
    EXPECT_TRUE(stream.eof());
    TestUtil::ExpectAllFieldsSet(message);
  }

  {
    // Test ParseFromBoundedZeroCopyStream.
    std::string data_with_junk(data);
    data_with_junk.append("some junk on the end");
    io::ArrayInputStream stream(data_with_junk.data(), data_with_junk.size());
    UNITTEST::TestAllTypes message;
    EXPECT_TRUE(message.ParseFromBoundedZeroCopyStream(&stream, data.size()));
    TestUtil::ExpectAllFieldsSet(message);
  }

  {
    // Test that ParseFromBoundedZeroCopyStream fails (but doesn't crash) if
    // EOF is reached before the expected number of bytes.
    io::ArrayInputStream stream(data.data(), data.size());
    UNITTEST::TestAllTypes message;
    EXPECT_FALSE(
        message.ParseFromBoundedZeroCopyStream(&stream, data.size() + 1));
  }
}

TEST(MESSAGE_TEST_NAME, ParseFailsIfNotInitialized) {
  UNITTEST::TestRequired message;
  std::vector<std::string> errors;

  {
    ScopedMemoryLog log;
    EXPECT_FALSE(message.ParseFromString(""));
    errors = log.GetMessages(ERROR);
  }

  ASSERT_EQ(1, errors.size());
  EXPECT_EQ(
      "Can't parse message of type \"" + std::string(UNITTEST_PACKAGE_NAME) +
          ".TestRequired\" because it is missing required fields: a, b, c",
      errors[0]);
}

TEST(MESSAGE_TEST_NAME, ParseFailsIfSubmessageNotInitialized) {
  UNITTEST::TestRequiredForeign source, message;
  source.mutable_optional_message()->set_dummy2(100);
  std::string serialized = source.SerializePartialAsString();

  EXPECT_TRUE(message.ParsePartialFromString(serialized));
  EXPECT_FALSE(message.IsInitialized());

  std::vector<std::string> errors;
  {
    ScopedMemoryLog log;
    EXPECT_FALSE(message.ParseFromString(source.SerializePartialAsString()));
    errors = log.GetMessages(ERROR);
  }

  EXPECT_THAT(
      errors,
      testing::ElementsAre(
          "Can't parse message of type \"" +
          std::string(UNITTEST_PACKAGE_NAME) +
          ".TestRequiredForeign\" because it is missing required fields: "
          "optional_message.a, optional_message.b, optional_message.c"));
}

TEST(MESSAGE_TEST_NAME, ParseFailsIfExtensionNotInitialized) {
  UNITTEST::TestChildExtension source, message;
  auto* r = source.mutable_optional_extension()->MutableExtension(
      UNITTEST::TestRequired::single);
  r->set_dummy2(100);
  std::string serialized = source.SerializePartialAsString();

  EXPECT_TRUE(message.ParsePartialFromString(serialized));
  EXPECT_FALSE(message.IsInitialized());

  std::vector<std::string> errors;
  {
    ScopedMemoryLog log;
    EXPECT_FALSE(message.ParseFromString(source.SerializePartialAsString()));
    errors = log.GetMessages(ERROR);
  }

  EXPECT_THAT(errors,
              testing::ElementsAre(strings::Substitute(
                  "Can't parse message of type \"$0.TestChildExtension\" "
                  "because it is missing required fields: "
                  "optional_extension.($0.TestRequired.single).a, "
                  "optional_extension.($0.TestRequired.single).b, "
                  "optional_extension.($0.TestRequired.single).c",
                  UNITTEST_PACKAGE_NAME)));
}

TEST(MESSAGE_TEST_NAME, MergeFromUninitialized) {
  UNITTEST::TestNestedRequiredForeign o, p, q;
  UNITTEST::TestNestedRequiredForeign* child = o.mutable_child();
  constexpr int kDepth = 2;
  for (int i = 0; i < kDepth; i++) {
    child->set_dummy(i);
    child = child->mutable_child();
  }
  UNITTEST::TestRequiredForeign* payload = child->mutable_payload();
  payload->mutable_optional_message()->set_a(1);
  payload->mutable_optional_message()->set_dummy2(100);
  payload->mutable_optional_message()->set_dummy4(200);
  ASSERT_TRUE(p.ParsePartialFromString(o.SerializePartialAsString()));

  q.mutable_child()->set_dummy(500);
  q = p;
  q.ParsePartialFromString(q.SerializePartialAsString());
  EXPECT_TRUE(TestUtil::EqualsToSerialized(q, o.SerializePartialAsString()));
  EXPECT_TRUE(TestUtil::EqualsToSerialized(q, p.SerializePartialAsString()));
}

TEST(MESSAGE_TEST_NAME, ExplicitLazyExceedRecursionLimit) {
  UNITTEST::NestedTestAllTypes original, parsed;
  // Build proto with recursion depth of 3.
  original.mutable_lazy_child()
      ->mutable_child()
      ->mutable_payload()
      ->set_optional_int32(-1);
  std::string serialized;
  EXPECT_TRUE(original.SerializeToString(&serialized));

  // User annotated LazyField ([lazy = true]) is eagerly verified and should
  // catch the recursion limit violation.
  io::ArrayInputStream array_stream(serialized.data(), serialized.size());
  io::CodedInputStream input_stream(&array_stream);
  input_stream.SetRecursionLimit(2);
  EXPECT_FALSE(parsed.ParseFromCodedStream(&input_stream));

  // Lazy read results in parsing error which can be verified by not having
  // expected value.
  EXPECT_NE(parsed.lazy_child().child().payload().optional_int32(), -1);
}

TEST(MESSAGE_TEST_NAME, NestedExplicitLazyExceedRecursionLimit) {
  UNITTEST::NestedTestAllTypes original, parsed;
  // Build proto with recursion depth of 5, with nested annotated LazyField.
  original.mutable_lazy_child()
      ->mutable_child()
      ->mutable_lazy_child()
      ->mutable_child()
      ->mutable_payload()
      ->set_optional_int32(-1);
  std::string serialized;
  EXPECT_TRUE(original.SerializeToString(&serialized));

  // User annotated LazyField ([lazy = true]) is eagerly verified and should
  // catch the recursion limit violation.
  io::ArrayInputStream array_stream(serialized.data(), serialized.size());
  io::CodedInputStream input_stream(&array_stream);
  input_stream.SetRecursionLimit(4);
  EXPECT_FALSE(parsed.ParseFromCodedStream(&input_stream));

  // Lazy read results in parsing error which can be verified by not having
  // expected value.
  EXPECT_NE(parsed.lazy_child()
                .child()
                .lazy_child()
                .child()
                .payload()
                .optional_int32(),
            -1);
}

TEST(MESSAGE_TEST_NAME, ParseFailsIfSubmessageTruncated) {
  UNITTEST::NestedTestAllTypes o, p;
  constexpr int kDepth = 5;
  auto* child = o.mutable_child();
  for (int i = 0; i < kDepth; i++) {
    child = child->mutable_child();
  }
  TestUtil::SetAllFields(child->mutable_payload());

  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  // Should parse correctly.
  EXPECT_TRUE(p.ParseFromString(serialized));

  constexpr int kMaxTruncate = 50;
  ASSERT_GT(serialized.size(), kMaxTruncate);

  for (int i = 1; i < kMaxTruncate; i += 3) {
    EXPECT_FALSE(
        p.ParseFromString(serialized.substr(0, serialized.size() - i)));
  }
}

TEST(MESSAGE_TEST_NAME, ParseFailsIfWireMalformed) {
  UNITTEST::NestedTestAllTypes o, p;
  constexpr int kDepth = 5;
  auto* child = o.mutable_child();
  for (int i = 0; i < kDepth; i++) {
    child = child->mutable_child();
  }
  // -1 becomes \xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1
  child->mutable_payload()->set_optional_int32(-1);

  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  // Should parse correctly.
  EXPECT_TRUE(p.ParseFromString(serialized));

  // Overwriting the last byte to 0xFF results in malformed wire.
  serialized[serialized.size() - 1] = 0xFF;
  EXPECT_FALSE(p.ParseFromString(serialized));
}

TEST(MESSAGE_TEST_NAME, ParseFailsIfOneofWireMalformed) {
  UNITTEST::NestedTestAllTypes o, p;
  constexpr int kDepth = 5;
  auto* child = o.mutable_child();
  for (int i = 0; i < kDepth; i++) {
    child = child->mutable_child();
  }
  // -1 becomes \xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1
  child->mutable_payload()->mutable_oneof_nested_message()->set_bb(-1);

  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  // Should parse correctly.
  EXPECT_TRUE(p.ParseFromString(serialized));

  // Overwriting the last byte to 0xFF results in malformed wire.
  serialized[serialized.size() - 1] = 0xFF;
  EXPECT_FALSE(p.ParseFromString(serialized));
}

TEST(MESSAGE_TEST_NAME, ParseFailsIfExtensionWireMalformed) {
  UNITTEST::TestChildExtension o, p;
  auto* m = o.mutable_optional_extension()->MutableExtension(
      UNITTEST::optional_nested_message_extension);

  // -1 becomes \xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1
  m->set_bb(-1);

  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  // Should parse correctly.
  EXPECT_TRUE(p.ParseFromString(serialized));

  // Overwriting the last byte to 0xFF results in malformed wire.
  serialized[serialized.size() - 1] = 0xFF;
  EXPECT_FALSE(p.ParseFromString(serialized));
}

TEST(MESSAGE_TEST_NAME, UninitializedAndMalformed) {
  UNITTEST::TestRequiredForeign o, p1, p2;
  o.mutable_optional_message()->set_a(-1);

  // -1 becomes \xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1
  std::string serialized;
  EXPECT_TRUE(o.SerializePartialToString(&serialized));

  // Should parse correctly.
  EXPECT_TRUE(p1.ParsePartialFromString(serialized));
  EXPECT_FALSE(p1.IsInitialized());

  // Overwriting the last byte to 0xFF results in malformed wire.
  serialized[serialized.size() - 1] = 0xFF;
  EXPECT_FALSE(p2.ParseFromString(serialized));
  EXPECT_FALSE(p2.IsInitialized());
}

inline UNITTEST::NestedTestAllTypes InitNestedProto(int depth) {
  UNITTEST::NestedTestAllTypes p;
  auto* child = p.mutable_child();
  for (int i = 0; i < depth; i++) {
    child->mutable_payload()->set_optional_int32(i);
    child = child->mutable_child();
  }
  // -1 becomes \xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\x1
  child->mutable_payload()->set_optional_int32(-1);
  return p;
}

// Parsing proto must not access beyond the bound.
TEST(MESSAGE_TEST_NAME, ParseStrictlyBoundedStream) {
  UNITTEST::NestedTestAllTypes o, p;
  constexpr int kDepth = 2;
  o = InitNestedProto(kDepth);
  TestUtil::SetAllFields(o.mutable_child()->mutable_payload());
  o.mutable_child()->mutable_child()->mutable_payload()->set_optional_string(
      std::string(1024, 'a'));

  std::string data;
  EXPECT_TRUE(o.SerializeToString(&data));

  TestUtil::BoundedArrayInputStream stream(data.data(), data.size());
  EXPECT_TRUE(p.ParseFromBoundedZeroCopyStream(&stream, data.size()));
  TestUtil::ExpectAllFieldsSet(p.child().payload());
}

TEST(MESSAGE_TEST_NAME, AllSetMethodsOnStringField) {
  UNITTEST::TestAllTypes msg;


  msg.set_optional_string("Asciiz");
  EXPECT_EQ(msg.optional_string(), "Asciiz");

  msg.set_optional_string("Length delimited", 6);
  EXPECT_EQ(msg.optional_string(), "Length");

  std::string value = "std::string value 1";
  msg.set_optional_string(value);
  EXPECT_EQ(msg.optional_string(), "std::string value 1");

  value = "std::string value 2";
  msg.set_optional_string(std::cref(value));
  EXPECT_EQ(msg.optional_string(), "std::string value 2");

  value = "std::string value 3";
  msg.set_optional_string(std::move(value));
  EXPECT_EQ(msg.optional_string(), "std::string value 3");
}

TEST(MESSAGE_TEST_NAME, SuccessAfterParsingFailure) {
  UNITTEST::NestedTestAllTypes o, p, q;
  constexpr int kDepth = 5;
  o = InitNestedProto(kDepth);
  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  // Should parse correctly.
  EXPECT_TRUE(p.ParseFromString(serialized));

  // Overwriting the last byte to 0xFF results in malformed wire.
  serialized[serialized.size() - 1] = 0xFF;
  EXPECT_FALSE(p.ParseFromString(serialized));

  // Subsequent serialization should be parsed correctly.
  EXPECT_TRUE(q.ParseFromString(p.SerializeAsString()));
}

TEST(MESSAGE_TEST_NAME, ExceedRecursionLimit) {
  UNITTEST::NestedTestAllTypes o, p;
  const int kDepth = io::CodedInputStream::GetDefaultRecursionLimit() + 10;
  o = InitNestedProto(kDepth);
  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  // Recursion level deeper than the default.
  EXPECT_FALSE(p.ParseFromString(serialized));
}

TEST(MESSAGE_TEST_NAME, SupportCustomRecursionLimitRead) {
  UNITTEST::NestedTestAllTypes o, p;
  const int kDepth = io::CodedInputStream::GetDefaultRecursionLimit() + 10;
  o = InitNestedProto(kDepth);
  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  // Should pass with custom limit + reads.
  io::ArrayInputStream raw_input(serialized.data(), serialized.size());
  io::CodedInputStream input(&raw_input);
  input.SetRecursionLimit(kDepth + 10);
  EXPECT_TRUE(p.ParseFromCodedStream(&input));

  EXPECT_EQ(p.child().payload().optional_int32(), 0);
  EXPECT_EQ(p.child().child().payload().optional_int32(), 1);

  // Verify p serializes successfully (survives VerifyConsistency).
  std::string result;
  EXPECT_TRUE(p.SerializeToString(&result));
}

TEST(MESSAGE_TEST_NAME, SupportCustomRecursionLimitWrite) {
  UNITTEST::NestedTestAllTypes o, p;
  const int kDepth = io::CodedInputStream::GetDefaultRecursionLimit() + 10;
  o = InitNestedProto(kDepth);
  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  // Should pass with custom limit + writes.
  io::ArrayInputStream raw_input(serialized.data(), serialized.size());
  io::CodedInputStream input(&raw_input);
  input.SetRecursionLimit(kDepth + 10);
  EXPECT_TRUE(p.ParseFromCodedStream(&input));

  EXPECT_EQ(p.mutable_child()->mutable_payload()->optional_int32(), 0);
  EXPECT_EQ(
      p.mutable_child()->mutable_child()->mutable_payload()->optional_int32(),
      1);
}

// While deep recursion is never guaranteed, this test aims to catch potential
// issues with very deep recursion.
TEST(MESSAGE_TEST_NAME, SupportDeepRecursionLimit) {
  UNITTEST::NestedTestAllTypes o, p;
  constexpr int kDepth = 1000;
  auto* child = o.mutable_child();
  for (int i = 0; i < kDepth; i++) {
    child = child->mutable_child();
  }
  child->mutable_payload()->set_optional_int32(100);

  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  io::ArrayInputStream raw_input(serialized.data(), serialized.size());
  io::CodedInputStream input(&raw_input);
  input.SetRecursionLimit(1100);
  EXPECT_TRUE(p.ParseFromCodedStream(&input));
}

TEST(MESSAGE_TEST_NAME, Swap) {
  UNITTEST::NestedTestAllTypes o;
  constexpr int kDepth = 5;
  auto* child = o.mutable_child();
  for (int i = 0; i < kDepth; i++) {
    child = child->mutable_child();
  }
  TestUtil::SetAllFields(child->mutable_payload());

  std::string serialized;
  EXPECT_TRUE(o.SerializeToString(&serialized));

  {
    Arena arena;
    UNITTEST::NestedTestAllTypes* p1 =
        Arena::CreateMessage<UNITTEST::NestedTestAllTypes>(&arena);

    // Should parse correctly.
    EXPECT_TRUE(p1->ParseFromString(serialized));

    UNITTEST::NestedTestAllTypes* p2 =
        Arena::CreateMessage<UNITTEST::NestedTestAllTypes>(&arena);

    p1->Swap(p2);

    EXPECT_EQ(o.SerializeAsString(), p2->SerializeAsString());
  }
}

TEST(MESSAGE_TEST_NAME, BypassInitializationCheckOnParse) {
  UNITTEST::TestRequired message;
  io::ArrayInputStream raw_input(nullptr, 0);
  io::CodedInputStream input(&raw_input);
  EXPECT_TRUE(message.MergePartialFromCodedStream(&input));
}

TEST(MESSAGE_TEST_NAME, InitializationErrorString) {
  UNITTEST::TestRequired message;
  EXPECT_EQ("a, b, c", message.InitializationErrorString());
}

TEST(MESSAGE_TEST_NAME, DynamicCastToGenerated) {
  UNITTEST::TestAllTypes test_all_types;

  Message* test_all_types_pointer = &test_all_types;
  EXPECT_EQ(&test_all_types, DynamicCastToGenerated<UNITTEST::TestAllTypes>(
                                 test_all_types_pointer));
  EXPECT_EQ(nullptr, DynamicCastToGenerated<UNITTEST::TestRequired>(
                         test_all_types_pointer));

  const Message* test_all_types_pointer_const = &test_all_types;
  EXPECT_EQ(&test_all_types,
            DynamicCastToGenerated<const UNITTEST::TestAllTypes>(
                test_all_types_pointer_const));
  EXPECT_EQ(nullptr, DynamicCastToGenerated<const UNITTEST::TestRequired>(
                         test_all_types_pointer_const));

  Message* test_all_types_pointer_nullptr = nullptr;
  EXPECT_EQ(nullptr, DynamicCastToGenerated<UNITTEST::TestAllTypes>(
                         test_all_types_pointer_nullptr));
}

#ifdef PROTOBUF_HAS_DEATH_TEST  // death tests do not work on Windows yet.

TEST(MESSAGE_TEST_NAME, SerializeFailsIfNotInitialized) {
  UNITTEST::TestRequired message;
  std::string data;
  EXPECT_DEBUG_DEATH(EXPECT_TRUE(message.SerializeToString(&data)),
                     "Can't serialize message of type \"" +
                         std::string(UNITTEST_PACKAGE_NAME) +
                         ".TestRequired\" because "
                         "it is missing required fields: a, b, c");
}

TEST(MESSAGE_TEST_NAME, CheckInitialized) {
  UNITTEST::TestRequired message;
  EXPECT_DEATH(message.CheckInitialized(),
               "Message of type \"" + std::string(UNITTEST_PACKAGE_NAME) +
                   ".TestRequired\" is missing required "
                   "fields: a, b, c");
}

#endif  // PROTOBUF_HAS_DEATH_TEST

namespace {
// An input stream that repeats a std::string's content for a number of times.
// It helps us create a really large input without consuming too much memory.
// Used to test the parsing behavior when the input size exceeds 2G or close to
// it.
class RepeatedInputStream : public io::ZeroCopyInputStream {
 public:
  RepeatedInputStream(const std::string& data, size_t count)
      : data_(data), count_(count), position_(0), total_byte_count_(0) {}

  bool Next(const void** data, int* size) override {
    if (position_ == data_.size()) {
      if (--count_ == 0) {
        return false;
      }
      position_ = 0;
    }
    *data = &data_[position_];
    *size = static_cast<int>(data_.size() - position_);
    position_ = data_.size();
    total_byte_count_ += *size;
    return true;
  }

  void BackUp(int count) override {
    position_ -= static_cast<size_t>(count);
    total_byte_count_ -= count;
  }

  bool Skip(int count) override {
    while (count > 0) {
      const void* data;
      int size;
      if (!Next(&data, &size)) {
        break;
      }
      if (size >= count) {
        BackUp(size - count);
        return true;
      } else {
        count -= size;
      }
    }
    return false;
  }

  int64_t ByteCount() const override { return total_byte_count_; }

 private:
  std::string data_;
  size_t count_;     // The number of strings that haven't been consumed.
  size_t position_;  // Position in the std::string for the next read.
  int64_t total_byte_count_;
};
}  // namespace

TEST(MESSAGE_TEST_NAME, TestParseMessagesCloseTo2G) {
  constexpr int32_t kint32max = std::numeric_limits<int32_t>::max();

  // Create a message with a large std::string field.
  std::string value = std::string(64 * 1024 * 1024, 'x');
  UNITTEST::TestAllTypes message;
  message.set_optional_string(value);

  // Repeat this message in the input stream to make the total input size
  // close to 2G.
  std::string data = message.SerializeAsString();
  size_t count = static_cast<size_t>(kint32max) / data.size();
  RepeatedInputStream input(data, count);

  // The parsing should succeed.
  UNITTEST::TestAllTypes result;
  EXPECT_TRUE(result.ParseFromZeroCopyStream(&input));

  // When there are multiple occurrences of a singular field, the last one
  // should win.
  EXPECT_EQ(value, result.optional_string());
}

TEST(MESSAGE_TEST_NAME, TestParseMessagesOver2G) {
  constexpr int32_t kint32max = std::numeric_limits<int32_t>::max();

  // Create a message with a large std::string field.
  std::string value = std::string(64 * 1024 * 1024, 'x');
  UNITTEST::TestAllTypes message;
  message.set_optional_string(value);

  // Repeat this message in the input stream to make the total input size
  // larger than 2G.
  std::string data = message.SerializeAsString();
  size_t count = static_cast<size_t>(kint32max) / data.size() + 1;
  RepeatedInputStream input(data, count);

  // The parsing should fail.
  UNITTEST::TestAllTypes result;
  EXPECT_FALSE(result.ParseFromZeroCopyStream(&input));
}

TEST(MESSAGE_TEST_NAME, BypassInitializationCheckOnSerialize) {
  UNITTEST::TestRequired message;
  io::ArrayOutputStream raw_output(nullptr, 0);
  io::CodedOutputStream output(&raw_output);
  EXPECT_TRUE(message.SerializePartialToCodedStream(&output));
}

TEST(MESSAGE_TEST_NAME, FindInitializationErrors) {
  UNITTEST::TestRequired message;
  std::vector<std::string> errors;
  message.FindInitializationErrors(&errors);
  ASSERT_EQ(3, errors.size());
  EXPECT_EQ("a", errors[0]);
  EXPECT_EQ("b", errors[1]);
  EXPECT_EQ("c", errors[2]);
}

TEST(MESSAGE_TEST_NAME, ReleaseMustUseResult) {
  UNITTEST::TestAllTypes message;
  auto* f = new UNITTEST::ForeignMessage();
  f->set_c(1000);
  message.set_allocated_optional_foreign_message(f);
  auto* mf = message.mutable_optional_foreign_message();
  EXPECT_EQ(mf, f);
  std::unique_ptr<UNITTEST::ForeignMessage> rf(
      message.release_optional_foreign_message());
  EXPECT_NE(rf.get(), nullptr);
}

TEST(MESSAGE_TEST_NAME, ParseFailsOnInvalidMessageEnd) {
  UNITTEST::TestAllTypes message;

  // Control case.
  EXPECT_TRUE(message.ParseFromArray("", 0));

  // The byte is a valid varint, but not a valid tag (zero).
  EXPECT_FALSE(message.ParseFromArray("\0", 1));

  // The byte is a malformed varint.
  EXPECT_FALSE(message.ParseFromArray("\200", 1));

  // The byte is an endgroup tag, but we aren't parsing a group.
  EXPECT_FALSE(message.ParseFromArray("\014", 1));
}

// Regression test for b/23630858
TEST(MESSAGE_TEST_NAME, MessageIsStillValidAfterParseFails) {
  UNITTEST::TestAllTypes message;

  // 9 0xFFs for the "optional_uint64" field.
  std::string invalid_data = "\x20\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF";

  EXPECT_FALSE(message.ParseFromString(invalid_data));
  message.Clear();
  EXPECT_EQ(0, message.optional_uint64());

  // invalid data for field "optional_string". Length prefix is 1 but no
  // payload.
  std::string invalid_string_data = "\x72\x01";
  {
    Arena arena;
    UNITTEST::TestAllTypes* arena_message =
        Arena::CreateMessage<UNITTEST::TestAllTypes>(&arena);
    EXPECT_FALSE(arena_message->ParseFromString(invalid_string_data));
    arena_message->Clear();
    EXPECT_EQ("", arena_message->optional_string());
  }
}


namespace {

void ExpectMessageMerged(const UNITTEST::TestAllTypes& message) {
  EXPECT_EQ(3, message.optional_int32());
  EXPECT_EQ(2, message.optional_int64());
  EXPECT_EQ("hello", message.optional_string());
}

void AssignParsingMergeMessages(UNITTEST::TestAllTypes* msg1,
                                UNITTEST::TestAllTypes* msg2,
                                UNITTEST::TestAllTypes* msg3) {
  msg1->set_optional_int32(1);
  msg2->set_optional_int64(2);
  msg3->set_optional_int32(3);
  msg3->set_optional_string("hello");
}

}  // namespace

// Test that if an optional or required message/group field appears multiple
// times in the input, they need to be merged.
TEST(MESSAGE_TEST_NAME, ParsingMerge) {
  UNITTEST::TestParsingMerge::RepeatedFieldsGenerator generator;
  UNITTEST::TestAllTypes* msg1;
  UNITTEST::TestAllTypes* msg2;
  UNITTEST::TestAllTypes* msg3;

#define ASSIGN_REPEATED_FIELD(FIELD) \
  msg1 = generator.add_##FIELD();    \
  msg2 = generator.add_##FIELD();    \
  msg3 = generator.add_##FIELD();    \
  AssignParsingMergeMessages(msg1, msg2, msg3)

  ASSIGN_REPEATED_FIELD(field1);
  ASSIGN_REPEATED_FIELD(field2);
  ASSIGN_REPEATED_FIELD(field3);
  ASSIGN_REPEATED_FIELD(ext1);
  ASSIGN_REPEATED_FIELD(ext2);

#undef ASSIGN_REPEATED_FIELD
#define ASSIGN_REPEATED_GROUP(FIELD)                \
  msg1 = generator.add_##FIELD()->mutable_field1(); \
  msg2 = generator.add_##FIELD()->mutable_field1(); \
  msg3 = generator.add_##FIELD()->mutable_field1(); \
  AssignParsingMergeMessages(msg1, msg2, msg3)

  ASSIGN_REPEATED_GROUP(group1);
  ASSIGN_REPEATED_GROUP(group2);

#undef ASSIGN_REPEATED_GROUP

  std::string buffer;
  generator.SerializeToString(&buffer);
  UNITTEST::TestParsingMerge parsing_merge;
  parsing_merge.ParseFromString(buffer);

  // Required and optional fields should be merged.
  ExpectMessageMerged(parsing_merge.required_all_types());
  ExpectMessageMerged(parsing_merge.optional_all_types());
  ExpectMessageMerged(parsing_merge.optionalgroup().optional_group_all_types());
  ExpectMessageMerged(
      parsing_merge.GetExtension(UNITTEST::TestParsingMerge::optional_ext));

  // Repeated fields should not be merged.
  EXPECT_EQ(3, parsing_merge.repeated_all_types_size());
  EXPECT_EQ(3, parsing_merge.repeatedgroup_size());
  EXPECT_EQ(
      3, parsing_merge.ExtensionSize(UNITTEST::TestParsingMerge::repeated_ext));
}

TEST(MESSAGE_TEST_NAME, MergeFrom) {
  UNITTEST::TestAllTypes source, dest;

  // Optional fields
  source.set_optional_int32(1);  // only source
  source.set_optional_int64(2);  // both source and dest
  dest.set_optional_int64(3);
  dest.set_optional_uint32(4);  // only dest

  // Optional fields with defaults
  source.set_default_int32(13);  // only source
  source.set_default_int64(14);  // both source and dest
  dest.set_default_int64(15);
  dest.set_default_uint32(16);  // only dest

  // Repeated fields
  source.add_repeated_int32(5);  // only source
  source.add_repeated_int32(6);
  source.add_repeated_int64(7);  // both source and dest
  source.add_repeated_int64(8);
  dest.add_repeated_int64(9);
  dest.add_repeated_int64(10);
  dest.add_repeated_uint32(11);  // only dest
  dest.add_repeated_uint32(12);

  dest.MergeFrom(source);

  // Optional fields: source overwrites dest if source is specified
  EXPECT_EQ(1, dest.optional_int32());   // only source: use source
  EXPECT_EQ(2, dest.optional_int64());   // source and dest: use source
  EXPECT_EQ(4, dest.optional_uint32());  // only dest: use dest
  EXPECT_EQ(0, dest.optional_uint64());  // neither: use default

  // Optional fields with defaults
  EXPECT_EQ(13, dest.default_int32());   // only source: use source
  EXPECT_EQ(14, dest.default_int64());   // source and dest: use source
  EXPECT_EQ(16, dest.default_uint32());  // only dest: use dest
  EXPECT_EQ(44, dest.default_uint64());  // neither: use default

  // Repeated fields: concatenate source onto the end of dest
  ASSERT_EQ(2, dest.repeated_int32_size());
  EXPECT_EQ(5, dest.repeated_int32(0));
  EXPECT_EQ(6, dest.repeated_int32(1));
  ASSERT_EQ(4, dest.repeated_int64_size());
  EXPECT_EQ(9, dest.repeated_int64(0));
  EXPECT_EQ(10, dest.repeated_int64(1));
  EXPECT_EQ(7, dest.repeated_int64(2));
  EXPECT_EQ(8, dest.repeated_int64(3));
  ASSERT_EQ(2, dest.repeated_uint32_size());
  EXPECT_EQ(11, dest.repeated_uint32(0));
  EXPECT_EQ(12, dest.repeated_uint32(1));
  ASSERT_EQ(0, dest.repeated_uint64_size());
}

TEST(MESSAGE_TEST_NAME, IsInitialized) {
  UNITTEST::TestIsInitialized msg;
  EXPECT_TRUE(msg.IsInitialized());
  UNITTEST::TestIsInitialized::SubMessage* sub_message =
      msg.mutable_sub_message();
  EXPECT_TRUE(msg.IsInitialized());
  UNITTEST::TestIsInitialized::SubMessage::SubGroup* sub_group =
      sub_message->mutable_subgroup();
  EXPECT_FALSE(msg.IsInitialized());
  sub_group->set_i(1);
  EXPECT_TRUE(msg.IsInitialized());
}

TEST(MESSAGE_TEST_NAME, IsInitializedSplitBytestream) {
  UNITTEST::TestRequired ab, c;
  ab.set_a(1);
  ab.set_b(2);
  c.set_c(3);

  // The protobuf represented by the concatenated string has all required
  // fields (a,b,c) set.
  std::string bytes =
      ab.SerializePartialAsString() + c.SerializePartialAsString();

  UNITTEST::TestRequired concatenated;
  EXPECT_TRUE(concatenated.ParsePartialFromString(bytes));
  EXPECT_TRUE(concatenated.IsInitialized());

  UNITTEST::TestRequiredForeign fab, fc;
  fab.mutable_optional_message()->set_a(1);
  fab.mutable_optional_message()->set_b(2);
  fc.mutable_optional_message()->set_c(3);

  bytes =
      fab.SerializePartialAsString() + fc.SerializePartialAsString();

  UNITTEST::TestRequiredForeign fconcatenated;
  EXPECT_TRUE(fconcatenated.ParsePartialFromString(bytes));
  EXPECT_TRUE(fconcatenated.IsInitialized());
}

TEST(MESSAGE_FACTORY_TEST_NAME, GeneratedFactoryLookup) {
  EXPECT_EQ(MessageFactory::generated_factory()->GetPrototype(
                UNITTEST::TestAllTypes::descriptor()),
            &UNITTEST::TestAllTypes::default_instance());
}

TEST(MESSAGE_FACTORY_TEST_NAME, GeneratedFactoryUnknownType) {
  // Construct a new descriptor.
  DescriptorPool pool;
  FileDescriptorProto file;
  file.set_name("foo.proto");
  file.add_message_type()->set_name("Foo");
  const Descriptor* descriptor = pool.BuildFile(file)->message_type(0);

  // Trying to construct it should return nullptr.
  EXPECT_TRUE(MessageFactory::generated_factory()->GetPrototype(descriptor) ==
              nullptr);
}

TEST(MESSAGE_TEST_NAME, MOMIParserEdgeCases) {
  {
    UNITTEST::TestAllTypes msg;
    // Parser ends in last 16 bytes of buffer due to a 0.
    std::string data;
    // 12 bytes of data
    for (int i = 0; i < 4; i++) data += "\370\1\1";
    // 13 byte is terminator
    data += '\0';  // Terminator
    // followed by the rest of the stream
    // space is ascii 32 so no end group
    data += std::string(30, ' ');
    io::ArrayInputStream zcis(data.data(), data.size(), 17);
    io::CodedInputStream cis(&zcis);
    EXPECT_TRUE(msg.MergePartialFromCodedStream(&cis));
    EXPECT_EQ(cis.CurrentPosition(), 3 * 4 + 1);
  }
  {
    // Parser ends in last 16 bytes of buffer due to a end-group.
    // Must use a message that is a group. Otherwise ending on a group end is
    // a failure.
    UNITTEST::TestAllTypes::OptionalGroup msg;
    std::string data;
    for (int i = 0; i < 3; i++) data += "\370\1\1";
    data += '\14';  // Octal end-group tag 12 (1 * 8 + 4(
    data += std::string(30, ' ');
    io::ArrayInputStream zcis(data.data(), data.size(), 17);
    io::CodedInputStream cis(&zcis);
    EXPECT_TRUE(msg.MergePartialFromCodedStream(&cis));
    EXPECT_EQ(cis.CurrentPosition(), 3 * 3 + 1);
    EXPECT_TRUE(cis.LastTagWas(12));
  }
  {
    // Parser ends in last 16 bytes of buffer due to a end-group. But is inside
    // a length delimited field.
    // a failure.
    UNITTEST::TestAllTypes::OptionalGroup msg;
    std::string data;
    data += "\22\3foo";
    data += '\14';  // Octal end-group tag 12 (1 * 8 + 4(
    data += std::string(30, ' ');
    io::ArrayInputStream zcis(data.data(), data.size(), 17);
    io::CodedInputStream cis(&zcis);
    EXPECT_TRUE(msg.MergePartialFromCodedStream(&cis));
    EXPECT_EQ(cis.CurrentPosition(), 6);
    EXPECT_TRUE(cis.LastTagWas(12));
  }
  {
    // Parser fails when ending on 0 if from ZeroCopyInputStream
    UNITTEST::TestAllTypes msg;
    std::string data;
    // 12 bytes of data
    for (int i = 0; i < 4; i++) data += "\370\1\1";
    // 13 byte is terminator
    data += '\0';  // Terminator
    data += std::string(30, ' ');
    io::ArrayInputStream zcis(data.data(), data.size(), 17);
    EXPECT_FALSE(msg.ParsePartialFromZeroCopyStream(&zcis));
  }
}


TEST(MESSAGE_TEST_NAME, CheckSerializationWhenInterleavedExtensions) {
  UNITTEST::TestExtensionRangeSerialize in_message;

  in_message.set_foo_one(1);
  in_message.set_foo_two(2);
  in_message.set_foo_three(3);
  in_message.set_foo_four(4);

  in_message.SetExtension(UNITTEST::TestExtensionRangeSerialize::bar_one, 1);
  in_message.SetExtension(UNITTEST::TestExtensionRangeSerialize::bar_two, 2);
  in_message.SetExtension(UNITTEST::TestExtensionRangeSerialize::bar_three, 3);
  in_message.SetExtension(UNITTEST::TestExtensionRangeSerialize::bar_four, 4);
  in_message.SetExtension(UNITTEST::TestExtensionRangeSerialize::bar_five, 5);

  std::string buffer;
  in_message.SerializeToString(&buffer);

  UNITTEST::TestExtensionRangeSerialize out_message;
  out_message.ParseFromString(buffer);

  EXPECT_EQ(1, out_message.foo_one());
  EXPECT_EQ(2, out_message.foo_two());
  EXPECT_EQ(3, out_message.foo_three());
  EXPECT_EQ(4, out_message.foo_four());

  EXPECT_EQ(1, out_message.GetExtension(UNITTEST::TestExtensionRangeSerialize::bar_one));
  EXPECT_EQ(2, out_message.GetExtension(UNITTEST::TestExtensionRangeSerialize::bar_two));
  EXPECT_EQ(3, out_message.GetExtension(UNITTEST::TestExtensionRangeSerialize::bar_three));
  EXPECT_EQ(4, out_message.GetExtension(UNITTEST::TestExtensionRangeSerialize::bar_four));
  EXPECT_EQ(5, out_message.GetExtension(UNITTEST::TestExtensionRangeSerialize::bar_five));
}

TEST(MESSAGE_TEST_NAME, PreservesFloatingPointNegative0) {
  UNITTEST::TestAllTypes in_message;
  in_message.set_optional_float(-0.0f);
  in_message.set_optional_double(-0.0);
  std::string serialized;
  EXPECT_TRUE(in_message.SerializeToString(&serialized));
  UNITTEST::TestAllTypes out_message;
  EXPECT_TRUE(out_message.ParseFromString(serialized));
  EXPECT_EQ(in_message.optional_float(), out_message.optional_float());
  EXPECT_EQ(std::signbit(in_message.optional_float()),
            std::signbit(out_message.optional_float()));
  EXPECT_EQ(in_message.optional_double(), out_message.optional_double());
  EXPECT_EQ(std::signbit(in_message.optional_double()),
            std::signbit(out_message.optional_double()));
}

}  // namespace protobuf
}  // namespace google
