/* Copyright 2018 Google LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow_data_validation/anomalies/test_util.h"

#include <stddef.h>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include "absl/strings/str_cat.h"
#include "tensorflow_data_validation/anomalies/map_util.h"
#include "tensorflow/core/lib/core/status.h"
#include "tensorflow/core/lib/core/status_test_util.h"
#include "tensorflow/core/platform/logging.h"
#include "tensorflow/core/platform/types.h"
#include "tensorflow_metadata/proto/v0/anomalies.pb.h"
#include "tensorflow_metadata/proto/v0/schema.pb.h"

namespace tensorflow {
namespace data_validation {
namespace testing {
namespace {
using std::vector;

vector<string> GetRegion(const vector<absl::string_view>& a_lines,
                         const tensorflow::metadata::v0::DiffRegion& region) {
  switch (region.details_case()) {
    case tensorflow::metadata::v0::DiffRegion::kUnchanged:
      return std::vector<string>(region.unchanged().contents().begin(),
                                 region.unchanged().contents().end());
    case tensorflow::metadata::v0::DiffRegion::kRemoved:
      return {};
    case tensorflow::metadata::v0::DiffRegion::kAdded:
      return std::vector<string>(region.added().contents().begin(),
                                 region.added().contents().end());
    case tensorflow::metadata::v0::DiffRegion::kChanged:
      return std::vector<string>(region.changed().right_contents().begin(),
                                 region.changed().right_contents().end());
    case tensorflow::metadata::v0::DiffRegion::kHidden: {
      CHECK_GE(region.hidden().left_start(), 1);
      CHECK_LE(region.hidden().left_start(), a_lines.size() + 1);
      CHECK_LE(region.hidden().left_start() + region.hidden().size(),
               a_lines.size() + 1);
      CHECK_GE(region.hidden().size(), 0);
      size_t begin_point = region.hidden().left_start() - 1;
      size_t end_point =
          region.hidden().left_start() - 1 + region.hidden().size();
      return std::vector<string>(a_lines.begin() + begin_point,
                                 a_lines.begin() + end_point);
    }
    default:
      LOG(FATAL) << "Unknown DiffRegion type: " << region.details_case();
  }
}

}  // namespace

vector<string> Patch(
    const vector<absl::string_view>& a_lines,
    const vector<tensorflow::metadata::v0::DiffRegion>& diff_regions) {
  vector<string> result;
  for (const tensorflow::metadata::v0::DiffRegion& diff_region : diff_regions) {
    vector<string> next = GetRegion(a_lines, diff_region);
    result.insert(result.end(), next.begin(), next.end());
  }
  return result;
}

ProtoStringMatcher::ProtoStringMatcher(const string& expected)
    : expected_(expected) {}
ProtoStringMatcher::ProtoStringMatcher(
    const ::tensorflow::protobuf::Message& expected)
    : expected_(expected.DebugString()) {}


void TestAnomalies(
    const tensorflow::metadata::v0::Anomalies& actual,
    const tensorflow::metadata::v0::Schema& old_schema,
    const std::map<string, ExpectedAnomalyInfo>& expected_anomalies) {
  EXPECT_THAT(actual.baseline(), EqualsProto(old_schema));
  for (const auto& pair : expected_anomalies) {
    const string& name = pair.first;
    const ExpectedAnomalyInfo& expected = pair.second;
    ASSERT_TRUE(ContainsKey(actual.anomaly_info(), name))
        << "Expected anomaly for feature name: " << name
        << " not found in Anomalies: " << actual.DebugString();
    TestAnomalyInfo(actual.anomaly_info().at(name), old_schema, expected,
                    absl::StrCat(" column: ", name));
  }
  for (const auto& pair : actual.anomaly_info()) {
    const string& name = pair.first;
    const tensorflow::metadata::v0::Schema actual_new_schema =
        PatchProto(old_schema, pair.second.diff_regions());
    metadata::v0::AnomalyInfo simple_anomaly_info = pair.second;
    simple_anomaly_info.clear_diff_regions();
    EXPECT_TRUE(ContainsKey(expected_anomalies, name))
        << "Unexpected anomaly: " << name << " "
        << simple_anomaly_info.DebugString()
        << " New schema: " << actual_new_schema.DebugString();
  }
}

void TestAnomalyInfo(const tensorflow::metadata::v0::AnomalyInfo& actual,
                     const tensorflow::metadata::v0::Schema& baseline,
                     const ExpectedAnomalyInfo& expected,
                     const string& comment) {
  tensorflow::metadata::v0::AnomalyInfo actual_info = actual;
  if (!actual_info.diff_regions().empty()) {
    tensorflow::metadata::v0::Schema actual_new_schema =
        PatchProto(baseline, actual_info.diff_regions());
    EXPECT_THAT(actual_new_schema, EqualsProto(expected.new_schema)) << comment;
    actual_info.clear_diff_regions();
  }
  EXPECT_THAT(actual_info, EqualsProto(expected.expected_info_without_diff))
      << comment;
}

}  // namespace testing
}  // namespace data_validation
}  // namespace tensorflow
